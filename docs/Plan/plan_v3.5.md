# ClassFlow 规划：课程/教学班实体分离 + 双标签新增/删除 + A 档编排重构（plan_v3.5）

> 本文档与 v3.5 代码改动同步，记录范围、口径、设计与对 v3.4 假设的修订。
> 2026-09-03 与开发者敲定；版本管理由人类执行。

## 1. 为什么做（现状问题）

v3.4 及之前把「课程」与「教学班」绑成一个强整体：

- `DataStore::removeTeachingClass` 在删除某课最后一个班时**级联删除课程**，空课程无法存在；
- 「新增」弹窗（NewCourseDialog）强制“课程+其首个教学班”同表单提交，且提交时按**课程名**反推归属、同名判重，导致一门课名不方便对应多个课程号；
- 「删除」只有一个“按课程分组的教学班树”页面，勾课程头语义 = 连课带班一起删；
- 快照里课程只是教学班行内嵌字段、靠班行去重反推，空课程既无法存在也无法存盘；
- 主窗口 `onSchedulingDone` 把 `runKind()` 字符串当**控制流键**特判（“新增课程最小排”）做升格询问，编排逻辑散落在界面层；
- 多个弹窗内重复“按课程分组 / 教师显示名 / 课程标签”的小逻辑。

## 2. 已确认口径（开发者拍板）

1. **删除教学班永不自动删课程**：删到某课最后一个班，课程保留为空课程；删除只分两类——班页删班、课程页删整门课。相关确认/摘要文案不再写“末班连课删”。
2. **新增两页独立**：建完空课程即结束，不做“继续加班”联动；新增教学班要选课号另行操作。
3. **识别课程一律用课程号（courseId 主键）**：允许同名不同号，废除课程名反推/判重。
4. 本改动命名为 **A 档**（编排收口 + 实体分离）；B 档（完整 EditorController 大重构）本轮不做。

## 3. 设计总览

- **数据语义**：course 与 teachingClass 一对多、course 可 0 班；删除彻底分离——`removeTeachingClass` 只删班及其记录（保留课程），`removeCourse` 删整门课（连其班/记录/锁）。
- **持久化**：快照新增 `[Courses]` 分节，保存全部课程（含空课）；加载时若存在该节则以它**整体重建课程表**，缺该节的老快照回退到“由教学班行去重推导”的旧逻辑（向前兼容）。
- **UI**：侧边栏“新增课程”改名“新增课程/教学班”，弹 **AddDialog 两个标签页**（新增课程 / 新增教学班）；“删除课程/教学班”弹 **DeleteDialog 两个标签页**（删除课程 / 删除教学班）。
- **编排**：新增教学班后的“最小排 → 排不下询问升格局部重排”整体收进 `ScheduleController::scheduleNewClass`；`MainWindow::onSchedulingDone` 不再用 runKind 做控制流，`runKind()` 只用于进度框/状态栏文案。
- **公共方法抽取**：`ui/dialog/classgrouping.{h,cpp}` 的 `namespace courseui` 收口分组/标签/教师名/按课计数，LockDialog / DeleteDialog / AddDialog / MainWindow 共用。

## 4. 代码改动对照

### 4.1 数据层（core）
- `datastore.cpp/h`：`removeTeachingClass` 去掉“无剩余班则删课程”的级联；课程与班相互独立，删班不删课，允许空课程。文档注释同步。
- `snapshot.cpp`：`saveSnapshot` 在 `[Term]` 后、`[TeachingClasses]` 前写 `[Courses]`（9 列：courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,startWeek,endWeek,requiredRoomType），含空课、顺序随 `courses()`；`[TeachingClasses]` 段保持 13 列内嵌课程字段（兼容老构建跳过未知段）。`loadSnapshot` 分节解析加 `Courses` 分支；loadCsv 成功后若该段存在则整体重建 `m_courses` 并失效索引，缺段行为与旧版一致。文件头注释改“九段；[Courses] 与 [Locks] 允许缺失”。
- **排课核心零改动**（探查结论）：贪心按课程迭代但空课 `pending.isEmpty() → continue`；退火 `Candidate` 只由 `scheduleEntries()` 反建；`unlockedMovableClasses()` 遍历教学班。空课对全量排/局部排天然安全。

### 4.2 UI 层（新增/删除弹窗）
- **AddDialog（替换 NewCourseDialog）**：双标签。
  - 「新增课程」：课程号必填且 `!courseById`、课程名、学分/每周课次/单次学时/学院/起始周/结束周/所需教室。结果 = 仅一条 `Course`（不排课）。
  - 「新增教学班」：课程下拉列**全部课程**（含空课），文案 `课程名（课程号）· N 班`；选中课程号 → 只读预览带出该课其余信息（课程名/学分/课次/周范围/教室），即“由课程号推导其它信息”；再输教学班号（唯一）、教师、计划人数/容量。结果 = 一条 `TeachingClass`（`courseId` 指向所选课程）。无任何课程时该页禁用并提示。
  - OK 校验**当前激活页**，返回 `Result{ Mode mode; Course course; TeachingClass klass; }`。
- **DeleteDialog → 双标签**，返回 `DeletionRequest{ courseIds; classIds; }`。
  - 「删除课程」：逐行一门课（**含空课**），第二列 `空课程` / `共 N 班 · 已排 K`；勾选整门删。
  - 「删除教学班」：沿用课程分组三态树（分组改用 `courseui::groupClassesByCourse`），勾课程头只级联其下属班、勾单班只删该班；文案明示“删班不删课，空课在删除课程页处理”。
  - OK 把两页选择合并，交主窗口确认。

### 4.3 编排收口（A 档）
- `ScheduleController`：
  - 新增公开 `scheduleNewClass(classId)` = 旧“新增课程最小排”（movable = 新班），私有成员 `m_pendingNewClassId` 标记“最小排等待者”。
  - `onWorkerFinished` 收尾：取消 → 清 pending、发 `scheduleDone(false,0,0,true)`；正常先换源+刷新；若 `!ok && pending 非空` → 控制器内弹升格询问——“选是”清 pending、保持 busy、`QTimer::singleShot(0, …)` 延迟到线程收尾后 `beginSchedule(unlockedMovableClasses(), "局部重排")` 并 **return（不发射 scheduleDone）**；“选否”清 pending、照常发失败汇总。
  - **时序关键（写死注释）**：`scheduleDone` 在主线程内联直连发射，此刻 `m_thread` 尚未经 `QThread::finished` 置空，而 `beginSchedule` 有 `if (m_thread) return;` 重入守卫；升格若同步发起会被静默吞掉。0ms 定时事件排在已入队的 finished 清场之后，天然等到 `m_thread==nullptr`。升格 run 内 pending 已清，即使再失败也不二次询问 → **一次新增至多一次最终 scheduleDone**。
- `MainWindow::onSchedulingDone`：删除整段 runKind 字符串控制流分支（含升格询问），`!ok` 统一 `showScheduleErrors()`。
- `MainWindow` 槽：
  - `onAddCourseClicked` → 走 AddDialog：AddCourse → `addCourse` 后仅状态栏提示（不排课）；AddClass → `addTeachingClass` 后 `scheduleNewClass(classId)`。空数据守卫放宽为“courses 与 teachingClasses **皆空**才拦”。
  - `onDeleteClicked` → 走 DeleteDialog，确认摘要分“整门课(含 N 班) / 单班”两类，先 `removeCourse` 再 `removeTeachingClass`，文案去掉“末班连课删”并加“空课程保留”提示。
  - 按钮文字 “新增课程” → “新增课程/教学班”。

### 4.4 细节文案
- `coursedetaildialog`：删除“此教学班”确认：末班场景改为“删除后课程保留为空课程，可在删除课程页整门移除”；`removalSummary` 恒为“已删除教学班 …（属…）”（不再有“及整门课”分支）。
- 各处与“末班=删整课”相关的注释/占位符按新语义改。

## 5. 公共方法（courseui）

`src/ui/dialog/classgrouping.{h,cpp}`（`namespace courseui`）：

- `struct CourseGroup { const Course *course; QVector<const TeachingClass*> classes; };`
- `groupClassesByCourse(store)`：按课程顺序收集有班课程；孤儿班归末尾 `course==nullptr` 组。
- `classCountOfCourse(store, courseId)` / `scheduledCountOfCourse(store, courseId)`。
- `courseLabel(c)` = `"课程名（课程号）"`。
- `teacherDisplayName(store, id)`：空 = “未安排”；查不到 = 原 id；推导表（name==id）= id，否则姓名。

使用方：AddDialog（课程下拉 + 班数）、DeleteDialog（两页）、LockDialog（分组与教师名，勾选树/初始勾选逻辑不动）、MainWindow（删除摘要）。

## 6. 测试（与代码同步）

- `tst_datastore`：`removeLastClassRemovesCourse` → 反转为 `removeLastClassKeepsEmptyCourse`（删末班后课程仍在）；`removeClassCleansRecords` 尾部追加“删到末班课程仍保留”断言；新增 `removeEmptyCourseWorks`（建空课→removeCourse 生效）。
- `tst_snapshot`：`removedDataRoundTrip` 改为删光 C01 三个班再往返，断言 `dst.courseById("C01")` **仍存在**（空课往返不丢）；新增 `emptyCourseRoundTrip`（只建空课→[Courses] 往返字段完整）。`roundTrip`/`addedCourseRoundTrip`/`missingLocksSectionLoads` 天然仍绿。
- 引擎回归：核心不改，未新加用例（低优先）。

## 7. 不改 / 明确不做（本轮）

- LockDialog 交互语义不动（仅分组/教师名数据来源换 helper）。
- “删除所有班后的全空工作区可重开 / 可新建空工作区”不在本轮：某工作区把教学班全删光只剩空课/空数据时，loadCsv 的三文件非空守卫会让快照加载失败——与旧版行为一致，无回归，后续若要“全空工作区”单独做。
- 不做 B 档 EditorController 大重构。

## 8. 验证

1. `ninja -C build/Desktop_Qt_6_9_2_MinGW_64_bit-Debug` 全绿（应用 + 8 套单测可执行文件链接）。
2. 全量单测 `ninja test`：8 套全 PASSED（tst_annealing 最慢约 85–155s）。
3. 手动冒烟（ClassFlow.exe）：
   - 只建空课程（不建班）→ 无排课、无报错；存快照重开 → 空课仍在。
   - 新增教学班：下拉选刚建空课 → 预览按该课程号自动带出课程信息；提交 → 最小排成功。
   - 拥挤场景加不进 → 弹升格询问：选是 → 局部重排；选否 → 错误列表可见、已锁班原样。
   - 删教学班最后一班 → 空课保留；到“删除课程”页可删空课/整课。
   - 老快照（无 `[Courses]` / 无 `[Locks]`）仍能打开且课表完好。

## 9. 对 v3.4 假设的修订

v3.4 的“课程恒有 ≥1 教学班、末班连课删”“新建即课程+首班一体”等假设在本版**作废**；统一改为“课程与教学班相互独立、课程可 0 班、识别用课程号”。v3.4 的 movable/冻结统一引擎参数、锁定与最小排同源的结论**保留**并成为 scheduleNewClass/runMovable 的公共底座。
