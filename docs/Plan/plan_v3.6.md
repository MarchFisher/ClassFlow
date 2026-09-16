# ClassFlow 规划：教务手动调整 W4（plan_v3.6）

> 本文档与 v3.6 代码改动同步，记录范围、口径、设计与对 plan_v3.4 §5 的落地。
> 2026-09-03 与开发者敲定；版本管理由人类执行。

## 1. 为什么做（现状问题）

课表排完后教务仍需要人工微调某次课 / 整班（换时间、换教室），现有唯一途径是删除重排，破坏其余排课。W4 提供**直接改排课条目**的入口，只查硬约束，不经排课线程/退火。

关键事实（决定实现成本的前提）：现状数据模型**本来就是「每班 = N 条独立 ScheduleEntry，各自带 timeSlot(星期×节) 与教室」**（`models/timetable.h`）。因此“按每次课维护时间、逐次课去挪”的心智**不需要新结构**——每次课就是一条现成条目。本版几乎全是“对现有条目做受校验的原位编辑”，无新持久化、无引擎改动。

## 2. 已确认口径（开发者拍板）

1. 入口 = 课表点卡片 → 现有 `CourseDetailDialog` → 新增「调整本课」→ `ManualAdjustDialog`。
2. 手动调整 = **直接改 DataStore 的 ScheduleEntry**，不走排课线程/退火。
3. **每次课是独立对象**：同班各次课可各落不同时间/不同教室（不守“一班一教室”）。N = 该班 store 内**现时条目数**（排不满的班无条目、点不到卡）。
4. **整班搬（时刻可整体重选）语义取 (a)**：列出该班 N 行、各行独立选星期/起始节/教室，一次提交做**原子整批校验 + 落库**。刚性平移 / “一键全同教室”便利按钮**不在本期**。
5. 「第 k 次课」序号**仅展示、不落盘**：班内条目按 (day→startSection) 排序即时编号。
6. 只查硬约束 H1~H5：时间/教室/教师冲突（ConflictTable）、`room.capacity ≥ class.plannedSize`、`requiredRoomType != Any` 时须 `room.type` 匹配。软约束不管。
7. 调完**不自动保护**——想保留就随后手动锁定该班（锁定 = 下次自动排课原样保留；二者正交、可叠加）。
8. 不改周范围（startWeek/endWeek 保持原值）；一次课长度 = 原条目 span；改起始节时 end 不超作息最大节。**entryId 保持原值不变（不透明，不做反解析/重编号）**。
9. 单节 / 整班为**排他模式**（不做“本批只挑 2/3 条”的自由选择，边界二义留给以后）。

## 3. 设计决定

- **core 收“最小变更”**：`manual::Change { entryId; dayOfWeek; startSection; classroomId }`。core 按 entryId 反查旧条目，回填 `classId/teacherId/weeks/span` 拼出有效新条目 → “只许改时间+教室，其余不变”由 core 结构性强制，UI 拼不坏。
- **批量剔除边界**：被剔除集 = 本批每个 `Change.entryId` 对应的旧条目；其余（含同班未改次课、锁定班）全作背景种入 ConflictTable。逐条 `canPlace→place`，故批内自撞（同班同 slot / 同教室同时段）也会被命中。
- **`ConflictTable::busyKeysOf`**（新增只读小方法）返回命中的 busy 键前缀 R/C/T，供 UI 说清“教室被占 / 本班冲突 / 教师冲突”。
- **DataStore 只加 `updateScheduleEntry`（原位替换保下标，返回 bool）与只读 `scheduleEntryById`**，不加 remove（无删单节课场景）。原位更新保住 `scheduleEntries()` 遍历序 = 快照写出序 = 单元格卡序。
- **三层刷新协议**：ManualAdjustDialog 直接写 store（modal 单线程），成功后置自身 `adjusted()/summary()`；CourseDetailDialog 记 `m_adjusted` 并**保持打开**、按 entryId 现读刷新“上课时间/教室”两行（不 accept，方便接着锁定）；外层 `TimetableController::showCourseDetail` 在详情关闭后统一 `refresh()` + 状态栏一次。

## 4. 代码改动对照

### 4.1 core
- `store/datastore.{h,cpp}`：新增 `bool updateScheduleEntry(const ScheduleEntry &)`（线性扫 entryId 原位替换，找不到 false；条目不进 lookup 索引，无需 invalidate）与 `const ScheduleEntry *scheduleEntryById(const QString &) const`。
- `schedule/conflicttable.{h,cpp}`：新增只读 `QStringList busyKeysOf(const ScheduleEntry &) const`（复用私有 keysOf，`m_busy.contains` 收集；不改既有行为）。
- `schedule/manualmove.{h,cpp}`（新建，无 QObject）：
  - `struct Change{ entryId; dayOfWeek; startSection; classroomId }`
  - `enum class Reject{ None, EntryMissing, RoomMissing, CapacityTooSmall, RoomTypeMismatch, BadRange, Conflict }`
  - `struct Report{ ok; reject; index; entryId; day; section; busyTags }`
  - `Report validate(const DataStore &, const QVector<Change> &)`（只读）、`bool apply(DataStore &, const QVector<Change> &)`（先 validate，Ok 才逐条 update）。
  - helper：反查旧条目回填拼 `effectiveEntry`；staticReject（教室/容量/类型/星期与 end 范围，maxSection = `store.sections()` 最大 index）；ConflictTable = 全量条目剔除 replacedIds 后逐条 canPlace→place。
  - Remark：不查软约束、不改周/teacher/锁、entryId 不透明、apply 原子性依赖 modal 单线程。

### 4.2 UI
- `ui/dialog/manualadjustdialog.{h,cpp}`（新建）：`ManualAdjustDialog(DataStore&, const ScheduleEntry &clicked, QWidget*)`；`adjusted()/summary()`。
  - 每次打开**现读 store**：`scheduleEntryById(clicked.entryId)` 定位“本节”；该班全部条目按 (day→start) 排序做“整班”行。
  - 顶层 radio 排他：本节（默认，1 行）/ 整班（N 行，每行「第 k 次｜星期｜起始节｜教室」下拉，行首标注当前时间对照）。
  - 候选：`roomCandidates` = classrooms() 过滤 capacity≥plannedSize 且类型匹配、按 (capacity, roomNumber) 排序；`startCandidates` 使 start+span-1 ≤ maxSection（span 取该行旧跨度）。
  - 「确定」：组 batch Change → 与旧值全等则直接 accept（无改动）→ `manual::validate` 失败弹 `QMessageBox::warning`（第几行 + 中文原因：R 教室被占 / C 本班该时段已有课或批内互撞 / T 教师此时段有课，另有容量/类型/范围/缺失文案）保持打开 → `manual::apply` → 置状态 → accept()。Cancel/× 不落库。
  - v1 不做逐行即时粗判。
- `ui/dialog/coursedetaildialog.{h,cpp}`：新增成员拷贝 `m_entry`、`adjusted()/adjustSummary()`、`m_timeLabel/m_roomLabel`（把“上课时间/教室”两行存成员）、「调整本课」按钮（放锁定钮下、删除行上）；`scheduleEntryById` 或教学班/课程查无时禁用；锁定班**不禁用**。`openManualAdjust()`：exec 后 `Accepted && adjusted()` → 置 m_adjusted、存 summary、`refreshEntryLabels()`（按 entryId 现读重拼两行；查不到置“（已不存在）”）。
- `ui/timetable/timetablecontroller.cpp` `showCourseDetail`：关闭后按 `removed → adjusted → locksChanged` 分支统一 `refresh()` + 状态栏（adjusted 且未锁时追加“如需保留请锁定本班”）。

### 4.3 CMake / 文档
- 根 `CMakeLists.txt`：CORE_SOURCES schedule 段 + APP_SOURCES dialog 段登记新文件。
- `tests/CMakeLists.txt` TEST_SOURCES 加 `tst_manualmove.cpp`。

## 5. 测试（与代码同步）

- 新建 `tests/tst_manualmove.cpp`（loadCsv 样例，QTEST_GUILESS_MAIN）：
  单节换时间/换教室 OK；容量不足；类型不符；教室号不存在；教室被它班占 → Conflict(R)；同班两 Change 同 slot → 批内自撞 Conflict；整班两节一起换 OK；整批一节与他人冲突 → validate/apply false 且前后 entries 完全一致（不写库）；entryId 不存在 → EntryMissing；end 超作息最大节 → BadRange。
- `tests/tst_datastore.cpp`：`updateEntryInPlace`（id 下标不变、字段变、其它条目不动）、`updateUnknownReturnsFalse`。

## 6. 不改 / 明确不做

- 不动排课引擎（greedy/SA）；ConflictTable 仅加只读方法。H4/H5 在 manualmove 是第 4 处拷贝 → 标 TODO“将来抽共享 helper”，本轮为避免动 engine 不抽。
- 无“删除某次课 / 补排缺失节 / 改成某周不上”等条目增删语义。
- 不做整班刚性平移 / 一键同教室；不做持久“手动锁定”；不做 entryId 重编号。
- 详情弹窗存活期假设 store 仅经 modal 动作变更（不引入非 modal 并行编辑）。

## 7. 风险与边界

- **entryId 陈旧标签**：keep-entryId 下 id 字面与真实时间可能不吻合；唯一性靠 C 键（同班不落同 slot）保持，勿反解析 / 当时间用。
- **元数据缺失**（教学班/课程查无）无法判容量/类型 → 禁用调整入口，走删除/重排（`dropClassRecords` 能清记录）。
- **与自动排课关系**：未锁班手动改动会被下次全量/局部重排覆盖（预期，状态栏提示）；锁班作为背景保留。真“永久手动优先”是另一特性。
- **apply 原子性依赖 modal 单线程**；若未来改非 modal / 加异步，须在 apply 内重校验。

## 8. 验证

1. 构建（Qt Creator 标准构建目录，勿另建 Ninja 双构建）+ `ninja test`（9 套含 tst_manualmove；tst_annealing 最慢约 85–155s）。
2. 手工冒烟见本计划获批文件（plan_v3.6 的获批版）：单节改成功→详情两行即时变、关闭后卡片移格、筛选/周次保留；整班批量成功 / 一节冲突整体拒绝不写库；未锁重排被覆盖、锁定后保留；容量/类型/范围文案分别出现；无排课班 / 元数据缺失时「调整」禁用。

## 建议提交信息（AI 不执行 git）

```
feat: 教务手动调整——详情弹窗内经校验直接改排课条目

- core/schedule/manualmove：整批 Change 纯校验（H1~H5）+ 原子落库；entryId 不透明保持原值
- DataStore 加 updateScheduleEntry(原位替换)/scheduleEntryById
- ConflictTable 加 busyKeysOf 细分 R/C/T 冲突原因
- CourseDetailDialog 加「调整本课」入口（单节/整班两模式），ManualAdjustDialog 原子提交，外层关闭后统一刷新
- 只查硬约束，不自动锁定；文档补 plan_v3.6
```
