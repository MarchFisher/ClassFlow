# ClassFlow 高校排课系统

基于 Qt Widgets 的高校排课桌面应用：导入 CSV（教学班 / 教室 / 作息表），自动为教学班分配「授课时间 + 教室」，展示全校课表；支持锁定局部重排、课程 / 教学班的增删与基本信息编辑、教务手动调整，以及筛选、会话级撤销 / 重做、快照与双层保存持久化。

**版本：v5.0** · 面向教务管理员 · Windows 桌面端

## 功能特性

- **数据导入**：教学班 / 教室 / 作息表三份 CSV 一键导入（教师 CSV 可选）；教学班内嵌课程字段，按课程号（courseId 主键）自动去重生成课程模板；自动识别教室类型（普通 / 机房 / 操场）
- **自动排课（两阶段）**：贪心构造可行初始解 → 模拟退火邻域优化；贪心保底、SA 只优不劣
  - **硬约束 H1~H5** 零违反：同班时间互斥、同教室同时间唯一、同教师同时间唯一、容量满足、教室类型匹配课程需求
  - **软约束 S1~S7** 加权罚分：课程不拆分、时间离散度、同班同教室、教室浪费、时间负载均匀、教室负载均匀、避免周末上课
- **锁定与局部重排**：锁定教学班或一键锁整门课；再次「自动排课」只动未锁定班，锁定班原排课一像素保留；锁定随工作区快照持久化
- **课程 / 教学班编辑**
  - 增删：双标签新增（建空课程 / 为课程加教学班，新班最小排、放不下升格局部重排）、双标签删除（删整门课 / 删单班）；删班不删课，空课程可保留并另删
  - 基本信息：课程名 / 开课学院、任课教师 / 人数 / 容量可改（侧栏「编辑」列表入口 + 详情弹窗）；扩容超当前教室 → 确认后对该班局部重排换大教室；**换师撞车 → 不自动排，登记冲突列表、延迟应用**（教务把该班腾到新师空闲时段后重新换师即应用）
- **教务手动调整（合并进「编辑」）**：点卡片 → 详情 →「编辑」→ 双页签（基本信息 + 时间·教室）；「时间·教室」页本节 / 整班（N 次课）两种模式，改时间 + 教室整批原子校验硬约束（只查 H1~H5，不走排课线程）；未排课（0 课次）的失败班该页转「**从零排入**」：列出应排的 N 次课，逐次指定 星期 / 起始节 / 教室，打开即自动预填首个不冲突空位，保存时 N 次课都有合法空位才原子落库并移出错误列表（绝不写入冲突条目，其余班失败保留）
- **会话级撤销 / 重做**：顶栏 撤销 / 重做 图标钮 + Ctrl+Z / Ctrl+Shift+Z；新建 / 导入 / 新增 / 删除 / 锁定 / 编辑 / 手动调整 / 自动排课均为可撤销动作（动作前状态环，上限 20 步）
- **双层保存（plan_v4.6）**
  - L1 自动恢复：任意改动后 ~1.5s 防抖自动写 `.classflow/workspace.dat`，关窗再补一次 → 没按过保存也能回到现场（VSCode 式）
  - L2 手动保存：Ctrl+S / 顶部「保存」写教务选定的**项目文件**；首次 = 选路径（导出语义），之后直接覆盖，路径跨会话记住，窗口标题带脏标记 ●
  - 侧栏「导出快照」：另存**独立副本**，不改当前项目文件、不影响 Ctrl+S 落点
- **全校课表**：横看周几、竖看第几节；同格多班多行卡片堆叠（>3 门折叠为「更多」列表），逐卡自绘着色、点击直达详情；支持按周切换
- **筛选**：按教师 / 教室 / 课程多选过滤课表；筛选生效时侧栏「筛选」按钮点亮并显示条件数
- **课程详情**：查看课程信息 + 锁定/解锁本班 + 编辑 + 删除（本节 / 连整门课）
- **失败诊断**：排不下时列出失败教学班及归因；顶栏「排课错误」带 danger 计数徽标；每行可直接「编辑」该班——复用卡片详情的同款双页签编辑窗（基本信息 + 时间·教室）。0 课次的失败班打开即进入「**从零排入**」，教务手动指定 N 次课的时段 + 教室，排满且全不冲突才落库（该班移出列表，不必再走引擎重排）；其它失败班确认后自动局部重排重试一次（其余失败明细保留）
- **快照与工作区**：九段单文件快照导入/导出（含课程实体 `[Courses]` 与锁定 `[Locks]`，老快照缺段兼容）；快照加载一致性校验（班行课程列与 `[Courses]` 不符时以权威段为准并记入告警）
- **主题系统**：明 / 暗（可跟随系统）× 蓝 / 绿 / 紫 / 橙 / 青 accent；课表卡片随主题着色，选中格描边随 accent，禁用态与 danger 语义独立配色
- **空态引导**：无数据时显示引导页（新建… / 导入快照… CTA），替代单行状态栏提示
- **运行期防误操作**：排课进行中，自动排课 / 锁定 / 撤销 / 重做 / 新建 / 导入 / 导出 / 编辑 / 新增 / 删除 / 筛选等入口全部禁用
- **应用图标**：窗口 / 程序均使用 ClassFlow 图标（`icons/classflow.png` 经 qrc 供运行期窗口图标；`icons/classflow.ico` 经 windres 嵌入 exe 程序图标）

## 技术栈

C++17 · Qt 6 Widgets · CMake / Ninja · 中文本地化（`tr()` + TS/QM）；Feather 图标（SVG → 主题色渲染）；Windows 图标嵌入（windres / `.rc`）

## 构建

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=<Qt 安装路径> \
  -DCMAKE_CXX_COMPILER=<g++ 路径>
cmake --build build
```

也可用 Qt Creator 打开 `CMakeLists.txt` 直接构建运行（推荐，勿另建手动构建目录）。

## 运行与使用

```bash
./build/ClassFlow.exe        # Windows
```

1. 点击「新建」，依次选择教学班 / 教室 / 作息表 CSV（教师 CSV 可选；可参考 `data/` 下示例），导入后自动排课
2. 查看全校课表，必要时用筛选 / 课程详情 / 排课错误检查结果
3. 需要维护课程表：用「新增 / 删除」编辑课程与教学班；用「编辑」改课程名 / 学院 / 教师 / 人数 / 容量；用「锁定」保护要保留的班，再点「自动排课」只重排其余
4. 局部微调：点卡片 → 详情 →「编辑」→「时间·教室」页挪某次课或整班的时间 / 教室（与基本信息改动一次保存）
5. 保存与存档：Ctrl+S 保存到项目文件（首次选路径）；侧栏「导出快照」另存独立副本；任意改动自动记忆，退出 / 崩溃后可恢复

## 测试

```bash
cd build && ctest
```

10 个 QtTest 套件（`tests/`，经 CTest 注册）：datastore / snapshot / conflicttable / scheduler / filter / annealing / movable / manualmove / manualadd / hittest。覆盖 CSV 解析与导出往返、快照往返（含 `[Courses]`/`[Locks]` 段与缺段容错、课程一致性校验）、冲突检测、贪心排课正确性、筛选判定、模拟退火、锁定冻结与最小排零扰动、手动调整（拒绝细分 / 整批回滚不写库）、从零排入（整批落库清失败 / 批内自撞 / 教室教师冲突细分 / 容量类型学时前提拒绝 / apply 失败零写库）、撤销/重做、entryId 兼容、课表点击命中回归。

## 示例数据（data/）

数据集按**分类 / 具体数据集**两级存放（详见 `data/README.md` 索引）。每个数据集都是自含目录，
内含统一命名四件套：`teaching_classes.csv`（教学班，13 列，含课程号与内嵌课程字段）、
`classrooms.csv`（教室）、`sections.csv`（作息表）、`teachers.csv`（可选，教师清单）。

- `small/classic`：11 班 / 6 门课经典样例（全可排通）
- `large/eighteen`：18 班 / 6 门课中量级样例（全可排通）
- `large/stress`：约 200 班压力样例（可全排通，压测算法耗时）
- `conflict/*`：必然冲突样例（无机房 / 超大合班 / 教师超载 / 机房饱和）
- `error/*`：错误或怪异数据样例（非法学时、周次颠倒、容量倒挂、重复 classId、
  列数不符、未知类型词、教师不在名册、courseId 内嵌字段不一致、越界周、空教室文件等）

## 目录结构

```
ClassFlow/
├── CMakeLists.txt
├── src/
│   ├── core/                 # 纯逻辑层（无 Widget，可独立单测）
│   │   ├── models/           # 数据模型：课程/教学班/教师/教室/作息/排课条目
│   │   ├── store/            # 数据存取：datastore/csv/snapshot/lookup/utility
│   │   │                     #   + datastore_edit(编辑方法)/undobuffer(撤销环)
│   │   ├── schedule/         # 排课核心：冲突表 + 贪心 + 模拟退火 + manualmove(手动调整) + manualadd(从零排入) + roomrules
│   │   └── filter/           # 筛选核心：条件数据 + 判定
│   └── ui/                   # Qt Widgets 界面
│       ├── mainwindow.*/.ui  # 主窗口（顶栏 + 侧栏 + 右栏画布）
│       ├── emptystate.*      # 空态引导页
│       ├── dialog/           # 弹窗（导入/编辑/新增/删除/锁定/详情/错误等）
│       ├── timetable/        # 课表 MVD：model/delegate/view + timetablecontroller
│       ├── schedule/         # 后台排课：worker + controller
│       └── theme/            # 主题系统 + 主题色图标（themeicons）
├── icons/                    # 应用图标 + Feather 图标源（icons.qrc，资源前缀 /theme/icons）
├── data/                     # 数据集（分类/数据集两级，自含四件套 CSV + README）
├── tests/                    # 单元测试（CTest，10 套件）
└── docs/
    ├── PRD/                  # 产品需求文档（ClassFlow_v5.0.md 现行；v4.0/v3.0/v1.0 为历史）
    ├── architecture.md       # 架构设计
    ├── data-model.md         # 数据模型规范
    └── algorithms/           # 算法设计（greedy-strategy / simulated-annealing）
```

## 文档

- 产品需求文档：[docs/PRD/ClassFlow_v5.0.md](docs/PRD/ClassFlow_v5.0.md)（历史版本见 [ClassFlow_v4.0.md](docs/PRD/ClassFlow_v4.0.md)、[ClassFlow_v3.0.md](docs/PRD/ClassFlow_v3.0.md)、[ClassFlow_v1.0.md](docs/PRD/ClassFlow_v1.0.md)）
- 架构设计：[docs/architecture.md](docs/architecture.md)
- 数据模型：[docs/data-model.md](docs/data-model.md)
- 排课算法：
  - 贪心策略：[docs/algorithms/greedy-strategy.md](docs/algorithms/greedy-strategy.md)
  - 模拟退火：[docs/algorithms/simulated-annealing.md](docs/algorithms/simulated-annealing.md)

## 许可

本项目自有代码以 [MIT License](LICENSE) 发布，Copyright (c) 2026 MarchFisher。

分发时请注意：免安装包中随程序附带的 Qt 运行库并非 MIT 授权，其版权归 The Qt Company
及贡献者所有，依 LGPLv3 条款随附分发。
