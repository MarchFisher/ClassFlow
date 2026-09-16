/**
 * 文件职责：新增课程 / 新增教学班双标签弹窗实现。课程与教学班是两个独立实体：
 * Tab0 只建空课程（不排课）；Tab1 从课程号下拉选已有课程并新增一个教学班。
 * 识别课程一律用课程号，不做课程名反推 / 同名判重。只读数据源，确定后调用方经
 * result() 读回要落库的动作：新增课程 或 给已有课程新增教学班（后者再做最小排）。
 */

#include "adddialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "classgrouping.h"
#include "core/store/datastore.h"

/*
roomTypeDisplayName - 教室类型显示名（与课程所需教室下拉同源）

Parameter：
    type: 教室类型枚举

Result:
    QString: 中文显示名（"不限"/"普通教室"/"机房"/"操场"）
*/
static QString roomTypeDisplayName(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Any:       return QStringLiteral("不限");
    case ClassroomType::Norm:      return QStringLiteral("普通教室");
    case ClassroomType::Lab:       return QStringLiteral("机房");
    case ClassroomType::PlayGround:return QStringLiteral("操场");
    }
    return QStringLiteral("不限");
}

/*
AddDialog - 构造：搭双标签页并接 OK/Cancel

Parameter：
    store: 数据仓库（只读；取候选课程/教师、校验课程号/教学班号唯一性、学期总周数）
    parent: 父窗口指针
*/
AddDialog::AddDialog(const DataStore &store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("新增课程 / 教学班"));
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildAddCourseTab(), QStringLiteral("新增课程"));
    m_tabs->addTab(buildAddClassTab(), QStringLiteral("新增教学班"));
    // 没有任何课程时不能新增教学班：禁用该页并引导先去新增课程
    m_tabs->setTabEnabled(1, !store.courses().isEmpty());
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AddDialog::onOkClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

/*
AddDialog::buildAddCourseTab - 「新增课程」页：填课程各属性，只建空课程

Result:
    QWidget*: 页面控件（字段成员已挂到成员指针供校验 / 取值）
*/
QWidget *AddDialog::buildAddCourseTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *cap = new QLabel(
        QStringLiteral("新增一门课程（暂不添加教学班，也不排课）：\n"
                       "课程号是识别课程的唯一主键；一门课名下可建多门不同课程号的课。\n"
                       "建好后可到「新增教学班」页为它添加班级。"),
        page);
    cap->setWordWrap(true);
    v->addWidget(cap);

    auto *form = new QFormLayout;
    // 所有输入框/下拉/微调框统一拉伸填满同一列宽，避免长短不一
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_courseIdEdit = new QLineEdit(page);
    m_courseIdEdit->setPlaceholderText(QStringLiteral("如 C05"));
    form->addRow(QStringLiteral("课程号"), m_courseIdEdit);

    m_courseNameEdit = new QLineEdit(page);
    m_courseNameEdit->setPlaceholderText(QStringLiteral("如 数据结构"));
    form->addRow(QStringLiteral("课程名"), m_courseNameEdit);

    m_creditSpin = new QDoubleSpinBox(page);
    m_creditSpin->setRange(0.5, 20.0);
    m_creditSpin->setDecimals(1);
    m_creditSpin->setSingleStep(0.5);
    m_creditSpin->setValue(4.0);
    form->addRow(QStringLiteral("学分"), m_creditSpin);

    m_sessionsSpin = new QSpinBox(page);
    m_sessionsSpin->setRange(1, 10);
    m_sessionsSpin->setValue(2);
    form->addRow(QStringLiteral("每周课次"), m_sessionsSpin);

    m_hoursSpin = new QSpinBox(page);
    m_hoursSpin->setRange(1, 8);          // 学时仅支持整数（引擎对非整数当排课失败）
    m_hoursSpin->setValue(2);
    form->addRow(QStringLiteral("单次学时(节)"), m_hoursSpin);

    m_departEdit = new QLineEdit(page);
    m_departEdit->setPlaceholderText(QStringLiteral("如 计算机学院（可留空）"));
    form->addRow(QStringLiteral("开课学院"), m_departEdit);

    m_startWeekSpin = new QSpinBox(page);
    m_startWeekSpin->setRange(1, m_store.semesterWeeks());
    m_startWeekSpin->setValue(1);
    form->addRow(QStringLiteral("起始周"), m_startWeekSpin);

    m_endWeekSpin = new QSpinBox(page);
    m_endWeekSpin->setRange(1, m_store.semesterWeeks());
    m_endWeekSpin->setValue(m_store.semesterWeeks());
    form->addRow(QStringLiteral("结束周"), m_endWeekSpin);

    m_roomTypeCombo = new QComboBox(page);
    const ClassroomType types[] = { ClassroomType::Any, ClassroomType::Norm,
                                    ClassroomType::Lab, ClassroomType::PlayGround };
    for (ClassroomType t : types)
        m_roomTypeCombo->addItem(roomTypeDisplayName(t), int(t));
    form->addRow(QStringLiteral("所需教室"), m_roomTypeCombo);

    v->addLayout(form);
    return page;
}

/*
AddDialog::buildAddClassTab - 「新增教学班」页：按课程号选课并输入班级信息

Result:
    QWidget*: 页面控件；课程下拉列出全部课程（含空课），选中即用该课程号带出其
              它课程信息（学分/课次/周范围/教室等），并显示现有班数
*/
QWidget *AddDialog::buildAddClassTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *cap = new QLabel(
        QStringLiteral("给已有课程新增一个教学班：先在下方按课程号选择课程，\n"
                       "课程名 / 课次 / 周范围等信息会随所选课程自动带出。提交后对新班做最小排，"
                       "排不下会询问是否对未锁定班做局部重排。"),
        page);
    cap->setWordWrap(true);
    v->addWidget(cap);

    auto *form = new QFormLayout;
    // 与新增课程页一致：字段统一拉伸填满同一列宽
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_courseCombo = new QComboBox(page);
    const auto &courses = m_store.courses();
    for (const Course &c : courses) {
        const int n = courseui::classCountOfCourse(m_store, c.id);
        m_courseCombo->addItem(courseui::courseLabel(c)
                                   + QStringLiteral(" · %1 班").arg(n),
                               c.id);
    }
    form->addRow(QStringLiteral("课程(课程号)"), m_courseCombo);

    m_derivedLabel = new QLabel(page);
    m_derivedLabel->setWordWrap(true);
    m_derivedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), m_derivedLabel);

    auto *divider = new QLabel(QStringLiteral("—— 教学班信息 ——"), page);
    form->addRow(QString(), divider);

    m_classIdEdit = new QLineEdit(page);
    m_classIdEdit->setPlaceholderText(QStringLiteral("如 C501"));
    form->addRow(QStringLiteral("教学班号"), m_classIdEdit);

    m_teacherCombo = new QComboBox(page);
    m_teacherCombo->setEditable(true);
    m_teacherCombo->setInsertPolicy(QComboBox::NoInsert);
    for (const TeacherInfo &t : m_store.teachers())
        m_teacherCombo->addItem(t.teacherId);
    m_teacherCombo->lineEdit()->setPlaceholderText(
        QStringLiteral("选已有教师或输入教师 ID（可留空 = 未指定）"));
    form->addRow(QStringLiteral("教师"), m_teacherCombo);

    m_plannedSpin = new QSpinBox(page);
    m_plannedSpin->setRange(1, 500);
    m_plannedSpin->setValue(30);
    form->addRow(QStringLiteral("计划人数"), m_plannedSpin);

    m_capacitySpin = new QSpinBox(page);
    m_capacitySpin->setRange(1, 1000);
    m_capacitySpin->setValue(50);
    form->addRow(QStringLiteral("最大容量"), m_capacitySpin);

    v->addLayout(form);

    // 课程号变化 → 刷新带出的课程信息预览
    connect(m_courseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateDerivedInfo(); });
    if (!courses.isEmpty())
        updateDerivedInfo();
    else
        m_derivedLabel->setText(QStringLiteral("（暂无课程，请先在「新增课程」页建立）"));

    return page;
}

/*
AddDialog::updateDerivedInfo - 按课程号下拉当前选中课程带出其其它信息

Remark:
    课程号是主键：课程名 / 学分 / 课次 / 周范围 / 所需教室都从选中的课程号推导展示，
    让用户确认所选课程后再输入班级号等信息。
*/
void AddDialog::updateDerivedInfo()
{
    const QString courseId = m_courseCombo->currentData().toString();
    const Course *c = m_store.courseById(courseId);
    if (!c) {
        m_derivedLabel->clear();
        return;
    }
    const int n = courseui::classCountOfCourse(m_store, courseId);
    m_derivedLabel->setText(
        QStringLiteral("课程名 %1\n学分 %2 · 每周 %3 次 × 单次 %4 节 · 第 %5~%6 周"
                       " · 需要%7\n现有 %8 个教学班")
            .arg(c->name)
            .arg(c->credit)
            .arg(c->sessionsPerWeek)
            .arg(c->hoursPerSession)
            .arg(c->startWeek)
            .arg(c->endWeek)
            .arg(roomTypeDisplayName(c->requiredRoomType))
            .arg(n));
}

/*
AddDialog::validateCourseTab - 校验「新增课程」页

Result:
    bool: 通过返回 true；否则弹错误提示并返回 false
*/
bool AddDialog::validateCourseTab()
{
    if (m_courseIdEdit->text().trimmed().isEmpty()) {
        warn(QStringLiteral("课程号不能为空。"));
        return false;
    }
    if (m_store.courseById(m_courseIdEdit->text().trimmed())) {
        warn(QStringLiteral("课程号 %1 已存在，请更换。").arg(m_courseIdEdit->text().trimmed()));
        return false;
    }
    if (m_courseNameEdit->text().trimmed().isEmpty()) {
        warn(QStringLiteral("课程名不能为空。"));
        return false;
    }
    if (m_startWeekSpin->value() > m_endWeekSpin->value()) {
        warn(QStringLiteral("结束周不能早于起始周。"));
        return false;
    }
    return true;
}

/*
AddDialog::validateClassTab - 校验「新增教学班」页

Result:
    bool: 通过返回 true；否则弹错误提示并返回 false
*/
bool AddDialog::validateClassTab()
{
    const QString classId = m_classIdEdit->text().trimmed();
    if (classId.isEmpty()) {
        warn(QStringLiteral("教学班号不能为空。"));
        return false;
    }
    if (m_store.teachingClassById(classId)) {
        warn(QStringLiteral("教学班号 %1 已存在。").arg(classId));
        return false;
    }
    if (m_store.courses().isEmpty()) {
        warn(QStringLiteral("还没有任何课程，请先到「新增课程」页建立课程。"));
        return false;
    }
    if (m_capacitySpin->value() < m_plannedSpin->value()) {
        warn(QStringLiteral("最大容量不能小于计划人数。"));
        return false;
    }
    return true;
}

/*
AddDialog::fillCourse - 从「新增课程」页表单填课程

Parameter：
    out: 待填充的课程
*/
void AddDialog::fillCourse(Course &out)
{
    out.id               = m_courseIdEdit->text().trimmed();
    out.name             = m_courseNameEdit->text().trimmed();
    out.credit           = m_creditSpin->value();
    out.sessionsPerWeek  = m_sessionsSpin->value();
    out.hoursPerSession  = m_hoursSpin->value();
    out.depart           = m_departEdit->text().trimmed();
    out.startWeek        = m_startWeekSpin->value();
    out.endWeek          = m_endWeekSpin->value();
    out.requiredRoomType = ClassroomType(m_roomTypeCombo->currentData().toInt());
}

/*
AddDialog::fillClass - 从「新增教学班」页表单填教学班（归属所选课程号）

Parameter：
    out: 待填充的教学班

Result:
    bool: 恒为 true（合法性已在 validateClassTab 校验）
*/
bool AddDialog::fillClass(TeachingClass &out)
{
    out.classId     = m_classIdEdit->text().trimmed();
    out.courseId    = m_courseCombo->currentData().toString();
    out.teacherId   = m_teacherCombo->currentText().trimmed();
    out.plannedSize = m_plannedSpin->value();
    out.maxCapacity = m_capacitySpin->value();
    return true;
}

/*
AddDialog::onOkClicked - 校验当前激活页并通过 accept 把结果写回 m_result

Remark:
    两个页签独立提交：当前是「新增课程」页 → 只建空课程（不排课）；
    当前是「新增教学班」页 → 产出该教学班（由调用方落库后做最小排）。
*/
void AddDialog::onOkClicked()
{
    if (m_tabs->currentIndex() == 0) {
        if (!validateCourseTab())
            return;
        m_result.mode = Mode::AddCourse;
        fillCourse(m_result.course);
    } else {
        if (!validateClassTab())
            return;
        m_result.mode = Mode::AddClass;
        fillClass(m_result.klass);
    }
    accept();
}

/*
AddDialog::result - 本次要落库的动作

Result:
    Result: mode=AddCourse 时 course 有效；mode=AddClass 时 klass.courseId 指向所选课程
*/
AddDialog::Result AddDialog::result() const
{
    return m_result;
}

/*
AddDialog::warn - 表单校验失败提示

Parameter：
    text: 错误原因（标题固定"新增课程/教学班"）
*/
void AddDialog::warn(const QString &text)
{
    QMessageBox::warning(this, QStringLiteral("新增课程/教学班"), text);
}
