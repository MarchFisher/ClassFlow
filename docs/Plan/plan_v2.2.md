# ClassFlow 开发计划 v2.2

> 版本：v2.2 · 当前版本：v2.2 · 日期：2026-08-30 · 目标：主题系统（明/暗 + 颜色主题）

## 目标

为应用引入主题系统：**明 / 暗两套基础模式** + **多套颜色主题**，覆盖全部 UI 控件；
课表卡片的「同课同色」配色改为**主题感知**（明暗各一套卡片盘），保证两种模式下都可读。
主题选择**持久化**（应用级偏好），启动时恢复。

## 需求

### 功能点

1. **明 / 暗模式**：整体浅色 ↔ 深色切换（背景、文字、边框、控件状态色）。
2. **颜色主题**：若干套预置 accent 色（强调色）——作用于按钮、选中高亮、链接、进度等强调处。
3. **课表卡片适配**：卡片底色 + 前景字色随明暗主题切换（暗模式不再用浅色底黑字）。
4. **切换入口**：主窗口内可切换，无需重启。
5. **持久化**：主题选择保存为应用级偏好，重启恢复。

### 现状（改造基线）

- 全部控件为 Qt 默认样式，无任何 QSS。
- 唯一硬编码颜色：[timetablemodel.cpp](src/ui/timetablemodel.cpp) `kCardPalette[10]`（浅色底），经 `Qt::BackgroundRole` 返回，黑字可读；暗模式下不适用。
- 主窗口顶部栏（保存 / 周选择器 / 自动排课 / 排课错误）与侧边栏（新建 / 导入 / 导出 / 筛选）均为 `QPushButton`；菜单栏（mainwindow.ui）目前为空。

## 架构设计

### 模块位置：`src/ui/theme/`（UI 表现层，禁入 core）

```
src/ui/theme/
├── theme.h / theme.cpp        # ThemeManager 单例：持有当前主题，生成并应用 QSS
└── theme.qss                  # QSS 模板，含占位符（配色变量），由 ThemeManager 渲染
```

### ThemeManager（单例，QObject）

- 状态：`scheme`（FollowSystem / Light / Dark）+ `accentId`（如 Blue / Green / Purple / Orange…）
- 实际明暗：`effectiveMode()`——手动态直接返回；跟随态经 `QStyleHints::colorScheme()` 解析（Qt 6.5+，Qt 5 恒 Light）
- `apply()`：按实际 mode × accent 渲染 QSS 模板 → `qApp->setStyleSheet(...)`
- `setScheme()` / `setAccent()`：切换后重发 `apply()` 并广播 `themeChanged` 信号
- 持久化：`QSettings` 存 `theme/scheme`、`theme/accent`，构造时恢复
- 暴露课表配色盘：`QVector<QColor> cardBackgrounds()` / `cardForegrounds()`（明暗各一套，随当前主题返回）
- 跟随系统模式下监听系统明暗变化（`QApplication::paletteChanged` 或 `QEvent::ApplicationPaletteChange`），变化即重发 `apply()`

### 配色模型

- **基础色**：由 `mode` 决定——背景、前景、边框、禁用、悬停、选中底色与前景。
- **强调色 accent**：在基础色之上叠加——主按钮底色、选中高亮、链接、聚焦边框。
- 二者可自由组合（明蓝、明绿、暗紫、暗橙…），而非预置死整套主题。

### 课表卡片适配

- `TimetableModel` 不再持有硬编码 `kCardPalette`，改为由外部注入「当前主题卡片盘」（明暗两套），存成员 `QVector<QColor> m_cardPalette`。
- `data()` 返回 `Qt::BackgroundRole` 时取卡片盘对应色；同时新增 `Qt::ForegroundRole` 返回卡片**文字色**（暗盘用浅字），由卡片盘带出。
- 注入时机：`setDataStore()` 时；主题切换时由 MainWindow 调用 `m_model.setCardPalette(...)` 后刷新。

## 决策记录

| 决策 | 结论 |
|------|------|
| 切换入口 | 顶栏加「主题」按钮 → QMenu（明/暗 + accent 列表） |
| 主题粒度 | **明暗 × accent 自由组合**（2 模式 × 4~5 accent = 8~10 种） |
| 跟随系统明暗 | **支持，作为可选项**（Qt 6.5+ `QStyleHints::colorScheme()`；Qt 5 下自动隐藏该选项） |
| 实现机制 | QSS 模板 + 变量渲染（不硬编码每条规则） |
| 持久化 | `QSettings`（应用级，非工作区） |
| 卡片配色 | 明暗各一套卡片盘 + 对应文字色 |

### 跟随系统语义

「跟随系统」是第三种模式选择：菜单里三选一——**跟随系统 / 手动明 / 手动暗**。
- 跟随系统时，实际明暗由 `QStyleHints::colorScheme()` 动态解析（Qt 6.5+）；系统切换时应用实时跟随。
- 手动选明 / 暗后即退出跟随模式；切回「跟随系统」恢复自动。
- Qt 5 编译时该项不出现在菜单（`QT_VERSION >= QT_VERSION_CHECK(6,5,0)` 条件）。

## 任务分解

### W1 主题模块（`src/ui/theme/`）

- [x] `theme.qss`：基础控件规则（按钮 / 输入框 / 列表 / 表格 / 表头 / 菜单 / 弹窗 / 状态栏 / 滚动条 / 工具提示），用占位符表示配色变量
- [x] `ThemeManager`：单例 + `apply()` 渲染 QSS + `setMode`/`setAccent` + `themeChanged` 信号 + `QSettings` 持久化 + `cardBackgrounds()` 明暗卡片盘
- [x] CMake：`theme.qss` 加入资源（`theme.qrc`，AUTORCC）

**验收**：构造后 `apply()` 能正确应用 QSS；切换 mode/accent 后样式即时变化；重启后恢复上次选择。

### W2 主窗口接入

- [x] 顶部栏加「主题」按钮 + 弹出菜单：跟随系统 / 明 / 暗（Qt 6.5+ 才有「跟随系统」）+ accent 列表
- [x] 主题切换 → `ThemeManager::setScheme/setAccent` → `themeChanged` 后刷新课表卡片配色
- [x] 启动时先 `ThemeManager` 恢复主题再 `setupUi`（main.cpp 中 apply 后建窗口）

**验收**：点击切换全 UI 即时变色；课表卡片随主题换配色盘；重启保持；跟随系统态下切换系统深浅色实时跟随。

### W3 课表卡片配色（`src/ui/timetablemodel.*`）

- [x] 移除硬编码 `kCardPalette`，改注入 `setCardPalette(QVector<QColor>, QVector<QColor>)`（底色盘 + 文字色盘）
- [x] `data()` 增补 `Qt::ForegroundRole`（卡片文字色）
- [x] `setDataStore()` 与主题切换时同步注入（`assignCourseColors()`）

**验收**：明/暗两种模式下卡片底色与文字均可读；同课同色逻辑不变（轮换取色）。

### W4 构建验证

- [x] `cmake --build build/Desktop_Qt_6_9_2_MinGW_64_bit-Debug` 通过（v2.2）
- [ ] 手动走查验收用例（明暗 × 各 accent，含课表卡片可读性）

## 验收

1. 明 / 暗切换后，背景、文字、按钮、选中高亮、表头全部随主题变化，无「白底白字 / 黑底黑字」死区。
2. 切换 accent 后强调色（主按钮、选中高亮）变化，其余色不变。
3. 明 / 暗下课表卡片底色与文字均可读；同课同色逻辑保持（同课同色、异课异色）。
4. 主题选择重启后保持。
5. 「跟随系统」若启用：系统切明暗时应用跟随。
6. 切换过程无闪退、无控件残留旧色。

## 阶段依赖

```
W1 主题模块 ──> W2 主窗口接入 ──> W3 卡片配色 ──> W4 构建验证
```

## 后续（非本版本）

- 自定义 accent 颜色（调色板）
- 主题应用于导出图片 / 打印
