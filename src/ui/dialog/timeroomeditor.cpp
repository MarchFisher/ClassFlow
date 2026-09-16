/**
 * 文件职责：「时间·教室」调整编辑区实现（可嵌入任意 QWidget）。
 * 按该班现存课次分两种形态：
 *   · 有课次：编辑形态——两种互斥模式（本节 / 整班），每行 星期/起始节/教室 三下拉、
 *     教室候选按 容量≥班人数 + 类型匹配 过滤并排序；collectChanges() 组整批 manual::Change。
 *   · 0 课次（排课失败的班）：「从零排入」形态——内部交 FromScratchEditor 展开 N 行待新增
 *     课（collectAdd() 组整批 manual::AddSlot），首次可见（showEvent）自动建议空位。
 * 本组件**只表达"要改成什么/要排入什么"**，落库（manual::validate/apply 与
 * manual::validateAdd/applyAdd）由宿主（ClassEditDialog）按统一顺序编排，自身绝不改
 * store、绝不触发排课。星期名 / 节次 / 教室类型文案与教室候选口径统一取自 slotui。
 */

#include "timeroomeditor.h"

#include <algorithm>

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include "core/store/datastore.h"
#include "fromscratcheditor.h"
#include "roomtimeutil.h"

namespace {

/*
sessionsOfClass - 该班现存全部课次（按 星期→起始节 排序）

Parameter：
    store: 数据仓库
    classId: 教学班 id

Result:
    QVector<ScheduleEntry>: 排序后的课次副本
*/
QVector<ScheduleEntry> sessionsOfClass(const DataStore &store, const QString &classId)
{
    QVector<ScheduleEntry> list;
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (e.teachingClassId == classId)
            list.append(e);
    std::sort(list.begin(), list.end(), [](const ScheduleEntry &a, const ScheduleEntry &b) {
        if (a.timeSlot.dayOfWeek != b.timeSlot.dayOfWeek)
            return a.timeSlot.dayOfWeek < b.timeSlot.dayOfWeek;
        return a.timeSlot.startSection < b.timeSlot.startSection;
    });
    return list;
}

} // namespace

/*
TimeRoomEditor - 构造：按该班课次多寡进入「编辑」或「从零排入」形态

Parameter：
    store: 数据仓库（只读本组件；读现况、组变更，落库由宿主负责）
    classId: 教学班（整班模式 / 从零排入对象）
    anchorEntryId: 被点击条目 id（本节模式对象；整班/从零排入用不到）
    parent: 父窗口指针

Remark:
    每次构造都按锚现读 store 里该班当前课次，绝不信点击瞬间的旧拷贝；
    0 课次 = 从零排入形态（失败班）；有课次时模式默认「本节」，选中值即当前值。
*/
TimeRoomEditor::TimeRoomEditor(DataStore &store, const QString &classId,
                               const QString &anchorEntryId, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_classId(classId)
    , m_anchorEntryId(anchorEntryId)
    , m_sessions(sessionsOfClass(store, classId))
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    auto *info = new QLabel(
        m_sessions.isEmpty()
            ? QStringLiteral("教学班 %1 · 尚未排课，可在下方从零排入").arg(m_classId)
            : QStringLiteral("教学班 %1 · 现排 %2 次课")
                  .arg(m_classId).arg(m_sessions.size()),
        this);
    info->setWordWrap(true);
    v->addWidget(info);

    if (m_sessions.isEmpty()) {              // 从零排入形态：编辑区整体交给 FromScratchEditor
        m_addEditor = new FromScratchEditor(m_store, m_classId, this);
        v->addWidget(m_addEditor, 1);
        return;
    }

    // —— 编辑形态（本节 / 整班）——
    auto *hint = new QLabel(
        QStringLiteral("只改排课时间/教室，仅校验硬约束（时间/教室/教师冲突、容量、"
                       "教室类型）。如需本次结果在下次「自动排课」后保留，保存后请在"
                       "详情里「锁定」本班。"),
        this);
    hint->setWordWrap(true);
    hint->setProperty("secondary", true);   // 次要文字随主题（弹窗统一）
    v->addWidget(hint);

    // 模式切换：本节（默认）/ 整班
    auto *modeRow = new QHBoxLayout;
    m_singleRadio = new QRadioButton(QStringLiteral("本节"), this);
    m_batchRadio = new QRadioButton(
        QStringLiteral("整班（%1 次课）").arg(m_sessions.size()), this);
    modeRow->addWidget(m_singleRadio);
    modeRow->addWidget(m_batchRadio);
    modeRow->addStretch();
    v->addLayout(modeRow);

    // 编辑区（放滚动区，行数随模式与课次变化）
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_rowsHost = new QWidget(scroll);
    new QVBoxLayout(m_rowsHost);          // m_rowsHostLayout（重建行时清空再添）
    scroll->setWidget(m_rowsHost);
    v->addWidget(scroll, 1);

    // 进入某模式才重建；默认选中「本节」
    m_singleRadio->setChecked(true);
    connect(m_singleRadio, &QRadioButton::toggled, this,
            [this](bool on) { if (on) onModeSwitched(); });
    connect(m_batchRadio, &QRadioButton::toggled, this,
            [this](bool on) { if (on) onModeSwitched(); });
    rebuildRows();
}

/*
TimeRoomEditor::showEvent - 首次显示（切到本页）时对从零排入形态自动建议空位

Remark:
    页面在弹窗的页签 2 上，初次 show 即用户切到「时间·教室」；此时才自动建议，
    避免用户在页签 1 只改基本信息保存时被"自动排入"打扰。
*/
void TimeRoomEditor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (addMode() && !m_addSuggested) {
        m_addSuggested = true;
        m_addEditor->autoSuggest();
    }
}

/*
TimeRoomEditor::hasSessions - 该班现存课次是否非空

Result:
    bool: 有排课返回 true（编辑形态）；0 课次返回 false（从零排入形态）
*/
bool TimeRoomEditor::hasSessions() const
{
    return !m_sessions.isEmpty();
}

/*
TimeRoomEditor::sessionCount - 该班现存课次数

Result:
    int: 现存课次数
*/
int TimeRoomEditor::sessionCount() const
{
    return m_sessions.size();
}

/*
TimeRoomEditor::addMode - 是否处于"从零排入"形态

Result:
    bool: 该班 0 课次（内部为 FromScratchEditor）返回 true
*/
bool TimeRoomEditor::addMode() const
{
    return m_addEditor != nullptr;
}

/*
TimeRoomEditor::addNeedCount - 从零排入应排的课次数

Result:
    int: addMode 时为 FromScratchEditor::needCount()；0 = 该班无法从零排入
*/
int TimeRoomEditor::addNeedCount() const
{
    return m_addEditor ? m_addEditor->needCount() : 0;
}

/*
TimeRoomEditor::collectAdd - 读从零排入各行当前选择组整批新增槽位

Parameter：
    out: 输出整批 AddSlot（与真实指定行一一对应，按行序）
    specified: 输出真实指定的行数（0 = 没填任何课）
*/
void TimeRoomEditor::collectAdd(QVector<manual::AddSlot> *out,
                                int *specified) const
{
    if (m_addEditor) {
        m_addEditor->collectSlots(out, specified);
        return;
    }
    out->clear();
    *specified = 0;
}

/*
TimeRoomEditor::maxSection - 作息表最大节次号

Result:
    int: 最大节次号；作息表为空返回 0
*/
int TimeRoomEditor::maxSection() const
{
    return slotui::maxSectionOf(m_store);
}

/*
TimeRoomEditor::onModeSwitched - 切换 本节/整班 → 重建编辑行
*/
void TimeRoomEditor::onModeSwitched()
{
    rebuildRows();
}

/*
TimeRoomEditor::rebuildRows - 按当前模式重建编辑区各行
*/
void TimeRoomEditor::rebuildRows()
{
    // 清空旧行（先删布局条目与其上的控件）
    auto *lay = qobject_cast<QVBoxLayout *>(m_rowsHost->layout());
    if (lay) {
        while (QLayoutItem *item = lay->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
    m_rows.clear();

    const bool wholeClass = m_batchRadio && m_batchRadio->isChecked();
    QVector<ScheduleEntry> sel;      // 本次要编辑的课次
    if (wholeClass) {
        sel = m_sessions;
    } else {
        for (const ScheduleEntry &s : m_sessions)
            if (s.entryId == m_anchorEntryId)
                sel.append(s);
    }
    if (sel.isEmpty()) {
        auto *warn = new QLabel(QStringLiteral("（该次课已不存在，无法调整）"), m_rowsHost);
        lay->addWidget(warn);
        return;
    }

    int ordinal = 1;
    for (const ScheduleEntry &cur : sel) {
        EditRow row;
        row.entryId = cur.entryId;
        row.baseSpan = cur.timeSlot.endSection - cur.timeSlot.startSection + 1;
        row.day = new QComboBox(m_rowsHost);
        row.start = new QComboBox(m_rowsHost);
        row.room = new QComboBox(m_rowsHost);
        fillCombos(row, cur);

        const QString caption = QStringLiteral("第 %1 次课 · 现在 %2 %3")
            .arg(ordinal++)
            .arg(slotui::weekdayName(cur.timeSlot.dayOfWeek))
            .arg(slotui::sectionRangeText(cur.timeSlot.startSection,
                                          cur.timeSlot.endSection));
        lay->addWidget(buildRowWidget(row, caption));
        m_rows.append(row);
    }
    lay->addStretch();
}

/*
TimeRoomEditor::fillCombos - 填好一行三个下拉的候选与当前值

Parameter：
    row: 待填行（三个下拉已建）
    cur: 该行旧条目（起始值 = 现在排法）
*/
void TimeRoomEditor::fillCombos(const EditRow &row, const ScheduleEntry &cur)
{
    const TeachingClass *tc = m_store.teachingClassById(cur.teachingClassId);
    const Course *course = tc ? m_store.courseById(tc->courseId) : nullptr;
    const int maxSec = maxSection();
    const int span = row.baseSpan;

    // 星期
    for (int d = 1; d <= 7; ++d)
        row.day->addItem(slotui::weekdayName(d), d);
    row.day->setCurrentIndex(row.day->findData(cur.timeSlot.dayOfWeek));

    // 起始节：start + span - 1 ≤ 作息最大节
    for (int s = 1; s + span - 1 <= maxSec; ++s)
        row.start->addItem(slotui::sectionRangeText(s, s + span - 1), s);
    int s0;
    if (maxSec >= span)
        s0 = qBound(1, cur.timeSlot.startSection, maxSec - span + 1);
    else
        s0 = cur.timeSlot.startSection;      // 作息短于本课跨度（数据异常）：无从可选
    if (row.start->findData(s0) < 0 && maxSec >= span)
        row.start->addItem(slotui::sectionRangeText(s0, s0 + span - 1), s0);
    row.start->setCurrentIndex(row.start->findData(s0));

    // 教室候选：容量 ≥ 班人数 && 类型匹配（Any 不限）；当前教室若不在候选也保留
    const QVector<const Classroom *> cand = tc && course
        ? slotui::candidateRooms(m_store, tc->plannedSize, course->requiredRoomType)
        : QVector<const Classroom *>();
    bool curKept = false;
    for (const Classroom *r : cand) {
        row.room->addItem(QStringLiteral("%1（%2 人 · %3）")
                              .arg(r->roomNumber).arg(r->capacity)
                              .arg(slotui::roomTypeName(r->type)),
                          r->roomNumber);
        if (r->roomNumber == cur.classroomId)
            curKept = true;
    }
    if (!curKept) {                    // 现状教室已不达标（极端）：仍列出以便不改也能保存
        const Classroom *cr = m_store.classroomById(cur.classroomId);
        row.room->addItem(
            (cr ? QStringLiteral("%1（%2 人 · %3）").arg(cr->roomNumber)
                                                       .arg(cr->capacity)
                                                       .arg(slotui::roomTypeName(cr->type))
                : QStringLiteral("%1（已不存在）").arg(cur.classroomId)),
            cur.classroomId);
    }
    row.room->setCurrentIndex(row.room->findData(cur.classroomId));
}

/*
TimeRoomEditor::buildRowWidget - 组装一行：说明 + 星期/起始节/教室 下拉

Parameter：
    row: 该行（含三个下拉）
    caption: 行首说明（第几次 · 现在排法）

Result:
    QWidget*: 行控件（加入编辑区布局）
*/
QWidget *TimeRoomEditor::buildRowWidget(const EditRow &row, const QString &caption)
{
    auto *w = new QWidget(m_rowsHost);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 2, 0, 2);

    auto *cap = new QLabel(caption, w);
    cap->setMinimumWidth(190);
    cap->setProperty("secondary", true);   // 行标签用次要文字随主题（弹窗统一）
    h->addWidget(cap, 0, Qt::AlignVCenter);
    h->addWidget(row.day);
    h->addWidget(row.start);
    h->addWidget(row.room, 1);
    return w;
}

/*
TimeRoomEditor::collectChanges - 读各行当前选择组整批变更

Parameter：
    out: 输出整批 Change（与当前模式行一一对应）
    changedCount: 输出与现状确实不同的行数（0 = 无实际改动）

Result:
    bool: 恒为 true（行均有值；合法性由宿主 manual::validate 兜底）
*/
bool TimeRoomEditor::collectChanges(QVector<manual::Change> *out,
                                    int *changedCount) const
{
    out->clear();
    *changedCount = 0;
    for (const EditRow &row : m_rows) {
        manual::Change c;
        c.entryId = row.entryId;
        c.dayOfWeek = row.day->currentData().toInt();
        c.startSection = row.start->currentData().toInt();
        c.classroomId = row.room->currentData().toString();
        out->append(c);

        const ScheduleEntry *cur = m_store.scheduleEntryById(row.entryId);
        if (!cur || cur->timeSlot.dayOfWeek != c.dayOfWeek
            || cur->timeSlot.startSection != c.startSection
            || cur->classroomId != c.classroomId)
            ++(*changedCount);
    }
    return true;
}

/*
TimeRoomEditor::describeReject - 校验拒绝报告 → 可读文案

Parameter：
    rep: manual::validate / manual::validateAdd 的拒绝报告（宿主校验失败时传入展示）

Result:
    QString: 面向用户的拒绝文案（整句原因 或 指到拒绝的那一行）

Remark:
    AlreadyPlaced / InvalidHours / SlotCountMismatch 是整班级原因，整句展示不带行号；
    其余行级原因按 rep.entryId 是否为空决定前缀（add 行无真实 id → 只显示行号）。
*/
QString TimeRoomEditor::describeReject(const manual::Report &rep) const
{
    switch (rep.reject) {
    case manual::Reject::AlreadyPlaced:
        return QStringLiteral("该班已有排课，请用「本节 / 整班」模式调整，无需从零排入。");
    case manual::Reject::InvalidHours: {
        QString head = QStringLiteral("该课程学时非法");
        if (const TeachingClass *tc = m_store.teachingClassById(m_classId))
            if (const Course *c = m_store.courseById(tc->courseId))
                head = QStringLiteral("课程「%1」每周 %2 次、单次 %3 学时")
                           .arg(c->name).arg(c->sessionsPerWeek).arg(c->hoursPerSession);
        return head + QStringLiteral("，无法排入（单次学时必须 ≥1 的整数节）。");
    }
    case manual::Reject::SlotCountMismatch:
        return QStringLiteral("指定次数与该班每周应排次数不符，无法排入。");
    default:
        break;
    }

    // 行定位前缀：add 行无真实 entryId → 只显示行号
    const QString prefix = rep.entryId.isEmpty()
        ? QStringLiteral("第 %1 行").arg(rep.index + 1)
        : QStringLiteral("第 %1 行（%2）").arg(rep.index + 1).arg(rep.entryId);
    switch (rep.reject) {
    case manual::Reject::EntryMissing:
        return prefix + QStringLiteral("：对应的排课条目已不存在，无法调整。");
    case manual::Reject::RoomMissing:
        return prefix + QStringLiteral("：目标教室不存在，请重选。");
    case manual::Reject::CapacityTooSmall:
        return prefix + QStringLiteral("：目标教室容量不足该班人数。");
    case manual::Reject::RoomTypeMismatch:
        return prefix + QStringLiteral("：目标教室类型与课程所需不符。");
    case manual::Reject::BadRange:
        return prefix + QStringLiteral("：上课时间超出作息范围，请重选节次。");
    case manual::Reject::Conflict: {
        // 据 busy 键前缀归类：R=教室被占 / C=本班冲突 / T=教师冲突
        QStringList parts;
        bool roomBusy = false, classBusy = false, teacherBusy = false;
        for (const QString &k : rep.busyTags) {
            if (k.startsWith(QLatin1String("R|"))) roomBusy = true;
            if (k.startsWith(QLatin1String("C|"))) classBusy = true;
            if (k.startsWith(QLatin1String("T|"))) teacherBusy = true;
        }
        if (roomBusy)    parts << QStringLiteral("该教室此时段已被占用");
        if (classBusy)   parts << QStringLiteral("与本班其它次课时间冲突");
        if (teacherBusy) parts << QStringLiteral("该教师此时段已有其它课");
        if (parts.isEmpty())
            parts << QStringLiteral("与现有排课冲突");
        return prefix + QStringLiteral("：%1。").arg(parts.join(QStringLiteral("；")));
    }
    default:
        return prefix + QStringLiteral("：无法调整。");
    }
}
