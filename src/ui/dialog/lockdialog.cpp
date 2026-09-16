/**
 * 文件职责：锁定弹窗实现。按课程分组列出全部教学班，
 * 课程头 = 三态复选（勾 = 锁整门课），逐班可独立勾选。
 * 只读数据源，不改动任何状态；确定后由调用方经 selectedLocked() 读回锁定集合，
 * 写回 DataStore 并刷新锁标（不触发排课；排课由「自动排课」按锁定集执行）。
 */

#include "lockdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "classgrouping.h"
#include "core/store/datastore.h"

/*
LockDialog - 构造：按课程分组搭三态勾选树，初始态取自数据源锁定集

Parameter：
    store: 数据仓库（只读；取课程/教学班/教师与现有锁定集）
    parent: 父窗口指针

Remark:
    分组复用 courseui::groupClassesByCourse（顺序随 store.courses()；
    孤儿班归末尾"未归类"组）。课程头带 Qt::ItemIsAutoTristate：
    勾/解自动级联整课，子项勾选自动回写父态。
    说明列显示 教师 · 已排课/未排课（辅助判断"锁定后重排是否会被保留"）。
*/
LockDialog::LockDialog(const DataStore &store, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("锁定"));
    resize(480, 560);
    setMinimumWidth(430);

    auto *layout = new QVBoxLayout(this);

    auto *cap = new QLabel(
        QStringLiteral("勾选要锁定的教学班（勾课程头可一次锁定 / 解锁整门课）：\n"
                       "确定仅保存锁定，不立即排课。点「自动排课」时：已锁定的班原样保留，"
                       "只重排其余班；已锁定但尚未排上的班仍会尝试排入。"),
        this);
    cap->setWordWrap(true);
    layout->addWidget(cap);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({QStringLiteral("课程 / 教学班"), QStringLiteral("教师 · 排课状态")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    // 排课状态：哪些班当前已有课（决定锁定后是否原样保留）
    QSet<QString> scheduled;
    for (const ScheduleEntry &e : store.scheduleEntries())
        scheduled.insert(e.teachingClassId);
    const QSet<QString> &locked = store.lockedClassIds();

    // 组：按课程顺序收集其下教学班（复用 courseui；孤儿班归"未归类"）
    const auto groups = courseui::groupClassesByCourse(store);
    for (const auto &group : groups) {
        auto *courseItem = new QTreeWidgetItem(m_tree);
        courseItem->setText(0, group.course
            ? courseui::courseLabel(*group.course)
            : QStringLiteral("未归类"));
        courseItem->setText(1, QStringLiteral("共 %1 班").arg(int(group.classes.size())));
        courseItem->setFlags(courseItem->flags() | Qt::ItemIsUserCheckable
                             | Qt::ItemIsAutoTristate);

        int checked = 0;
        for (const TeachingClass *tc : group.classes) {
            auto *classItem = new QTreeWidgetItem(courseItem);
            classItem->setText(0, tc->classId);
            classItem->setText(1, courseui::teacherDisplayName(store, tc->teacherId)
                                   + QStringLiteral(" · ")
                                   + (scheduled.contains(tc->classId)
                                          ? QStringLiteral("已排课")
                                          : QStringLiteral("未排课")));
            classItem->setData(0, Qt::UserRole, tc->classId);
            classItem->setFlags(classItem->flags() | Qt::ItemIsUserCheckable);
            const bool isLocked = locked.contains(tc->classId);
            classItem->setCheckState(0, isLocked ? Qt::Checked : Qt::Unchecked);
            if (isLocked)
                ++checked;
        }

        // 课程头初始态对齐其子项（全勾=Checked / 全不勾=Unchecked / 混合=Partial）
        const Qt::CheckState cs = (checked == 0) ? Qt::Unchecked
            : (checked == int(group.classes.size())) ? Qt::Checked : Qt::PartiallyChecked;
        courseItem->setCheckState(0, cs);
    }
    m_tree->expandAll();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

/*
LockDialog::selectedLocked - 当前被勾选（要锁定）的教学班集合

Result:
    QSet<QString>: 全部勾选子项的教学班 id；全部未勾选时为空集
*/
QSet<QString> LockDialog::selectedLocked() const
{
    QSet<QString> result;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *courseItem = m_tree->topLevelItem(i);
        for (int j = 0; j < courseItem->childCount(); ++j) {
            QTreeWidgetItem *classItem = courseItem->child(j);
            if (classItem->checkState(0) == Qt::Checked)
                result.insert(classItem->data(0, Qt::UserRole).toString());
        }
    }
    return result;
}