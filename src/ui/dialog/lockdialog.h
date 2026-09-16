#ifndef LOCKDIALOG_H
#define LOCKDIALOG_H

#include <QDialog>
#include <QSet>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class DataStore;

// 锁定弹窗：按课程分组列出全部教学班，勾选要锁定的班。
// 课程头为三态复选（勾 = 锁整门课，解 = 整课解锁）；逐班可独立勾选。
// 本弹窗不改数据源，确定后由调用方读取 selectedLocked() 写回锁定集并刷新锁标，
// 不触发排课（真正的重排由「自动排课」按锁定集执行）。
class LockDialog : public QDialog
{
    Q_OBJECT

public:
    // store: 数据仓库（只读；初始勾选态取自其锁定集 lockedClassIds）
    LockDialog(const DataStore &store, QWidget *parent = nullptr);

    // 确定后调：当前被勾选（= 要锁定）的全部教学班 id
    QSet<QString> selectedLocked() const;

private:
    QTreeWidget *m_tree = nullptr;
};

#endif // LOCKDIALOG_H