/**
 * 文件职责：「从零排入」编辑区实现（FromScratchEditor）。
 * 给 0 课次的失败教学班按课程模板展开 N 行（星期/起始节/教室三下拉，每下拉首项
 * 「（未指定）」），供教务从零指定整班排法。本组件只表达"要排入什么"——
 * collectSlots() 组整批 manual::AddSlot，落库由宿主（ClassEditDialog）经
 * manual::validateAdd / manual::applyAdd 完成，自身绝不改 store、绝不触发排课。
 * 进入页面时 autoSuggest() 按"首个不冲突空位"逐行填默认：能填的填上、填不上的行
 * 保留「（未指定）」并在行尾注明卡点（复用 ConflictTable 判定，与落库同源，
 * 保证"自动建议可保存"与"保存校验"口径一致）。展示 / 候选文本共用 slotui 单源。
 */

#include "fromscratcheditor.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/schedule/conflicttable.h"
#include "core/store/datastore.h"
#include "roomtimeutil.h"
#include "ui/theme/theme.h"

namespace {

/*
hoursToSpan - 单次学时（double）→ 连续节数；仅接受 ≥1 的整数（与 manualadd 同源）

Parameter：
    hours: Course.hoursPerSession（double）
    span: 输出换算后的连续节数

Result:
    bool: 学时合法返回 true；非整数 / 小于 1 节返回 false
*/
bool hoursToSpan(double hours, int *span)
{
    if (hours < 1.0 || qAbs(hours - qRound(hours)) > 1e-9)
        return false;
    *span = int(qRound(hours));
    return true;
}

/*
dangerStyle - 错误语义文字的行内色（主题 QSS 未定义 QLabel[danger]，此处按需内联）

Result:
    QString: "color:#…;"（用于 QLabel::setStyleSheet）
*/
QString dangerStyle()
{
    return QStringLiteral("color:%1;")
        .arg(ThemeManager::instance().dangerColor().name());
}

} // namespace

/*
FromScratchEditor - 构造：读课程模板展开 N 行（或说明为何无法从零排入）

Parameter：
    store: 数据仓库（只读本组件；读课程/教学班/作息/教室表）
    classId: 目标教学班（须 0 现存课次）
    parent: 父窗口指针

Remark:
    学时/周次数非法或教学班·课程缺失时本组件退化为"说明 + 无行"（needCount()==0），
    宿主据此只允许保存基本信息改动，不让 0 课次的非法学时班被"假装排入"。
*/
FromScratchEditor::FromScratchEditor(const DataStore &store, const QString &classId,
                                     QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_classId(classId)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    const Course *course = tc ? m_store.courseById(tc->courseId) : nullptr;

    auto *title = new QLabel(this);
    title->setWordWrap(true);
    v->addWidget(title);

    if (!tc || !course) {
        m_blockReason = QStringLiteral("教学班 / 课程信息缺失，无法从零排入。");
        title->setText(QStringLiteral("教学班 %1 · 尚未排课").arg(m_classId));
    } else if (course->sessionsPerWeek < 1
               || !hoursToSpan(course->hoursPerSession, &m_span)) {
        m_blockReason = QStringLiteral("该课程每周次数 / 单次学时非法（须 ≥1 的整数节），"
                                       "无法从零排入。请先改课程模板后重试。");
        title->setText(QStringLiteral("教学班 %1 · 尚未排课").arg(m_classId));
    } else {
        m_need = course->sessionsPerWeek;
        m_span = int(qRound(course->hoursPerSession));
        m_startWeek = course->startWeek;
        m_endWeek = course->endWeek;
        title->setText(
            QStringLiteral("教学班 %1 · 尚未排课，从零排入 %2 次课"
                           "（第 %3~%4 周 · 每次 %5 节）")
                .arg(m_classId).arg(m_need).arg(m_startWeek).arg(m_endWeek).arg(m_span));
    }

    if (!m_blockReason.isEmpty()) {           // 不可排：只显示阻断原因
        auto *reason = new QLabel(m_blockReason, this);
        reason->setWordWrap(true);
        reason->setStyleSheet(dangerStyle());
        v->addWidget(reason);
        v->addStretch();
        return;
    }

    auto *hint = new QLabel(
        QStringLiteral("为该班手动指定每周 %1 次课的上课时间与教室；保存时整批按硬约束"
                       "校验（时间/教室/教师冲突、容量、类型），全部 %1 次课都有合法"
                       "空位才落库并从错误列表移除，否则提示卡在哪一行。")
            .arg(m_need),
        this);
    hint->setWordWrap(true);
    hint->setProperty("secondary", true);
    v->addWidget(hint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scroll);
    m_rowsLay = new QVBoxLayout(host);
    scroll->setWidget(host);
    v->addWidget(scroll, 1);

    m_rows.resize(m_need);
    for (int i = 0; i < m_need; ++i) {
        Row &row = m_rows[i];
        row.ordinal = i + 1;
        row.day = new QComboBox(host);
        row.start = new QComboBox(host);
        row.room = new QComboBox(host);
        row.reason = nullptr;
        fillCombos(row);
        m_rowsLay->addWidget(buildRowWidget(row));
    }
    m_rowsLay->addStretch();

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setProperty("secondary", true);
    v->addWidget(m_summary);
    refreshSummary();
}

/*
FromScratchEditor::needCount - 该班应排的课次数

Result:
    int: 课程每周次数；学时非法 / 教学班·课程缺失时为 0（宿主按 0 处理为不可排入）
*/
int FromScratchEditor::needCount() const
{
    return m_need;
}

/*
FromScratchEditor::buildRowWidget - 组装一行：说明 + 星期/起始节/教室 下拉 + 卡点行

Parameter：
    row: 该行（含三个下拉）

Result:
    QWidget*: 行控件（加入行容器布局）
*/
QWidget *FromScratchEditor::buildRowWidget(Row &row)
{
    auto *w = new QWidget(m_rowsLay ? m_rowsLay->parentWidget() : this);
    auto *col = new QVBoxLayout(w);
    col->setContentsMargins(0, 2, 0, 2);

    auto *line = new QHBoxLayout;
    auto *cap = new QLabel(QStringLiteral("第 %1 次课").arg(row.ordinal), w);
    cap->setMinimumWidth(84);
    cap->setProperty("secondary", true);
    line->addWidget(cap, 0, Qt::AlignVCenter);
    line->addWidget(row.day);
    line->addWidget(row.start);
    line->addWidget(row.room, 1);
    col->addLayout(line);

    row.reason = new QLabel(w);
    row.reason->setWordWrap(true);
    row.reason->setStyleSheet(dangerStyle());
    row.reason->hide();
    col->addWidget(row.reason);
    return w;
}

/*
FromScratchEditor::fillCombos - 填好一行三个下拉的候选（首项均为「（未指定）」）

Parameter：
    row: 待填行（三个下拉已建）
*/
void FromScratchEditor::fillCombos(Row &row)
{
    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    const Course *course = tc ? m_store.courseById(tc->courseId) : nullptr;
    const int maxSec = slotui::maxSectionOf(m_store);

    row.day->addItem(QStringLiteral("（未指定）"), -1);
    for (int d = 1; d <= 7; ++d)
        row.day->addItem(slotui::weekdayName(d), d);

    row.start->addItem(QStringLiteral("（未指定）"), -1);
    if (m_span <= maxSec)                       // 作息节数不足以容纳本课跨度 → 无真实项
        for (int s = 1; s + m_span - 1 <= maxSec; ++s)
            row.start->addItem(slotui::sectionRangeText(s, s + m_span - 1), s);

    row.room->addItem(QStringLiteral("（未指定）"), QString());
    if (tc && course) {
        const QVector<const Classroom *> cand =
            slotui::candidateRooms(m_store, tc->plannedSize, course->requiredRoomType);
        for (const Classroom *r : cand)
            row.room->addItem(QStringLiteral("%1（%2 人 · %3）")
                                  .arg(r->roomNumber).arg(r->capacity)
                                  .arg(slotui::roomTypeName(r->type)),
                              r->roomNumber);
    }

    row.day->setCurrentIndex(0);
    row.start->setCurrentIndex(0);
    row.room->setCurrentIndex(0);
}

/*
FromScratchEditor::rowFilled - 该行三下拉是否均为真实选择（非「（未指定）」）

Parameter：
    row: 待判断行

Result:
    bool: 三下拉均为真实值返回 true（该行才算"指定了一次课"）
*/
bool FromScratchEditor::rowFilled(const Row &row) const
{
    return row.day && row.start && row.room
        && row.day->currentData().toInt() > 0
        && row.start->currentData().toInt() > 0
        && !row.room->currentData().toString().isEmpty();
}

/*
FromScratchEditor::filledCount - 已真实指定的行数

Result:
    int: 三下拉均为真实值的行数
*/
int FromScratchEditor::filledCount() const
{
    int n = 0;
    for (const Row &row : m_rows)
        if (rowFilled(row))
            ++n;
    return n;
}

/*
FromScratchEditor::setRowValue - 把一次课写进某行三下拉

Parameter：
    row: 目标行
    day: 星期
    start: 起始节
    room: 教室号
*/
void FromScratchEditor::setRowValue(Row &row, int day, int start,
                                    const QString &room)
{
    row.day->setCurrentIndex(row.day->findData(day));
    row.start->setCurrentIndex(row.start->findData(start));
    row.room->setCurrentIndex(row.room->findData(room));
}

/*
FromScratchEditor::refreshSummary - 更新底部小结：已填 M/N、未填行号

Remark:
    每行卡点由 autoSuggest 各自维护；这里汇总已填数，给未填行一个总的口径，
    让"该班确无空位 / 还没开始填"一眼可辨。
*/
void FromScratchEditor::refreshSummary()
{
    if (!m_summary)
        return;
    const int filled = filledCount();
    QStringList unfilled;
    for (const Row &row : m_rows)
        if (!rowFilled(row))
            unfilled << QStringLiteral("第 %1 次").arg(row.ordinal);

    if (filled == m_need) {
        m_summary->setText(QStringLiteral("已自动填入全部 %1 次课的可用空位；如需调整"
                                          "可直接改各行的 星期/起始节/教室。")
                               .arg(m_need));
        return;
    }
    if (unfilled.isEmpty()) {
        m_summary->setText(QStringLiteral("尚未排入任何课次：请在下方为每次课选择 "
                                          "星期/起始节/教室。"));
        return;
    }
    m_summary->setText(
        QStringLiteral("已自动填入 %1/%2 次课；%3 未找到可用空位（教室/教师此时段可能"
                       "已被占，或容量/类型不满足）。可先手动调整前面各次课后重试，"
                       "或保持「（未指定）」仅保存基本信息。")
            .arg(filled).arg(m_need).arg(unfilled.join(QStringLiteral("、"))));
}

/*
FromScratchEditor::collectSlots - 读各行当前选择组整批新增槽位

Parameter：
    out: 输出整批 AddSlot（与真实指定的行一一对应，按行序）
    specified: 输出真实指定的行数（0 = 没填任何课）

Remark:
    只收集三下拉均为真实值的行；合法性由宿主 manual::validateAdd 兜底。
*/
void FromScratchEditor::collectSlots(QVector<manual::AddSlot> *out,
                                     int *specified) const
{
    out->clear();
    *specified = 0;
    for (const Row &row : m_rows) {
        if (!rowFilled(row))
            continue;
        manual::AddSlot s;
        s.dayOfWeek = row.day->currentData().toInt();
        s.startSection = row.start->currentData().toInt();
        s.classroomId = row.room->currentData().toString();
        out->append(s);
        ++(*specified);
    }
}

/*
FromScratchEditor::autoSuggest - 按"首个不冲突空位"给每行填默认

Remark:
    幂等：仅当尚无任何真实选择且未跑过时执行。seed ConflictTable 为全量现存条目，
    逐行按 星期→起始节→教室候选 顺序找首个 canPlace 并占位；找不着的行保持
    「（未指定）」并在行尾注明卡点（R/C/T 细分 / 无候选教室）。填完后刷底部小结。
*/
void FromScratchEditor::autoSuggest()
{
    if (m_suggested || m_rows.isEmpty())
        return;
    if (filledCount() > 0)          // 用户已手动指定过：不再覆盖
        return;
    m_suggested = true;

    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    const Course *course = tc ? m_store.courseById(tc->courseId) : nullptr;
    if (!tc || !course)
        return;
    const int maxSec = slotui::maxSectionOf(m_store);
    const QVector<const Classroom *> cand =
        slotui::candidateRooms(m_store, tc->plannedSize, course->requiredRoomType);
    const bool noRoom = cand.isEmpty();

    ConflictTable table;
    for (const ScheduleEntry &e : m_store.scheduleEntries())
        table.place(e);

    for (Row &row : m_rows) {
        bool placed = false;
        bool anyConflict = false;
        QString busyDesc;           // 扫过时的冲突类型（供卡点文案）
        for (int d = 1; d <= 7 && !placed; ++d) {
            for (int s = 1; s + m_span - 1 <= maxSec && !placed; ++s) {
                for (const Classroom *r : cand) {
                    ScheduleEntry e;
                    e.teachingClassId = m_classId;
                    e.teacherId       = tc->teacherId;
                    e.classroomId     = r->roomNumber;
                    e.timeSlot.dayOfWeek   = d;
                    e.timeSlot.startSection = s;
                    e.timeSlot.endSection   = s + m_span - 1;
                    e.startWeek = m_startWeek;
                    e.endWeek   = m_endWeek;
                    if (table.canPlace(e)) {
                        table.place(e);
                        setRowValue(row, d, s, r->roomNumber);
                        row.hint.clear();
                        placed = true;
                        break;
                    }
                    anyConflict = true;
                    for (const QString &k : table.busyKeysOf(e)) {
                        if (k.startsWith(QLatin1String("R|")))
                            busyDesc = QStringLiteral("该教室此时段已被占用");
                        else if (k.startsWith(QLatin1String("T|")))
                            busyDesc = QStringLiteral("该教师此时段已有其它课");
                        else if (k.startsWith(QLatin1String("C|")))
                            busyDesc = QStringLiteral("与本班已选课次时间冲突");
                    }
                }
            }
        }
        if (!placed) {
            if (noRoom || m_span > maxSec)
                row.hint = QStringLiteral("没有容量/类型满足且节数容得下的可选空位");
            else if (anyConflict)
                row.hint = busyDesc.isEmpty()
                               ? QStringLiteral("可选时段/教室均与现有排课冲突")
                               : busyDesc;
            else
                row.hint.clear();
            if (row.reason) {
                row.reason->setText(QStringLiteral("⚠ %1").arg(row.hint));
                row.reason->setVisible(!row.hint.isEmpty());
            }
        }
    }
    refreshSummary();
}
