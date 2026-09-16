# ClassFlow 开发计划 v3.3

> 版本：v3.3 · 当前版本：v3.2 · 日期：2026-09-02 · 目标：(b) 课表卡片 Delegate——Model-View-Delegate 落地：自绘"每格多张课程卡片"，点哪张开哪张，取代"整格拼文本 + QMenu 二选一"

## 目标

1. **引入自定义 Delegate**：目前课表网格 `QTableView` + `TimetableModel` 只用了 M(odel)+V(iew)，靠默认 delegate 把 `data()` 返回的堆叠文本平铺显示。改为 `QStyledItemDelegate` 子类逐格绘制课程卡片，一张卡片一门课（各自的课程色、圆角、文本）。
2. **点哪张开哪张**：多门课重合一格时，目前只能弹 QMenu 让用户二选一（`onTimetableClicked` 拿不到像素位置）。新增视图子类做"像素 → 具体条目"命中，直接打开对应 `CourseDetailDialog`，移除 QMenu 兜底。
3. **职责归位**：卡片文本拼装与配色逻辑从模型 `data()` 移入 delegate；模型只提供内容与工具访问器，单元格不再整体染成第一条课程色。

## 需求

### 功能点

1. **卡片绘制**：cell 内按条目数纵向等分 n 张圆角卡片；每卡底色/文字色取该教学班课程在主题卡片盘中的配色（沿用现有 `assignCourseColors` 的"同课同色"语义）；卡上文本"课程名" + "教师 · 教室"，过长 elide/换行。
2. **空单元格**：不画卡片，保留网格底纹与行列表头现状。
3. **选中态**：被选中单元格的卡片叠一层高亮/描边（默认 delegate 的选中绘制被替换后需自行表达）。
4. **命中测试**：点击某格内某张卡片 → 打开那门课的详情；点击空白格不动作。单选高亮行为不变。
5. **Tooltip**：保留现有按格聚合的周范围 Tooltip（模型 `ToolTipRole` 不变）。
6. **模型瘦身**：`data()` 不再为有课单元格拼堆叠文本、不再返回整格 `BackgroundRole/ForegroundRole`；DisplayRole 仅作可访问性/剪贴板的单行摘要。

### 现状（改造基线）

- 单元格目前是"堆叠文本 + 整格单色"：`TimetableModel::data`（[timetablemodel.cpp](src/ui/timetablemodel.cpp#L299-L343)）把多条 `ScheduleEntry` 拼成 `"课程名\n教师 · 教室"` 文本，`BackgroundRole`/`ForegroundRole` 只取第一条课程色整格铺色。同格不同课程无法各自着色。
- **无任何自定义 delegate**（全仓 `grep Delegate` 无命中）；行/列 `Stretch`、单词换行等见 [mainwindow.cpp](src/ui/mainwindow.cpp#L148-L156)。
- **点击无法区分同格多条课**：`MainWindow::onTimetableClicked(const QModelIndex&)`（[mainwindow.cpp](src/ui/mainwindow.cpp#L368-L393)）先 `entriesAtCell` 取整格条目，多条时弹 `QMenu` 选择。
- **配色已按教学班备好**：模型 `assignCourseColors()` 已把 `教学班 → (底色, 文字色)` 存在 `m_courseColor` / `m_courseFgColor`（[timetablemodel.cpp](src/ui/timetablemodel.cpp#L120-L141)），只是目前只能以"整格第一条"粒度暴露。
- **文本拼接在 v3.2 刚定型**：第二行"教师 · 教室"（含 `m_teacherName` 映射）是 v3.2 收尾后的形态——delegate 可原样复用这套 `courseNameOfClass` / 教师映射 / 配色，把"拼字符串"换成"逐卡画"。
- 版本/登记：`CMakeLists.txt` `VERSION 3.2`；UI 侧新文件需登记进 `APP_SOURCES`。

## 架构设计

### 模块位置

```
src/ui/
├── timetableview.{h,cpp}     # TimetableView : QTableView —— 命中测试 + entryClicked 信号
├── timetabledelegate.{h,cpp} # TimetableDelegate : QStyledItemDelegate —— 逐格画卡片 + entryAt()
├── timetablemodel.{h,cpp}    # 瘦身：去文本拼装/整格配色角色，暴露按教学班访问器
└── mainwindow.{h,cpp}        # m_view 改 TimetableView；接 entryClicked；删 QMenu 分支
```

### 数据/职责切分（关键决策）

- **内容访问**：delegate 持 `TimetableModel*`（`qobject_cast<TimetableModel*>(index.model())`）：
  - 条目列表 → 复用现成 `entriesAtCell(day, section)`（day = col+1、section = row+1，与模型行列约定一致）。
  - 每卡配色 → 模型新增两个公开访问器 `QColor cardBackground(const QString &classId)`、`QColor cardForeground(const QString &classId)`（返回 `m_courseColor` / `m_courseFgColor` 的现成结果）。
  - 课程名 / 教师名 → `courseNameOfClass` 已公开，教师名经同一查询语义由模型补一个 `QString teacherNameOf(const QString &teacherId)` 访问器（无则"未安排"，口径同 v3.2）。
  - delegate 与 model 同属 `ui/` 表现层，类型耦合可接受；模型公开的仍是"查表访问器"，非把内部结构外泄。
- **paint**：`paint()` 内对 `entriesAtCell` 为空则不画（交给基类画空表底）；有 n 条则调静态 `layoutCards(cellRect, n)` 得 n 个卡矩形，逐卡：圆角矩形填课程底色 → 深/浅文字色两行文本居中（按卡内高决定字号/是否换行）→ 首尾留隙。`option.state & State_Selected` 时在 cellRect 上加半透明选中层或给每卡描强调边。
- **几何单一来源**：`layoutCards` 与命中 `entryAt` 共用同一静态几何函数，保证"画在哪 = 点在哪"。
- **命中路径**：`TimetableView::mouseReleaseEvent` → `indexAt(pos)` → 若 delegate 为 `TimetableDelegate`，调其 `int entryAt(const QModelIndex&, const QPoint &cellLocalPos)`（返回 -1 表示没点中卡）→ 命中则 `emit entryClicked(day, section, ordinal)`。空格/空白不发射。
- **MainWindow 接线**：`connect(m_view, &TimetableView::entryClicked, this, &MainWindow::showCourseDetailAt)`；新槽按 `(day, section, ordinal)` 取 `entriesAtCell(day, section).at(ordinal)` 直接开详情。删除 `onTimetableClicked` 及其中 `QMenu` 逻辑；`showCourseDetail(const ScheduleEntry&)` 保留复用。
- **模型瘦身后的角色**：
  - `DisplayRole`：返回第一条课程名单行摘要（可访问性 / 键盘 / 复制兜底），不再拼多行；
  - `ToolTipRole`：保留按格聚合的逐课周范围文本；
  - `BackgroundRole` / `ForegroundRole` 对"有课格"不再返回整格色（整格底纹交还给视图默认）。
- **与主题 QSS**：delegate 自绘只覆盖"有课格"；表头/空表/选中底色等继续由 QSS + 默认绘制负责，避免冲突；卡片色全部来自注入的卡片盘，明暗切换路径（`applyCardPalette` → `setCardPalette` → 模型重分配）不变，delegate 每次 paint 实时取色，无需额外联动。

## 决策记录

| 决策 | 结论 |
|------|------|
| Delegate 形态 | `QStyledItemDelegate` 子类自绘；不用 QAbstractItemDelegate（保留默认 editor/tooltip 等能力最小覆盖） |
| 每格布局 | 纵向均分 n 张卡片（间距/边距常量集中定义），卡片横向撑满留白 |
| 每卡配色 | 复用模型 `assignCourseColors` 结果，delegate 按教学班查表实时取色（主题切换零额外刷新） |
| 命中方式 | 视图子类 `TimetableView` 把鼠标事件坐标经 delegate `entryAt` 解析为 (格, 卡序)，发 `entryClicked` 信号 |
| QMenu 去留 | 普通卡移除：点卡片即直达详情；同格 >3 门时折叠，「更多」卡位弹 `CourseListDialog` 列该格全部课（点卡开详情，走查反馈后补/升级） |
| 文本拼装位置 | 从 `data()` 移入 delegate（逐卡文本）；DisplayRole 仅单行摘要；ToolTip 保留在模型 |
| 类型耦合 | delegate↔model 为 ui 层内部耦合（`qobject_cast` 具体模型），不新增跨层接口 |
| 版本号 | CMake `project(ClassFlow VERSION 3.3 …)` |
| 构建 | 遵循 build 目录约定，仅用 Qt Creator 标准构建目录验证 |

## 任务分解

### W1 模型访问器与角色瘦身

- [x] `timetablemodel.h/.cpp`：新增 `QColor cardBackground(classId)` / `cardForeground(classId)` / `QString teacherNameOf(teacherId)` 公开访问器。
- [x] `data()`：有课格不再拼多行堆叠文本、不再返回整格 `BackgroundRole/ForegroundRole`；DisplayRole 收敛为单行摘要（第一条课程名），ToolTipRole 保持逐课周范围文本。
- [x] 保留 `entriesAtCell` / `courseNameOfClass` / `setCardPalette` 接口不变（delegate 与点击逻辑的既有消费点）。

**验收**：去掉 delegate 前若直接跑，网格仅剩单行摘要与整格底纹——此中间态允许，但**本版本提交前必须完成 W2**（delegate 补上绘制），避免中间态入库。

### W2 TimetableDelegate：布局 + 绘制

- [x] 新建 `src/ui/timetabledelegate.{h,cpp}`：静态 `layoutCards(const QRect&, int n)`（单源几何）；`paint()` 对空格直通基类、有课格逐卡绘制（底色/文字色/文本/选中态）；`entryAt()` 复用布局做命中。
- [x] 卡内文本组装：课程名（`courseNameOfClass`）+ "教师 · 教室"（`teacherNameOf`），按卡尺寸 elide；Tooltip 交给模型。

**验收**：同格多门课各显其色、边界清晰；亮/暗主题下文字可读；窗口拉伸卡矩形随之变化且"画点一致"。

### W3 TimetableView 命中 + MainWindow 接线

- [x] 新建 `src/ui/timetableview.{h,cpp}`：重写 `mouseReleaseEvent`，命中后 `emit entryClicked(int day, int section, int ordinal)`。
- [x] `mainwindow.cpp`：`m_view` 改用 `TimetableView`；连接 `entryClicked` 到新槽 `showCourseDetailAt(day, section, ordinal)`（内部 `entriesAtCell().at(ordinal)` → 既有 `showCourseDetail`）；删除 `onTimetableClicked` 与 `QMenu` 分支。
- [x] `CMakeLists.txt`：登记 `timetabledelegate.*` / `timetableview.*` 进 `APP_SOURCES`；`project` 版本升 3.3。

**验收**：单击单课直开详情；单击同格某张卡只开那一门；点空白格无反应；键盘/选中态不被破坏。

### W4 走查 / 测试 / 文档

- [ ] 走查：筛选后、跨周切换、多节连续课（跨行卡片对齐）、window 缩放等场景无错位/溢出。
- [ ] 布局几何与模型访问器为纯函数/纯查表：若便于单测可上移 core（可选，非必须；UI 几何以手工走查为主）。
- [x] `architecture.md`：课表网格的 MVD 结构（TimetableModel + TimetableView + TimetableDelegate）与卡片交互入档；版本号 3.3。
- [x] 本文档（plan_v3.3.md）。

> 注：上述 W1~W3 代码与 W4 文档项已完成并构建通过；下方"W4 走查"（筛选/跨周/连续课/缩放等）为 GUI 视觉验收，需人工在运行的应用中确认，暂未勾选。

### 走查反馈修正：同格课程折叠（防卡过矮，恢复逐卡点击）

人工走查反馈：同格课程过多时各卡被压到几像素，既难读也点不中（表现为"点卡片开详情"只在个别格生效）；需把过多课程折叠。

- **折叠规则**（与人类开发者敲定）：一格 ≤3 门课全部画成完整课程卡；>3 门时只画前 2 张完整卡，余下收进一张「更多 +N」卡位——**一格里最多 3 个视觉卡位**。
- **实现**：`TimetableDelegate` 增纯函数 `visibleCourseCount(total)` / `hasOverflow(total)` / `slotCount(total)`，`layoutCards` 改按 `slotCount` 均分实际绘制数；`paint` 对前 `visible` 张画完整卡、余位画中性色「更多 +N」卡位；`entryAt` 命中完整卡返回条目序、命中「更多」返回 `MoreHit`。单门课整格命中即算点中，消除格边留白漏点。
- **交互**：`TimetableView` 增 `moreClicked(day, section)` 信号；外层 `TimetableController::showMoreCourses` 弹 `CourseListDialog` 列该格全部课程（见下文"更多 → 全课程列表弹窗"）。
- **验收**：同格 ≥4 门时前 2 门可读可点、「更多」可弹出并直达任意折叠课；网格缩放/跨周后几何一致；`ctest` 6/6。

### 走查反馈修正：点卡命中失效只余首行首列（坐标帧不一致，根因）

折叠修正后人工走查仍报"点卡片开详情只有第一行第一列有效"。用无头 GUI 测试（QTest 注入点击 + QSignalSpy）复现定位：

- **根因**：`layoutCards` 返回的是**绝对坐标**卡矩形（`cellRect.left()+边距` 起，`paint` 直接按此落笔），而 `entryAt` 却拿视图换算的**格内局部坐标**（`event->pos()-cell.topLeft()`）去 `contains` 这些绝对矩形——两套坐标系错配。只有视口首格 `visualRect.topLeft()==(0,0)` 时局部==绝对，恰巧命中，故仅首行首列可开详情（滚动/其它格全失效）。
- **修复**：`entryAt` 把局部坐标加回 `cellRect.topLeft()` 换成与卡矩形同系再做命中判定；单门课判据改为相对尺寸矩形，语义不变。
- **回归**：新增 `tests/tst_hittest.cpp`（无头 GUI：真实 TimetableView+Model+Delegate，遍历全网格逐张卡点击，断言 `entryClicked(day,section,卡序)` 正确发射），tests/CMakeLists 单列该目标；`ctest` 7/7。

### 走查反馈修正：mainwindow.cpp 按功能拆出两个控制器

- **动机**：`mainwindow.cpp` 607 行超 500 行约束，且同一文件编排「文件动作 + 排课生命周期 + 课表查看」三类职责。按用户选定形态「抽排课+课表两个控制器」拆。
- **拆分**：新建 `ScheduleController`（src/ui/schedule/：worker+QThread 起停、进度框、取消/防重入、完成才换源，发 `scheduleDone` 汇总信号）与 `TimetableController`（src/ui/timetable/：周切换/周范围对齐/筛选/点卡详情/「更多」弹窗/错误查看）；`mainwindow.cpp` 瘦身到只留构造 + 文件动作（新建/导入快照/导出快照/保存记忆）+ 接线，397 行。行为全部沿用原函数（复制粘贴为主），取消仍保留旧课表。
- **验证**：构建通过，`ctest` 7/7 无回归；改动文件均 <500 行。

### 走查反馈修正（升级）：「更多」→ 全课程列表弹窗

人工走查确认折叠可用后提出：点「更多」时希望**新弹窗**里是一列可滑动的**长条形课程卡**，能浏览该格**全部**课程（不只被折叠的），点任意卡仍开详情。

- **实现**：新增 `src/ui/dialog/courselistdialog.{h,cpp}`（可滚动 `QScrollArea` + 逐张自绘长卡：课程色底/圆角/两行文本/悬停提示；点卡发 `cardClicked(entry)`）；`TimetableController::showMoreCourses` 不再用 QMenu，改为取该格**全部** `entriesAtCell` → 用模型访问器拼每行（标题/教师·教室[·周范围]/同课同色）→ 弹窗。行数据与配色与网格卡同源；入口仍是折叠「更多」卡位（>3 门才有）。
- **验证**：构建通过，`ctest` 7/7 无回归；改动文件均 <500 行。

## 验收

1. 网格每格内一门课一张卡片、多门各按课程色绘制，不再整格单色平铺文本。
2. 点击卡片直达对应课程详情；无 QMenu；空白格无动作。
3. `data()` 不再承担堆叠文本与整格配色；DisplayRole/ToolTip 语义明确。
4. 亮/暗主题、筛选、周切换、缩放均正常；`ctest` 6/6 无回归；版本号升 3.3。

## 阶段依赖

```
W1 模型访问器/角色瘦身 ──> W2 Delegate 绘制（先完成，避免中间态入库）
W2 ──> W3 视图命中 + 接线      W1/W3 并行度低，基本串行
W1~W3 ──> W4 走查/文档（W4 与 W3 末段并行）
```

## 后续（非本版本）

- **多视图共享 MVD**：卡片 delegate 就绪后，教师/学生个人课表只需再做一个模型 + 复用同一 `TimetableDelegate`（PRD §7 已规划个人课表）。
- **筛选弹窗 model-view 化**：三组 `QListWidget` 迁到 `QAbstractListModel` + `QListView` + `QSortFilterProxyModel`，替换手动 `setHidden` 子串过滤。
- **排课错误表 MVD 化**：`QTableWidget` 迁 `QTableView` + 读 `store.scheduleFailures()` 的只读模型。
