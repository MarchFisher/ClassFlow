#ifndef ANNEALINGSTRATEGY_H
#define ANNEALINGSTRATEGY_H

#include <QSet>

#include <random>

#include "core/models/models.h"
#include "annealingcost.h"
#include "strategy.h"

class Candidate;
class ConflictTable;
class DataStore;

// 退火参数：软权重（×2 整数化，名义值见 SA 文档 §7）+ 调度控制。
// 硬约束罚分 M 未列入：本实现用「冲突 move 拒绝」维持硬约束零违反（等价 M=∞）。
struct AnnealingParams {
    SoftWeights weights;              // 软约束权重
    double alpha = 0.98;              // 几何降温系数
    int L = 10;                       // 每温度迭代次数 = L × 教学班数
    double minTemp = 0.01;            // 终止温度
    int maxStallRounds = 30;          // 低温区连续无改进轮数上限
    int maxMoves = 150000;            // 总 move 上限（防失控）
    int seed = 20260831;              // 随机种子（固定，测试可复现）
};

// 模拟退火策略：贪心构造可行初始解 + 邻域搜索降低软约束罚分。
// 实现两阶段排课：跑 GreedyStrategy 拿初始解 → 反建候选解 → 退火优化 →
// 把 best-so-far 一次性写回 store；失败明细沿用贪心。
class SimulatedAnnealingStrategy : public IScheduleStrategy
{
public:
    explicit SimulatedAnnealingStrategy(AnnealingParams params = AnnealingParams());
    ScheduleResult run(DataStore &store, ScheduleContext *ctx = nullptr,
                       const QSet<QString> &movableClasses = {}) override;

private:
    // 采样初始温度：只对可行 move 采样 |Δ| 中位数 ×5，并 clamp 到 [5, 2000]。
    double sampleInitialTemp(Candidate &cand, ConflictTable &table,
                             LoadSnapshot &snap, const SoftWeights &w);
    // Metropolis 接受：Δ<0 必接受，否则以 exp(−Δ/T) 概率接受。
    bool acceptMetropolis(int delta, double temp);

    AnnealingParams m_params;
    std::mt19937 m_rng;
};

#endif // ANNEALINGSTRATEGY_H
