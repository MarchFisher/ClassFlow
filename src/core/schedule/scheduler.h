#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QSet>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

#include "core/models/models.h"

class DataStore;
class IScheduleStrategy;

// 排课进度阶段：贪心 → 初始化（建候选/采样温度）→ 退火 → 完成。
enum class SchedulePhase { Greedy = 0, Init = 1, Anneal = 2, Done = 3 };

// 排课进度/取消上下文：UI 置位取消、算法侧轮询（只读）；
// onProgress 由算法在工作线程调用，上报 (阶段, 已完成, 总数)。
struct ScheduleContext {
    std::atomic<bool> cancelled{false};
    std::function<void(SchedulePhase, int done, int total)> onProgress = nullptr;
};

// 排课结果摘要：成功/失败明细；aborted 表示中途取消（剩余班不计失败）。
struct ScheduleResult {
    bool ok = false;
    bool aborted = false;
    int  scheduledCount = 0;
    int  failedCount = 0;
    QVector<QString> failedClassIds;      // 未能排入的教学班（兼容旧接口）
    QVector<ScheduleFailure> failures;    // 未能排入的教学班及失败原因（教学班/课程/原因）
};

// 排课入口：对 DataStore 内的教学班与教室执行排课，结果写回 DataStore。
// movableClasses 为本次「可动教学班集合」：为空表示全量重排（所有班可动，旧行为）；
// 非空时，不在其中的已有排课教学班被冻结（条目原样保留占位，排课不触碰它们），
// 只重排/新排可动班 —— 供「锁定重排」与「新增课程后最小排」复用同一引擎。
class Scheduler
{
public:
    Scheduler() = default;

    // 未传策略时默认使用 SimulatedAnnealingStrategy（内部以贪心构造初始解）。
    // ctx 为进度/取消上下文，nullptr 时行为与旧版一致。
    ScheduleResult schedule(DataStore &store, IScheduleStrategy *strategy = nullptr,
                            ScheduleContext *ctx = nullptr,
                            const QSet<QString> &movableClasses = {});
};

#endif // SCHEDULER_H
