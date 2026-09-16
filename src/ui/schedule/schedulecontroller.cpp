/**
 * 文件职责：排课控制器实现。把「后台自动排课」从主窗口里抽成独立对象：
 * 工作线程/worker 的生命周期、进度框、取消、重入防护，以及"排完才换源 + 刷新课表模型"
 * 的收尾逻辑都在这里；完成后只发 scheduleDone 汇总信号，状态栏文案由主窗口负责。
 * 迁移自原 MainWindow::runScheduler / onScheduleFinished / setSchedulingBusy；
 * 取消 → 保留旧课表不换源。run 依据数据源锁定集把「已锁定且有排课」的班冻结为
 * 背景占位，只对可动班（其余班）局部排课；无锁定即全量，语义不变。
 */

#include "schedulecontroller.h"

#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include "core/schedule/scheduler.h"
#include "core/store/datastore.h"
#include "scheduleworker.h"
#include "ui/timetable/timetablemodel.h"

/*
ScheduleController - 构造：绑定主数据源与课表模型引用、需禁用的入口按钮与宿主窗口

Parameter：
    store: 主数据源（成功排课后被替换）
    model: 课表模型（换源后刷新）
    busyButtons: 排课期间禁用的入口按钮（自动排课 / 新建 / 导入）
    host: 弹窗 / 进度框父级（应为 MainWindow）
    parent: QObject 父对象
*/
ScheduleController::ScheduleController(DataStore &store, TimetableModel &model,
                                       const QList<QPushButton *> &busyButtons,
                                       QMainWindow *host, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_model(model)
    , m_busyButtons(busyButtons)
    , m_host(host)
{
}

/*
ScheduleController::unlockedMovableClasses - 未锁可动教学班集合（run 与升格同源派生）

Result:
    QSet<QString>: 全部教学班 −「已锁定且有排课的班」

Remark:
    已锁定的有课班被冻结原样保留；未锁定的班、以及已锁定但尚未排上的失败班
    都留在可动集里（后者本次仍会尝试安排，避免"锁定后永不排"）。只做集合换算，
    不触发排课。
*/
QSet<QString> ScheduleController::unlockedMovableClasses() const
{
    QSet<QString> scheduled;
    for (const ScheduleEntry &e : m_store.scheduleEntries())
        scheduled.insert(e.teachingClassId);
    QSet<QString> frozen = m_store.lockedClassIds();
    frozen.intersect(scheduled);

    QSet<QString> movable;
    for (const TeachingClass &tc : m_store.teachingClasses())
        if (!frozen.contains(tc.classId))
            movable.insert(tc.classId);
    return movable;
}

/*
ScheduleController::run - 自动排课：尊重锁定集，冻结已锁有课班、只重排可动班

Remark:
    可动班由 unlockedMovableClasses() 派生。无可动班（全部已排课班均被锁定）时
    弹窗提示并终止，不发起排课。无数据 / 已在排课时忽略并提示；副本用 std::move
    迁入 worker，主线程旧课表不动。锁定集只经此换算生效：锁定本身不触发排课，
    由「锁定」弹窗单独维护。
*/
void ScheduleController::run()
{
    if (m_store.teachingClasses().isEmpty()) {
        QMessageBox::warning(m_host, QStringLiteral("自动排课"),
                             QStringLiteral("请先新建或导入数据。"));
        return;
    }
    if (m_thread)             // 已在排课中（线程尚未收尾），防重入
        return;

    const QSet<QString> movable = unlockedMovableClasses();
    if (movable.isEmpty()) {
        QMessageBox::information(m_host, QStringLiteral("自动排课"),
            QStringLiteral("所有已排课教学班均已被锁定，本次自动排课没有可排的班。\n"
                           "可点「锁定」取消部分锁定后，再点「自动排课」。"));
        return;
    }
    beginSchedule(movable, QStringLiteral("自动排课"));
}

/*
ScheduleController::runMovable - 局部排课：只排调用方给定的可动教学班

Parameter：
    movableClasses: 本次可动教学班集合（空 = 直接忽略，不发排课）
    runKind: 本次排课名称（如"局部重排"），用于进度框与状态栏文案

Remark:
    与 run() 共用同一后台排课装配；冻结班的判定由引擎做
    （∉movable 且已有条目 = 原样回填占位）。无数据 / 已在排课时忽略并提示。
    新增教学班的最小排请走 scheduleNewClass（其内部负责排不下时的升格询问）。
*/
void ScheduleController::runMovable(const QSet<QString> &movableClasses,
                                    const QString &runKind)
{
    if (movableClasses.isEmpty())
        return;                     // 无可动班 = 无可排（调用方不该传空）
    if (m_store.teachingClasses().isEmpty()) {
        QMessageBox::warning(m_host, runKind, QStringLiteral("请先新建或导入数据。"));
        return;
    }
    if (m_thread)                   // 已在排课中（线程尚未收尾），防重入
        return;
    beginSchedule(movableClasses, runKind);
}

/*
ScheduleController::scheduleNewClass - 新增教学班后的最小排：movable=该新班

Parameter：
    classId: 刚落库、待最小排的教学班 id

Remark:
    存量已排课班全部冻结，只尝试把新班零扰动插入。排不下时由 onWorkerFinished
    在本控制器内弹升格询问（可动 = 未锁定班），主窗口不再关心 runKind。
*/
void ScheduleController::scheduleNewClass(const QString &classId)
{
    if (m_store.teachingClassById(classId) == nullptr)
        return;                     // 新班尚未落库 / 已被删：无课可排
    if (m_thread)                   // 已在排课中（线程尚未收尾），防重入
        return;
    m_pendingNewClassId = classId;
    beginSchedule({classId}, QStringLiteral("新增教学班最小排"));
}

/*
ScheduleController::beginSchedule - 后台排课装配（run 单一入口；可动班集合透传引擎）

Parameter：
    movableClasses: 本次可动教学班集合；空 = 全量（run 已确保无可动时不会走到这里）
    runKind: 本次排课名称（当前恒为"自动排课"），用于进度框与状态栏文案
*/
void ScheduleController::beginSchedule(const QSet<QString> &movableClasses,
                                       const QString &runKind)
{
    if (m_store.teachingClasses().isEmpty()) {
        QMessageBox::warning(m_host, QStringLiteral("排课"),
                             QStringLiteral("请先新建或导入数据。"));
        return;
    }
    if (m_thread)   // 已在排课中（线程尚未收尾），防重入
        return;

    m_runKind = runKind;
    setBusy(true);

    // 副本：工作线程只排这份，主线程旧课表全程不动
    DataStore copy = m_store;
    m_worker = new ScheduleWorker(std::move(copy), movableClasses);
    m_thread = new QThread;
    m_worker->moveToThread(m_thread);

    // 进度框（模态、可取消）
    m_progress = new QProgressDialog(QStringLiteral("正在%1…").arg(runKind),
                                     QStringLiteral("取消"), 0, 100, m_host);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);

    // 接线：启动 → 排课；进度 → 进度条；完成 → 收尾(换源或取消) + 线程清理
    connect(m_thread, &QThread::started, m_worker, &ScheduleWorker::run);
    connect(m_worker, &ScheduleWorker::progress, this,
            &ScheduleController::onProgress);
    connect(m_worker, &ScheduleWorker::finished, this,
            &ScheduleController::onWorkerFinished);
    connect(m_worker, &ScheduleWorker::finished, m_thread, &QThread::quit);
    connect(m_worker, &ScheduleWorker::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread = nullptr;
        m_worker = nullptr;
    });
    // 进度框取消 → 置位取消标记。必须用 DirectConnection：run() 由 QThread::started
    // 同步调用，先于后台线程进入 exec()，期间其事件循环不跑，Queued 的 cancel 会一直
    // 排到排课结束才执行（等于没取消）。直接连接在主线程同步置位（仅写原子位，线程安全）。
    connect(m_progress, &QProgressDialog::canceled, m_worker, &ScheduleWorker::cancel,
            Qt::DirectConnection);

    m_thread->start();
}

/*
ScheduleController::runKind - 本次排课名称

Result:
    QString: "自动排课"（run 统一文案），供状态栏收尾文案
*/
QString ScheduleController::runKind() const
{
    return m_runKind;
}

/*
ScheduleController::onProgress - 更新进度框文案与进度条

Parameter：
    phase: 阶段序号（0..3，对应名称表）
    done: 当前阶段已完成量
    total: 当前阶段总量
*/
void ScheduleController::onProgress(int phase, int done, int total)
{
    static const char *names[] = { "构造初始解…", "初始化…", "退火优化…", "完成" };
    if (!m_progress)
        return;
    m_progress->setLabelText(QString::fromUtf8(names[phase]));
    m_progress->setRange(0, total);
    m_progress->setValue(done);
}

/*
ScheduleController::onWorkerFinished - 后台排课完成（主线程收尾）：取消则保留旧课表，成功则换源

Parameter：
    store: 排课后的数据副本（经 queued 信号拷回）
    result: 排课摘要（含 aborted）

Remark:
    新增教学班的最小排（m_pendingNewClassId 非空）排不下时，在此弹升格询问：
    选"是" → 延迟到线程收尾后发起局部重排（movable = 未锁班），本次不发射
    scheduleDone，由升格那次收尾统一汇总；选"否" → 落到下方照常发失败汇总。
    升格必须用 QTimer::singleShot(0) 延迟：本函数运行在 worker-finished 的排队事件里，
    此刻 QThread::finished 的清场（m_thread=nullptr）尚未执行，而 beginSchedule 有
    `if (m_thread) return;` 重入守卫——直接在此发起会被静默吞掉；0ms 定时事件排在
    已入队的 finished 清场之后，天然等到 m_thread 置空。升格 run 内 pending 已清空，
    即便再失败也不会二次询问。一次新增操作至多一次最终 scheduleDone。
*/
void ScheduleController::onWorkerFinished(const DataStore &store,
                                          const ScheduleResult &result)
{
    if (m_progress) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    setBusy(false);

    if (result.aborted) {
        m_pendingNewClassId.clear();
        emit scheduleDone(false, 0, 0, true);   // 取消：未换源，保留排课前旧课表
        return;
    }

    m_store = store;                  // 成功后才替换（换源，地址不变）
    m_model.refresh();                // 原地刷新课表（保留当前筛选/周次，不整体重置）

    const bool newClassMinFailed = !result.ok && !m_pendingNewClassId.isEmpty();
    if (newClassMinFailed) {
        const auto ans = QMessageBox::question(
            m_host, QStringLiteral("新增教学班排课"),
            QStringLiteral("新教学班在不扰动现有课表的前提下排不进去。\n"
                           "是否对未锁定的班做一次局部重排来腾出空位？\n"
                           "（已锁定的班会原样保留）"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ans == QMessageBox::Yes) {
            m_pendingNewClassId.clear();
            setBusy(true);            // 保持入口禁用，直到升格排课真正启动
            QTimer::singleShot(0, this, [this]() {
                beginSchedule(unlockedMovableClasses(), QStringLiteral("局部重排"));
            });
            return;                   // 不发射 scheduleDone；升格结果由下次收尾汇总
        }
    }
    m_pendingNewClassId.clear();
    emit scheduleDone(result.ok, result.scheduledCount, result.failedCount, false);
}

/*
ScheduleController::cancelAndWait - 取消后台排课并等待线程退出

Remark:
    主窗口 closeEvent 用：关闭时若仍在排课，先置位取消再 wait，
    防止半截结果落盘 / 后台线程残留。
*/
void ScheduleController::cancelAndWait()
{
    if (m_thread && m_thread->isRunning()) {
        if (m_worker)
            m_worker->cancel();
        m_thread->wait();
    }
}

/*
ScheduleController::isRunning - 后台排课是否进行中

Result:
    bool: true 表示仍在排课
*/
bool ScheduleController::isRunning() const
{
    return m_thread && m_thread->isRunning();
}

/*
ScheduleController::setBusy - 排课期间禁用/恢复 自动排课/新建/导入 入口

Parameter：
    busy: true 禁用，false 恢复
*/
void ScheduleController::setBusy(bool busy)
{
    for (QPushButton *btn : m_busyButtons) {
        if (btn)
            btn->setEnabled(!busy);
    }
}
