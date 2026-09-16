# ClassFlow 课程 / 教学班基本信息编辑增强（plan_v4.2）

> 提案版本：v4.2（方案先行 · 草稿） · 当前版本：v4.0 · 日期：2026-09-04 · 性质：**方案先行**——收录"课程/教学班基本信息可调"的缺口、数据牵连与设计，W 范围由人工圈定后再分步实施（沿用 v3.5/v3.6 编辑功能与 W1..W 惯例）。

## 0. 一句话

现在只能**增删**课程/教学班、**锁定**、以及手动调**时间/地点**；基本信息（课程名 / 开课学院 / 任课老师 / 人数 / 容量）一律不可改。本稿把这些字段纳入可编辑，并按改动"是否会让已排结果失效"分档处置：纯展示字段即时生效；**容量扩大超当前教室** → 确认后对受影响班局部重排换大教室；**换任课老师撞车** → **不自动排**，提示并入冲突列表、**延迟应用**（教务先手动把该班腾到新师空闲时段，再重新换师即应用）。

## 1. 现状与缺口

### 1.1 现有编辑能力盘点
| 能力 | 现状 | 入口 |
|---|---|---|
| 新增课程/教学班 | ✅ 建空课 / 增班最小排 | 侧边栏「新增」双标签（[adddialog](../ui/dialog/adddialog)） |
| 删除课程/教学班 | ✅ 整门删 / 删班不删课 | 侧边栏「删除」双标签（[deletedialog](../ui/dialog/deletedialog)） |
| 锁定 / 解锁 | ✅ 整班粒度过 [Locks] 持久化 | 详情弹窗 + 「锁定」弹窗 |
| 时间 / 地点调整 | ✅ 只改条目时段/教室、只查硬约束 | 详情→「调整本课」（[manualadjustdialog](../ui/dialog/manualadjustdialog)） |
| 课程名 / 学院 / 任课老师 / 人数 / 容量 | ❌ **不可改** | — |

### 1.2 为什么改这些字段不是"存回去"那么简单（数据牵连，读码结论）
- **课程名/学院住在 `Course` 模板**（[course.h](src/core/models/course.h)：`name/depart`），教学班只挂 `courseId`。课表卡片显示的课程名由模型按班查课程现取（[timetablemodel.cpp:269](src/ui/timetable/timetablemodel.cpp#L269) `courseNameOfClass`）——改名后 `m_model.refresh()` 即全表即时生效，无排课影响。
- **任课老师住在 `TeachingClass.teacherId`**（[course.h](src/core/models/course.h#L25)），但**排课条目各自带一份 `entry.teacherId` 副本**；课表卡片/长卡副行显示的是条目里的老师（[timetablemodel.cpp:346](src/ui/timetable/timetablemodel.cpp#L346) `teacherNameOf`，经 `entry.teacherId`）。所以换老师**只改班不够，还得连带刷新该班全部已排条目的 teacherId**，否则卡片一直显示旧老师。
- **人数/容量住在 `TeachingClass.plannedSize / maxCapacity`**，约束 H 用 `plannedSize` 对教室容量；若扩容超出当前落点教室容量，现有落位即失效。
- **换老师存在撞车可能**：新师若在该班当前各时段已教别的班，就地应用会造出"一师两课同 slot"的非法态（引擎 / 手动调整都不允许这种结果）。
- 课程/教学班字段目前**没有原位改写的 DataStore 方法**（只有 add/remove，见 [datastore.h](src/core/store/datastore.h)）。
- **冲突列表会被引擎整表重写**：每次排课 `Scheduler` 先 `clearScheduleFailures` 再 `setScheduleFailures`（[scheduler.cpp:43,52](src/core/schedule/scheduler.cpp#L43)）。借它登记"待办"时，此后任意一次排课都会把该条清掉（见 §3.3 的边界说明）。

### 1.3 本轮明确不做（口径见 §2）
- 学时/学分/每周课次/单次学时/开课周范围/所需教室类型——会改变"每周该上几节/占几天"，属**节律字段**，本轮不做（§5 后续）。
- 教师名表管理（新增/改名教师等）——本轮只做"班↔教师绑定"调整。

## 2. 已确认口径（2026-09-04 与开发者敲定）

1. **字段范围 = 基本信息**：课程名、开课学院；教学班的任课老师、计划人数、最大容量。
2. **入口 = 双通道**：课表卡片详情弹窗内「编辑…」+ 侧边栏「编辑」列表式入口（覆盖空课 / 未排班 / 整门改名）。
3. **失效处置两档**（与开发者敲定）：
   - **换任课老师撞车 → 不自动排、延迟应用**：弹提示 + 把该班登记进冲突列表；`teacherId` **不立即改**，已排条目原样不动（无任何一师两课中间态）。教务先把该班时段手动调到新师空闲处，再重新执行"换师"即应用（此时预检通过）。
   - **其余使现有落位失效（如扩容超当前教室容量）→ 确认后对受影响班局部重排**，换更大教室；用户拒绝确认则本次保存不落库。
4. 任课老师为**教学班级**属性（同课不同班可不同老师），与数据模型一致。

## 3. 设计

### 3.1 数据层增量（core/store）
- `bool DataStore::updateCourse(const Course &updated)`：按 `id` 原位替换课程模板；不存在返回 false。
- `bool DataStore::updateTeachingClass(const TeachingClass &updated)`：按 `classId` 原位替换；不存在返回 false。
- `bool DataStore::reassignClassTeacher(const QString &classId, const QString &newTeacherId)`：**应用路径的域规则收口**——改班 `teacherId` 并把该班全部已排条目的 `teacherId` 一并改写（保留时间/教室/周范围）；返回班是否存在。用于预检通过的换师（保住已排时段）。
- `bool DataStore::removeScheduleFailureForClass(const QString &classId)`：过滤掉某班的失败/登记项（应用换师成功时清掉遗留的"拟换师"登记）。
- **教师撞车预检**（core 可单测函数，约 40 行，不放 UI）：判定"把班 X 的教师换成 T 是否撞车"。遍历 X 各已排条目时段 (day, section 区间, 周区间重叠)，与**其余班**中 `teacherId == T` 的条目比较；时段重叠口径与手动调整/引擎同一套（同星期 + 节次相交 + 周范围相交）。纯读不写库。

### 3.2 触发决策表（保存后做什么）
| 改动 | 影响 | 处置 |
|---|---|---|
| `Course.name / depart` | 纯目录/展示 | 落库 + `m_model.refresh()`，不排课 |
| `TeachingClass.teacherId`（该班**无**已排条目） | 无排课牵连 | 落库 + refresh |
| `TeachingClass.teacherId`（该班**有**已排条目） | 条目/卡片显示 + 潜在撞车 | **预检**：新师该时段空闲 → `reassignClassTeacher` 就地应用（保时间/教室）+ 清该班遗留"拟换师"登记 + refresh；**撞车** → **不应用**，弹提示 + 登记冲突列表（见 §3.3） |
| `plannedSize / maxCapacity`（`maxCapacity ≥ plannedSize` 校验通过；未超当前落点教室容量） | 无 | 落库 + refresh |
| `plannedSize` **扩容超出**当前落点教室容量 | H 失效 | 弹确认 → 是：`runMovable({classId})` 局部重排换大教室；否：**本次保存不落库**（保留原值）。仍无合适教室 → 引擎失败明细（现有语义），绑定保留 |
| `maxCapacity < plannedSize` / 非法值 | 数据自相矛盾 | **保存前校验拦截**（弹窗内提示，不落库） |

- 每次编辑操作**至多一次**局部重排；局部重排 async 换源不二次 push（与「新增班→最小排」同源，R1）。

### 3.3 冲突登记（换师撞车）与清除
- **登记载体**：借用现有排课冲突/错误列表（`store.scheduleFailures`，`ScheduleErrorDialog` 展示，随快照 `[ScheduleErrors]` 持久化）。追加一条，reason 如：
  `拟换任课教师 T002，与教学班 C102 当前上课时段冲突；请先手动调整该班时段至 T002 空闲，再重新执行「编辑→换师」以应用。`
- **弹提示**（Modal，教务确认用的路径说明）：撞车时不改任何字段，仅提示"已登记入冲突列表，未应用"。
- **清除**：① 该班随后换师**预检通过并应用**时，`removeScheduleFailureForClass` 去掉旧登记；② 删除该班时 `dropClassRecords` 已连带过滤；③ 此后任一次自动/局部排课会**整表重建** scheduleFailures——因引擎按旧师重排、该冲突不再成立，登记自然消失（待办语义随重排结束，见 §5 另案）。

### 3.4 入口与 UI
- **`CourseDetailDialog`「编辑信息」**：预填当前教学班 + 所属课程，打开共享的 `EditInfoDialog`。该弹窗沿用详情既有非只读动作风格（直接改 `m_store`）；详情打开期间的任何改动仍**整段一步**入环——现有 `removed/adjusted/locksChanged` 三标志扩为四（加 `edited`），出参再带 `classId + needsReschedule` 供外层触发。
- **侧边栏「编辑」列表入口**（排在「新增/删除/筛选」行）：双标签——「课程」页按课程改名/学院（含空课程）；「教学班」页复用 ClassGrouping 按课程分组选班 → 老师（下拉现有教师含"未安排"+ 可键入新 id）/ 人数 / 容量。**覆盖空课、未排班、整门课改名**这些卡片入口到不了的场景。
- **MainWindow 统一收口**：任一条入口 exec Accepted 且确有字段变化 → `m_undo.push(before)`（一步）；返回 `needsReschedule` → `runMovable`；否则 `m_model.refresh()`。撞车登记（scheduleFailures 变化）同样算一次数据变更，exec 边界 push（撤销会连同未应用的登记一并回退，语义干净）。
- 教师选择框列出 `store.teachers()`（含空"未安排"）；键入不在师表的新 id 时卡片显示**回退 id 本身**（与 [timetablemodel.cpp:346](src/ui/timetable/timetablemodel.cpp#L346) 缺名口径一致），不新增师表行（教师名表管理另案，§5）。

### 3.5 undo / 异步（对齐 R7 既有规则）
- 编辑保存 = 顶层动作，`exec()` 边界一次 push；触发的 `runMovable` 换源走异步、不经 pending，不 double-push（复用"增班→最小排"先例）。
- 详情弹窗内先改名再换师再缩容 = 仍整段一步（详情打开期间不拆步）。
- busy 期间「编辑」入口按钮禁用（同新增/删除进 busy 集）。

### 3.6 交互边界
- 改课程名即时全表生效（同课所有班卡片同步），不重排、不动卡片位置。
- 换师撞车只登记不应用 → 冲突项清楚指示"未应用、需先腾位"；教务照办后重走一遍即收尾；仍不接受可 Ctrl+Z 回退整步。

## 4. 任务划分（拟；待人工圈定后细化，每步 <500 行、可独立提交走查）
- **W1 数据层**：`updateCourse / updateTeachingClass / reassignClassTeacher / removeScheduleFailureForClass` + 教师撞车预检函数；并入 `tst_datastore`（不新增套件）：改名往返、换师同步条目、预检真假例、登记项清除。
- **W2 `EditInfoDialog`（共享）**：字段表单 + 校验（`maxCapacity ≥ plannedSize`、教师下拉） + 结果出参（changed + 是否撞车/needsReschedule + classId）；课程/班两种模式复用。
- **W3 双入口接线**：详情「编辑…」+ 四标志扩展 + 出参外传；侧边栏「编辑」双标签（复用 ClassGrouping）；撞车提示与冲突登记；MainWindow 收口（undo push + busy 集 + runMovable + refresh）。
- **W4 验收走查**：改名/缩容即时；扩容超教室确认后单班重排；换师预检通过即应用保时段、撞车提示入冲突列表且不应用、腾位后重试即收尾；空课与未排班能进列表编辑；Ctrl+Z 整段回退；`ctest` 9/9 无回归。

## 5. 不做 / 明确排除（另议）
- 节律字段（学时/学分/每周课次/开课周范围/所需教室类型）——须配套"课次数守恒"重排口径，另案。
- 教师名表管理（增删改名/一师多名）、按周跳过、单节冻结、删单节/补排缺失节（PRD §7 后续项）。
- **换师"待办"跨排课存活**：撞车登记借 `scheduleFailures`，会被下次排课整表重写清掉；若要求待办在排课后仍保留，需为这类登记单独持久化（独立字段/快照段），另案。
- UI 视觉美化（见 [plan_v4.5](plan_v4.5.md)，与本稿并行）。

## 6. 风险 / 边界
- 换师撞车预检口径要与手动调整/引擎的时段重叠判定**同一套**，避免"预检说没事、引擎说撞"。
- `reassignClassTeacher` 只改班与其条目 teacherId；后续该班被排课时引擎按新师从 class 重新派发，行为一致。
- 详情弹窗直接改 `m_store` + 触发 async 重排：复用"详情=整段一步、runMovable 异步不 push"约束，对话框内部**不自行**跑排课。

## 7. 关联文档
- 数据模型：[docs/data-model.md](../data-model.md)；架构：[docs/architecture.md](../architecture.md)
- 编辑/调整既有口径：[ClassFlow_v4.0.md §3.5/§3.6](../PRD/ClassFlow_v4.0.md)；撤销/重做：[plan_v4.1 §4](plan_v4.1.md)

## 8. 实施记录（2026-09-05，W1–W4 已全部实现）

### 8.1 落点（代码）
- **W1 数据层**（上一会话完成）：`src/core/store/datastore_edit.{h,cpp}` 提供 `updateCourse / updateTeachingClass / reassignClassTeacher / removeScheduleFailureForClass / teacherSwapClashes`；7 个新用例并入 `tests/tst_datastore.cpp`（改名往返 / 换师同步条目 / 撞车真·假例 / 周范围尊重 / 登记项清除）。
- **W2 共享弹窗**：`src/ui/dialog/editinfodialog.{h,cpp}`（`EditInfoDialog`：钉选课程 / 钉选班级 / 浏览双页签三种形态，只产出 `Result` 不落库）。
- **W2 决策助手独立成对**：`src/ui/dialog/editui.{h,cpp}`（`namespace editui`）收口落库决策——课程模板改写、换师撞车只登记、扩容溢出弹确认（为控单文件行数 <500，从 dialog 拆出）。
- **W3 双入口接线**：
  - [coursedetaildialog.h](../ui/dialog/coursedetaildialog.h) / [.cpp](../ui/dialog/coursedetaildialog.cpp)：新增「编辑信息」按钮 + `openEditInfo / refreshBasicLabels / edited() / editSummary() / editRoomMoveNeeded()`；标题 / 开课学院 / 教师 / 人数 四行改为成员标签并原地刷新；三标志扩为四标志。
  - [timetablecontroller.h](../ui/timetable/timetablecontroller.h) / [.cpp](../ui/timetable/timetablecontroller.cpp)：`showCourseDetail` 把 `edited()` 并入"整段一步"判断，编辑确认扩容换大教室时发 `rescheduleNeeded(classId)`。
  - [mainwindow.h](../ui/mainwindow.h) / [.cpp](../ui/mainwindow.cpp)：侧边栏新增整宽「编辑」按钮（`onEditClicked`，浏览弹窗）与 `onRescheduleNeeded`；入 busy 禁用集；连接 `rescheduleNeeded`；编辑成功 `push` 一步 + refresh + 撞车/放弃扩容弹提示 + `roomMoveNeeded → runMovable({classId})`。
- CMakeLists（APP dialog 组）登记 editinfodialog / editui 两对。

### 8.2 与 §3.4 的两处出入（实现取优）
- 教师下拉首项为显式「未安排」（占位 = 空教师），读回时翻译回空串；其余项为现有教师，NoInsert 可键入新 id。
- 侧边栏「编辑」按钮作**整宽纵排**（新建/导入/导出之后），而非并进「新增/删除/筛选」行——1/4 宽度塞四个等宽钮过挤。

### 8.3 验收口径对照（W4，代码自查；GUI 人工走查待做）
- 改名 / 缩容即时生效；空课、未排班可经浏览弹窗编辑。
- 扩容超当前教室 → 确认后对该班 `runMovable` 换大教室（详情内确认延迟到弹窗关闭后发 `rescheduleNeeded`）；拒绝 → 不落库。
- 换师预检通过 → `reassignClassTeacher` 就地应用（保时间/教室）并清旧"拟换师"登记；撞车 → 只登记 `scheduleFailures`（reason 前缀 `拟换任课教师`）不应用并弹提示；腾位后重试即收尾。
- 撤销整段一步：侧边栏一次编辑 push 一次；详情弹窗打开期间的编辑/换师登记与锁定、调整合并为一步（exec 边界 push）。
- 回归：全量构建通过，`ctest` 9/9 通过（W1 后 tst_datastore 42 用例含 7 新例）。
- 已知边界（沿用 §3.3/§5）：撞车登记会被下次任意排课整表重写清掉；"待办跨排课存活"另案未做。
