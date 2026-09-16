/**
 * 文件职责：后台排课工作对象实现。
 * 工作线程内对 DataStore 副本执行排课，进度经信号回传、取消经原子位置位，
 * 结果随 finished 信号回传主线程（避免主线程事后读成员的生命周期竞争）。
 * movableClasses 为空 = 全量排课；非空 = 冻结其余班、只排这批（局部重排/最小排）。
 */

#include "scheduleworker.h"

/*
ScheduleWorker - 构造函数：持有数据副本并绑定进度回调

Parameter：
    store: 数据仓库副本（工作线程只排这份，主线程旧数据不受影响）
    movableClasses: 本次可动教学班集合；空 = 全量排课（旧行为）
    parent: 父对象指针，默认 nullptr
*/
ScheduleWorker::ScheduleWorker(DataStore store,
                               const QSet<QString> &movableClasses,
                               QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
    , m_movable(movableClasses)
{
    // 进度回调 → 转发为信号（跨线程自动 queued，刷主线程进度条）
    m_ctx.onProgress = [this](SchedulePhase phase, int done, int total) {
        emit progress(int(phase), done, total);
    };
}

/*
ScheduleWorker::cancel - 置位取消标记

Remark:
    主线程在进度框取消 / 关窗时调用；工作线程在算法内轮询该原子位。
*/
void ScheduleWorker::cancel()
{
    m_ctx.cancelled = true;
}

/*
ScheduleWorker::run - 工作线程入口：同步执行排课并回传结果

Remark:
    由 QThread::started 连接到此槽；排课结束经 finished 把副本与摘要回传主线程。
*/
void ScheduleWorker::run()
{
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(m_store, nullptr, &m_ctx, m_movable);
    emit finished(m_store, r);
}
