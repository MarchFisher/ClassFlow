# ClassFlow 开发计划 v3.2

> 版本：v3.2 · 当前版本：v3.1 · 日期：2026-09-02 · 目标：(a) 排课异步化——排课移出 UI 线程并支持进度显示 / 中途取消；同时收尾未提交的"课表格子显示教师姓名"改动

## 目标

1. **排课不再冻结界面**：`runScheduler` 现为 UI 线程内同步跑完整策略（贪心 + 至多 15 万次 move 的退火），大工作量（`data/teaching_classes_large.csv`）会长时间无响应。改为后台线程排课，界面保持可交互。
2. **进度可感知、可取消**：进度按「贪心 → 初始化 → 退火」分阶段上报；退火阶段按温度给出 0~100% 估算。取消语义为"放弃本次排课、保留排课前课表"，并保证运行中关窗不崩溃。
3. **收尾教师姓名显示**：当前未提交改动在课表格子第二行显示"教师 · 教室"；本次收尾并统一"教师缺失"的缺省文案口径。

## 需求

### 功能点

1. **后台排课**：`Scheduler::schedule` 在非 UI 线程执行；排课对一份 `DataStore` **副本**进行，成功后才整体替换回主线程数据源（沿用 `ImportDialog` 的"拷贝 → 换源"既有范式）。
2. **进度上报**：核心新增可选 `ScheduleContext`（含分阶段进度回调 + 原子取消标记），贪心按「已排课程 / 总课程」、退火按「(T₀−T) / (T₀−T_min)」映射百分比；UI 用 `QProgressDialog`（可取消按钮）展示。
3. **取消**：取消分两种情形语义一致——无论在哪一阶段取消，主线程都**忽略**工作线程的副本结果，屏幕上保留排课前的旧课表。
4. **关窗安全**：窗口关闭时若后台仍在排，先请求取消并等待线程退出，再做工作区快照保存（现 `closeEvent` 直接 `saveMemory`）。
5. **防重入**：排课进行中禁用「自动排课 / 新建 / 导入」入口，避免并发排课互相覆盖。
6. **教师姓名收尾**：格子第二行教师姓名逻辑落盘并走查（含未收录教师的回退），详情弹窗与格子的"缺失"文案统一为同一词。

### 现状（改造基线）

- **排课全同步在 UI 线程**：`MainWindow::runScheduler`（[mainwindow.cpp](src/ui/mainwindow.cpp#L426-L450)）→ `Scheduler::schedule`（[scheduler.cpp](src/core/schedule/scheduler.cpp#L27-L39)）→ `IScheduleStrategy::run`。`onNewClicked` / `onScheduleClicked` / 启动恢复都会走到这里。
- **无任何进度 / 取消设施**：核心与 UI 都没有；SA 主循环（[annealingstrategy.cpp](src/core/schedule/annealingstrategy.cpp#L122-L151)）一次跑完，参数上限见 [annealingstrategy.h](src/core/schedule/annealingstrategy.h) 的 `AnnealingParams`（`L`、`maxStallRounds`、`maxMoves`、`alpha`、`minTemp`）。
- **先清后排**：`Scheduler::schedule` 开头 `clearScheduleEntries()` + `clearScheduleFailures()`（[scheduler.cpp](src/core/schedule/scheduler.cpp#L29-L30)）。因此"取消时保留旧课表"不能靠主线程原地重排，必须让工作线程在副本上折腾、主线程不动。
- **换源模式已有先例**：`onNewClicked` 用 `m_store = dialog.dataStore()`（[mainwindow.cpp](src/ui/mainwindow.cpp#L284)）整体替换后 `setDataStore` 全量重建 model——异步完成后的换源可复用同一套"替换 + 重建 + 刷新周选择器 + 状态栏"路径。
- **线程边界**：`DataStore` 各集合是 `QVector`（隐式共享 / COW），工作线程对副本首写即 detach，与主线程渲染的旧数据不同存储，无数据竞争；`mutable` 懒建索引在 `lookup.cpp`，属实例内自建，跨实例不共享。`DataStore` 无 `Q_OBJECT`、默认可拷贝。
- **测试调用**：`tst_scheduler` / `tst_annealing` 直接调用 `Scheduler::schedule(store, &greedy/…)` 与策略 `run(store)`；接口改动需保持这些调用点默认可编译。
- **未提交改动**：工作区 `timetablemodel.h`（新增 `m_teacherName` 成员）与 `timetablemodel.cpp`（`setDataStore` 建教师名映射 + `data()` 显示"教师 · 教室"）尚未 commit。
- **版本/登记**：`CMakeLists.txt` `project(ClassFlow VERSION 3.1 …)`；新增源文件需登记进 `APP_SOURCES`（UI 侧）或 `CORE_SOURCES`（core 侧）。

## 架构设计

### 接口演进：core 侧可选 ScheduleContext

在 [scheduler.h](src/core/schedule/scheduler.h) 定义纯数据上下文，`core/` 仍不含 Widget：

```cpp
enum class SchedulePhase { Greedy = 0, Init = 1, Anneal = 2, Done = 3 };

struct ScheduleContext {
    std::atomic<bool> cancelled{false};   // UI 置位、算法轮询（算法侧只读）
    std::function<void(SchedulePhase, int done, int total)> onProgress = nullptr;
};

// ScheduleResult 增加：bool aborted = false;   // 取消/中止时置位
```

- **签名**：`IScheduleStrategy::run` 与 `Scheduler::schedule` 增加尾参 `ScheduleContext *ctx = nullptr`（基类与各实现声明一致带默认值，旧调用点经默认参数免改）。`SimulatedAnnealingStrategy::run` 内部把 `ctx` 透传给 `GreedyStrategy::run(store, ctx)`。
- **进度**：贪心在每门课循环里上报 `(Greedy, i, courses.size())`（[strategy.cpp](src/core/schedule/strategy.cpp#L314-L365)）；SA 每温度轮上报 `(Anneal, …)`，done 映射为「(T₀−T)/(T₀−T_min) 的已过温度步数 / 总温度步数」——温度逐轮乘 `alpha` 严格单调，分母可预先确定，进度平滑逼近 100%。
- **取消注入点**：贪心每门课开头、SA 的每温度轮开头与每 move 后检查 `ctx->cancelled`。贪心被中断即停止后续课程并返回（剩余班不计失败、不诊断），SA 见贪心中断后**直接返回不再退火**；退火中被中断则返回当前 best-so-far。均置 `aborted = true`。

### 后台执行：worker 对象 + 副本交换

UI 侧新增 `src/ui/scheduleworker.{h,cpp}`（`QObject`，随 `QThread` 迁移）：

```
MainWindow::runScheduler
 ├─ QProgressDialog（模态、可取消、标题"正在排课…"） + 禁用 排课/新建/导入 按钮（防重入）
 ├─ DataStore copy = m_store            // 含当前旧课表；工作线程排课只动这份副本
 ├─ worker->run(copy)                   // worker 移入后台线程
 │    ├─ Scheduler().schedule(copy, nullptr, &ctx)   // ctx.onProgress → queued 信号刷进度条
 │    └─ ctx.cancelled 由 进度框取消按钮 / 关窗 置位
 ├─ 完成信号（成功）
 │    ├─ aborted → 忽略副本，保留旧课表；状态栏提示"已取消"
 │    ├─ 否则 m_store = std::move(result)；setDataStore / applyCardPalette / syncWeekSelector
 │    └─ r.ok==false 时照旧 showScheduleErrors()
 └─ 结束清理：关闭进度框、恢复按钮
```

- **跨线程信号**：`DataStore` 与 `ScheduleResult` 需 `qRegisterMetaType` 后经 queued 连接回传（或 worker 持有结果、完成槽在主线程取）。进度回调用 `QMetaObject::invokeMethod(…, QueuedConnection)` 桥回 UI 线程。
- **关窗**：`closeEvent` 先置取消标记、`wait()` 线程退出，再执行原 `saveMemory`；用 RAII/局部对象保证异常路径也退出线程。
- 新增文件登记进 `CMakeLists` 的 `APP_SOURCES`。

### 线程安全结论

- 工作线程只写自己的副本；主线程读 `m_store` 渲染——COW 首写即分离，二者不同缓冲。切换数据源时 `TimetableModel` 持 `const DataStore*`，仅在替换完成、主线程空闲时才 `setDataStore`，指针生命周期安全。
- `ctx` 的 `cancelled` 为 `std::atomic<bool>`，跨线程置位/读取合法；进度回调仅由工作线程调用。

## 决策记录

| 决策 | 结论 |
|------|------|
| 后台执行方案 | 专用 `ScheduleWorker`（QObject + QThread），而非 QtConcurrent：便于跨线程回传 `DataStore` 结果、持原子取消位、关窗时同步收尾 |
| 线程安全策略 | **副本交换**：worker 在 `DataStore` 副本上排课，成功才替换主源；主线程全程不动旧数据 |
| 取消语义 | 一律"保留排课前课表"：aborted 则忽略 worker 结果。避免用户对"半截课表"困惑 |
| 进度分母 | 贪心=课程索引/总数；退火=温度几何降温的已走步数/总步数（T 单调，可预先算总步数） |
| 接口演进 | `run` / `schedule` 增尾参 `ScheduleContext* = nullptr`（基类与实现一致带默认），旧测试调用点免改；`ScheduleResult` 增 `aborted` |
| 失败明细 | 取消导致的"未处理完的班"**不**计入失败（不算排课失败），仅由 `aborted` 标识 |
| 防重入 | 排课期间禁用 自动排课/新建/导入 按钮 |
| 教师文案统一 | 课表格子与详情弹窗对"该教师未收录/未指定"统一显示 **"未安排"**（当前格子回退 "教师待安排"、详情回退 "未指定"，一并对齐） |
| 版本号 | CMake `project(ClassFlow VERSION 3.2 …)` |
| 构建 | 遵循 build 目录约定，仅用 Qt Creator 标准构建目录验证 |

## 任务分解

### W1 收尾教师姓名显示（未提交改动）

- [x] 走查并整理当前 `timetablemodel.{h,cpp}` 的 `m_teacherName` 改动（建表 + `data()` 第二行），与 `CourseDetailDialog` 的教师行统一缺省文案 **"未安排"**（两处），格子在教师表中缺名但 teacherId 非空时显示 teacherId。
- [x] 视觉走查：亮/暗主题、多班重合格、筛选后，第二行文本不溢出、Tooltip 与显示一致。

**验收**：课表格子显示"教师姓名 · 教室"，未收录教师有清晰占位；详情弹窗与格子文案口径一致。

### W2 core 进度/取消接口 + 植入

- [x] [scheduler.h](src/core/schedule/scheduler.h)：`SchedulePhase`、`ScheduleContext`、`ScheduleResult::aborted`。
- [x] [strategy.h](src/core/schedule/strategy.h) / [annealingstrategy.h](src/core/schedule/annealingstrategy.h)：`run` 增尾参 `ScheduleContext* ctx = nullptr`（声明带默认）。
- [x] [scheduler.cpp](src/core/schedule/scheduler.cpp)：`schedule` 增尾参并透传。
- [x] [strategy.cpp](src/core/schedule/strategy.cpp)：贪心每门课循环内上报进度 + 检查取消；被中断即停，不补诊断失败。
- [x] [annealingstrategy.cpp](src/core/schedule/annealingstrategy.cpp)：`sampleInitialTemp` 后接取消检查；每温度轮上报进度 + 查取消；主循环内 move 计数处查取消；置 `aborted`。

**验收**：无 ctx（nullptr）时行为与 v3.1 逐位一致，`ctest` 6/6 通过。

### W3 UI：ScheduleWorker + 进度框 + 接线

- [x] 新建 `src/ui/scheduleworker.{h,cpp}`：持副本 + `ctx`，`run()` 内同步调 `Scheduler::schedule`；暴露 `progress` / `finished(const DataStore&, const ScheduleResult&)` 信号；`cancel()` 置位原子位；提供线程退出收尾。
- [x] `MainWindow`：`runScheduler` 改造为"建进度框 → 起 worker → 接完成槽"；完成后副本替换 `m_store` + `setDataStore` + `applyCardPalette` + `syncWeekSelector` + 状态栏；`r.ok==false` 照旧弹错误列表。
- [x] 按钮防重入：排课期间禁用 自动排课/新建/导入；完成/取消后恢复。
- [x] `closeEvent`：后台运行中先 `cancel()` + 等线程退出，再走原 `saveMemory`。
- [x] `CMakeLists.txt`：登记 `scheduleworker.*` 进 `APP_SOURCES`；`project` 版本升 3.2；`qRegisterMetaType` 就位。

**验收**：大 CSV 排课时窗口可拖动、进度框推进；点取消后课表回到排课前状态且无半截结果；运行中关窗不崩溃、不残留后台线程；小数据同步完成无感知差异。

### W4 测试（core，`tests/`）

- [x] 无 ctx 回归：现有 `tst_scheduler` / `tst_annealing` 全绿（默认参数兼容）。
- [x] 进度回调：注入 `onProgress`，断言退火阶段有上报且 done 单调不减。
- [x] 取消：注入在首次回调即置 `cancelled` 的 ctx，断言 `schedule` 快速返回、`aborted==true`；对照组不取消则 `aborted==false`。
- [x] `ctest` 6/6 无回归。

> 说明：取消用例只断言语义与返回值、不做耗时断言，避免 CI 抖动。

### W5 构建 / 版本 / 文档

- [x] Qt Creator 标准构建目录编译零告警。
- [x] [architecture.md](../architecture.md)：排课异步化（worker + 副本交换 + 进度/取消）、教师姓名进格子的行为入档；版本号 3.2。
- [x] 本文档（plan_v3.2.md）。

## 验收

1. 排课在后台执行：排课期间界面可交互、进度框实时推进（分阶段）。
2. 取消后保留排课前课表、无部分排课结果残留；排课中关窗安全退出。
3. 无 ctx 的直接调用（含全部既有测试）行为与 v3.1 一致；`aborted` 语义正确。
4. 课表格子显示教师姓名，缺省文案与详情弹窗统一为"未安排"。
5. `ctest` 6/6；版本号升 3.2。

## 阶段依赖

```
W1 教师姓名收尾（独立）      W2 core 接口+植入（不依赖 UI）
W2 ──> W3 UI worker/进度框     W2 ──> W4 测试
W3/W4 ──> W5 构建/文档（W1 可并入 W5 前完成）
```

## 后续（非本版本）

- **(b) 课表卡片 Delegate**（MVD 第 2 步，排 v3.3，见 [plan_v3.3.md](plan_v3.3.md)）：自绘课程卡片替代"拼文本 + QMenu 二选一"，点哪张开哪张。
- **参数/权重 GUI 化**：`AnnealingParams` 目前硬编码，随异步化就绪后可安全外置到界面（PRD §7 已规划权重滑块）。
- **手动改课 / 锁定重排**：异步排课框架铺好后，可扩展"改单条后局部重排"，需先把索引失效从 `clear()` 扩到逐条变更。
