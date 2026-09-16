/**
 * 文件职责：排课入口实现。先清空旧排课结果，再调用策略执行排课并返回摘要。
 * 默认策略为 SimulatedAnnealingStrategy（两阶段：贪心构造 + 退火优化）；
 * 冲突判定由策略内部依赖 ConflictTable。
 */

#include "scheduler.h"

#include "annealingstrategy.h"
#include "core/store/datastore.h"

/*
Scheduler::schedule - 对 DataStore 执行排课（支持冻结部分教学班的局部/最小排）

Parameter：
    store: 数据仓库，排课结果与失败明细写回其中
    strategy: 排课策略指针；nullptr 时默认使用 SimulatedAnnealingStrategy
    ctx: 进度/取消上下文；nullptr 时不启用
    movableClasses: 本次可动教学班集合；空 = 全量重排（所有班可动，旧行为）

Result:
    ScheduleResult: 排课结果摘要；未排入的教学班及原因记录在 failures

Remark:
    清空旧排课结果前，先把「不在可动集合」的冻结班条目快照下来，清空后原样回填；
    策略据此把它们当背景占位、永不触碰 —— 这就是锁定重排与新增后最小排的引擎基础。
    可动集合为空时不回填（全量重建）；排课后把失败明细写入 store（随快照持久化）。
*/
ScheduleResult Scheduler::schedule(DataStore &store, IScheduleStrategy *strategy,
                                   ScheduleContext *ctx,
                                   const QSet<QString> &movableClasses)
{
    // 冻结班条目 = 现有排课中、本次不可动（∉ movableClasses）的班；清空前快照。
    QVector<ScheduleEntry> frozen;
    if (!movableClasses.isEmpty()) {
        const QVector<ScheduleEntry> &entries = store.scheduleEntries();
        frozen.reserve(entries.size());
        for (const ScheduleEntry &e : entries)
            if (!movableClasses.contains(e.teachingClassId))
                frozen.append(e);
    }
    store.clearScheduleEntries();
    store.clearScheduleFailures();
    for (const ScheduleEntry &e : frozen)
        store.addScheduleEntry(e);

    SimulatedAnnealingStrategy sa;
    if (strategy == nullptr)
        strategy = &sa;

    const ScheduleResult r = strategy->run(store, ctx, movableClasses);
    store.setScheduleFailures(r.failures);
    return r;
}
