#ifndef COURSELISTDIALOG_H
#define COURSELISTDIALOG_H

#include <QColor>
#include <QDialog>
#include <QString>
#include <QVector>

#include "core/models/models.h"

class QVBoxLayout;

// 一行课程的数据（由调用方在 showMoreCourses 预拼好：显示文本与配色与课表卡一致）
struct CourseListRow {
    ScheduleEntry entry;   // 用于点卡后开课程详情
    QString title;         // 课程名
    QString subtitle;      // 教师 · 教室[ · 第 a–b 周]（周范围为部分学期时）
    QColor  background;    // 卡底色（同课同色，取自课表模型卡片盘）
    QColor  foreground;    // 卡文字色
    bool    locked = false;   // 该教学班是否已锁定（锁定 → 卡右上画小锁标，与课表一致）
};

// 本格全部课程长卡列表弹窗：滚动展示某一 (星期,节次) 格内的所有课程（含未折叠的），
// 点任意一张长卡发 cardClicked(entry) 交由外层开课程详情。
class CourseListDialog : public QDialog
{
    Q_OBJECT

public:
    // rows: 该格全部课程行（按格内顺序）；caption: 弹窗顶部说明（含格位置 / 门数）
    CourseListDialog(const QVector<CourseListRow> &rows, const QString &caption,
                     QWidget *parent = nullptr);

signals:
    void cardClicked(const ScheduleEntry &entry);   // 点某张长卡（外层开详情）

private:
    void addRow(const CourseListRow &row);          // 追加一张长卡到 m_col

    QVBoxLayout *m_col = nullptr;       // 滚动区内容容器布局（卡片顺序存放处）
    QVector<ScheduleEntry> m_entries;   // 行内条目副本：保证 cardClicked 引用在 exec 期间有效
};

#endif // COURSELISTDIALOG_H
