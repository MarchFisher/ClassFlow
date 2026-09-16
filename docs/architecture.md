# ClassFlow 架构设计

> 版本：v5.0 · 日期：2026-09-07 · 现状口径：v5.0 收拢 v3.x~v4.x 全部已实现能力
> （两阶段排课 + 异步化、课表卡片 MVD、锁定/冻结局部重排、课程·教学班实体分离与编辑、
> 教务手动调整、会话级撤销/重做、双层保存、UI 视觉优化、主题语义 token、图标化与应用图标）。
> 演进过程见 `docs/Plan/plan_v1.0 ~ plan_v4.6`；与产品现状对照见 `docs/PRD/ClassFlow_v5.0.md`。

## 1. 目标与范围

- **完整系统**：三类角色——管理员（导入数据、排课、查看全校课表、编辑课程表与手动微调）、教师（个人课表、课程学生名单）、学生（个人课表）。
- **当前阶段（v5.0，管理员单机版）**：输入 CSV（教学班 / 教室 / 作息表）→ 自动为教学班分配「授课时间 + 教室」→ 输出全校课表网格视图 → 可在其上锁定 / 局部重排、增删课程班、改基本信息、手动调整、撤销 / 重做并持久化。教师 / 学生端为远期规划（PRD §7）。

## 2. 分层架构

```
┌ 表现层（Qt Widgets）──────────────────────────────────────┐
│ MainWindow：窗口拼装 + 文件级动作（新建 / 导入快照 / 保存 / │
│             导出快照 / 撤销·重做）                          │
│ 顶栏：保存·撤销·重做(图标钮) │ 周次 │ 自动排课 │ 排课错误 │  │
│       锁定 │ 主题(明暗×accent 菜单)                        │
│ 侧栏：「文件」=新建/导入快照/导出快照                       │
│       「课表」=编辑 + 新增·删除·筛选(等宽行)                │
│ 右栏：QStackedWidget ⇄ EmptyState(空态引导) / 课表画布      │
│ ScheduleController + ScheduleWorker（后台线程/副本/进度/    │
│     取消/换源；最小排→升格局部重排收口）                    │
│ TimetableController（周/筛选/点卡详情/「更多」/错误查看）     │
│ 课表 MVD：TimetableView(命中) + TimetableDelegate(逐卡自绘  │
│     + layoutCards/entryAt) + TimetableModel(内容/查表访问器)│
│ 弹窗：Import/SnapshotImport/Filter/Add(双标签)/            │
│      Delete(双标签)/Lock/CourseDetail/ClassEdit(双页签)/    │
│      EditInfo/EditInfo(浏览)/CourseList/ScheduleError；     │
│      classgrouping(courseui)；editui(落库决策助手)          │
├ 编排 / 校验（UI 侧薄编排 + core 纯校验）─────────────────┤
│ core/filter：ScheduleFilter + matchesFilter（纯判定）       │
│ core/schedule：Scheduler + IScheduleStrategy                │
│  ├ GreedyStrategy（构造可行初始解）                         │
│  ├ SimulatedAnnealingStrategy（邻域优化，默认策略）         │
│  │   ├ Candidate（候选解）/ 负载快照 + 增量 Δ               │
│  │   └ 邻域 M1~M5（平移/归并/换教室/对调）                 │
│  ├ movable/冻结：F(冻结班原样占位) + M(可动班)              │
│  │   → 全量 / 锁定重排 / 新增最小排 / 局部重排 一套引擎      │
│  ├ roomrules（H4 容量 / H5 类型 判定单源）                  │
│  ├ manualmove（manual::validate/apply：手动调整纯校验落库） │
│  ├ manualadd（manual::validateAdd/applyAdd：从零排入）      │
│  └ ConflictTable：教室/班/教师 × 时间 占用表 + busyKeysOf   │
├ 数据层 core/store + core/models─────────────────────────┤
│ DataStore（CSV 导入/导出、九段快照 + snapshotText、ID 索引  │
│     O(1) 查询、锁定集、条目原位替换 updateScheduleEntry、    │
│     编辑方法 updateCourse/updateTeachingClass/换师…）       │
│   ├ datastore.cpp / csv.cpp / snapshot.cpp / lookup.cpp    │
│   ├ datastore_edit.cpp（编辑落库域方法）                    │
│   ├ undobuffer.cpp（会话级撤销/重做环）                     │
│   └ utility.cpp（公共解析辅助，namespace store）           │
│ core/models/（数据模型，规范见 data-model.md）              │
└──────────────────────────────────────────────────────────┘
```

- 依赖自上而下：UI → core/filter、core/schedule → core/store → core/models。
- `core/` 四层均为纯逻辑、禁含 Widget，可独立单测；`DataStore` 是唯一数据源。
- **编辑 / 编排流水线**（与排课核心解耦，均直接改 DataStore，语义见 data-model.md）：
  1. **锁定重排 / 新增最小排 / 局部重排**：引擎以「可动班集合」为参数，冻结班条目原样进冲突表——`ScheduleController` 负责线程与升格编排，core 只认集合；
  2. **增删课程/班**：`AddDialog` / `DeleteDialog` 双标签经 `addCourse/addTeachingClass/removeTeachingClass/removeCourse` 改库，删班不删课；
  3. **基本信息编辑**：`EditInfoDialog`（侧栏浏览）/ `ClassEditDialog`（详情内）产出「想改成什么」→ `editui` 统一决策落库（改名即时生效 / 换师撞车只登记 / 扩容溢出换大教室确认）；
  4. **手动调整 / 从零排入（时间·教室）**：`TimeRoomEditor` 收最小变更 → `manualmove::validate`（只查硬约束）→ `apply` 原子落库，作为 `ClassEditDialog`「时间·教室」页签与基本信息一次保存；**0 课次的失败班**该页转 `FromScratchEditor`「从零排入」→ `manualadd::validateAdd/applyAdd` 整批新增 N 次课（有空位才落库并移出失败列表）。
  5. **撤销 / 重做**：顶层动作边界对 `DataStore` 值快照入环（COW 成本 O(1) 级），`restoreStore` 整体换源。

## 3. 文件结构

```
ClassFlow/
├── CMakeLists.txt / app.rc.in(Win 图标) / ClassFlow_zh_CN.ts
├── icons/                      # Feather 图标 + 应用图标（icons.qrc，前缀 /theme/icons）
├── src/
│   ├── main.cpp                # 入口：setWindowIcon + 翻译 + 主题恢复 + 主窗口
│   ├── core/                   # 非 UI 核心（内部分四层）
│   │   ├── models/             # 数据模型（term/user/course/classroom/teacher/timetable + 聚合头）
│   │   ├── store/              # 数据存取：datastore/csv/snapshot/lookup/utility +
│   │   │                       #   datastore_edit(编辑方法) / undobuffer(撤销环)
│   │   ├── schedule/           # 排课核心：conflicttable/scheduler/strategy/annealingstrategy/
│   │   │                       #   annealingcandidate/moves/cost + roomrules + manualmove + manualadd
│   │   └── filter/             # 筛选核心（schedulefilter：条件数据 + matchesFilter）
│   └── ui/                     # Qt Widgets 表现层（按职责分子目录）
│       ├── mainwindow.*/.ui    # 主窗口：拼装 + 文件动作 + 撤销/重做 + 保存两层
│       ├── emptystate.*        # 空态引导页（无数据 CTA）
│       ├── theme/              # ThemeManager（明暗×accent + token 访问器）
│       │                       #   theme.qss(模板) + themeicons(Feather→主题色图标)
│       ├── dialog/             # 弹窗：import / snapshotimport / filter / add(双标签) /
│       │                       #   delete(双标签) / lock / coursedetail(锁·编·删) /
│       │                       #   classedit(基本信息+时间·教室 双页签) / editinfo(浏览) /
│       │                       #   classinfoform(基本信息表单) / timeroomeditor(时间·教室编辑区) /
│       │                       #   courselist(「更多」长卡) / scheduleerror(失败归因) /
│       │                       #   classgrouping(courseui helper) / editui(落库决策助手)
│       ├── timetable/          # 课表网格 MVD + 查看控制器
│       │                       #   timetablemodel / timetabledelegate / timetableview /
│       │                       #   timetablecontroller
│       └── schedule/           # 后台排课 scheduleworker + schedulecontroller
├── data/                       # 示例 CSV（教学班/教师/教室/作息 + 大批量用例）
├── tests/                      # 单元测试（9 套件，CTest）
└── docs/
    ├── architecture.md         # 本文档
    ├── data-model.md           # 数据模型规范
    ├── algorithms/             # 算法设计（greedy-strategy / simulated-annealing）
    └── PRD/                    # 产品需求文档（ClassFlow_v5.0.md 现行）
```

## 4. 主窗口 / 控制器职责（v3.3 拆分 + v4.x 演进）

- **MainWindow（src/ui/）**：只做窗口拼装与**文件级动作**——新建（ImportDialog + 立即自动排课）、快照导入/导出、保存（两层，见 §6）、撤销/重做、closeEvent 收尾；把按钮/视图信号接到两个控制器上。持 `m_store` / `m_model` / `m_view` / `m_weekSpin` / `UndoBuffer m_undo`。
- **ScheduleController（src/ui/schedule/）**：持有 `DataStore&` / `TimetableModel&` / 需禁用的入口按钮 / 宿主窗口。封装一次排课完整生命周期（worker+QThread 起停、进度框、取消、防重入）；完成（成功才）换源 `m_store = 副本` + `setDataStore`，发 `scheduleDone(...)`。另承载**编排收口**（v3.5/v4.2）：`scheduleNewClass`（新增班最小排→排不下询问升格局部重排）、`runMovable({classId}, "局部重排")`（编辑扩容换大教室 / 详情内 rescheduleNeeded 用）。
- **TimetableController（src/ui/timetable/）**：持有 `DataStore&` / `TimetableModel&` / 周选择器 / `UndoBuffer*` / 宿主窗口。只读查看/切换类动作 + 点卡详情：`setWeek`、`syncWeekSelector`、`openFilter`、`showCourseDetailAt`/`showMoreCourses`、`showScheduleErrors`。详情弹窗（CourseDetailDialog）打开期间改的锁定/编辑/调整/删除在 **exec 边界整段一步入环**（对 m_undo push），关闭后按 `removed → adjusted → edited → locksChanged` 统一 `refresh()` + 状态栏。
- **接线约定**：`scheduleDone` → `MainWindow::onSchedulingDone`（刷主题卡色 / 对齐周 / 状态栏 / danger 徽标，失败回调错误列表）；点卡信号 → 控制器；按钮直连控制器槽。数据仍是 MainWindow 的 `m_store` 单一数据源。

## 5. 排课设计（两阶段：贪心 + 模拟退火）

| 输入 | 课程/教学班（课次、单次学时、教师、人数、周范围、所需类型）、教室（容量/类型）、时间槽（作息表生成） |
|------|------|
| 输出 | 每个教学班每次课 →（时间槽，教室），即 `ScheduleEntry` |
| 硬约束 H1~H5 | ① 同班各次课时间互不冲突 ② 同教室同时间只能一班 ③ 同教师同时间只能一班 ④ 教室容量 ≥ 人数 ⑤ 教室类型匹配课程所需类型 |
| 软约束 S1~S7 | 课程不拆分、时间离散度、同班同教室、教室浪费、时间负载均匀、教室负载均匀、避免周末上课（加权罚分，越小越好） |

**阶段一：贪心构造可行初始解**（规则细节见 `docs/需求分析.md`）

1. **以课程为单位**：同一课程所有教学班优先统一安排在同一时间模式（课次、星期几、节次完全一致），靠教室区分教学班。
2. **课程排序**：课次多者先排 → 课次相同按单次学时长者先排 → 再按教师 ID 顺序排。
3. **时间离散度**：一周 N 次课间隔 `(5 − N) / (N − 1)` 天；找不到空档时逐步缩小间隔放宽。
4. **兜底拆分**：同一课程放不进同一时间槽时按 `classId` 拆组，每组沿用同一套时间模式。
5. **教室分配**：同一教学班每次课固定同一教室；空闲教室中 best-fit（容量 ≥ 人数且最小）并匹配类型（经 `roomrules`）。

复杂度：O(课程 × 教学班 × 时间槽 × 教室)。

**阶段二：模拟退火优化**（细节见 `docs/algorithms/simulated-annealing.md`）

1. 贪心写回条目反建 `Candidate`（classId → N 个 Session），维护冲突表与负载快照。
2. 邻域 M1~M5（单课平移 / 整班平移 / 课程归并 / 换教室 / 双班对调），每个 move 旧条目出表 → 新条目逐条 canPlace+place → 增量更新负载快照，冲突即回滚，硬约束全程零违反。
3. 增量 Δcost 只算被移动班/课程及受影响槽位/教室，全部整数、无浮点。
4. Metropolis 接受 + 几何降温；T₀ 由可行 move 的 |Δ| 中位数 ×5 采样并 clamp。
5. best-so-far 写回，保证成本 ≤ 贪心；课次数全程守恒。

### 5.1 可动集合 / 冻结：一套引擎三处受益（v3.4 / v4.2）

- 引擎参数 = 可动教学班集合 `movable`，集合外 = 冻结（已排有课条目**原样种入冲突表**，周范围/教室/时段不动）；贪心只排 movable 班，退火只对 movable 班做邻域（冻结条目计入负载与软成本，永不被选中移动）。
- 取法：`全部班` → 全量排；`全部未锁定班` → 锁定重排；`刚新增班` → 最小排；`{某班}` → 编辑扩容后的单班局部重排。同一基建，无两套逻辑。

### 5.2 硬约束 H4/H5 判定单源（v4.1 R3）

容量（H4 `capacity ≥ plannedSize`）与类型（H5 `requiredRoomType ≠ Any 时须匹配`）收进 `core/schedule/roomrules.{h,cpp}` 纯函数 `capacityOk / typeOk / roomOk`。贪心 best-fit、退火 M4/M5 调组合 `roomOk`；manualmove 分别调 `capacityOk` / `typeOk` 以细分 `CapacityTooSmall` / `RoomTypeMismatch` 拒绝原因。消灭此前四处内联同义比较。

### 5.3 排课条目代理主键（v4.1 R2）

`entryId` 与展示语义解耦：新生成格式 `classId#班内序号`（如 `C102#2`，序号从 1 递增，**绝不内嵌时间字段**）；老快照旧格式 id（`classId-星期-起始节`）原样兼容（只保证唯一、不做反解析）。展示层「第 k 次课 / 周X 第 a~b 节」标签现算不入库。生成点仅贪心 `makeEntry` 与退火 `entryFor` 两处。

## 6. 排课异步化（v3.2）与双层保存（v4.6）

### 6.1 异步化

- **副本交换**：`ScheduleController::run` 拷贝 `m_store` 交 `ScheduleWorker`（QObject+QThread），工作线程只排副本；主线程旧课表不动。完成后 queued 信号回传「排好的副本 + 摘要」，成功才 `m_store = 副本` + `setDataStore` 重建。
- **进度/取消**：core 侧纯数据上下文 `ScheduleContext`（`std::atomic<bool> cancelled` + `onProgress`）；取消 → `aborted=true`，主线程忽略副本、保留旧课表，未处理班不计失败。
- **线程边界**：`DataStore` 各集合为 `QVector`（隐式共享 COW），worker 对副本首写即 detach；`ctx.cancelled` 为原子位。`closeEvent` 后台运行中先 `cancelAndWait()` 再保存。
- **防重入**：排课期间禁用 自动排课 / 撤销 / 重做 / 锁定 / 新建 / 导出快照 / 新增 / 删除 / 编辑 / 导入 / 筛选（顶部「保存」除外——写的是排课期间不动的静态 `m_store`，安全）。

### 6.2 保存分两层（L1 自动恢复 / L2 项目文件）

```
                    ┌──────────── 运行态内存 DataStore（m_store）─────────────┐
                    │  唯一数据源：编辑/调课/排课结果都在这里                    │
                    └──────────▲─────────────────────────────▲──────────────┘
        防抖自动写(~1.5s)      │                             │  Ctrl+S / 保存
        关窗/新建前 flush       │ 启动:总是恢复它              │
                               │                             ▼
        L1 自动恢复            L1 自动恢复 .classflow/workspace.dat  L2 项目文件(教务自选)
        snapshotText 指纹比对  （gitignored；用户感知不到）     首次保存→弹路径，此后覆盖
```

- **L1**：`DataStore::snapshotText()` 把九段写盘内容先在内存拼成确定文本（`[Locks]` 排序）；`MainWindow` 起 ~1.5s 防抖 `QTimer`，内容与上次写盘不同才 `writeWorkspace()` 写 `.classflow/workspace.dat`；关窗 / 新建前 flush 兜底。启动总从它恢复。
- **L2**：`persistProjectFile()` — Ctrl+S / 顶部「保存」写**项目文件**；无目标首次弹路径（导出语义），此后直接覆盖，路径经 `QSettings("projectFile/last")` 跨会话记住；导入快照成功即设为当前项目文件。标题 `ClassFlow · <文件名>●`：● = 当前内容 ≠ 最近手动保存/导入到项目文件的 `snapshotText()`。
- **侧栏「导出快照」= 独立副本**：`onExportClicked` 弹路径另存一份当前现场，**不设为当前项目文件**、不挪 Ctrl+S 落点、不影响 ●（可分发给他人/备份）。
- 快照格式不变（九段），老文件 / 老版本兼容。

## 7. 会话级撤销 / 重做（v4.1 R7）

- **机制**：`DataStore` 全部成员是 Qt 值容器，`DataStore before = m_store;` 为 O(1) 级 COW 共享引用 → `UndoBuffer`（`core/store/undobuffer.{h,cpp}`，有界 `QVector<DataStore>` 环，默认容量 20）存「动作前」状态代价极低、无需序列化。
- **入环边界（顶层可变动作）**：新建 / 导入快照 / 新增课程·教学班 / 删除 / 锁定 / 基本信息编辑 / 自动排课（`onRunSchedulingClicked` 捕获 before，`onSchedulingDone` 非取消才 push）；详情弹窗整段一步（CourseDetailDialog 打开期间 锁定/编辑/调整/删除 合并，exec 边界 push）。**不入环**：保存 / 导出 / 周切换 / 筛选 / 主题 / 错误列表 / 启动自动恢复 / worker 异步换源（其入环由自动排课中转槽的 pending 提交唯一承担，杜绝 double-push）。
- **交互**：顶栏 撤销/重做 + Ctrl+Z / Ctrl+Shift+Z；`restoreStore(target)` 整体换源 + 刷新（复用「换源 → 刷新」协议）；排课 busy 期间按钮禁用（快捷键 isRunning 兜底）。空步规避：删除按实删计数、锁定净零变化不入、详情四标志早退、取消不 push、run 早退前置守卫。

## 8. 课表网格 MVD（v3.3 + v4.5 取色）

- **结构**：`TimetableModel`（内容 + 查表访问器）+ `TimetableDelegate`（自绘）+ `TimetableView`（命中）。模型不拼堆叠文本；`DisplayRole` 收敛为第一条课程名单行摘要，`ToolTipRole` 保留逐课周范围文本。
- **Delegate 自绘**：`visibleCourseCount/hasOverflow/slotCount` 决定布局——同格 ≤3 门画完整卡，>3 门画前 2 张 + 1 张「更多 +N」卡位（一格最多 3 个视觉卡位）；`layoutCards(cellRect,n)` 单源几何，`paint()` 空格直通基类、有课格画圆角卡（课程名 / 教师·教室，横向 elide）。**选中格以主题 accent 描边**（`option.palette.highlight()` → `ThemeManager::accentColor()`，切 accent 全应用可见，v4.5）。
- **像素命中**：`mouseReleaseEvent` → `indexAt` → 局部坐标 → `entryAt()`（与 paint 共用 layoutCards，画在哪=点在哪）→ `entryClicked(day,section,ordinal)` / `moreClicked`；TimetableController `showCourseDetailAt` 直达详情、`showMoreCourses` 弹 `CourseListDialog` 长卡列表（含未折叠全部课程，配色与网格卡同源）。单门课整格命中即开。
- **坐标帧坑**：`entryAt` 须把格内局部坐标加回 `cellRect.topLeft()`；由无头回归 `tests/tst_hittest.cpp` 全网格覆盖。

## 9. 锁定与局部重排（v3.4）

- `DataStore` 锁定集 `lockedClassIds()`/`lockClass`/`unlockClass`/`setLockedClasses`；随快照 `[Locks]` 持久化（缺段容错=空）。锁定是**纯管理标记**：被锁有课班在重排中原样保留；未排上的班即使被锁仍会尝试排入（否则变"永不排"）。
- UI：顶栏「锁定」开 `LockDialog`（按课程分组三态树 + 一键锁整门课）；`CourseDetailDialog` 内锁图标按钮单班快捷。引擎取 movable = 全部未锁定班（§5.1）。

## 10. 课程 / 教学班实体分离 + 编辑（v3.5 / v4.2）

- **语义**：course 与 teachingClass 一对多、course 可 0 班；`removeTeachingClass` 删班及记录（排课/失败/锁）**不删课**；`removeCourse` 整门删。快照 `[Courses]` 段存全部课程（含空课），班行课程列仅作兼容（保存时从 `m_courses` 现取回填）；**`[Courses]` 是唯一权威**——加载端逐字段一致性校验，不一致以 `[Courses]` 覆盖并记入 `DataStore::loadWarnings()`（v4.1 R4，不阻断）。CSV 仍按 13 列 courseId 主键导入（课程名不判重）。
- **增删双标签**：`AddDialog`（新增课程 = 建空课不排课 / 新增教学班 = 选课程号+预览带出 → 最小排）；`DeleteDialog`（删除课程 含空课 / 删除教学班 三态树，删班不删课）。
- **基本信息编辑（v4.2）**：字段 = 课程名 / 开课学院（Course），任课教师 / 计划人数 / 最大容量（TeachingClass）。数据层 `core/store/datastore_edit.{h,cpp}` 提供 `updateCourse / updateTeachingClass / reassignClassTeacher / removeScheduleFailureForClass / teacherSwapClashes`（换师会连带改写该班全部已排条目的冗余 `teacherId`）。落库决策收口 `editui::applyCourseEdit / applyClassEdit`（换师撞车 → 只登记 `scheduleFailures` 不应用、弹提示，教务腾位后重试即应用；扩容超当前教室容量 → 确认后 `runMovable({classId})` 换大教室；拒绝则不落库）。
- **入口两处**：
  - 侧栏「编辑」→ `EditInfoDialog` 浏览形态（课程/教学班双页签，`ClassInfoForm` 共享表单），覆盖空课 / 未排班 / 整门改名；
  - 课表点卡片 → `CourseDetailDialog` → 右上「编辑」图标 → `ClassEditDialog`（**双页签一次保存**：页签1「基本信息」嵌 `ClassInfoForm`，页签2「时间·教室」嵌 `TimeRoomEditor`），先落排课后落基本信息。
- 一次编辑 = 一步入环（含仅登记撞车冲突）；触发的局部重排 async 换源不重复 push。

## 11. 教务手动调整（v3.6 core + v4.2 合窗）

- **core 收"最小变更"（manualmove）**：`namespace manual`，`Change{entryId; dayOfWeek; startSection; classroomId}`。`validate`（只读）按 entryId 反查旧条目回填 classId/teacherId/周范围/跨度——**只许改时间+教室，其余由 core 结构性强制**；被替换条目从 ConflictTable 背景剔除，其余（含同班未改次课、锁定班）作背景种入，批内自撞也命中。`apply` 先 validate，Ok 才逐条 `updateScheduleEntry` 原位替换原子落库。
- **原因细分**：`ConflictTable::busyKeysOf(entry)` 返回 R/C/T 命中前缀，配合 Reject 枚举（EntryMissing/RoomMissing/CapacityTooSmall/RoomTypeMismatch/BadRange/Conflict）给可读中文拒绝文案。
- **入口（UI 演进）**：v3.6 为独立 `ManualAdjustDialog`；**v4.2 起并入合并编辑窗** —— `TimeRoomEditor`（本节 / 整班 N 次课两排他模式、逐行 星期/起始节/教室、容量≥人数+类型过滤）被抽取为可嵌入 widget，作为 `ClassEditDialog`「时间·教室」页签；点卡 → 详情 →「编辑」→ 时间·教室页，一次「保存」把排课改动与基本信息一起原子落库（先 `manual::validate/apply` 后 `editui`）。v3.6~v4.2 对未排课（0 课次）的失败班该页签直接禁用并提示（只能改基本信息后靠引擎局部重排重试）。
- **从零排入（v5.0，manualadd）**：0 课次失败班的「时间·教室」页签改由 `FromScratchEditor` 接管：列出该班应排 N 次课（N=`Course.sessionsPerWeek`），每行 星期/起始节/教室 三下拉，打开即 `autoSuggest()` 按首个不冲突空位自动预填（填不满的行标红显示卡点）。保存须 N 次课**全部指定**才走 `manualadd::validateAdd`（整批 H1~H5 + 批内自撞，`ConflictTable` 全量背景），全过 → `applyAdd` 逐条 `addScheduleEntry` + `removeScheduleFailureForClass` 原子落库并移出失败列表；只填部分提示留窗。前提约束结构性强制：该班须 0 课次（`AlreadyPlaced`）、学时 ≥1 整数节（`InvalidHours`）、恰排 N 条（`SlotCountMismatch`）；其余字段（teacherId / 周范围 / entryId=班#1..N）由 core 现读推导，不许 UI 乱改。**成功后外层绝不 runMovable**（引擎会把刚手排的 N 次课清掉重建），仅"确认扩容换大教室"（`editRoomMoveNeeded`）才对该班局部重排；错误列表还剩其它失败班则续开列表。
- **边界**：entryId 不透明不反解析；不改周范围、不自动锁定（提示"如需保留请锁定本班"）；元数据查无时禁用入口。

## 12. 主题系统与图标（v2.2 + v4.5 + v5.0）

- **ThemeManager 单例**：明暗方案 `Scheme{FollowSystem, Light, Dark}`（可跟随系统，监听 `QPalette` 实时切换）× 5 个 accent（蓝/绿/紫/橙/青，各含亮/暗两套色）。偏好经 `QSettings` 持久化；`apply()` 渲染 `theme.qss` 模板（`%VAR%` 占位符）`qApp->setStyleSheet`。
- **语义 token 访问器**（供 QSS 管不到的自绘位显式取用，v4.5）：`accentColor/accentTint/dangerColor/dangerTint/secondaryText/disabledForeground/surfaceColor`；明暗两套基础盘从"换灰"改为**表面分层**（亮：窗底略灰→画布/弹窗 SURFACE 浮起；暗：窗底加深 `#1E1F22`、画布 `#2A2A2E` + 高光描边）。
- **accent 语义角色**：主操作「自动排课」实底；筛选生效点亮（active 态 accent tint）；列表/菜单选中浅衬；选中格描边随 accent；危险独立（排课错误 danger 徽标、状态栏失败首词 danger / 成功首词 accent）。`QSS` 覆盖按钮 primary/danger/active/flat、输入焦点环、列表选中、网格画布、菜单、滚动条、次要文字等。
- **图标**：Feather 图标源集中存根目录 `icons/`（`icons.qrc`，运行前缀 `/theme/icons`）。`themeicon::fromFeather` 读 SVG 字节替换 `currentColor` → 指定色渲染；`buttonIcon` 额外生成 `QIcon::Disabled` 禁用位图（QIcon 不会自动变灰）。顶栏 保存/撤销/重做 为图标+文字钮，随明暗换色、禁用落灰。详情弹窗三钮（锁/编/删）用 fromFeather。`theme.qrc` 现只含 `theme.qss`。
- **应用图标（v5.0）**：`icons/classflow.png` 经 icons.qrc 别名 `app.png` → `main.cpp` `setWindowIcon`（标题栏/任务栏/对话框）；`icons/classflow.ico` 经 `app.rc.in` + windres 编译进 `ClassFlow.exe` 作程序图标（资源管理器/快捷方式）。仅 Windows。

## 13. 空态与视图切换（v4.5）

无教学班数据（未新建/导入）时右栏显示 `EmptyState` 引导页（一句引导 + 两个 accent CTA「新建… / 导入快照…」），替代只刷状态栏；`MainWindow::updateEmptyState` 判据为"是否有教学班"，在数据源整体更换 / 删除后 / 撤销重做换源后切换 `QStackedWidget` 页；空课程（课程存在但无班）仍视为空。

## 14. 数据层要点（store，v3.1 拆分 + v4.1 增量）

- **ID 索引查询**：懒建 `QHash<QString,int>`（id→集合下标）五张表（课程/班/教师/教室/排课条目），`ensureLookupIndexes()` 一次构建、`clear/clearScheduleEntries/dropClassRecords` 失效重建；`courseById/teachingClassById/teacherById/classroomById/scheduleEntryById` 均 O(1)，`updateScheduleEntry` 原位替换保下标（= 保快照序 = 保卡片序，整班搬后卡片不漂移）。
- **snapshotText / loadWarnings**：见 §6.2 / §10。
- **编辑方法**：`datastore_edit` 域的 update / 换师 / 清失败 / 撞车预检（§10）。

## 15. 后续规划

教师个人课表 / 学生个人课表、选课与学生名单、GUI 暴露软约束权重、手动调整增强（整班刚性平移 / 永久手动优先）、全空工作区支持等见 `docs/PRD/ClassFlow_v5.0.md §7`；各分步设计与决策历史见 `docs/Plan/plan_v1.0 ~ plan_v4.6`（plan_v4.5 为 UI 视觉优化、plan_v4.6 为双层保存，均已实施）。
