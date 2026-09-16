/**
 * 文件职责：课表交互控制器实现。把查看/切换类动作从主窗口里抽成独立对象：
 * 周次切换、周选择器范围对齐、筛选应用、点卡直达详情、点「更多」列被折叠课、查看排课错误。
 * 迁移自原 MainWindow::onWeekChanged / syncWeekSelector / showCourseDetailAt /
 * showMoreCourses / showCourseDetail / showScheduleErrors / onFilterClicked，
 * 行为保持一致（只读数据源，对话框以宿主窗口为父级）。
 */

#include "timetablecontroller.h"

#include <QMainWindow>
#include <QMessageBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>

#include "core/models/models.h"
#include "core/store/datastore.h"
#include "core/store/undobuffer.h"
#include "ui/dialog/coursedetaildialog.h"
#include "ui/dialog/courselistdialog.h"
#include "ui/dialog/filterdialog.h"
#include "ui/dialog/scheduleerrordialog.h"
#include "ui/timetable/timetablemodel.h"

/*
TimetableController - 构造：绑定主数据源、课表模型、周选择器与撤销缓冲的引用/指针

Parameter：
    store: 主数据源（只读）
    model: 课表模型
    weekSpin: 顶栏周选择器（范围对齐 / 联动课表）
    undo: 会话级撤销缓冲（详情弹窗整段一步入环用，可空）
    host: 对话框 / 菜单父级（应为 MainWindow）
    parent: QObject 父对象
*/
TimetableController::TimetableController(DataStore &store, TimetableModel &model,
                                         QSpinBox *weekSpin, UndoBuffer *undo,
                                         QMainWindow *host, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_model(model)
    , m_weekSpin(weekSpin)
    , m_undo(undo)
    , m_host(host)
{
}

/*
TimetableController::setStatusSink - 注入状态栏文字出口（MainWindow 的富文本 QLabel）

Parameter：
    sink: 接收纯文本文案的回调；空函数则复位为无出口
*/
void TimetableController::setStatusSink(std::function<void(const QString &text)> sink)
{
    m_statusSink = std::move(sink);
}

/*
TimetableController::reportStatus - 写状态栏：优先走注入的 sink，否则直写宿主 statusBar
*/
void TimetableController::reportStatus(const QString &text)
{
    if (m_statusSink) {
        m_statusSink(text);
        return;
    }
    if (m_host)
        m_host->statusBar()->showMessage(text);
}

/*
TimetableController::setWeek - 周次切换，刷新课表网格显示

Parameter：
    week: 新的周次
*/
void TimetableController::setWeek(int week)
{
    m_model.setCurrentWeek(week);
}

/*
TimetableController::syncWeekSelector - 周选择器范围与当前学期周数对齐

Remark:
    新建 / 导入 / 恢复 / 排课完成后调用；当前值超出新范围时回落到上限。
*/
void TimetableController::syncWeekSelector()
{
    if (!m_weekSpin)
        return;
    const int weeks = qMax(1, m_store.semesterWeeks());
    m_weekSpin->setRange(1, weeks);
    m_weekSpin->setValue(qMin(m_weekSpin->value(), weeks));
}

/*
TimetableController::openFilter - 打开筛选弹窗，应用筛选后刷新课表
*/
void TimetableController::openFilter()
{
    if (m_store.teachingClasses().isEmpty()) {
        QMessageBox::information(m_host, QStringLiteral("筛选"),
                                 QStringLiteral("当前没有数据，请先新建或导入。"));
        return;
    }
    FilterDialog dialog(m_store, m_model.filter(), m_host);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_model.setFilter(dialog.result());
    reportStatus(QStringLiteral("已应用筛选（空 = 不限）"));
}

/*
TimetableController::showCourseDetailAt - 点某格内某张课程卡片 → 直达该课详情

Parameter：
    day: 星期（1..7）
    section: 节次（从 1 起）
    ordinal: 卡片在该格条目列表中的序号（0 起，由 TimetableView 命中给出）

Remark:
    由 delegate 命中保证 ordinal 指向确实被点中的那张卡，无需 QMenu 二选一；
    越界（理论不会，防御性）直接忽略。
*/
void TimetableController::showCourseDetailAt(int day, int section, int ordinal)
{
    const QVector<ScheduleEntry> entries = m_model.entriesAtCell(day, section);
    if (ordinal < 0 || ordinal >= entries.size())
        return;
    showCourseDetail(entries.at(ordinal));
}

/*
TimetableController::showMoreCourses - 点「更多」卡位 → 弹窗列出该格全部课程（可滚动长卡）

Parameter：
    day: 星期（1..7）
    section: 节次（从 1 起）

Remark:
    列表展示的是该格"全部"课程（含未折叠的前几张），而非只列被折叠课；
    每张长卡的标题/副标题/课程色均取自模型访问器，与网格卡同源同色；
    点任意一张卡 → showCourseDetail 开详情。空格防御性直接返回。
*/
void TimetableController::showMoreCourses(int day, int section)
{
    const QVector<ScheduleEntry> entries = m_model.entriesAtCell(day, section);
    if (entries.isEmpty())
        return;

    const int semesterWeeks = qMax(1, m_store.semesterWeeks());
    QVector<CourseListRow> rows;
    rows.reserve(entries.size());
    for (const ScheduleEntry &e : entries) {
        CourseListRow row;
        row.entry = e;
        row.title = m_model.courseNameOfClass(e.teachingClassId);
        row.subtitle = m_model.teacherNameOf(e.teacherId)
                       + QStringLiteral(" · ") + e.classroomId;
        if (e.startWeek > 1 || e.endWeek < semesterWeeks)   // 部分学期才标注周范围
            row.subtitle += QStringLiteral(" · 第 %1–%2 周").arg(e.startWeek).arg(e.endWeek);
        row.background = m_model.cardBackground(e.teachingClassId);
        row.foreground = m_model.cardForeground(e.teachingClassId);
        row.locked = m_model.isClassLocked(e.teachingClassId);   // 长卡右上画锁标
        rows.append(row);
    }

    static const QStringList kWeekName = { QStringLiteral("一"), QStringLiteral("二"),
                                           QStringLiteral("三"), QStringLiteral("四"),
                                           QStringLiteral("五"), QStringLiteral("六"),
                                           QStringLiteral("日") };
    const QString weekName =
        (day >= 1 && day <= kWeekName.size()) ? kWeekName.at(day - 1)
                                              : QString::number(day);
    const QString caption = QStringLiteral("周%1 · 第 %2 节 · 本格共 %3 门课")
                                .arg(weekName).arg(section).arg(rows.size());

    CourseListDialog dlg(rows, caption, m_host);
    connect(&dlg, &CourseListDialog::cardClicked, this,
            [this](const ScheduleEntry &entry) { showCourseDetail(entry); });
    dlg.exec();
}

/*
TimetableController::showCourseDetail - 弹出课程详情窗口（可编辑信息/调整/改锁定态/删除）

Parameter：
    entry: 需要展示详情的排课条目

Remark:
    详情弹窗是数据源的非只读入口：「编辑信息」改课程名/学院/教学班教师/人数，
    手动调整直接改排课条目，锁定/解锁改锁定集，删除此班/整课直接改数据源；关闭后若
    发生过任一变化，原地刷新课表（不动当前筛选/周次）并整段一步入环。状态栏文案按
    优先级取其一：删除 > 调整 > 编辑 > 锁定（同一窗内可先编后调，只报调整；删除会先
    accept 关窗，故报删除）。编辑若确认"扩容换大教室"，再补发 rescheduleNeeded 让
    主窗口对该班做局部重排。
*/
void TimetableController::showCourseDetail(const ScheduleEntry &entry)
{
    CourseDetailDialog dlg(m_store, entry, m_host);
    const DataStore before = m_store;      // 入环捕获：exec 前——详情弹窗内直接改 m_store
    dlg.exec();
    if (!dlg.removed() && !dlg.adjusted() && !dlg.edited() && !dlg.locksChanged())
        return;

    // 整段详情 = 一步（弹窗内的编辑/锁定/调整/删除合并入环，不做细粒度拆步）
    if (m_undo) {
        m_undo->push(before);
        emit undoStackChanged();           // MainWindow 借此刷新撤销/重做按钮
    }
    m_model.refresh();                         // 卡片即时到位/消失
    if (!m_host)
        return;
    // 编辑确认扩容换大教室（弹窗里可能还叠加了调整/锁定）→ 让主窗口对该班局部重排；
    // 弹窗内删除过该班则无意义（上方 removed 分支已处理）。失败会落到排课错误列表。
    if (!dlg.removed() && dlg.editRoomMoveNeeded())
        emit rescheduleNeeded(entry.teachingClassId);
    if (dlg.removed()) {
        reportStatus(dlg.removalSummary());
        return;
    }
    if (dlg.adjusted()) {
        QString text = dlg.adjustSummary();
        if (!m_store.isClassLocked(entry.teachingClassId))
            text += QStringLiteral("；如需保留请锁定本班");
        reportStatus(text);
        return;
    }
    if (dlg.edited()) {
        reportStatus(dlg.editSummary());
    } else {
        reportStatus(
            m_store.isClassLocked(entry.teachingClassId)
                ? QStringLiteral("已锁定 %1（自动排课时原样保留）").arg(entry.teachingClassId)
                : QStringLiteral("已解锁 %1").arg(entry.teachingClassId));
    }
}

/*
TimetableController::showScheduleErrors - 弹出排课错误列表；无错误时给出提示

Remark:
    数据来自 store 的 scheduleFailures()（随快照持久化，重开工作区后仍可查看）。
*/
void TimetableController::showScheduleErrors()
{
    const QVector<ScheduleFailure> &failures = m_store.scheduleFailures();
    if (failures.isEmpty()) {
        QMessageBox::information(m_host, QStringLiteral("排课错误"),
                                 QStringLiteral("当前没有排课错误。"));
        return;
    }
    ScheduleErrorDialog dlg(failures, m_host);
    QString editClassId;                       // 被点「编辑」的失败教学班（无则不空转）
    connect(&dlg, &ScheduleErrorDialog::editRequested, this,
            [&dlg, &editClassId](const QString &classId) {
                editClassId = classId;
                dlg.accept();                  // 收起错误列表，交主窗口钉选编辑该班
            });
    dlg.exec();
    if (!editClassId.isEmpty())
        emit editClassRequested(editClassId);
}
