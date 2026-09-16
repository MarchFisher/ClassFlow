#ifndef SCHEDULEWORKER_H
#define SCHEDULEWORKER_H

#include <QObject>
#include <QSet>

#include "core/schedule/scheduler.h"
#include "core/store/datastore.h"

// 后台排课工作对象：在工作线程内对 DataStore 副本执行排课，
// 进度经 progress 信号回传主线程、结果随 finished 信号回传；取消经原子位置位。
// movableClasses 决定本次排课范围：空 = 全量重排；非空 = 仅重排/新排这些班（局部）。
class ScheduleWorker : public QObject
{
    Q_OBJECT

public:
    // movableClasses: 本次可动教学班集合（空 = 全量，透传给 Scheduler::schedule）
    explicit ScheduleWorker(DataStore store,
                            const QSet<QString> &movableClasses = {},
                            QObject *parent = nullptr);

    void cancel();   // 置位取消标记（工作线程在算法内轮询该原子位）

public slots:
    void run();      // 工作线程入口：同步执行排课并回传结果

signals:
    void progress(int phase, int done, int total);                          // 进度（phase = SchedulePhase）
    void finished(const DataStore &store, const ScheduleResult &result);    // 排课结束（成功/取消）

private:
    DataStore m_store;          // 数据副本，排课只动它（主线程旧数据不受影响）
    QSet<QString> m_movable;    // 本次可动教学班（空 = 全量）
    ScheduleContext m_ctx;      // 取消位 + 进度回调
};

#endif // SCHEDULEWORKER_H
