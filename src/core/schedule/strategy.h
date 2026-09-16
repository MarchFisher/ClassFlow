#ifndef STRATEGY_H
#define STRATEGY_H

#include <QSet>

#include "core/models/models.h"
#include "scheduler.h"

class DataStore;

// 排课策略接口：Scheduler 只依赖此接口，可替换为贪心 / 回溯 / 遗传等。
class IScheduleStrategy
{
public:
    virtual ~IScheduleStrategy() = default;

    // 尝试为 store 中的教学班排课；结果写回 store，并返回排课摘要。
    // ctx 为进度/取消上下文，nullptr 时行为与旧版一致。
    // movableClasses 非空 = 局部排课：只排可动班，其余班作为冻结背景不触碰。
    virtual ScheduleResult run(DataStore &store, ScheduleContext *ctx = nullptr,
                               const QSet<QString> &movableClasses = {}) = 0;
};

// 默认策略：以课程为单位按「周次数↓、单次学时↓、教师ID↑」排序，
// 每课生成离散度时间模式（间隔 (5-N)/(N-1) 天，逐步放宽），
// 同课程教学班共用模式、不足时按 classId 兜底拆分，同班同教室 best-fit。
class GreedyStrategy : public IScheduleStrategy
{
public:
    ScheduleResult run(DataStore &store, ScheduleContext *ctx = nullptr,
                       const QSet<QString> &movableClasses = {}) override;
};

#endif // STRATEGY_H
