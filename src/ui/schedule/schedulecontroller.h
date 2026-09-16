#ifndef SCHEDULECONTROLLER_H
#define SCHEDULECONTROLLER_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QMainWindow;
class QPushButton;
class QThread;
class QProgressDialog;
class DataStore;
class TimetableModel;
class ScheduleWorker;
struct ScheduleResult;

// 排课控制器：负责「一次后台排课」的完整生命周期。
// 封装工作线程/worker 的起停、进度框、取消与重入防护；完成（成功）后才把排好的
// 数据仓库副本换入主数据源并刷新课表模型，然后以 scheduleDone 汇总信号通知界面。
// 本类对象常驻主窗口（主线程），m_store / m_model 是引用，指向 MainWindow 的成员。
// run 依据当前锁定集自动排课：已锁定且有排课的班被冻结原样保留，其余班
// （含未锁定、及已锁定但尚未排上的失败班）由引擎安排；全部已排课班都被锁定则无可排。
// runMovable / scheduleNewClass 按可动班集合排局部课：升格局部重排
// （movable=unlockedMovableClasses，即未锁可动班）走 runMovable；
// 新增教学班后的最小排（movable=该新班，排不下由本控制器内询问升格）走 scheduleNewClass。
// 三者共用 beginSchedule 的后台线程/进度框/取消/换源基建，无两套逻辑。
class ScheduleController : public QObject
{
    Q_OBJECT

public:
    // store: 主数据源（成功后被替换）；model: 课表模型（换源后刷新）
    // busyButtons: 排课期间禁用的入口按钮；host: 弹窗 / 进度框父级
    ScheduleController(DataStore &store, TimetableModel &model,
                       const QList<QPushButton *> &busyButtons,
                       QMainWindow *host, QObject *parent = nullptr);

    void run();               // 自动排课：按锁定集冻结已锁有课班、只重排其余班（全被锁则提示不排）
    // 局部排课入口：只排给定可动教学班（冻结其余班）。runKind 用于进度框/状态栏文案。
    // 供「新增课程后的最小排」(movable=新班) 与「升格局部重排」(movable=unlockedMovableClasses) 复用。
    void runMovable(const QSet<QString> &movableClasses, const QString &runKind);
    // 新增教学班后的最小排入口：movable=该新班（存量班冻结零扰动插入）。
    // 排不下时本控制器内部弹升格询问（对未锁定班局部重排），一次操作至多一次最终 scheduleDone。
    void scheduleNewClass(const QString &classId);
    // 未锁可动班集合 = 全部教学班 −「已锁定且有排课的班」（已锁未排上班仍会尝试排入）。
    // 与 run() 内部派生同源，供主窗口在最小排失败后询问升格时计算可动范围。
    QSet<QString> unlockedMovableClasses() const;
    void cancelAndWait();     // 取消并等待后台线程退出（closeEvent 收尾用）
    bool isRunning() const;   // 后台排课是否进行中
    QString runKind() const;  // 本次排课名称（run()=自动排课 / runMovable 自定），供状态栏文案

signals:
    // 排课收尾汇总（主线程发射）：ok/失败/取消。aborted=true 时未换源、保留旧课表。
    void scheduleDone(bool ok, int scheduledCount, int failedCount, bool aborted);

private slots:
    void onWorkerFinished(const DataStore &store, const ScheduleResult &result);
    void onProgress(int phase, int done, int total);

private:
    void beginSchedule(const QSet<QString> &movableClasses,
                       const QString &runKind);  // 两种入口共用的后台排课装配
    void setBusy(bool busy);        // 排课期间禁用/恢复入口按钮

    DataStore &m_store;
    TimetableModel &m_model;
    QList<QPushButton *> m_busyButtons;
    QMainWindow *m_host;
    QString m_runKind = QStringLiteral("自动排课");  // 本次排课名称（进度框/状态栏文案）
    QString m_pendingNewClassId;    // 等待最小排结果的教学班 id（为空则不会询问升格）
    QThread *m_thread = nullptr;        // 后台排课线程（完成即删除）
    ScheduleWorker *m_worker = nullptr; // 后台排课工作对象（完成即删除）
    QProgressDialog *m_progress = nullptr;  // 排课进度框
};

#endif // SCHEDULECONTROLLER_H
