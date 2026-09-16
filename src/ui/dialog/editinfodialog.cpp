/**
 * 文件职责：课程/教学班基本信息编辑弹窗实现（复用 ClassInfoForm）。
 * EditInfoDialog 提供三种形态（钉选课程 / 钉选班级 / 浏览双页签），只产出 result()，
 * 不落库；落库决策助手 editui（课程改名即时生效 / 换师撞车只登记 / 扩容超教室弹确认）
 * 声明于 editui.h、实现在 editui.cpp，供课程详情弹窗与主窗口共用。教学班基本信息表单
 * 由共享 widget ClassInfoForm 提供（钉选班级页与浏览班级页都嵌入同一实例），避免双源。
 * 本文件不触发排课线程。
 */

#include "editinfodialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "classgrouping.h"
#include "classinfoform.h"
#include "core/store/datastore.h"

// ==================== EditInfoDialog ====================

/*
EditInfoDialog - 构造：按钉选目标决定形态，搭界面并预填

Parameter：
    store: 数据仓库（只读；取课程/班级现状、候选教师）
    courseId: 非空 = 钉选该课程（只编辑课程）
    classId:  非空 = 钉选该教学班（编辑班级及其所属课程）
    parent: 父窗口指针

Remark:
    courseId / classId 都空 = 浏览形态（两页签选目标）。courseId 与 classId 都非空时
    以 classId 为准（详情场景只用 classId）。
*/
EditInfoDialog::EditInfoDialog(const DataStore &store, const QString &courseId,
                               const QString &classId, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_pinCourseId(classId.isEmpty() ? courseId : QString())
    , m_pinClassId(classId)
    , m_curCourseId(m_pinCourseId)
    , m_curClassId(m_pinClassId)
{
    setWindowTitle(QStringLiteral("编辑课程 / 教学班信息"));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    const bool browse = m_pinCourseId.isEmpty() && m_pinClassId.isEmpty();

    if (browse) {
        m_tabs = new QTabWidget(this);
        m_tabs->addTab(buildCoursePage(), QStringLiteral("课程"));
        m_tabs->addTab(buildClassPage(), QStringLiteral("教学班"));
        layout->addWidget(m_tabs, 1);
        setupBrowse();
    } else if (!m_pinClassId.isEmpty()) {
        layout->addWidget(buildClassPage());
    } else {
        layout->addWidget(buildCoursePage());
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &EditInfoDialog::onOkClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (!browse) {
        if (!m_pinClassId.isEmpty())
            m_infoForm->populate(m_pinClassId);   // 钉选班级：预填共享表单
        else
            populateCourse();                     // 钉选课程：直接填课程表单
    }
}

/*
EditInfoDialog::buildCoursePage - 课程编辑页（课程名 / 开课学院）

Result:
    QWidget*: 页面；浏览模式顶部是课程下拉（选哪门改哪门）
*/
QWidget *EditInfoDialog::buildCoursePage()
{
    m_coursePage = new QWidget(this);
    auto *v = new QVBoxLayout(m_coursePage);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    if (m_pinCourseId.isEmpty()) {        // 浏览：先选课程
        m_courseCombo = new QComboBox(m_coursePage);
        for (const Course &c : m_store.courses()) {
            const int n = courseui::classCountOfCourse(m_store, c.id);
            m_courseCombo->addItem(courseui::courseLabel(c)
                                       + (n ? QStringLiteral(" · %1 班").arg(n)
                                            : QStringLiteral(" · 空课程")),
                                   c.id);
        }
        form->addRow(QStringLiteral("选择课程"), m_courseCombo);
        v->addLayout(form);
    } else {
        m_courseIdCaption = new QLabel(
            QStringLiteral("课程号：%1").arg(m_pinCourseId), m_coursePage);
        m_courseIdCaption->setWordWrap(true);
        v->addWidget(m_courseIdCaption);
    }

    auto *editForm = new QFormLayout;
    editForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_courseNameEdit = new QLineEdit(m_coursePage);
    m_courseNameEdit->setPlaceholderText(QStringLiteral("如 高等数学"));
    editForm->addRow(QStringLiteral("课程名"), m_courseNameEdit);
    m_courseDepartEdit = new QLineEdit(m_coursePage);
    m_courseDepartEdit->setPlaceholderText(QStringLiteral("开课学院（可留空）"));
    editForm->addRow(QStringLiteral("开课学院"), m_courseDepartEdit);
    v->addLayout(editForm);

    v->addWidget(new QLabel(
        QStringLiteral("改课程名 / 开课学院即时生效：该课程全部教学班与卡片同步更新。"
                       "学分、课次、周范围等节律字段不在本次编辑范围。"), m_coursePage));

    if (m_courseCombo) {
        connect(m_courseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    const QString id = m_courseCombo->currentData().toString();
                    if (!id.isEmpty())
                        pickCourse(id);
                });
    }
    return m_coursePage;
}

/*
EditInfoDialog::buildClassPage - 教学班编辑页（浏览含分组树；共享 ClassInfoForm 表单）

Result:
    QWidget*: 页面；浏览模式顶部是课程分组树（选叶 = 选班），下方即共享基本信息表单
*/
QWidget *EditInfoDialog::buildClassPage()
{
    m_classPage = new QWidget(this);
    auto *v = new QVBoxLayout(m_classPage);

    if (m_pinClassId.isEmpty()) {         // 浏览：分组树选班
        m_classTree = new QTreeWidget(m_classPage);
        m_classTree->setColumnCount(2);
        m_classTree->setHeaderLabels({QStringLiteral("课程 / 教学班"),
                                      QStringLiteral("教师 · 人数")});
        m_classTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_classTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_classTree->setUniformRowHeights(true);
        v->addWidget(m_classTree, 1);

        const auto groups = courseui::groupClassesByCourse(m_store);
        for (const auto &group : groups) {
            auto *courseItem = new QTreeWidgetItem(m_classTree);
            courseItem->setText(0, group.course
                ? courseui::courseLabel(*group.course)
                : QStringLiteral("未归类"));
            courseItem->setText(1, QStringLiteral("共 %1 班").arg(int(group.classes.size())));
            courseItem->setFlags(courseItem->flags() & ~Qt::ItemIsSelectable);
            for (const TeachingClass *tc : group.classes) {
                auto *leaf = new QTreeWidgetItem(courseItem);
                leaf->setText(0, tc->classId);
                leaf->setText(1, courseui::teacherDisplayName(m_store, tc->teacherId)
                                 + QStringLiteral(" · 计划%1").arg(tc->plannedSize));
                leaf->setData(0, Qt::UserRole, tc->classId);
            }
        }
        m_classTree->expandAll();
        connect(m_classTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
            const QList<QTreeWidgetItem *> sel = m_classTree->selectedItems();
            for (QTreeWidgetItem *it : sel) {
                const QString id = it->data(0, Qt::UserRole).toString();
                if (!id.isEmpty()) {
                    pickClass(id);
                    return;
                }
            }
        });
    }

    // 共享基本信息表单（钉选班级 / 浏览班级页都嵌入同一实例）
    m_infoForm = new ClassInfoForm(m_store, m_classPage);
    v->addWidget(m_infoForm);
    return m_classPage;
}

/*
EditInfoDialog::setupBrowse - 浏览形态：默认选中首项并随选中联动 populate

Remark:
    课程页下拉选中 → 课程目标（pickCourse）；教学班页树选中叶 → 班级目标（pickClass）。
    首次切到「教学班」页且尚未选过班时，默认选中首棵树的第一个叶。
*/
void EditInfoDialog::setupBrowse()
{
    if (m_courseCombo && m_courseCombo->count() > 0)
        pickCourse(m_courseCombo->itemData(0).toString());

    if (m_classTree && m_tabs) {
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (index != 1 || !m_curClassId.isEmpty())
                return;                 // 已在班级页且选过班：保持现状
            QTreeWidgetItem *first = m_classTree->topLevelItem(0);
            if (first && first->child(0))
                pickClass(first->child(0)->data(0, Qt::UserRole).toString());
        });
    }
}

/*
EditInfoDialog::populateCourse - 按 m_curCourseId 填充课程字段
*/
void EditInfoDialog::populateCourse()
{
    const Course *c = m_store.courseById(m_curCourseId);
    if (!c)
        return;
    if (m_courseNameEdit)
        m_courseNameEdit->setText(c->name);
    if (m_courseDepartEdit)
        m_courseDepartEdit->setText(c->depart);
}

/*
EditInfoDialog::pickCourse - 切到课程目标并填充表单
*/
void EditInfoDialog::pickCourse(const QString &courseId)
{
    m_curCourseId = courseId;
    m_curClassId.clear();
    populateCourse();
}

/*
EditInfoDialog::pickClass - 切到班级目标并填充共享表单
*/
void EditInfoDialog::pickClass(const QString &classId)
{
    m_curClassId = classId;
    if (m_infoForm)
        m_infoForm->populate(classId);
}

/*
EditInfoDialog::warn - 校验失败提示
*/
void EditInfoDialog::warn(const QString &text) const
{
    QMessageBox::warning(const_cast<EditInfoDialog *>(this),
                         QStringLiteral("编辑信息"), text);
}

/*
EditInfoDialog::onOkClicked - 校验当前目标并写回 m_result 后 accept

Remark:
    课程形态 → 改课程名/学院；班级形态 → 委托 ClassInfoForm 校验 + collect
    （可连带改所属课程）。结果只含确有变化的实体（与数据源比对）。
*/
void EditInfoDialog::onOkClicked()
{
    const bool browse = m_pinCourseId.isEmpty() && m_pinClassId.isEmpty();
    const bool courseMode = browse ? (m_tabs && m_tabs->currentIndex() == 0)
                                   : m_pinClassId.isEmpty();
    if (courseMode) {
        if (m_curCourseId.isEmpty()) {
            warn(QStringLiteral("请先在「课程」页选择要编辑的课程。"));
            return;
        }
        const Course *old = m_store.courseById(m_curCourseId);
        if (!old) {
            warn(QStringLiteral("课程 %1 已不存在。").arg(m_curCourseId));
            return;
        }
        if (m_courseNameEdit->text().trimmed().isEmpty()) {
            warn(QStringLiteral("课程名不能为空。"));
            return;
        }
        Course c = *old;
        c.name = m_courseNameEdit->text().trimmed();
        c.depart = m_courseDepartEdit->text().trimmed();
        if (c.name != old->name || c.depart != old->depart) {
            m_result.courseEdited = true;
            m_result.course = c;
        }
    } else {
        if (m_curClassId.isEmpty()) {
            warn(QStringLiteral("请先在「教学班」页选择要编辑的教学班。"));
            return;
        }
        if (!m_store.teachingClassById(m_curClassId)) {
            warn(QStringLiteral("教学班 %1 已不存在。").arg(m_curClassId));
            return;
        }
        if (!m_infoForm->validate())
            return;
        const ClassInfoForm::Result r = m_infoForm->collect();
        m_result.courseEdited = r.courseEdited;
        m_result.course = r.course;
        m_result.classEdited = r.classEdited;
        m_result.klass = r.klass;
    }

    if (!m_result.courseEdited && !m_result.classEdited) {
        warn(QStringLiteral("没有改动任何字段。"));
        return;
    }
    accept();
}

/*
EditInfoDialog::result - 本次要改成的实体（仅含确有变化者）
*/
EditInfoDialog::Result EditInfoDialog::result() const
{
    return m_result;
}
