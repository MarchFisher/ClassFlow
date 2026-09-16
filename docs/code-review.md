# ClassFlow 代码审查指南（v5.0）

> 版本：v5.0 · 日期：2026-09-07
> 用途：给人类开发者做代码 review 的**检查清单与路线图**——指出各子系统里「改动哪里会连带坏哪里」、
> 重要函数之间的调用关系与先后约束，以及一批**已标注证据等级的待核实疑点**。
> 阅读对象：熟悉本仓库、准备走查某个模块或改某个功能的开发者。
>
> 证据等级：**[已核实]**= 已亲自对照源码确认；**[高疑]** = 静态分析强指向、建议先写用例复现再修；
> **[口径]** = 非 bug，但语义易被后续改动破坏，需确认是否想要的。

---

## 1. 总则：分层、依赖方向与「一改就坏」的全局不变量

### 1.1 分层与依赖红线

```
src/core/models   ← 纯数据结构（Course/TeachingClass/ScheduleEntry/…），无逻辑
src/core/store    ← DataStore 单一数据源（CSV/快照/索引/编辑方法/撤销环）
src/core/schedule ← 排课引擎（冲突表/贪心/退火/manualmove/roomrules）只读 store 产物
src/core/filter   ← 筛选纯判定
src/ui/*          ← Qt Widgets 表现层与编排（controller 是「换源/入环/禁用」的唯一入口）
```

review 时逐条对照：
- **core 四层禁含任何 Widget / QObject 信号槽依赖**（可独立单测）；`ui` 只能单向依赖 core。看到 `core/schedule` 里 `#include <QWidget>` 即失败。
- **排课引擎不读锁标记**：引擎只见 `movable` 集合参数；「锁定」→ movable 的换算发生在 `ScheduleController`（`unlockedMovableClasses()` = 全教学班 − (锁定 ∩ 已排)）。改引擎的人别去读 `lockedClassIds()`，改 controller 的人别忘未排课的锁定班仍要进 movable（否则变"永不排"）。
- **改库入口必须收敛**：能直接改 `DataStore` 的位置只有——排课 worker（副本）、`CourseDetailDialog`/`ClassEditDialog`（详情编辑，modal 内直改）、`editui`/`datastore_edit`（编辑落库）、`manual::apply`（手动调整）、MainWindow 槽（新建/导入/增删/锁定）。新增弹窗想改库，先看能不能复用这几个，别新开旁路。

### 1.2 全局状态不变量（任何改动前先问：有没有破坏下面某条）

1. **`m_store` 唯一数据源且地址不变**。控制器持 `DataStore&`、模型缓存 `const DataStore*`。换源（排课/撤销/重做/新建/导入）一律**拷贝赋值到同一个对象**（`m_store = x;`），绝不允许换一个指针。一旦改为"重指向新对象"，模型/控制器的旧指针全部失联。
2. **撤销步边界**：只有这些动作各自恰好 push 一步——新建 / 导入 / 新增课程 / 新增教学班 / 删除 / 锁定（净变化）/ 自动排课 / 编辑 / 详情整段。自动排课走 `m_schedPending` 中转防 double-push（onRunSchedulingClicked 置位、onSchedulingDone 非 aborted 提交一次）；取消 / 早退**不 push**。
3. **脏标记 ● 的基准是 L2 项目文件 `m_projectText`，不是 L1 自动文本**。保存/导入成功才熄 ●；「导出快照」不落 ●；undo/redo 后 ● 由 1.5s 防抖 timer 末尾 `updateWindowTitle()` 兜底（有 ≤1.5s 陈旧窗口，属已知）。
4. **ID 索引纪律**：凡「下标可能整体变动」的操作（`clear`/`clearScheduleEntries`/`addCourse`/`addTeachingClass`/`removeTeachingClass`/`removeCourse`/`dropClassRecords`/`[Courses]` 重建）必须 `invalidateLookupIndexes()`；**原位替换**（`updateCourse`/`updateTeachingClass`/`reassignClassTeacher`/`updateScheduleEntry`）不改 id 不改序，可免失效——前提是调用方不得用它们改主键（见疑点 5.2）。
5. **`entryId` 是不透明锚点**：手动调整靠它反查旧条目并原位替换；改时间/教室时 entryId **保持不变**。任何"按内容反推 entryId / 重编号 / 在 id 里内嵌时间"都会破坏手动调整锚点与老快照兼容。
6. **`[Courses]` 段是课程事实唯一权威**：快照加载时班行内嵌课程列与 `[Courses]` 不一致以课程段为准并进 `loadWarnings`，不阻断。别把"读班行课程列"当权威路径（只在无 `[Courses]` 的老快照回退用）。
7. **课次守恒 / SA 只优不劣 / 硬约束零违反**：贪心每班恰好排 `sessionsPerWeek` 次；SA 的 best 初值=贪心解、只在更优时更新；冲突 move 一律拒绝（M=∞）。改邻域/接受准则时先保这三条。
8. **模型裸指针 + refresh 语义**：换源成功后模型 `refresh()` 保留筛选/周次、按 courseId 重分卡色；undo/redo 的历史态可能缺筛选引用对象，必须走 `setDataStore`（重置筛选为空）。**任何换源后漏 `applyCardPalette` → 新课程卡色是旧主题或回退首色。**
9. **busy 禁用 11 入口**（自动排课/撤销/重做/锁定/新建/导出/新增/删除/编辑/导入/筛选）；**保存与周选择器豁免**（保存写的是排课期间不动的静态 m_store）。
10. **冻结语义**：冻结=「已有条目且 ∉ movable」，不是锁标记本身。局部排时冻结条目原样种入冲突表占位，引擎永不移动。

---

## 2. 按主链路看「函数先后关系」与断点

> 阅读习惯：先看调用链图找"谁是入口、谁是落库点、谁在中间做裁决"，再对应「断点」逐条盯。
> 行号仅作锚点提示，函数名是准的。

### 2.1 导入 / 启动自动恢复链

```
MainWindow 启动 / 侧栏「导入快照」/ 空态页 CTA
  └─ onImportClicked: before=m_store 捕获(在 load 之前!)
      └─ DataStore::loadSnapshot(path)
            ├─ store::readAllLines → 切九段（[Courses]/[Locks] 可缺）
            ├─ 把 tc/room/sec 写临时 CSV → 复用 DataStore::loadCsv（内部先 clear()）
            ├─ [Courses] 覆盖 m_courses + invalidateLookupIndexes()        (snapshot.cpp:240-262)
            ├─ R4 一致性比对（courseById() 首次触发建索引）→ loadWarnings    (:268-312)
            ├─ [Term] / [Teachers] 覆盖 / [ScheduleEntries] / [Errors] / [Locks]
      └─ 成功: setDataStore/applyCardPalette/updateEmptyState/syncWeekSelector;
            m_projectFile=path; m_projectText=snapshotText(); 写 QSettings 记忆
      └─ 失败: 不刷 UI（数据已可能半清），靠 Ctrl+Z 兜底（before 已入环）
```

断点与重点：
- `before=m_store` **必须先于 loadSnapshot**（它内部 clear+重填，原地破坏），否则撤销也救不回原工作区。
- `loadCsv` 失败即返回 false 且**不自行恢复**（store 已清/半清）——恢复责任在调用方的 before。别"优化"成 loadCsv 自己 rollback（当前契约是调用方兜底）。
- 老格式 `[ScheduleEntries]` 7 列 → 周范围依赖结构体默认 1~16；**`[Teachers]` 重建无 invalidate** → 见疑点 5.1（高疑）。
- 加载顺序敏感性：`[Term]` 必须在 loadCsv 之后覆盖（loadCsv 的 clear 已把 semesterWeeks 复位 16）。

### 2.2 自动排课链（全量 / 锁定重排 / 局部 / 最小排共用一套引擎）

```
onRunSchedulingClicked（MainWindow 中转）
  守卫: isRunning / 空班 / unlockedMovableClasses().isEmpty() → run() 内早退(不置 pending)
  ├─ m_schedBefore = m_store;  m_schedPending = true;  run()
ScheduleController::run()          └→ movable = unlockedMovableClasses()
  └─ beginSchedule(movable, "自动排课")
        ├─ setBusy(true)                    // 禁 11 入口
        ├─ DataStore copy = m_store → ScheduleWorker(std::move(copy), movable) + QThread
        ├─ worker.moveToThread; QProgressDialog
        ├─ 接线: started→run(Direct 到 worker 线程内执行)
        │        progress→onProgress; finished→onWorkerFinished + thread.quit + worker.deleteLater
        │        thread.finished→thread.deleteLater + 清 m_thread/m_worker
        │        progressDialog.canceled→worker.cancel() 【必须 DirectConnection】
        └─ thread.start()
ScheduleWorker::run()（worker 线程, 私有副本）
  └─ Scheduler::schedule(副本, nullptr, &ctx, movable)
        ├─ 冻结快照(∉movable 的已有条目) → clearScheduleEntries/clearScheduleFailures → 回填冻结
        └─ SimulatedAnnealingStrategy::run
              ├─ GreedyStrategy::run（初始可行解；失败明细在这里产生并沿用）
              ├─ Candidate::buildFromEntries + setLockedClasses(frozen=候选∉movable)
              ├─ ConflictTable 全 place + LoadSnapshot
              ├─ sampleInitialTemp → 几何降温主循环(M1~M5: commit→Δ→Metropolis→commit/rollback)
              └─ best.applyToStore(副本)   // 取消也写回 best, 由 aborted 标识丢弃
  └─ emit finished(副本, ScheduleResult{ok,aborted,scheduledCount,failedCount,…})
ScheduleController::onWorkerFinished（queued 回主线程）
  ├─ 关进度框; setBusy(false)
  ├─ aborted → 不换源; emit scheduleDone(false,0,0,true)
  ├─ 否则 m_store = 副本; m_model.refresh()
  ├─ 升格询问(newClassMinFailed && pending): Yes → singleShot(0) 再 beginSchedule(全未锁定) [时序!]
  └─ emit scheduleDone(ok,…)
MainWindow::onSchedulingDone
  ├─ m_schedPending && !aborted → push(m_schedBefore)（一步入环）; 清 pending
  ├─ applyCardPalette / syncWeekSelector / updateErrorBadge / 失败弹错误列表
```

断点（本仓库最脆的时序区）：
- **worker 从不在 run 期间跑事件循环** → 所有控制信号必须 Direct 或原子位：`cancel()` 置 `std::atomic<bool>`，进度框 canceled→cancel **DirectConnection**（queued 的 cancel 排在 run 之后永不执行）。
- `finished` 载荷必须**值拷贝回传**，主线程不得事后读 worker 成员（生命周期竞争）。
- **升格重排必须 `QTimer::singleShot(0)`**：本函数在 finished 排队事件里运行时，线程清场 lambda（`m_thread=null`）还没跑，而 `beginSchedule` 有 `if(m_thread) return;` 重入守卫——不延迟会被**静默吞掉**。
- **部分成功（!ok）也要换源**：失败班同样落到主源（否则"失败列表"与实际课表不符）。别在 `!ok` 时误判为不落地。
- `onRunSchedulingClicked` 的早退判据是 `run()` 内部判据的**手工副本**（isRunning/空班/无可动）。两者一旦漂移 → pending 永久悬挂。改动 run 的守卫时同步改这里。
- closeEvent 里 `cancelAndWait()` 是阻塞 wait：若恰在 worker 已 emit finished、quit 还没被主线程消费的窗口内触发，可能死锁。别在模态框存活期间依赖它。

### 2.3 撤销 / 重做链

```
MainWindow::onUndoClicked / onRedoClicked（isRunning 快捷键兜底）
  └─ m_undo.undo(m_store, &target)        // 当前态 append 进 redo 栈; *target=undo栈尾
  └─ restoreStore(target, status)
        ├─ m_store = target（原地赋值，保地址）
        ├─ m_model.setDataStore(&m_store)  // 重置筛选=空(历史态可能缺引用对象)
        ├─ applyCardPalette / updateEmptyState / syncWeekSelector / updateUndoActions
        └─ (不 updateWindowTitle → 靠 1.5s timer 兜底 ●)
UndoBuffer::push(before)  // append→清空 redo(开新分支)→超容量 removeAt(0) 淘汰最旧
```

断点：
- **undo/redo 自己绝不再 push**（目标态已被环管理）；`restoreStore` 后 UI 全量联动是它唯一的副作用。
- 容量语义：undo 栈 ≤20；redo 栈间接有界（每 pop 一个 undo 才产生一个 redo）。
- 撤销把工作区弹回「无班」时，`updateEmptyState` 要能切到空态页（入口已包含）。
- UndoBuffer API：`undo(current,&target)` 失败时（栈空）不改 target；调用方要 `canUndo/canRedo` 先判或容忍不改。

### 2.4 编辑链 A：侧栏「编辑」（浏览式，只改基本信息）

```
MainWindow::onEditClicked
  └─ EditInfoDialog(m_store, 空, 空) 浏览双页签（只读源，零落库）
  └─ 读 result(): courseEdited→editui::applyCourseEdit / classEdited→editui::applyClassEdit
  └─ editui::applyClassEdit(store, tc, promptParent) → ApplyOutcome{changed,teacherBlocked,roomMoveNeeded,message}
        ├─ 撞车预检: teacherChanged && hasEntries && !newTeacher 空 → teacherSwapClashes(纯读)
        ├─ 扩容预检: sizeChanged && hasEntries && planned 变大 → 扫各条目教室容量
        ├─ overflow → QMessageBox::question: 拒绝→(撞车仍登记) / 接受→roomMoveNeeded=true
        ├─ 落库: 撞车→registerSwapConflict(reason 前缀"拟换任课教师") 不换师
        │        否则→updateTeachingClass + reassignClassTeacher(班+全部条目 teacherId 同步) + removeSwapMarker
        └─ changed = fieldChanged || clash   // "仅登记冲突"也算 changed(一步撤销)
  └─ 改成功: push(before)+refresh; roomMoveNeeded→runMovable({classId})
```

断点：
- **换师撞车 = 登记延迟应用，不是硬失败**：`teacherSwapClashes` 纯读预检、`reassignClassTeacher` 不判撞（预检/应用分离）。撞车分支 changed=true → 调用方 push 一步——语义是"这次编辑(含登记)可撤销"，别误判为成功而清登记。
- **reassign 要连带改写该班全部已排条目的冗余 `teacherId`**，否则按教师查课表 / H3 判定 / 卡片显示会失联。删掉这条就坏。
- 扩容只在 `plannedSize 变大` 时弹确认（缩人数不弹，合理）；拒绝扩容且无撞车时 classEdited 视为未应用。

### 2.5 编辑链 B：详情 →「编辑」（合并窗，基本信息 + 时间·教室 一次保存）

```
CourseDetailDialog「编辑」→ ClassEditDialog(store 非 const, classId, entryId)
  onOkClicked 顺序（设计硬约束，别重排）:
  0  m_infoForm->validate()             // 容量≥人数、课程名非空
  1  collect()(只取确有变化) + m_timeRoom->collectChanges(&changes,&changedCount)
  2  全无改动 → warn 留窗
  3  changedCount>0 → manual::validate(纯读预检) 失败 → describeReject 留窗(零副作用)
  4  manual::apply 落排课 → m_adjusted=true        // 先落排课
  5  applyCourseEdit(课程名/学院即时生效)
  6  applyClassEdit → 读 roomMoveNeeded/teacherBlocked/changed  // 后落基本信息
  7  anyApplied = scheduleApplied||courseApplied||out.changed
     全无 → info 留窗; 有任一 → accept
```

断点：
- **顺序价值**：先落排课再落基本信息，使"同窗移时段 + 换新师且不撞"一次成立；页签2 选了大教室可免扩容弹窗。
- **落排课改动是分步的**（validate 纯预检→apply 落库）。ClassEditDialog 是 modal 单线程，预检与应用之间无第三方写入才安全——别把别的弹窗/信号插进这两步之间。
- 详情（CourseDetailDialog）只做**标志搬运**（adjusted/edited/roomMoveNeeded/teacherBlocked），**绝不 push**；撤销由外层 TimetableController「整段一步」负责（见 2.9）。

### 2.6 手动调整核心链（manualmove：最小变更 + 原子落库）

```
TimeRoomEditor（只表达"要改成什么"）
  ├─ hasSessions()/sessionCount(): 现读课次, 宿主据此启用页签(未排课的班禁用)
  ├─ collectChanges(out,&changedCount): 组 manual::Change{entryId,day,startSection,classroomId}
  │      entryId 恒为锚; changedCount=与 store 现值确实不同的行数
  └─ describeReject(report): Conflict 按 busyTags 前缀 R/C/T → 教室被占/本班冲突/教师冲突
        │
        ▼
manual::validate(store, changes)  纯读
  ├─ resolveAll; dup entryId → Reject
  ├─ 每条: scheduleEntryById(entryId) 反查旧条目 → EntryMissing(课/班缺)/RoomMissing(教室缺)
  │        → roomrules::capacityOk(H4) / typeOk(H5) → BadRange(越界/跨度超最大节)
  │        → e=*old 只改 classroom+起止节（保持旧跨度、周范围、教师、entryId——结构性强制）
  ├─ ConflictTable: 背景=全量条目剔除本批 replacedIds → 新条目逐条 canPlace→place（批内自撞也命中）
  └─ 失败填 busyTags
manual::apply(store, changes)
  ├─ resolveAll + 再 validate 一次(防御; 依赖 modal 单线程无第三方写入)
  └─ 逐条 DataStore::updateScheduleEntry(effective) 原位替换; 任一失败整批不写库
```

断点：
- **只许改时间+教室**：其余字段由 core 从旧条目回填——这是"调用方拼不坏"的结构性保证。任何"允许传任意 ScheduleEntry 进来落库"的重构都在拆这道闸。
- `updateScheduleEntry` 按 entryId 原位替换保下标 = 保快照序 = 保卡片序（整班搬后卡片不漂移）。
- 锁定不拦截手动调整（锁只在自动重排时经 movable 生效）——若哪天想"锁定班不许手动挪"，要在此加，但注意与现有口径的差异（见疑点 5.11，口径）。

### 2.7 增删课程/班、锁定链

```
新增(MainWindow::onAddCourseClicked): AddDialog 双页签
  AddCourse → addCourse → push(before), 不排课(提示去加班)
  AddClass  → addTeachingClass → push(before) → ScheduleController::scheduleNewClass(classId)
删除(onDeleteClicked): DeleteDialog.request 合并 → removeCourse 先 / removeTeachingClass 后(跳过已删课班级)
  removedClasses>0 才 push(避免空步)
锁定(onLockClicked): LockDialog(三态树) → next==lockedClassIds() 净零不入环 → setLockedClasses → push → refresh
```

断点：
- 删班不删课：`removeTeachingClass` 只删班及记录（排课/失败/锁），到最后一班课程留空课；删整门用 `removeCourse`（先收集该课全部班逐个 `dropClassRecords`）。
- **删除时 removeTeachingClass/removeCourse 内部失败会静默 return**（CourseDetailDialog confirmDelete 亦然）——确认弹窗说删了就该保证删干净，否则撤销步与实际不一致。
- 锁定 `setLockedClasses` 整体替换；**锁与存在性解耦**（`lockClass` 不做 classId 校验；`removeTeachingClass` 会顺手清锁）。

### 2.8 双层保存链（指纹驱动）

```
任何改动 → 1.5s 防抖 timer(timeout):
  text=snapshotText(); if(!teachingClasses空 && text!=m_autoText){ writeWorkspace(); m_autoText=text; }
  末尾无条件 updateWindowTitle()   // ● = snapshotText()!=m_projectText
Ctrl+S → persistProjectFile: 无 m_projectFile 则 getSaveFileName(首次=导出语义)
  → saveSnapshot → 设 projectFile/projectText → 写 QSettings → updateWindowTitle
侧栏「导出快照」→ saveSnapshot 另存; 不改 projectFile/projectText（不落 ●）
```

断点：
- `m_autoText` **只在 timer 内更新**；onNewClicked/closeEvent 的 writeWorkspace 是 flush 语义、不改它 → 新建后会补一次重复写（无害）。
- **删到全空不写 workspace.dat** → 下次启动"复活"删空前的数据（已知接受，但 review 新逻辑时别误改：它防的是误关全空仍留档？见疑点 5.12）。
- `snapshotText()` 的稳定性（确定遍历顺序 + `[Locks]` 排序）是**指纹可比的前提**——任何人往快照里加一段时，若遍历 QSet/无序容器会破坏脏态判定。

### 2.9 课表显示链（画 = 点的几何单源）

```
TimetableView::mouseReleaseEvent(左键)
  index=indexAt(event->pos())  (viewport 坐标)
  cell=visualRect(index); local=event->pos()-cell.topLeft()
  delegate->entryAt(cell, index, local)  → entryClicked(day,col+1; section,row+1; ordinal) / moreClicked / NoHit
  → 交回基类维持选中
TimetableDelegate（paint 与命中共用 layoutCards 同一坐标系）
  visibleCourseCount(n): ≤3 全显, >3 显 2 (kShowAllUpTo=3 / kShowOnOverflow=2)
  hasOverflow / slotCount(n): visible + (overflow?1:0)，一格最多 3 视觉块
  layoutCards(cellRect, n): 纵向均分→N 个矩形（paint 用 option.rect / 命中用 visualRect，同系）
  paint: 前 visible 张 paintCard + overflow 则末位 paintMoreCard("更多 +N")
  entryAt(cellRect, index, cellLocalPos):
    n==1 → 整格 contains(local) 即中(消 2px 留白漏点)
    n>1 → absPos = cellLocalPos + cellRect.topLeft()【必须加回左上角】 再与 layoutCards 比
TimetableModel::data(): DisplayRole=首条课程名单行(兜底); ToolTipRole=逐课周范围; 无颜色
  m_byCell 键 "day|section"（data/rebuildCells/entriesAtCell 三处同一字符串拼接约定）
MainWindow → TimetableController::showCourseDetailAt/showMoreCourses
```

断点：
- **多卡命中漏加 `cellRect.topLeft()` 会复现历史 bug**（"只有首行首列能点开详情"）——`layoutCards` 返回 cellRect 坐标系，命中的 local 是格内坐标，二者差一个格左上角。
- n==1 全格命中 vs 多卡只命中卡矩形的不对称是**有意为之**，别当 bug 改。
- paint 与 entryAt 必须共用 `layoutCards(cellRect,…)` 且 cellRect 同源（option.rect vs visualRect 是同一坐标系）；谁另写一套几何谁负责 bug。
- `"day|section"` 字符串键是三处隐式重复约定，改格式整网格漂移。
- 模型缓存裸指针、`refresh()` 内部是 setDataStore+再 reset 的**双重整体重建**（浪费但正确）——reset 间隙别去读模型。

### 2.10 详情弹窗状态机（「整段一步」撤销）

```
TimetableController::showCourseDetail(entry)
  const DataStore before = m_store;        // exec 前捕获（关键!）
  dlg.exec()                                // CourseDetailDialog 直接改 m_store(modal 同步)
  removed||adjusted||edited||locksChanged 全假 → return(净零变化不入环)
  任一为真 → push(before) 整段一步 + emit undoStackChanged → m_model.refresh()
  !removed && editRoomMoveNeeded → emit rescheduleNeeded(classId)
        → MainWindow::onRescheduleNeeded → runMovable({classId})（异步换源, 不重复 push）
  状态栏: removed>adjusted>edited>locksChanged 取一; 调整未锁时补"如需保留请锁定本班"
```

断点：
- 「整段一步」成立的两个前提：**before 在 exec 前捕获** + **弹窗内禁止中途向外 flush/入环**（详情内每次编辑只搬运标志）。破坏任一个 → 撤销回到错误的中间态。
- 若将来弹窗引入"改后又改回原样"的净零路径却没清标志，会 push 空撤销步（当前靠 `!…` 早退挡住，是脆弱点而非防御）。
- 删除会先 accept 关窗 → removed 分支报告删除；调整未锁追加提示是 UX 口径，别删。

### 2.11 主题 / 图标链

```
main.cpp: 创建 MainWindow 前 ThemeManager::instance().apply()(防首帧闪白)
ThemeManager 单例: scheme(跟随系统/亮/暗) × 5 accent, QSettings 持久化
  apply() → renderQss 模板( %VAR% 替换) → qApp->setStyleSheet
  themeChanged 信号 → MainWindow: applyCardPalette + applyTopButtonIcons
themeicons: fromFeather(name,color) 读 SVG 字节替换 currentColor→渲 24×24(dpr)
  buttonIcon(name,color,disabledColor): 同一 SVG 渲两张, 分别登记 Normal(color)/Disabled(disabledColor)
     —— QIcon 禁用时只切 Disabled 态位图, 不会自动转灰, 故须显式补
```

断点：
- 语义色（accent/danger/surface/tint/secondary/disabled）都由访问器**现算**，不落 QPalette——自绘代码要色一律走 ThemeManager 访问器，别写死。
- 选中格描边用 `ThemeManager::accentColor()`（QPalette.highlight 恒默认蓝，不随 accent 走）。
- `setStyleSheet` 全量重渲整棵 widget 树：主题切换的卡顿要在卡片多的数据上实测。
- Feather 的 `currentColor` QtSvg 不解析会落黑 → 必须走 fromFeather 替换。

---

## 3. 数据层内部：函数关系与索引纪律（store 专属）

```
addScheduleEntry(尾插)      → 已建索引时仅补尾(m_entryIndex[entryId]=末尾下标)    [索引免失效]
clearScheduleEntries         → invalidateLookupIndexes()(整表重建)
addCourse/addTeachingClass  → 判重(线性) + append + invalidate
removeTeachingClass         → dropClassRecords + removeAt + invalidate
removeCourse                → 收集该课全部班 → 逐个 dropClassRecords → 过滤班行 → removeAt + invalidate
updateScheduleEntry(原位替换)→ 按 entryId 定位 m_scheduleEntries[i]=updated; 不失效、不校验
updateCourse/updateTeachingClass/reassignClassTeacher → 原位改写, 不改 id/序 → 不失效
dropClassRecords(私有)      → 过滤 entries/failures + 清锁; 末尾 invalidate
snapshotText()             → 九段确定顺序; 班行课程列 courseById 现读回填; [Locks] 先 sort
loadSnapshot()             → [Courses] 重建+invalidate; R4 比对走 courseById(首建索引); [Teachers] 覆盖[!]
```

断点：
- **索引失效是"纪律哨兵"**：审查任何一个新增/删除/加载/clear 的改动，先问"下标会不会整体变？变了有没有 invalidate？"。漏一处 = `teacherById`/`courseById` 等静默错位或越界。
- `updateScheduleEntry` 把 entryId 当不透明锚点、不校验传入条目与库内是否同 id——调用方改了 entryId 会造成**假象命中**（新 id 无映射、旧 id 指向换名条目）。防御应加在方法内。
- 原位替换系列（update*/reassign）的安全前提是调用方不改主键——契约未强制，review 时盯调用方。

---

## 4. 每文件 review 焦点速查（按改动文件查）

| 文件 | 改它时重点看 |
|------|--------------|
| mainwindow.cpp | pending 悬挂(2.2)、失败导入不清 UI、● 依赖 timer、busy 禁导出不禁保存的自洽性、导出弹框期间 scheduleDone 换源一致性 |
| timetablecontroller.cpp | 详情整段一步前提(2.10)、四标志净零早退、rescheduleNeeded 时序与 beginSchedule 守卫一致 |
| schedulecontroller.cpp | singleShot(0) 升格时序、aborted 不换源、cancelAndWait 死锁窗口、busy 清单 |
| scheduleworker.cpp | cancel 必须 Direct+原子位、finished 载荷值拷贝 |
| timetabledelegate/view | layoutCards 单源、entryAt 加回 topLeft、n==1 vs 多卡不对称、交回基类 |
| timetablemodel | "day\|section" 键约定、refresh 双重 reset、裸指针依赖换源不变址 |
| snapshot.cpp | [Teachers] 覆盖缺 invalidate(5.1)、[Courses] 权威覆盖+告警只写 warnings、7 列老格式回退、load 顺序 |
| datastore.cpp/lookup.cpp | 每个改向量的入口是否恰当地 invalidate(索引纪律)、updateScheduleEntry 锚点契约 |
| datastore_edit.cpp | reassign 连带条目 teacherId、removeScheduleFailureForClass 语义过宽(5.4) |
| csv.cpp | 13 列强校验静默丢行、Any 不闭环、教室/教师不去重(5.8) |
| undobuffer.cpp | push 清 redo 开新分支、容量淘汰最旧、undo 后才可 redo |
| strategy.cpp | sections 空→maxSection=0 全失败、hours 整数判定、半截保护(全模式全空才落子) |
| annealingstrategy.cpp | best 深拷贝(CoW)隐蔽性、aborted 后仍 applyToStore、停滞只从 0.5·t0 起计 |
| annealingcandidate.cpp | commit/rollback journal 单槽不防重入、m_info 缓存不与课程编辑同步 |
| annealingmoves.cpp | M1 允许同日多课、M3 QHash 迭代、move 共享 movable 空短路 |
| annealingcost.cpp | waste 无负钳制、variance 除零 NaN(5.9)、SoftWeights 注释漂移(5.10) |
| conflicttable.cpp | place/remove 不做存在性断言、C 键只锁班内(建模盲区) |
| manualmove.cpp | validate 纯读、apply 再验依赖单线程契约、不校验锁定态 |
| roomrules.cpp | 单源 H4/H5——改一处全引擎生效，必须全量回归(贪心/M4/M5/manualmove 四消费方) |
| editui.cpp | 预检/应用分离、撞车登记 changed=true 语义、扩容只在增大时弹 |
| classeditdialog.cpp | onOkClicked 顺序(2.5)、anyApplied 静默丢提示(5.3) |
| coursedetaildialog.cpp | 锁→解净零仍置 locksChanged(5.5)、删除失败静默 return、三钮直改 store |
| classinfoform/timeroomeditor | 「未安排」哨兵文本、collectChanges 恒 true 押宿主 validate |
| theme/themeicons | setStyleSheet 全量重渲、buttonIcon dpr、renderQss 读资源失败=全应用无样式 |

---

## 5. 待核实疑点清单（按证据等级排序，建议先复现再决定修不修）

### 5.1 [高疑] `loadSnapshot` 里 `[Teachers]` 覆盖后索引未失效
**位置**：[snapshot.cpp:323-336](src/core/store/snapshot.cpp#L323-L336)
**已核实的不对称**：`[Courses]` 重建后紧跟 `invalidateLookupIndexes()`（L262），而 `[Teachers]` 段 `m_teachers.clear()+append` 覆盖真表**没有失效**。且失效缺失路径的触发点是 R4 比对（L275 `courseById()`）会先 `ensureLookupIndexes()` **一次性建齐五张表**——此时 m_teachers 还是 loadCsv 的推导表（name=id、按教学班首现顺序）；之后真表以不同顺序/长度覆盖。此后 `m_indexesBuilt==true`、m_teacherIndex 指向旧推导表下标 → `teacherById` 可能错返回他师 / 返回 nullptr / 越界 `.at()`。
**复现建议**：构造快照，其中 `[Teachers]` 顺序 ≠ 教学班行 teacherId 首现顺序（标准 保存→加载 往返就可能满足）。加单测：`loadSnapshot` 后断言若干 `teacherById` 与真表一致。
**注**：UI 层 `teacherNameOf` 是否经 teacherById 决定实际可感影响——若模型是遍历 m_teachers 构建名字表，则此 bug 只在走索引的路径（按教师筛选等）暴露。

### 5.2 [高疑] `updateScheduleEntry` 锚点契约不校验
**位置**：[datastore.cpp](src/core/store/datastore.cpp) 的 `updateScheduleEntry`
**风险**：按传入 `updated.entryId` 定位后整体赋值；若调用方传入条目的 entryId 与库内不同 → m_entryIndex[旧 id] 指向一条"换名"条目（新 id 无映射）。目前唯一落库方 `manual::apply` 严守 `e=*old` 保 entryId，属防御缺失而非现行 bug。**修法**：方法内校验 `updated.entryId` 与定位条目一致（不一致 return false/assert）。

### 5.3 [高疑] ClassEditDialog：信息改动被扩容确认拒绝时提示可能被吞
**位置**：[classeditdialog.cpp](src/ui/dialog/classeditdialog.cpp) 的 `onOkClicked` 去留判据
**机制**：`anyApplied = scheduleApplied || courseApplied || out.changed`。若本次同时改了排课/课程 + 计划人数，且扩容被拒绝，`out.changed` 可能为假但 `anyApplied` 因前两者为真而**恒真** → 扩容拒绝的提示既不进摘要也不弹窗，accept 后**人数/容量改动被无声丢弃**。
**验证**：给既有排课的班：先在同一窗改排课（或课程名），再把计划人数改到超过当前教室容量 → 确认弹"拒绝"后是否无任何提示地关窗。

### 5.4 [中] `removeScheduleFailureForClass` 语义过宽，与 UI 的 `removeSwapMarker` 冲突
**位置**：[datastore_edit.cpp](src/core/store/datastore_edit.cpp)
**风险**：store 版会清掉该班**全部**失败项（含引擎真失败）；editui 用私有 `removeSwapMarker` 只清 reason 前缀「拟换任课教师」的登记、保留引擎失败。目前 store 版**无生产调用方**（仅测试）——属"缺口"。谁要是换师成功路径误调 store 版，会静默抹掉排课失败提示。review 新调用方时盯这个。

### 5.5 [中] CourseDetailDialog：锁→解净零仍入环
**位置**：[coursedetaildialog.cpp](src/ui/dialog/coursedetaildialog.cpp) 锁钮 lambda
**风险**：先锁再解（净零），`m_locksChanged` 仍为 true → 外层 push 一个空撤销步，占一个 undo 槽。外部 LockDialog 已做"净零不入环"（`next==lockedClassIds()`），详情内没有。**修法**：exec 后或锁钮处比对锁定集净变化。

### 5.6 [中] `m_schedPending` 悬挂风险（守卫判据的手工副本）
**位置**：[mainwindow.cpp](src/ui/mainwindow.cpp) `onRunSchedulingClicked`
**机制**：前置守卫（isRunning/空班/`unlockedMovableClasses().isEmpty()`）是 `run()` 内部早退判据的复制。任一漂移 → pending 置位后 run 早退，scheduleDone 永不回来 → pending 悬挂。已注释提醒，仍是维护雷。改动 run() 的守卫分支时务必同步此槽。

### 5.7 [中] busy 禁导出不禁保存的自洽性（口径待确认）
**位置**：[mainwindow.cpp](src/ui/mainwindow.cpp) busy 禁用清单 / 注释
**现状**：导出与保存都调 `snapshotText()`，却只禁导出不禁保存。注释理由是"导出会在线程写盘途中抓到半截结果"——但 worker 排副本期间 `m_store` 恒为动作前内容，snapshotText 不会撕裂。更可能的真实动机是防「导出文件对话框打开期间 worker 完成换源」。建议改注释或改实现（两者取一致），review 时确认。

### 5.8 [中] CSV / 快照解析的静默降级
**位置**：[csv.cpp](src/core/store/csv.cpp)、[snapshot.cpp](src/core/store/snapshot.cpp)
- 教学班行 `f.size()!=13` **静默丢行**（无日志/计数）；
- `[Courses]` 行 <9 列静默跳过；
- classroom/teacher CSV **不做主键去重**（重复 roomNumber/teacherId 双份入库，索引只留最后）；
- loadSnapshot 排课段不校验 teachingClassId 是否存在（孤儿条目照收）；
- `parseClassroomType` 未知→Norm，而 `classroomTypeToString(Any)` 写 "Any"——**Any 不闭环**（依赖"教室本体不标注 Any"约定）。
review：任何"用户数据不对 → 查无提示"的 bug，先查这层有没有静默丢。

### 5.9 [中] 退火成本边角
**位置**：[annealingcost.cpp](src/core/schedule/annealingcost.cpp)、[annealingstrategy.cpp](src/core/schedule/annealingstrategy.cpp)
- `wasteOfClass` 无负钳制：课程计划人数改大后未重排，落位的教室容量仍满足（roomrules 只在落子时保证）→ 理论上不会负，但改数据路径后要复核"容量−人数"不为负；
- 方差公式分母 T/R 为 0（sections 空/无教室）→ NaN 静默污染成本比较——入口已挡空数据，但直接调策略的测试/未来路径要防。

### 5.10 [低] SoftWeights 注释名义值与实值漂移
**位置**：[annealingcost.cpp](src/core/schedule/annealingcost.cpp)
注释称 w_split/w_weekend 名义 1000，实值 w_split=500、w_weekend=800（×2 前的名义分别是 250/400？）——与 [simulated-annealing.md](algorithms/simulated-annealing.md) §7 是否一致需核对，判断是调参遗留还是文档漂移。

### 5.11 [口径] 手动调整可动「已锁定」班
manualmove 不校验条目所属班是否锁定（锁只在自动重排时经 movable 生效）。若产品想"锁定班不许手动挪"，需在此加；若认为"锁是自动排课保护，手动人说了算"，则现状正确——**先确认口径再动代码**。

### 5.12 [口径] 删到全空不写 workspace.dat → "复活"
timer 只在 `!teachingClasses().isEmpty()` 时写盘；把班全删光后不写 → workspace.dat 保留删空前的最后非空态，下次启动会"复活"已删数据。防的是"误删空也想回到删空"vs"删空后崩溃想保留删空"的取舍，当前取前者。改它前先确认产品要哪个。

### 5.13 [口径] 撤销/重做后 ● 的 ≤1.5s 陈旧窗口
`restoreStore` 不立即 `updateWindowTitle`，靠 timer 兜底。若要在撤销瞬间刷新标题，在 restoreStore 尾补一次即可（注意别把 timer 与即时刷新搞成两套状态）。

---

## 6. 回归测试对照（改哪层 → 跑哪个/补哪个）

| 子系统 | 守护它的测试 | 典型回归 |
|--------|--------------|----------|
| CSV / 快照往返、缺段、R4 一致性、snapshotText 稳定 | tst_snapshot、tst_datastore | 加段忘兼容、[Locks] 忘排序、[Teachers] 覆盖后索引（补 5.1 用例） |
| 冲突检测键（R/C/T×day×section×week） | tst_conflicttable | 键格式、week 展开 |
| 贪心正确性 / 失败归因 / 学时非法 | tst_scheduler | 课程排序、best-fit、diagnoseFailure |
| 模拟退火（硬约束零违反/成本≤贪心/负载均匀/同种子/课次守恒/周末罚分） | tst_annealing | M1~M5、接受准则、增量成本、SoftWeights |
| 锁定冻结 / 最小排零扰动 | tst_movable | movable 换算、冻结原样保留 |
| 手动调整（拒绝细分/批内自撞/整批回滚） | tst_manualmove | validate/apply、updateScheduleEntry 原位替换 |
| 筛选判定 | tst_filter | 维度 AND/内 OR、courseOfClass 缺失 |
| 撤销/重做 | tst_datastore（含 undo） | push/undo/redo 游标、容量 |
| 课表点击命中回归（坐标帧） | tst_hittest | layoutCards/entryAt 漏加 topLeft |
| UI 编排（升格时序/禁用清单/●） | 无自动测试 → **手工冒烟** | 见 §7 冒烟单 |

---

## 7. 一次 review 的操作建议

1. **先跑基线**：`ctest`（9 套件）全绿再动手；改 core 优先补/跑对应 tst_*，改 UI 至少手工冒烟。
2. **按链路读，不按文件读**：从入口（MainWindow 槽 / worker）沿调用链走，重点核对 §2 每个「断点」。改一个函数时，问三件事——谁调用我（参数/时序契约）、我落库还是只读（是否走 editui/manual/datastore_edit 收口）、动了库有没有 invalidate / push / refresh / applyCardPalette / updateEmptyState 的对应。
3. **改 UI 前必答**：这个改动会换源吗？（→ setDataStore 或 refresh 的选型）会入撤销环吗？（→ 找对应 push 点，别 double-push）会改脏标记吗？（→ ● 基准是 projectText）。
4. **改排课前必答**：movable 集合对吗？冻结会动吗？课次守恒/只优不劣/硬约束零违反三条哪条可能被我打破？roomrules 改了有没有回归四个消费方？
5. **冒烟单（改 UI 后）**：
   - 新建导入 → 自动排课 → 快速连续 Ctrl+Z×N 看是否回弹一致（含回到空态）；
   - 导出快照对话框开着时点「自动排课」完成换源，看导出内容一致性；
   - 详情内 锁→编辑(同窗移时段换新师)→删除 一把做下来，Ctrl+Z 一步是否回到弹窗前；
   - 主题连切 5 accent×明暗，看卡片色/选中描边/禁用图标；大用例切主题看卡不卡；
   - 改课程人数超大→扩容确认→拒绝，确认提示没被吞（5.3 复现点）；
   - 标准 保存→关闭→重开（自动恢复），再 Ctrl+S 后改几处看 ● 亮/熄节奏。

## 8. 关联文档

- 架构与各链路总览：[docs/architecture.md](architecture.md)
- 数据模型与索引/指纹语义：[docs/data-model.md](data-model.md)
- 产品口径（锁定/编辑/手动调整语义的"为什么"）：[docs/PRD/ClassFlow_v5.0.md](PRD/ClassFlow_v5.0.md)
- 排课算法名义值与约束：`docs/algorithms/*.md`
