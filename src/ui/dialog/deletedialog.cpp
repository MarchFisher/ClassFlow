/**
 * 文件职责：删除课程 / 删除教学班双标签弹窗实现。课程与教学班是两个独立实体，
 * 两页分别处理整门删与单班删：
 * Tab「删除课程」逐行列出全部课程（含空课程），勾选整门删除；
 * Tab「删除教学班」沿用课程分组三态勾选树，只删所选班、永不级联删课。
 * 两页只读数据源、各自维护选择；OK 时合并成 DeletionRequest 返回调用方确认后落库。
 */

#include "deletedialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "classgrouping.h"
#include "core/store/datastore.h"

/*
DeleteDialog - 构造：搭「删除课程 / 删除教学班」两标签页并接 OK/Cancel

Parameter：
    store: 数据仓库（只读；取课程/教学班/教师与排课状态）
    parent: 父窗口指针
*/
DeleteDialog::DeleteDialog(const DataStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("删除课程/教学班"));
    resize(480, 560);
    setMinimumWidth(430);

    auto *layout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildCourseTab(), QStringLiteral("删除课程"));
    m_tabs->addTab(buildClassTab(), QStringLiteral("删除教学班"));
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

/*
DeleteDialog::buildCourseTab - 「删除课程」页：每行一门课（含空课），勾选整门删

Result:
    QWidget*: 页面控件；课程行数据存课程号，勾选 = 该整门课（连班/排课/锁）一起删
*/
QWidget *DeleteDialog::buildCourseTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *cap = new QLabel(
        QStringLiteral("勾选要删除的整门课程（含空课程）：删除会连带移除其全部教学班、"
                       "现有排课与锁定，不可撤销。\n只删个别教学班请到「删除教学班」页；"
                       "点确定后仍会再确认一次。"),
        page);
    cap->setWordWrap(true);
    v->addWidget(cap);

    m_courseTree = new QTreeWidget(page);
    m_courseTree->setColumnCount(2);
    m_courseTree->setHeaderLabels({QStringLiteral("课程(课程号)"),
                                   QStringLiteral("教学班情况")});
    m_courseTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_courseTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_courseTree->setUniformRowHeights(true);
    v->addWidget(m_courseTree, 1);

    for (const Course &c : m_store.courses()) {
        const int n = courseui::classCountOfCourse(m_store, c.id);
        auto *item = new QTreeWidgetItem(m_courseTree);
        item->setText(0, courseui::courseLabel(c));
        item->setText(1, (n == 0)
            ? QStringLiteral("空课程")
            : QStringLiteral("共 %1 班 · 已排 %2")
                  .arg(n).arg(courseui::scheduledCountOfCourse(m_store, c.id)));
        item->setData(0, Qt::UserRole, c.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);
    }

    if (m_store.courses().isEmpty()) {
        auto *hint = new QLabel(QStringLiteral("（当前没有任何课程）"), page);
        v->addWidget(hint);
    }
    return page;
}

/*
DeleteDialog::buildClassTab - 「删除教学班」页：课程分组三态树，只删所选班

Result:
    QWidget*: 页面控件；课程头勾选只级联其下属班，单班勾选 = 只删该班（课程保留）

Remark:
    分组复用 courseui::groupClassesByCourse（跳过无班课程；孤儿班归"未归类"）。
    与旧版不同：删除某一课的全部班后课程保留为空，不再自动删整门课。
*/
QWidget *DeleteDialog::buildClassTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *cap = new QLabel(
        QStringLiteral("勾选要删除的教学班（勾课程头 = 只勾其下全部班）：\n"
                       "删除教学班不会删除课程——删到最后一班，课程保留为空课程，"
                       "可到「删除课程」页整门移除。\n删除后该班原有排课一并移除；"
                       "点确定后仍会再确认一次。"),
        page);
    cap->setWordWrap(true);
    v->addWidget(cap);

    m_classTree = new QTreeWidget(page);
    m_classTree->setColumnCount(2);
    m_classTree->setHeaderLabels({QStringLiteral("课程 / 教学班"),
                                  QStringLiteral("教师 · 人数 · 排课")});
    m_classTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_classTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_classTree->setRootIsDecorated(true);
    m_classTree->setUniformRowHeights(true);
    v->addWidget(m_classTree, 1);

    // 已排课集合：标注"删除会移除当前排课"
    QSet<QString> scheduled;
    for (const ScheduleEntry &e : m_store.scheduleEntries())
        scheduled.insert(e.teachingClassId);

    const auto groups = courseui::groupClassesByCourse(m_store);
    for (const auto &group : groups) {
        auto *courseItem = new QTreeWidgetItem(m_classTree);
        courseItem->setText(0, group.course
            ? courseui::courseLabel(*group.course)
            : QStringLiteral("未归类"));
        courseItem->setText(1, QStringLiteral("共 %1 班").arg(int(group.classes.size())));
        courseItem->setFlags(courseItem->flags() | Qt::ItemIsUserCheckable
                             | Qt::ItemIsAutoTristate);
        courseItem->setCheckState(0, Qt::Unchecked);

        for (const TeachingClass *tc : group.classes) {
            auto *classItem = new QTreeWidgetItem(courseItem);
            classItem->setText(0, tc->classId);
            classItem->setText(1, courseui::teacherDisplayName(m_store, tc->teacherId)
                                   + QStringLiteral(" · 计划%1")
                                         .arg(tc->plannedSize)
                                   + QStringLiteral(" · ")
                                   + (scheduled.contains(tc->classId)
                                          ? QStringLiteral("已排课")
                                          : QStringLiteral("未排课")));
            classItem->setData(0, Qt::UserRole, tc->classId);
            classItem->setFlags(classItem->flags() | Qt::ItemIsUserCheckable);
            classItem->setCheckState(0, Qt::Unchecked);
        }
    }
    m_classTree->expandAll();
    return page;
}

/*
DeleteDialog::request - 合并两个页签各自的选择

Result:
    DeletionRequest: 课程页勾的整门课程号 + 班页勾的单班 id；全未勾时两集皆空
*/
DeleteDialog::DeletionRequest DeleteDialog::request() const
{
    DeletionRequest req;

    for (int i = 0; i < m_courseTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_courseTree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            req.courseIds.insert(item->data(0, Qt::UserRole).toString());
    }

    for (int i = 0; i < m_classTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *courseItem = m_classTree->topLevelItem(i);
        for (int j = 0; j < courseItem->childCount(); ++j) {
            QTreeWidgetItem *classItem = courseItem->child(j);
            if (classItem->checkState(0) == Qt::Checked)
                req.classIds.insert(classItem->data(0, Qt::UserRole).toString());
        }
    }
    return req;
}
