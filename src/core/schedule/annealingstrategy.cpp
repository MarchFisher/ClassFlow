/**
 * 文件职责：SimulatedAnnealingStrategy 实现——两阶段排课主循环。
 * ① 贪心构造可行初始解 → ② 反建候选解与负载快照 → ③ 采样初始温度 →
 * ④ 几何降温 + Metropolis 接受，记录 best-so-far → ⑤ 把 best 写回 store。
 * 硬约束由邻域操作（annealingcandidate）冲突回滚保证零违反；
 * 失败明细沿用贪心，SA 不改变「排得上 / 排不上」的集合。
 */

#include "annealingstrategy.h"

#include <algorithm>
#include <cmath>

#include "annealingcandidate.h"
#include "conflicttable.h"
#include "core/store/datastore.h"

/*
SimulatedAnnealingStrategy::SimulatedAnnealingStrategy - 构造函数

Parameter：
    params: 退火参数（权重 / 降温系数 / 每温度迭代 / 种子等）
*/
SimulatedAnnealingStrategy::SimulatedAnnealingStrategy(AnnealingParams params)
    : m_params(std::move(params)), m_rng(m_params.seed)
{
}

/*
SimulatedAnnealingStrategy::sampleInitialTemp - 采样初始温度 T₀

Parameter：
    cand: 候选解（引用；采样过程应用又回滚，保持初始状态）
    table: 冲突表（引用）
    snap: 负载快照（引用）
    w: 软约束权重

Result:
    double: 初始温度 = 可行 move 的 |Δ| 中位数 × 5，clamp 到 [5, 2000]

Remark:
    只采样通过冲突校验的 move；否则少数大 Δ（如课程拆分罚分）会把中位数
    拉高，令初始温度虚高。
*/
double SimulatedAnnealingStrategy::sampleInitialTemp(Candidate &cand, ConflictTable &table,
                                                     LoadSnapshot &snap,
                                                     const SoftWeights &w)
{
    QVector<int> absD;
    absD.reserve(100);
    for (int i = 0; i < 100; ++i) {
        int delta = 0;
        if (cand.tryRandomMove(m_rng, table, snap, w, delta)) {
            absD.append(qAbs(delta));
            cand.rollback(table, snap);
        }
    }
    if (absD.isEmpty())
        return 100.0;
    std::sort(absD.begin(), absD.end());
    const double med = absD.at(absD.size() / 2);
    return qBound(5.0, med * 5.0, 2000.0);
}

/*
SimulatedAnnealingStrategy::acceptMetropolis - Metropolis 接受判据

Parameter：
    delta: Δcost（≤0 必接受）
    temp: 当前温度

Result:
    bool: 接受返回 true
*/
bool SimulatedAnnealingStrategy::acceptMetropolis(int delta, double temp)
{
    if (delta < 0)
        return true;
    return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng)
           < std::exp(-double(delta) / temp);
}

/*
SimulatedAnnealingStrategy::run - 两阶段排课入口（支持冻结部分班的局部排课）

Parameter：
    store: 数据仓库；初始解由贪心写入，优化后的 best 覆盖写回
    ctx: 进度/取消上下文；nullptr 时不启用
    movableClasses: 本次可动教学班集合；空 = 全量（全部可动，旧行为）

Result:
    ScheduleResult: 排课摘要（成功/失败沿用贪心；失败明细含原因）

Remark:
    候选为空或全部班冻结（无可动班）时跳过优化，直接返回贪心结果；
    冻结班（不在可动集合）会话保留在候选参与成本/负载，但邻域只作用于可动班，
    退火 best-so-far 写回时冻结班条目原样保留 —— 锁定重排/最小排共用此路径。
*/
ScheduleResult SimulatedAnnealingStrategy::run(DataStore &store, ScheduleContext *ctx,
                                               const QSet<QString> &movableClasses)
{
    // ① 贪心构造可行初始解（透传 ctx；贪心被中断则不再退火，直接返回）
    GreedyStrategy greedy;
    const ScheduleResult greedyResult = greedy.run(store, ctx, movableClasses);
    if (greedyResult.aborted)
        return greedyResult;

    // ② 反建候选解，并维护冲突表与负载快照
    Candidate cand;
    cand.buildFromEntries(store.scheduleEntries(), store);
    // 冻结集合 = 候选内「不在本次可动集合」的班（可动集合空 = 全量，无需冻结）
    if (!movableClasses.isEmpty()) {
        QSet<QString> frozen;
        const QVector<QString> &ids = cand.classIds();
        for (const QString &cid : ids)
            if (!movableClasses.contains(cid))
                frozen.insert(cid);
        cand.setLockedClasses(frozen);
    }
    if (cand.empty() || cand.movableClassCount() == 0)
        return greedyResult;
    ConflictTable table;
    for (const ScheduleEntry &e : store.scheduleEntries())
        table.place(e);
    LoadSnapshot snap = buildSnapshot(cand, store);

    // ③ 初始成本、采样 T₀、best 初始化为贪心解（保证返回不劣于贪心）
    const double initialCost = fullSoftCost(cand, snap, m_params.weights);
    const double t0 = sampleInitialTemp(cand, table, snap, m_params.weights);
    double temp = t0;
    double runningCost = initialCost;
    double bestCost = initialCost;
    Candidate best = cand;

    // 初始化阶段完成：上报一次（建候选 + 采样温度归入 Init）
    if (ctx && ctx->onProgress)
        ctx->onProgress(SchedulePhase::Init, 1, 1);
    // 采样完成后查取消：未进退火即中止，返回贪心解（aborted 置位）
    if (ctx && ctx->cancelled) {
        best.applyToStore(store);
        ScheduleResult r;
        r.ok = greedyResult.ok;
        r.aborted = true;
        r.scheduledCount = greedyResult.scheduledCount;
        r.failedCount = greedyResult.failedCount;
        r.failedClassIds = greedyResult.failedClassIds;
        r.failures = greedyResult.failures;
        return r;
    }

    // ④ 退火主循环：每温度迭代按可动班数定（冻结班不产生可行动作），预判轮数用于进度
    const int L = qMax(1, m_params.L * cand.movableClassCount());
    const int totalRounds = qMax(1,
        int(std::ceil(std::log(m_params.minTemp / t0) / std::log(m_params.alpha))));
    int round = 0;
    int totalMoves = 0;
    int stallRounds = 0;
    while (temp > m_params.minTemp && totalMoves < m_params.maxMoves) {
        if (ctx && ctx->onProgress)
            ctx->onProgress(SchedulePhase::Anneal, round, totalRounds);
        ++round;
        if (ctx && ctx->cancelled)
            break;   // 每温度轮开头查取消

        bool improved = false;
        for (int i = 0; i < L; ++i) {
            if (totalMoves >= m_params.maxMoves)
                break;
            ++totalMoves;
            int delta = 0;
            if (!cand.tryRandomMove(m_rng, table, snap, m_params.weights, delta))
                continue;
            if (acceptMetropolis(delta, temp)) {
                runningCost += delta;
                if (runningCost < bestCost) {
                    bestCost = runningCost;
                    best = cand;
                    improved = true;
                }
            } else {
                cand.rollback(table, snap);
            }
            if (ctx && ctx->cancelled)
                break;   // 每 move 后查取消
        }
        if (ctx && ctx->cancelled)
            break;

        temp *= m_params.alpha;
        if (temp < 0.5 * t0) {          // 进入低温区才计停滞，防高温随机游走误触发
            if (improved)
                stallRounds = 0;
            else
                ++stallRounds;
            if (stallRounds >= m_params.maxStallRounds)
                break;
        }
    }

    // ⑤ best 写回 store（取消时也写回 best-so-far，结果经 aborted 标识后丢弃）
    best.applyToStore(store);

    // ⑥ 组装结果：失败明细沿用贪心；完成时上报 Done
    if (ctx && ctx->onProgress)
        ctx->onProgress(SchedulePhase::Done, totalRounds, totalRounds);
    ScheduleResult r;
    r.ok = greedyResult.ok;
    r.aborted = (ctx && ctx->cancelled);
    r.scheduledCount = greedyResult.scheduledCount;
    r.failedCount = greedyResult.failedCount;
    r.failedClassIds = greedyResult.failedClassIds;
    r.failures = greedyResult.failures;
    return r;
}
