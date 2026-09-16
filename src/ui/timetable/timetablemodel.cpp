/**
 * 文件职责：课表网格模型实现（QAbstractTableModel）。
 * 行 = 节次，列 = 星期（横看周几、竖看节次）；单元格显示 "课程名\n教室"。
 */

#include "timetablemodel.h"

#include <QColor>

#include "core/models/models.h"
#include "core/store/datastore.h"

namespace {
// 列头文本：周一..周日
const QString kWeeks[] = {
    QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
    QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
    QStringLiteral("周日")
};
}

/*
TimetableModel - 构造函数

Parameter：
    parent: 父对象指针，默认 nullptr

Remark:
    默认注入亮色卡片盘（主题接入前可独立使用）；主题切换时由 MainWindow 调 setCardPalette。
*/
TimetableModel::TimetableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // 默认亮色卡片盘：同课同色的浅色背景（黑字可读，轮换取用）
    static const QColor lightBg[] = {
        QColor(0xFFE0B2), QColor(0xC8E6C9), QColor(0xB3E5FC), QColor(0xCE93D8),
        QColor(0xFFCCBC), QColor(0xF0F4C3), QColor(0xFFF9C4), QColor(0xFFAB91),
        QColor(0xE1BEE7), QColor(0xB2DFDB)
    };
    for (const QColor &c : lightBg)
        m_cardBg.append(c);
    m_cardFg.fill(QColor(0x1A1A1A), 10);
}

/*
TimetableModel::setDataStore - 绑定数据仓库并刷新模型

Parameter：
    store: 数据仓库指针；为 nullptr 时清空网格
*/
void TimetableModel::setDataStore(const DataStore *store)
{
    beginResetModel();
    m_store = store;
    m_byCell.clear();
    m_courseOfClass.clear();
    m_courseOfClassId.clear();
    m_teacherName.clear();
    m_courseColor.clear();
    m_courseFgColor.clear();
    m_filter = ScheduleFilter();   // 新数据源默认不过滤
    m_maxSection = 0;

    if (store) {
        // 节数由作息表最大节次号决定
        for (const Section &s : store->sections())
            m_maxSection = qMax(m_maxSection, s.index);

        // 教学班 id → 课程名 + 课程 id（O(1) 查表，课程缺失时回退课程 id）
        for (const TeachingClass &tc : store->teachingClasses()) {
            const Course *c = store->courseById(tc.courseId);
            m_courseOfClass.insert(tc.classId, c ? c->name : tc.courseId);
            m_courseOfClassId.insert(tc.classId, tc.courseId);
        }

        // 教师 id → 姓名（课格子第二行显示；未收录时回退为 id）
        for (const TeacherInfo &t : store->teachers())
            m_teacherName.insert(t.teacherId, t.name);

        // 按当前卡片盘分配同课同色（底色 + 文字色）
        assignCourseColors();
    }

    rebuildCells();
    endResetModel();
}

/*
TimetableModel::refresh - 同数据源原地重建（数据已变、指针未变时调用）

Remark:
    排课换源（ScheduleController 成功收尾）/ 手动改条目后，m_store 指向同一对象但
    内容已更新；复用 setDataStore 做完整内容重建，再把筛选条件回填并按其重建单元格，
    达到"保留筛选/周次只刷新内容"的效果，避免筛选被整体重置。
*/
void TimetableModel::refresh()
{
    if (!m_store)
        return;
    const ScheduleFilter keep = m_filter;   // 记住当前筛选
    setDataStore(m_store);                  // 内容整体重建（会重置筛选为空）
    m_filter = keep;
    beginResetModel();
    rebuildCells();                         // 按保留的筛选重建网格
    endResetModel();
}

/*
TimetableModel::isClassLocked - 教学班是否被锁定

Parameter：
    classId: 教学班 id

Result:
    bool: 已锁定返回 true；无数据源返回 false

Remark:
    直读数据仓库锁定集，供卡片锁标 / 详情弹窗按钮态判断。
*/
bool TimetableModel::isClassLocked(const QString &classId) const
{
    return m_store && m_store->isClassLocked(classId);
}

/*
TimetableModel::setCardPalette - 注入当前主题的卡片配色盘与「更多」卡位配色

Parameter：
    backgrounds: 卡片底色盘（轮换取用，同课同色）
    foregrounds: 卡片文字色盘（与底色盘一一对应）
    moreBackground: 「更多」折叠卡位的中性底色
    moreForeground: 「更多」折叠卡位的文字色

Remark:
    主题切换（明暗 / accent）时由 MainWindow 调用；有数据源时重分配颜色并刷新网格。
    「更多」色不依赖 palette 角色（QSS 下易合成成同色），显式注入保证两色可读。
*/
void TimetableModel::setCardPalette(const QVector<QColor> &backgrounds,
                                    const QVector<QColor> &foregrounds,
                                    const QColor &moreBackground,
                                    const QColor &moreForeground)
{
    if (backgrounds.isEmpty() || foregrounds.isEmpty())
        return;
    m_cardBg = backgrounds;
    m_cardFg = foregrounds;
    m_moreBg = moreBackground;
    m_moreFg = moreForeground;
    if (!m_store)
        return;
    beginResetModel();
    assignCourseColors();
    rebuildCells();
    endResetModel();
}

/*
TimetableModel::assignCourseColors - 按当前卡片盘重建 教学班 → 底色/文字色 映射

Remark:
    同课程的所有教学班取同一底色（轮换取色，异课异色）；供 BackgroundRole /
    ForegroundRole 返回。
*/
void TimetableModel::assignCourseColors()
{
    if (!m_store)
        return;

    QHash<QString, QColor> bgOfCourse, fgOfCourse;
    int idx = 0;
    for (const Course &c : m_store->courses()) {
        const int i = idx++ % m_cardBg.size();
        bgOfCourse.insert(c.id, m_cardBg.at(i));
        fgOfCourse.insert(c.id, m_cardFg.at(i % m_cardFg.size()));
    }

    m_courseColor.clear();
    m_courseFgColor.clear();
    for (const TeachingClass &tc : m_store->teachingClasses()) {
        m_courseColor.insert(tc.classId,
                             bgOfCourse.value(tc.courseId, m_cardBg.first()));
        m_courseFgColor.insert(tc.classId,
                               fgOfCourse.value(tc.courseId, m_cardFg.first()));
    }
}

/*
TimetableModel::setCurrentWeek - 设置当前显示周次并重建单元格

Parameter：
    week: 新的周次；与当前值相同则不刷新
*/
void TimetableModel::setCurrentWeek(int week)
{
    if (week == m_currentWeek)
        return;
    m_currentWeek = week;
    if (!m_store)
        return;
    beginResetModel();
    rebuildCells();
    endResetModel();
}

/*
TimetableModel::setFilter - 设置筛选条件并重建网格

Parameter：
    filter: 筛选条件（空 = 不过滤）

Remark:
    维度间 AND、维度内 OR，判定逻辑在 core/filter 层（matchesFilter）。
*/
void TimetableModel::setFilter(const ScheduleFilter &filter)
{
    if (m_filter.teacherIds == filter.teacherIds
        && m_filter.classroomIds == filter.classroomIds
        && m_filter.courseIds == filter.courseIds)
        return;
    m_filter = filter;
    if (!m_store)
        return;
    beginResetModel();
    rebuildCells();
    endResetModel();
}

/*
TimetableModel::filter - 取当前筛选条件

Result:
    ScheduleFilter: 当前筛选（空 = 不限）

Remark:
    供筛选弹窗初始化勾选状态用。
*/
ScheduleFilter TimetableModel::filter() const
{
    return m_filter;
}

/*
TimetableModel::entriesAtCell - 取某 (星期, 节次) 在当前周内的条目

Parameter：
    day: 星期（1..7，周一..周日）
    section: 节次（从 1 起）

Result:
    QVector<ScheduleEntry>: 该格子的条目；无课返回空

Remark:
    直接读 m_byCell（已按当前周过滤），供点击卡片时判断「单课直开 / 多课先选」。
*/
QVector<ScheduleEntry> TimetableModel::entriesAtCell(int day, int section) const
{
    return m_byCell.value(QString::number(day) + '|' + QString::number(section));
}

/*
TimetableModel::courseNameOfClass - 教学班 id → 课程名

Parameter：
    classId: 教学班 id

Result:
    QString: 课程名；未知教学班原样返回其 id
*/
QString TimetableModel::courseNameOfClass(const QString &classId) const
{
    return m_courseOfClass.value(classId, classId);
}

/*
TimetableModel::cardBackground - 教学班 id → 卡片底色

Parameter：
    classId: 教学班 id

Result:
    QColor: 同课同色底色；未知教学班回退卡片盘首色（盘空回退浅灰）

Remark:
    供 delegate 逐卡取色；底色盘随主题由 setCardPalette 注入。
*/
QColor TimetableModel::cardBackground(const QString &classId) const
{
    if (m_courseColor.contains(classId))
        return m_courseColor.value(classId);
    return m_cardBg.isEmpty() ? QColor(Qt::lightGray) : m_cardBg.first();
}

/*
TimetableModel::cardForeground - 教学班 id → 卡片文字色

Parameter：
    classId: 教学班 id

Result:
    QColor: 卡片文字色（随底色盘保证可读）；未知教学班回退文字色盘首色

Remark:
    供 delegate 逐卡取色，明/暗主题下与底色配对。
*/
QColor TimetableModel::cardForeground(const QString &classId) const
{
    if (m_courseFgColor.contains(classId))
        return m_courseFgColor.value(classId);
    return m_cardFg.isEmpty() ? QColor(Qt::black) : m_cardFg.first();
}

/*
TimetableModel::moreBackground - 「更多」折叠卡位的中性底色

Result:
    QColor: 由 setCardPalette 注入的当前主题色（默认亮色浅灰）
*/
QColor TimetableModel::moreBackground() const
{
    return m_moreBg;
}

/*
TimetableModel::moreForeground - 「更多」折叠卡位的文字色

Result:
    QColor: 由 setCardPalette 注入的当前主题色（默认亮色深字）
*/
QColor TimetableModel::moreForeground() const
{
    return m_moreFg;
}

/*
TimetableModel::teacherNameOf - 教师 id → 姓名

Parameter：
    teacherId: 教师 id

Result:
    QString: 教师姓名；未指定（空 id）返回"未安排"，缺名但 id 非空回退 id 本身

Remark:
    口径与 v3.2 收尾一致：格卡第二行「姓名 · 教室」缺省文案统一。
*/
QString TimetableModel::teacherNameOf(const QString &teacherId) const
{
    if (teacherId.isEmpty())
        return QStringLiteral("未安排");
    return m_teacherName.value(teacherId, teacherId);
}

/*
TimetableModel::rebuildCells - 按当前周重建 (星期, 节次) → 条目映射

Remark:
    只纳入周范围覆盖当前周（startWeek ≤ 当前周 ≤ endWeek）的条目，
    因此切换周次可看到不同课程；不同周的课不会同时出现在同一格子。
*/
void TimetableModel::rebuildCells()
{
    m_byCell.clear();
    if (!m_store)
        return;

    for (const ScheduleEntry &e : m_store->scheduleEntries()) {
        if (m_currentWeek < e.startWeek || m_currentWeek > e.endWeek)
            continue;   // 当前周不在该课周范围内，不显示
        if (!matchesFilter(m_filter, e, m_courseOfClassId))
            continue;   // 不满足筛选（教师/教室/课程）的条目不显示
        for (int s = e.timeSlot.startSection; s <= e.timeSlot.endSection; ++s) {
            const QString key = QString::number(e.timeSlot.dayOfWeek)
                                + '|' + QString::number(s);
            m_byCell[key].append(e);
        }
    }
}

/*
TimetableModel::rowCount - 行数（节数）

Parameter：
    parent: 父索引，仅当无效时返回全表行数

Result:
    int: 行数 = 作息表最大节次号
*/
int TimetableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_maxSection;
}

/*
TimetableModel::columnCount - 列数（星期数）

Parameter：
    parent: 父索引，仅当无效时返回全表列数

Result:
    int: 列数，固定 7（周一..周日）
*/
int TimetableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 7;
}

/*
TimetableModel::data - 取某个单元格的显示数据（卡片视觉已由 delegate 自绘承担）

Parameter：
    index: 单元格索引（行 = 节次，列 = 星期）
    role: 数据角色

Result:
    QVariant: DisplayRole 返回第一条课程名单行摘要（可访问性/剪贴板兜底）；
    ToolTipRole 返回逐课周范围文本；无课返回空

Remark:
    卡片文本与配色由 TimetableDelegate 经条目列表 + 访问器实时取，
    本角色不再拼堆叠文本、不再返回整格 BackgroundRole/ForegroundRole。
*/
QVariant TimetableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_store)
        return QVariant();

    const QString key = QString::number(index.column() + 1)   // 星期
                        + '|' + QString::number(index.row() + 1);   // 节次
    const QVector<ScheduleEntry> entries = m_byCell.value(key);

    if (entries.isEmpty())
        return QVariant();

    if (role == Qt::DisplayRole) {
        // 单行摘要：第一条课程名（多班时以第一条为准），供键盘/复制/可访问性兜底
        return m_courseOfClass.value(entries.first().teachingClassId,
                                     entries.first().teachingClassId);
    }
    if (role == Qt::ToolTipRole) {
        // 逐课周范围文本，按格聚合（一行一门课）
        QString tip;
        for (const ScheduleEntry &e : entries) {
            if (!tip.isEmpty())
                tip += '\n';
            const QString name = m_courseOfClass.value(e.teachingClassId, e.teachingClassId);
            const QString weekText = (e.startWeek == e.endWeek)
                ? QStringLiteral("第 %1 周").arg(e.startWeek)
                : QStringLiteral("第 %1~%2 周").arg(e.startWeek).arg(e.endWeek);
            tip += name + QStringLiteral(" · ") + e.classroomId
                   + QStringLiteral(" · ") + weekText;
        }
        return tip;
    }
    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignCenter);
    return QVariant();
}

/*
TimetableModel::headerData - 取行头 / 列头文本

Parameter：
    section: 行或列序号
    orientation: Horizontal 为列头，Vertical 为行头
    role: 数据角色

Result:
    QVariant: 列头 "周一".."周日"，行头 "第 n 节"

*/
QVariant TimetableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        if (section >= 0 && section < 7)
            return kWeeks[section];
        return QVariant();
    }

    return QStringLiteral("第 %1 节").arg(section + 1);
}
