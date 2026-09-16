#ifndef FILTERDIALOG_H
#define FILTERDIALOG_H

#include <QDialog>

#include <QPair>
#include <QSet>
#include <QVector>

#include "core/filter/schedulefilter.h"

class QLineEdit;
class QListWidget;
class QVBoxLayout;
class DataStore;

// 排课筛选弹窗：三个维度（教师 / 教室 / 课程）各一组「搜索框 + 可多选列表」。
// 勾选栏（列表）是唯一真源，勾选状态由用户手动维护；
// 搜索框仅做子串过滤（不自动勾选、不回写），与勾选状态完全独立、不同步。
// 顶部「清空」按钮一键复位「不限」；点「应用」后经 result() 返回 ScheduleFilter。
class FilterDialog : public QDialog
{
    Q_OBJECT

public:
    // store: 数据仓库（提供教师 / 教室 / 课程选项）
    // current: 当前筛选条件（用于初始化勾选状态）
    FilterDialog(const DataStore &store, const ScheduleFilter &current,
                 QWidget *parent = nullptr);

    ScheduleFilter result() const;   // 收集各维度勾选项

private:
    static QSet<QString> checkedKeys(const QListWidget *list);  // 已勾选项的 key 集合
    static void applySearch(QListWidget *list, const QString &text); // 按子串过滤显示（不勾选）
    static void clearGroup(QListWidget *list, QLineEdit *search);    // 取消勾选 + 清空搜索框

    QVBoxLayout *addGroup(const QString &title,
                          const QVector<QPair<QString, QString>> &options,  // (key, 显示文本)
                          const QSet<QString> &selected,
                          QLineEdit *&search, QListWidget *&list);

    QLineEdit  *m_teacherSearch = nullptr;
    QListWidget *m_teacherList  = nullptr;
    QLineEdit  *m_roomSearch    = nullptr;
    QListWidget *m_roomList     = nullptr;
    QLineEdit  *m_courseSearch  = nullptr;
    QListWidget *m_courseList   = nullptr;
};

#endif // FILTERDIALOG_H
