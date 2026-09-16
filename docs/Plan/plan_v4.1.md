# ClassFlow 架构技术债清理方案（plan_v4.1）

> 版本：v4.1（方案稿） · 当前版本：v4.0 · 日期：2026-09-04
> 性质：**方案先行（技术债整理）**——收录 2026-09-04 架构评审中指出的「不合理点 2 / 3 / 4 / 7 / 8」，
> 逐条写清现状、影响与改造方向；W 范围与实施顺序由人工圈定后再分步实施（沿用 W1..W5 惯例）。
> 评审原文出处：本对话「架构上有什么亮点/不合理」答复。较重的结构性取舍——**点 1（写路径原子性依赖 modal 单线程）、
> 点 5（启发式而非约束求解）、点 6（锁定语义边界）——明确不在本稿**，理由见 §8。
> **实施状态（2026-09-04）**：R2 / R3 / R4 / R7 / R8 均已按本稿实施并跑通（改动见 §1 / §2 / §3 / §4 / §5 的"实施记录"）；
> 各笔测试分别并入 `tst_manualmove` / `tst_conflicttable` / `tst_snapshot` / `tst_datastore`（开发者决定**不新增测试套件**，总数保持 9）。

---

## 0. 一句话

把五笔"知道但欠着"的债整理成可独立圈定的改动：**代理主键与展示标签解耦（R2）· 约束 H4/H5 收成单源（R3）·
课程事实双写收口（R4）· 会话级撤销/重做（R7）· 排课条目 O(1) 索引（R8）**。前四笔动数据模型/文件格式语义，后两笔纯增量，建议按「R8 → R3 → R2 → R4 → R7」的风险递增顺序实施，每笔独立可验收、可人工走查。

---

## 1. R2 — `entryId`：解耦"代理主键"与"人读语义"

### 现状
- 条目主键字面形如 `classId-星期-起始节`（测试 helper 与条目录入均按此格式构造，见 [tst_manualmove.cpp:40](../../tests/tst_manualmove.cpp#L40)）。
- v3.6 起明确 **entryId 不透明**：代码不反解析、不重编号，手动调整改时间/教室时**保持原值**（[data-model §6](../../docs/data-model.md)）。

### 问题与影响
- 语义与身份耦合：手动调整后 **id 字面与真实时间必然对不上**，查快照/打日志/读测试都会被误导（`C102-3-1` 实际已在周四）。
- 唯一性没有主键约束兜底，只靠规则「同班不落同 slot」间接保证；一旦出现同一班同一 (day,start) 两条（数据异常），索引类代码没有报错位点。
- 想加按班遍历/按序重排等能力时，被这个"形似语义"的 id 绊住——不敢重编号，又不好反解析。

### 方向（两选一，建议 A）
- **A（推荐）：身份与展示解耦**
  1. 生成改为 **`classId` + 班内递增序号**（如 `C102#2`），或纯自增；**绝不内嵌时间字段**。展示层另给"第 k 次课 / 周X 第 a~b 节"的人读标签（现在详情/调整弹窗已按 (day→start) 排序编号，标签可现算，不入库）。
  2. 写回与加载沿用现有字符串主键，老快照里的旧格式 id（含时间字面）**原样兼容**：只保证唯一，不要求格式一致。
  3. 唯一性前提：同一班内序号不重复 → 主键 = `(classId, seq)` 结构上成立；`ConflictTable` H1 继续防时间重叠。
- B（最小改）：保持现状但把"不透明"写成强约定——新增断言/单测：任何读代码不得 `split('-')` 反解析 id；仍欠语义误导的问题，仅止血不治本。

### 涉及文件
- 生成点两处：贪心 [strategy.cpp:97](../../src/core/schedule/strategy.cpp#L97)（makeEntry，`classId-day-section`）与退火写回 [annealingcandidate.cpp:227](../../src/core/schedule/annealingcandidate.cpp#L227)；[tst_manualmove.cpp:40](../../tests/tst_manualmove.cpp#L40) 等测试 helper 字面量；[data-model.md §6](../../docs/data-model.md)、architecture §3/§10 同步。

### Remark
- 已核（2026-09-04）：`src/` 下无任何对 entryId 做 `split/left/mid` 的反解析点，替换面仅生成端两处 + 测试字面量——这印证"没有代码反解析 id"的口子一直在。

### 风险 / 成本
- 中。多数是"生成格式 + 测试字面量"的机械改动；**没有代码反解析 id**（这是当初刻意留的口子），故替换面比看起来小。快照格式不新增字段、老条目可原样读，无迁移。

### 验收
- 新增用例：手动调整后 id 不变且**不含**真实时间信息；同班条目 id 两两不同；老格式 id 条目仍可被 update/scheduleEntryById 命中。

> **实施记录（2026-09-04）**：生成端仅两处改格式——贪心 [strategy.cpp](../../src/core/schedule/strategy.cpp) `makeEntry`
> 与退火 [annealingcandidate.cpp](../../src/core/schedule/annealingcandidate.cpp) `entryFor`，
> entryId 由 `classId-星期-起始节` 改为 `classId#班内序号`（序号从 1 起，绝不内嵌时间）。两函数各加末位 `int seq = 0` 默认参数：
> 落库调用点传班内循环序号（贪心 `i+1`、退火 `applyToStore` 内层 `idx+1`），纯校验调用点（annealingmoves 的 10 处 canPlace/commit 粗筛）
> 不传、用默认 0（ConflictTable 不读 entryId，零改动）。展示层（sessionsOfClass 排序 + ordinal "第 k 次课" + timeSlot 现算）早已完备，**零改**。
> 经核 `src/` 下无任何 split/left/mid 反解析 entryId，消费端全从已有条目取锚；快照原样字符串读写，老格式 id 靠字符串相等命中，无迁移。

> **验收更新（2026-09-04）**：2 用例并入 `tst_manualmove`（`legacyIdStillHits` 验老格式 id 可命中 + 原位替换、
> `sameClassEntriesHaveDistinctIds` 验同班 id 形如 `C102#1`/`C102#2` 两两不同且不含 `-`）；
> `makeSession` 改格式 + fixture 传序号 + 20 处字面量机械替换（`C102-3-1`→`C102#1` 等）。
> 验收点 1（手动调整后 id 不变且不含时间）由现有 apply 用例改格式后隐式覆盖。tst_conflicttable / tst_annealing
> 的老格式字面量保留（ConflictTable 不读 entryId / buildFromEntries 丢弃 entryId，兼作兼容样本），**不新建套件**，总数保持 9。

---

## 2. R3 — 硬约束 H4/H5：收成共享单源

### 现状
- 容量（H4 `capacity ≥ plannedSize`）与类型（H5 `requiredRoomType≠Any 时须匹配`）的判定在**至少四处**独立实现：贪心 best-fit、退火 M4/M5、`manualmove` 的 staticReject（plan_v3.6 §6 已自注「第 4 处拷贝 → TODO 抽共享 helper」）。

### 问题与影响
- 约束口径一旦演进（教室加设备属性、容量取 `maxCapacity` 还是 `plannedSize`、Any 语义变化），**漏改一处即产生静默不一致**：自动排课不违反、手动调整却拒绝（或反之）。
- 现在四份行为恰好一致，纯靠"复制时小心"维持，无编译期保证。

### 方向
- 抽 **`core/schedule/roomrules.{h,cpp}`**（无 QObject，纯函数）：
  - `RoomFit roomFit(const Course &course, const Classroom &room)` → `{ ok; Reject reason; }`（Reject 复用/并入 `manual::Reject` 的 `CapacityTooSmall / RoomTypeMismatch`，或提升为共享枚举）；
  - `bestFitClassroom(...)` 的"可行 + 最小浪费"判定也走同一函数。
- 贪心、退火 M4/M5、manualmove 三处调用它，删掉内联拷贝。

> **实施记录（2026-09-04）**：落为三个纯函数 `roomrules::capacityOk / typeOk / roomOk`（比方向稿更小粒度）——
> manualmove 需分别调 `capacityOk` / `typeOk` 以细分 `CapacityTooSmall` / `RoomTypeMismatch` 两种拒绝原因，
> 贪心 best-fit 与退火 M4/M5 直接调组合 `roomOk`。

### 涉及文件
- 新建 `roomrules.{h,cpp}`（登记进根 [CMakeLists.txt](../../CMakeLists.txt) CORE_SOURCES）；[strategy.cpp](../../src/core/schedule/strategy.cpp)、annealingmoves、manualmove 改调。

### 风险 / 成本
- 低。纯抽取、行为零变化；跑全量引擎回归（tst_annealing/tst_scheduler/tst_manualmove）即可证明。

### 验收
- 新增 `tst_roomrules`（边界：恰等于、Any 不限、Lab 只进 Lab、容量 0/负防御）；三处调用点不再含内联容量/类型字面。

> **验收更新（2026-09-04）**：按开发者决定，roomrules 纯函数用例并入既有 `tst_conflicttable`
> （`roomRulesCapacityOk / roomRulesTypeOk / roomRulesRoomOk` 三槽），**不新建 tst_roomrules**，测试总数保持 9。

---

## 3. R4 — 课程事实"双写"收口

### 现状（先澄清范围）
- **内存里没有双写**：`TeachingClass` 只存 classId/courseId/teacherId/plannedSize/maxCapacity，课程事实唯一在 `Course`。
- 双写出现在**文件层**：教学班 CSV 13 列内嵌整套课程字段（导入的单一数据来源）；快照 `[TeachingClasses]` 段同样内嵌课程字段（为了**没有 `[Courses]` 段的老构建**能由班行推导课程），同时又有独立 `[Courses]` 段（[snapshot.cpp:3-5](../../src/core/store/snapshot.cpp#L3)）。

### 问题与影响
- 同一课程事实在快照文件里出现两份，**只为了兼容"老版本构建读新快照"**——而本项目是单机应用，该兼容是否真有必要，值得重新问一遍。
- 若未来加"编辑课程（改名/改学分/改类型）"，这两份必须同步；现在没编辑入口所以没人踩，但接口一旦开放就是地雷。
- 载入逻辑有两条建课路径（[Courses] 段整体重建 / 老快照由班行推导），不变量「[Courses] 为准」只写在注释里，无校验。

### 方向（建议 A，B 为远期）
- **A（收口为"单权威 + 只读兼容"）**：
  1. 快照**保存**：`[TeachingClasses]` 的内嵌课程列改为**保存时从 `m_courses` 现取重填**，且文档写明"该段课程列仅作兼容、读端不得信"；`[Courses]` 是唯一权威。
  2. 快照**加载**：有 `[Courses]` 段时忽略班行课程列（现状即如此）；**增加一致性校验**——班行课程列与 `[Courses]` 不一致时告警或直接以 [Courses] 覆盖，杜绝静默分歧。
  3. CSV 导入维持 13 列内嵌格式（它是产品级单文件输入，合理保留），但解析时课程仍按 courseId 去重归并——语义与快照一致。
- **B（远期，另立项）**：彻底去掉 `[TeachingClasses]` 内嵌课程列，改为严格依赖 `[Courses]`；以"不再支持老构建读新快照"为代价换格式单一。需在快照版本号/迁移上先做决策，**不在本稿实施**。

### 涉及文件
- [snapshot.cpp](../../src/core/store/snapshot.cpp)（保存回填路径、加载一致性校验）、[datastore.h](../../src/core/store/datastore.h)（如需暴露一致性判定）、快照/CSV 往返测试。

### 风险 / 成本
- 低（A）。不改文件格式、不破坏老快照兼容；只加"保存端从权威源取数 + 加载端一致性校验"。

### 验收
- 新增用例：快照往返后 `[Courses]` 与 `[TeachingClasses]` 内嵌课程列**必然一致**；手工篡改班行课程列再加载 → 以 [Courses] 为准（或产生可见告警）；老快照（无 [Courses]）仍能打开。

> **实施记录（2026-09-04）**：经核保存端早已从 m_courses 现取（[snapshot.cpp:69](../../src/core/store/snapshot.cpp#L69) 调 courseById）、
> CSV 导入去重也已是现状（loadCsv 按 courseId 首行获胜），方向 A 第 1 / 3 点无需改代码；
> 实质改动只在加载端——`[Courses]` 覆盖后遍历 `[TeachingClasses]` 原始班行做逐字段一致性校验，
> 不一致以 `[Courses]` 为准（现状）并记入新暴露的 `DataStore::loadWarnings()` + qWarning，不阻断加载。
> 悬空班（courseId 不在 `[Courses]`）单独告警；浮点字段（credit/hoursPerSession）用 1e-9 容差比对，
> 规避 QString::number 往返精度误报。老快照无 `[Courses]` 段则跳过校验（课程由班行推导，无权威可比）。

> **验收更新（2026-09-04）**：4 用例并入 `tst_snapshot`（`coursesConsistentAfterRoundTrip` /
> `tamperedClassRowFallsBackToCourses` / `legacySnapshotWithoutCoursesOpens` / `danglingCourseIdWarns`），
> **不新建套件**，总数保持 9。口径：不一致 = 以 `[Courses]` 覆盖 + 暴露 loadWarnings（开发者确认）；
> CSV 导入端不纳入，维持首行获胜。

---

## 4. R7 — 会话级撤销 / 重做

### 现状
- 破坏性动作**不可逆**：删除教学班/整门课、重排换源只有确认框；手动调整整批一旦 apply 无 undo。
- 已有的"廉价备份"（记忆文件自动保存/Ctrl+S、工作区快照导出）没有接到"一键回退"上。

### 方向
- 关键利好：`DataStore` 全部成员是 Qt 值容器（QVector/QSet），**默认拷贝 = COW（写时复制）**——`DataStore before = m_store;` 是 O(1) 级的共享引用，之后任何一处 mutation 才 detach。因此**会话级 undo 环（ring of DataStore 值）成本极低**，无需序列化。
- **方案 A（推荐：动作边界快照环）**：
  1. 新增 `core/store/undobuffer`（或 ui 侧轻类）持有有界 `QVector<DataStore>`（如 20 份），`push(preState)` / `undo()` / `redo()`。
  2. 在**顶层可变动作边界**入环：`MainWindow` 的 新建/导入/新增/删除/锁定 处理、`TimetableController::showCourseDetail` 打开详情前（覆盖锁定/手动调整/删除三态）、`ScheduleController` 完成换源前（排课也算一次可撤销动作）。
  3. `undo()`：`m_store = ring.pop()` + `setDataStore` 全量重建 + 刷新（复用现有"换源 → 刷新"协议）；`redo` 对称。周/筛选重置口径与新建/导入一致。
  4. Ctrl+Z / Ctrl+Shift+Z + 顶栏按钮。
- 方案 B（改动更小的止血）：不做通用 undo，只给**删除**加会话级"最近删除回收"（`removeCourse/removeTeachingClass` 前暂存被删实体，弹窗可恢复）。覆盖窄，可作 A 的过渡。

### 涉及文件
- undo 环新类（ui/schedule 或 core/store）；`MainWindow`/`TimetableController`/`ScheduleController` 的动作边界接入；现有"锁定/删除/调整"的确认文案可保留（undo 是补救不是替代）。

### 风险 / 成本
- 中（A）。难点不在"存"，在**梳理清哪些边界入环**——漏一个 mutable 入口就等于没有。动作边界清单要随本稿固化并在接入处注释。排课换源入环时注意只压**换源前**一份，避免 worker 每进度 push。

### 验收
- 手工：删除整课 → Ctrl+Z 全量复原（含锁定集/失败明细）；手动调整整班后 Ctrl+Z 回到调整前；自动排课后 Ctrl+Z 回到排课前旧表；环满后最旧项被挤出（不崩）。锁定态属于 DataStore，自动覆盖。

### 实施记录（2026-09-04）
- 落点 = `UndoBuffer`（`core/store/undobuffer.{h,cpp}`，纯 core 无 QObject）动作边界快照环 +
  `MainWindow` / `TimetableController` 边界接线：
  - 新建 / 导入快照 / 新增课程·教学班 / 删除 / 锁定 / 自动排课（经中转槽 `onRunSchedulingClicked`
    捕获 before、`onSchedulingDone` 非取消才提交）六处顶层动作入环；详情弹窗整段一步
    （`CourseDetailDialog` 三标志 `removed/adjusted/locksChanged` 通过则 exec 前捕获、整段 push）。
  - 撤销/重做：`onUndoClicked` / `onRedoClicked` / `restoreStore`（整体换源 + 刷新协议）/
    `updateUndoActions`（按钮可用态）；顶栏按钮 + Ctrl+Z / Ctrl+Shift+Z 快捷键；
    排课 busy 期间按钮禁用（快捷键 isRunning 兜底）。
  - 不入环清单（代码注释固化）：保存 / 导出 / 周切换 / 筛选 / 主题 / 错误列表 / 启动自动恢复 /
    `onWorkerFinished` 异步换源（后者的入环由自动排课中转槽的 pending 提交段唯一承担，杜绝 double-push）。
  - 空步规避：删除按实删计数、锁定按集合净零变化、详情三标志早退、自动排课取消不 push、
    run 早退（无数据/无可动班）前置同守卫拦截（不发 scheduleDone → 防 pending 悬挂）。
- 测试并入 `tst_datastore`（6 槽，见下方验收更新），外加顶部 `storeBytesEqual`（快照逐字节）与
  `loadedSample` 两个文件级 helper。

### 验收更新（2026-09-04）
- 并入 `tst_datastore` 六槽：`undoRedoBasicCycle / pushClearsRedoStack / capacityEvictsOldest /
  undoRestoresStructuralDelete / undoRestoresEntryUpdate / undoRestoresInPlaceOverwrite`（后两槽分别对应
  手动调整落库与导入快照原地 clear+重填两条入环时序）。
- GUI 人工走查待做：删除整课 Ctrl+Z 全量复原、手动调整后 Ctrl+Z 回调整前、详情内锁定 Ctrl+Z
  整段一步、自动排课后 Ctrl+Z 回旧表、自动排课取消不产生空步、导入快照后 Ctrl+Z 回旧工作区、
  连续 21 步最旧被挤出（不崩）。

---

## 5. R8 — 排课条目 O(1) 索引

### 现状
- 条目不进懒建 ID 索引：`scheduleEntryById` / `updateScheduleEntry` 每次**线性扫描** `m_scheduleEntries`（architecture §3 注明刻意为之）。

### 问题与影响
- 当前调用次数少（手动调整/详情刷新各几次）无感；但 `scheduleEntryById` 会随"按条目查询"类功能增多而成为热点，且"知道能 O(1) 却不改"属于顺手可还的债。

### 方向
- 把条目并入现有懒建索引体系（lookup）：新增 `QHash<QString,int> m_entryIndex`（entryId → 下标）。
- **关键性质（可安全增索引的原因）**：集合是 `QVector`，下标在存续期稳定；
  - `addScheduleEntry` 尾插**不移动**既有下标 → 索引对已建条目一直有效；
  - `updateScheduleEntry` 原位替换**不改下标** → 无需失效；
  - 只有整表重建（`clearScheduleEntries` / `clear` / `dropClassRecords` 经清空重建）才 `invalidateLookupIndexes()`（复用现有清空点）。
- `scheduleEntryById` → 索引命中 O(1)（未建则懒建）；`updateScheduleEntry` 经索引定位后 `m_scheduleEntries[i] = updated`。
- 前提：entryId 唯一（R2 已强调）；防御性：索引构建时发现重复 id 取最后一条并在调试期告警。

### 涉及文件
- [datastore.h](../../src/core/store/datastore.h) / [lookup.cpp](../../src/core/store/lookup.cpp)；`clearScheduleEntries` 等清空点确认已失效。

> **实施记录（2026-09-04）**：`ensureLookupIndexes()` 一次建五张表（课程/班/教师/教室/条目），
> `m_indexesBuilt` 单标记共享；失效点收在 `clear / clearScheduleEntries / dropClassRecords`
> （后两处现各补 `invalidateLookupIndexes()`）；`addScheduleEntry` 在索引已建时把新尾巴补插进 `m_entryIndex`。
> `m_entryIndex` 重复 id 未做调试期告警（id 由 H1 规则保证唯一，追加路径不重复，靠断言语义兜底）。

### 风险 / 成本
- 低。行为零变化；纯性能。

### 验收
- `tst_datastore` 增：构造数百条目后 `scheduleEntryById` 命中；`updateScheduleEntry` 后下标序不变（快照序 = 卡序回归已有）；`clearScheduleEntries` 后再查返回 nullptr 且重建索引正确。

> **验收更新（2026-09-04）**：已并入 `tst_datastore`，新增四槽
> `entryIndexAfterManyAdds / entryIndexAppendAfterBuild / entryIndexClearInvalidates / entryIndexDropRecordsRebuild`。

---

## 6. 建议实施顺序

| 序 | 项 | 理由 | 规模 |
|----|----|------|------|
| 1 | **R8 条目 O(1) 索引** | 纯增量、零行为变化、立刻降热点 | S |
| 2 | **R3 约束收单源** | 纯抽取，全量引擎回归可证零回归 | S |
| 3 | **R2 entryId 代理化** | 依赖测试/字面量机械替换，无快照迁移 | M |
| 4 | **R4 课程双写收口** | 涉及文件格式语义与兼容决策，需人工先审口径 | M |
| 5 | **R7 会话级撤销** | 覆盖面最广、边界清单要人工敲定，放最后独立验收 | M |

> 每项独立提交、独立可验收；**不建议一次全做**（R4 的 A/B 口径与 R7 的边界清单都必须先经人工确认）。

## 7. 验收与回归

- 全量 `ninja test`（9 套；R8 条目索引并入 tst_datastore、R3 roomrules 用例并入 tst_conflicttable，套件总数不变；tst_annealing 最慢约 50–150s）。
- 每项改动后手工冒烟对应场景（见各节"验收"）；R2/R7 还需 GUI 走查（详情/调整/锁定/删除/重排往返）。

## 8. 不做 / 明确排除（另议）

评审中另三点**不在本稿**，各自更重、需独立立项：
- **点 1 写路径原子性**：manual::apply 原子性依赖 modal 单线程假设；若要"后台重排 + 前台并发微调"需先在 apply 内重校验 + store 级事务/版本，属并发模型变更。
- **点 5 求解架构**：贪心+SA 相对 CP/SAT 的建模局限（教师不可用日、双周课、合班、跨校区、设备教室、学时仅整数）——换求解器量级，应先补真实约束清单再决定。
- **点 6 锁定语义**："保留结果"与"彻底冻结该班"两义并存；真"永久手动优先"是独立产品特性，需先定口径。

## 建议提交信息（AI 不执行 git）

```
docs: 整理架构技术债清理方案 plan_v4.1（R2/R3/R4/R7/R8）

- R2 entryId 代理主键与展示标签解耦；R3 硬约束 H4/H5 收成共享 roomrules 单源
- R4 课程事实以 [Courses] 为唯一权威、班行课程列只读兼容并加一致性校验
- R7 会话级 DataStore 撤销/重做（COW 值环 + 动作边界清单）
- R8 scheduleEntryById/updateScheduleEntry 并入懒建 ID 索引 O(1)
- 点 1/5/6（写路径原子性、求解架构、锁定语义）明确排除另议
```
