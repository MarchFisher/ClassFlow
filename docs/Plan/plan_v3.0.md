# ClassFlow 开发计划 v3.0

> 版本：v3.0 · 当前版本：v2.2 · 日期：2026-08-31 · 目标：排课算法核心升级——模拟退火优化阶段落地 + 时间/教室负载均匀

## 目标

把排课核心从"贪心构造"升级为文档既定的**两阶段策略**：

1. **构造阶段**：沿用 `GreedyStrategy` 生成满足全部硬约束的可行初始解（已支持多学时跨节）。
2. **优化阶段**：新增 `SimulatedAnnealingStrategy`（模拟退火），在可行解邻域内微调，达成用户明确要求——**所有课程在时间与教室的利用上尽可能均匀**。

落地后，"自动排课"**直接使用模拟退火**（不加手动切换入口）；贪心保留为初始解生成器与对比基线（测试中显式指定）。

## 需求

### 功能点

1. **多学时跨节（基线）**：`hoursPerSession` = 每节课占的节数，一次课跨多个连续节次在网格渲染；非整数学时当作数据错误拒绝。
2. **模拟退火优化**：在贪心可行解基础上，通过邻域操作降低软约束罚分，硬约束全程零违反。
3. **时间负载均匀（S5）**：各时间槽 `(day, section)` 并发负载尽量均衡，避免"周一塞满、周五空转"。
4. **教室负载均匀（S6）**：各教室每周占用节数尽量均衡，避免"几间教室忙不过来、其余闲置"。
5. **直接接入退火**：`Scheduler` 默认策略改为模拟退火，UI 不加手动选择入口。

### 现状（改造基线）

- `GreedyStrategy`（[strategy.cpp](src/core/schedule/strategy.cpp)，约 380 行）已支持多学时跨节、非整数学时报失败（本次会话完成，见 §任务分解 W1）。
- `ConflictTable` 已具备 `canPlace` / `place` / `remove`；`remove` 目前无调用者，SA 将首次使用它做移动回退。
- `Scheduler::schedule(store, strategy)` 已有策略参数，但 `MainWindow` 目前走默认贪心。
- SA 设计文档 [docs/algorithms/simulated-annealing.md](docs/algorithms/simulated-annealing.md) 已完备（含 S1~S6 软约束、M1~M5 邻域操作、退火参数、负载均匀度量）。
- 现有 5 个测试套件全部通过；CMake `project(VERSION 2.2)`，`CORE_SOURCES` / `TEST_SOURCES` 逐文件列出（新增文件需登记）。

## 架构设计

### 模块位置：`src/core/schedule/`（排课核心层，新增独立文件）

```
src/core/schedule/
├── scheduler.h/cpp          # 入口：schedule(store, strategy)（默认 = 模拟退火）
├── strategy.h/cpp           # IScheduleStrategy + GreedyStrategy（初始解生成器）
├── conflicttable.h/cpp      # 复用：硬约束校验与占用维护（place/remove）
├── annealingstrategy.h/cpp  # SimulatedAnnealingStrategy：参数 + 退火主循环 + best 写回
├── annealingcandidate.h/cpp # 候选解 Candidate 核心例程（反建/写回/commit/回滚/Δ）
├── annealingmoves.cpp       # 邻域操作 M1~M5（Candidate 成员定义，无独立头文件）
└── annealingcost.h/cpp      # 软约束成本 S1~S6 + 负载快照 + 增量 Δ（含测试用自由函数）
```

> **文件长度约束**：CLAUDE.md 限定单文件 ≤ 500 行。SA 主体按职责拆为 annealingstrategy / annealingcandidate / annealingmoves / annealingcost 四个 `.cpp`（最大 428 行）。CMake `CORE_SOURCES` 已同步登记全部新增文件。

### SimulatedAnnealingStrategy（实现 `IScheduleStrategy`）

- **内部候选解**（不对 `DataStore` 直接改）：
  - 课程时间模式表：`course → QVector<(day, startSection)>`，共 N 个块，每块占 `duration = hoursPerSession` 个连续节次；
  - 教学班教室分配表：`classId → 每次课所用教室`（同班同教室优先）。
- **初始解**：先跑 `GreedyStrategy` 写回 store，再从 `store.scheduleEntries()` 反建内部结构；失败明细沿用贪心结果（SA 只优化已排上的班，不改变"排得上/排不上"集合）。
- **成本函数**：`cost = Σ硬约束(M=10⁶) + Σ软约束(S1~S6)`，复用 `ConflictTable` 维护占用与硬约束判定。
- **邻域操作**（每次迭代随机选一个）：
  | Move | 操作 |
  |------|------|
  | M1 单课平移 | 某班一次课移到另一空闲块（`duration` 连续节） |
  | M2 整班平移 | 某班全部 N 次课换到一组新模式（保持相对间隔） |
  | M3 课程归并 | 被拆分课程的少数派教学班移回多数派模式 |
  | M4 换教室 | 某班换一个满足 H4/H5 的教室 |
  | M5 双班教室对调 | 交换两个班的教室（双方须满足对方 H4/H5） |
- **退火调度**：T₀ 由采样 `|Δcost|` 定（×5）；`α = 0.98`；`L = 10 × 教学班数`；`T < 0.01` 或连续多轮无改进终止；全程记录 **best-so-far**，结束返回 best 而非当前解；`std::mt19937` + 可配置 seed（默认固定，可复现）。
- **Δcost 局部增量**：只算被移动教学班及其影响槽位/教室的变化（S5 只动旧/新时间槽，S6 只动旧/新教室）。
- **结束写回**：`clearScheduleEntries()` → 按 best 解生成 `ScheduleEntry` 写回 → 组装 `ScheduleResult`（scheduledCount / failures 沿用贪心）。

### 负载均匀度量（S5/S6，详见 SA 文档 §3.2）

- **S5 时间负载**：`load(t)` = 时间槽 `t=(day,section)` 并发上课班数；罚分 `Σ_t (load(t) − μ_time)² × w_time`（`w_time = 60`）。
- **S6 教室负载**：`usage(r)` = 教室 r 每周占用节数；罚分 `Σ_r (usage(r) − μ_room)² × w_roomload`（`w_roomload = 40`）。
- **代表周快照**：负载统计以"覆盖教学班最多的周"为快照（周范围不覆盖该周的课不计入），避免全周展开；Δ 计算只涉及被移动的槽位 / 教室。

## 决策记录

| 决策 | 结论 |
|------|------|
| SA 实现位置 | 拆为 `annealingstrategy.*`（主循环）+ `annealingcandidate.*`（候选核心）+ `annealingmoves.cpp`（M1~M5）+ `annealingcost.*`（成本/负载），每个文件 ≤ 500 行 |
| `Scheduler::schedule` 默认策略 | **改为模拟退火**——"自动排课"直接接入退火，不加手动选择入口；贪心保留为初始解生成器 |
| 贪心回归保留 | 现有 `tst_scheduler` 统一**显式传 `GreedyStrategy`**（该套件本就是贪心回归），SA 由新增 `tst_annealing` 覆盖 |
| 初始解 | 复用 `GreedyStrategy` 先行排课再反建内部结构；失败明细沿用，SA 不改变可排集合 |
| 负载统计周 | 代表周 = 覆盖教学班最多的周（近似"每周均匀"，文档已注明可抽样/加权） |
| 均匀性权重 | `w_time=60`、`w_roomload=40`（可调，参数结构暴露） |
| 可复现 | SA 默认固定 seed，测试断言同 seed 结果一致 |
| 版本号 | CMake `project(ClassFlow VERSION 3.0 ...)` |

## 任务分解

### W1 多学时跨节（本次会话已完成，v3.0 基线）

- [x] `strategy.cpp`：`makeEntry` 支持 `duration`；`placeClassInPattern` 找连续 `duration` 节空窗；`endSection = startSection + duration − 1`
- [x] `strategy.cpp`：非整数学时 → 该课程教学班全部记失败（原因含"学时"），不静默丢学时
- [x] `data/teaching_classes.csv` / `_large.csv`：学时 `1.5 → 2`
- [x] `tst_scheduler.cpp`：新增 `multiSectionSessions` / `nonIntegerHoursRejected`；修正 2 个受影响的用例（作息表补第 2 节）

**验收**：2 学时课程 UI 渲染两节；1.5 学时被拒绝；`tst_scheduler` 全套通过。

### W2 模拟退火策略核心（`annealingstrategy.*` + `annealingcandidate.*` + `annealingmoves.cpp` + `annealingcost.*`）

- [x] `annealingstrategy.h`：`SimulatedAnnealingStrategy` 声明 + 参数结构（S1~S6 权重、α、L、seed、上限）
- [x] `annealingcandidate.*`：内部候选解 `Candidate`（classId → 会话块）+ M1~M5 邻域 + 三段式回滚；从 `store.scheduleEntries()` 构建 / 排序序列化回写
- [x] 初始解：复用 `GreedyStrategy` 排课 → 反建内部结构；失败明细沿用
- [x] `annealingcost.*`：软约束成本 S1~S6 + 负载快照 + 增量 Δ；硬约束走「冲突拒绝」不进入成本
- [x] 邻域操作 M1~M4（单课/整班平移、归并、换教室），Δcost 局部增量 + μ 不变式
- [x] 退火主循环：采样 T₀（median|Δ|×5，clamp [5,2000]）、几何降温、Metropolis 接受、best-so-far、停滞门控 + 总 move 上限、固定 seed
- [x] CMake `CORE_SOURCES` 登记 3 对新文件

**验收**：`SimulatedAnnealingStrategy` 可在两套示例数据上运行，结果 H1~H5 零违反；同 seed 两次结果一致。

### W3 负载均匀软约束 S5/S6

- [x] S5 时间负载：`(day, section)` 并发负载表 + 方差罚分（`w_time`，7 天网格）
- [x] S6 教室负载：教室占用节数表 + 方差罚分（`w_roomload`）
- [x] 代表周快照统计（覆盖 session 数最多的周，平局取最小）
- [x] Δcost 增量扩展：`addOccupancy` / `subOccupancy` 增量维护 `Σx²`，S5/S6 精确整数 Δ
- [x] 新增 M5 双班教室对调（免去找空位，纯改教室负载；容量/类型预检 + 逐条落子）
- [x] 软权重内部 ×2 整数化（`w_waste` 由 0.5 变 1），十万级 move 位精确可复现

**验收**：SA 结果的时间 / 教室负载方差 ≤ 贪心结果；权重可调。

### W4 默认策略接入（无手动入口）

- [x] `Scheduler::schedule` 默认策略改为 `SimulatedAnnealingStrategy`（无参数默认构造）；"自动排课"直接走退火
- [x] 现有 `tst_scheduler` 用例统一显式传 `GreedyStrategy`（保持其为贪心回归套件）；`MainWindow` 无 UI 改动
- [x] CMake `project(ClassFlow VERSION 3.0 ...)`

**验收**：点击"自动排课"直接得到退火结果；贪心仅作为 SA 初始解生成器，回归由显式指定覆盖。

### W5 测试（`tests/tst_annealing.cpp` 新增）

- [x] 硬约束零违反：两套示例数据 H1~H5 均满足
- [x] 软约束更优：SA 总软 cost ≤ 贪心（best 初始化为贪心候选，必然成立）
- [x] 均匀性更优：时间/教室负载方差 ≤ 贪心（按固定 seed 经验属性，断言放宽到 ×1.2+1；实测两数据集均有大幅下降）
- [x] 可复现：同 seed 两次运行写回条目序列一致
- [x] 规模健壮性：`teaching_classes_large.csv`（18 班）收敛且不破硬约束（debug 实测 ~10s 量级）
- [x] 失败沿用：贪心失败的教学班在 SA 结果中仍上报（`ScheduleResult::failures`）
- [x] `tests/CMakeLists.txt` 登记 `tst_annealing.cpp`；全套 CTest 通过、无回归
- [x] W1 回归：多学时课程经 SA 后 `endSection` 仍正确跨节

**验收**：`ctest` 全部通过（6/6，含新 tst_annealing）；新增 6 项 SA 断言成立。

## 验收

1. 两套示例数据下，SA 结果硬约束（H1~H5）**零违反**。
2. SA 软约束总 cost、时间负载方差、教室负载方差均 ≤ 贪心结果。
3. 课表直观上不再出现"某几天塞满、某几天空"、或"个别教室连轴转"。
4. 同 seed 两次运行结果一致（可复现）。
5. `teaching_classes_large.csv` 收敛耗时可接受（release 秒级、debug ~10s 量级）。
6. "自动排课"直接走模拟退火，无手动切换入口；贪心回归由显式指定的测试覆盖，现有 5 套测试无回归。
7. 多学时跨节保持：2 学时课程仍渲染两节（W1 回归）。

## 阶段依赖

```
W1 多学时跨节（已完成，基线）
W2 SA 核心 ──> W3 S5/S6 负载均匀
W2 ──> W4 默认策略接入    W2/W3 ──> W5 测试
```

## 后续（非本版本）

- **GUI 均匀性调节**：权重滑杆（密集优先 vs 均匀优先）或自适应权重（SA 文档 §10）
- **回溯归因诊断**：排不下时用受限回溯证明"哪个班确实排不进、还缺什么"
- **教师 / 教室不可用时间段**（H6 禁用集）
- **教师个人课表**与发布流程
