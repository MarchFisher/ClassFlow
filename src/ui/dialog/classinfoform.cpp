/**
 * 文件职责：单个教学班「基本信息」编辑表单实现（可嵌入 widget）。
 * 提供 所属课程（名/学院）+ 任课教师 + 计划人数/最大容量 的编辑界面，只读数据源、
 * 只出 collect() 结果不落库；落库与预检（换师撞车登记 / 扩容超教室确认）由调用方走
 * editui 助手。UI 与逻辑从 EditInfoDialog 钉选班级页抽取而来，供 EditInfoDialog /
 * ClassEditDialog 复用，避免双源漂移。
 */

#include "classinfoform.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/store/datastore.h"

/*
ClassInfoForm - 构造：搭基本信息表单（所属课程段 + 教学班段）

Parameter：
    store: 数据仓库（只读；取课程/班级现状与候选教师）
    parent: 父窗口指针

Remark:
    表单字段在 populate(classId) 时按目标班级预填；教师下拉在构造即备好候选，
    populate 会清空重建以保留「未安排」首项语义。
*/
ClassInfoForm::ClassInfoForm(const DataStore &store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    m_caption = new QLabel(this);
    m_caption->setWordWrap(true);
    v->addWidget(m_caption);

    // —— 所属课程信息（可连带改名/学院）；孤儿班（课程缺失）隐藏该段 ——
    m_ownerBox = new QGroupBox(QStringLiteral("所属课程信息"), this);
    auto *oform = new QFormLayout(m_ownerBox);
    oform->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_ownerCaption = new QLabel(m_ownerBox);
    oform->addRow(m_ownerCaption);
    m_ownerNameEdit = new QLineEdit(m_ownerBox);
    oform->addRow(QStringLiteral("课程名"), m_ownerNameEdit);
    m_ownerDepartEdit = new QLineEdit(m_ownerBox);
    oform->addRow(QStringLiteral("开课学院"), m_ownerDepartEdit);
    v->addWidget(m_ownerBox);

    // —— 教学班 ——
    auto *classBox = new QGroupBox(QStringLiteral("教学班信息"), this);
    auto *cform = new QFormLayout(classBox);
    cform->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_teacherCombo = new QComboBox(classBox);
    m_teacherCombo->setEditable(true);
    m_teacherCombo->setInsertPolicy(QComboBox::NoInsert);
    connectTeacherCombo();
    cform->addRow(QStringLiteral("任课教师"), m_teacherCombo);
    m_plannedSpin = new QSpinBox(classBox);
    m_plannedSpin->setRange(1, 1000);
    cform->addRow(QStringLiteral("计划人数"), m_plannedSpin);
    m_capacitySpin = new QSpinBox(classBox);
    m_capacitySpin->setRange(1, 1000);
    cform->addRow(QStringLiteral("最大容量"), m_capacitySpin);
    v->addWidget(classBox);
}

/*
ClassInfoForm::connectTeacherCombo - 重建教师下拉选项：空项「未安排」+ 现有教师

Remark:
    首项「未安排」代表空教师（读回时空串）；其余项文本即教师 id（不新增师表行）。
    调用前需 clear()；键入不在表的新 id 按 NoInsert 不落地。
*/
void ClassInfoForm::connectTeacherCombo()
{
    if (!m_teacherCombo)
        return;
    m_teacherCombo->addItem(QStringLiteral("未安排"), QString());
    for (const TeacherInfo &t : m_store.teachers())
        if (!t.teacherId.isEmpty())
            m_teacherCombo->addItem(t.teacherId);
}

/*
ClassInfoForm::populate - 切到班级目标并预填表单字段

Parameter：
    classId: 要编辑的教学班 id（不存在时表单置空并提示）
*/
void ClassInfoForm::populate(const QString &classId)
{
    const TeachingClass *tc = m_store.teachingClassById(classId);
    if (!tc) {
        m_classId.clear();
        if (m_caption)
            m_caption->setText(QStringLiteral("教学班 %1 已不存在。").arg(classId));
        if (m_ownerBox)
            m_ownerBox->setVisible(false);
        if (m_teacherCombo)
            m_teacherCombo->clear();
        return;
    }
    m_classId = classId;

    if (m_caption)
        m_caption->setText(QStringLiteral("教学班 %1（课程 %2）")
                               .arg(tc->classId, tc->courseId));

    // 所属课程段：孤儿班（课程缺失）隐藏；正常班预填并允许连带改
    const Course *owner = m_store.courseById(tc->courseId);
    if (m_ownerBox && m_ownerCaption) {
        if (owner) {
            m_ownerCaption->setText(QStringLiteral("课程号：%1").arg(owner->id));
            m_ownerNameEdit->setText(owner->name);
            m_ownerDepartEdit->setText(owner->depart);
            m_ownerBox->setVisible(true);
        } else {
            m_ownerBox->setVisible(false);
        }
    }

    m_teacherCombo->clear();
    connectTeacherCombo();
    // 选中当前值：空教师 → 首项「未安排」；下拉缺当前教师（如班引用不在师表）
    // 时补一项，保证能读回现值
    int idx = m_teacherCombo->findText(tc->teacherId);
    if (tc->teacherId.isEmpty())
        idx = 0;
    else if (idx < 0) {
        idx = m_teacherCombo->count();
        m_teacherCombo->addItem(tc->teacherId);
    }
    m_teacherCombo->setCurrentIndex(idx);
    m_plannedSpin->setValue(tc->plannedSize);
    m_capacitySpin->setValue(tc->maxCapacity);
}

/*
ClassInfoForm::validate - 表单自身一致性校验（容量 ≥ 计划人数、课程名非空）

Result:
    bool: 通过返回 true；违规弹提示并返回 false
*/
bool ClassInfoForm::validate()
{
    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    if (!tc) {
        warn(QStringLiteral("教学班 %1 已不存在。").arg(m_classId));
        return false;
    }
    if (m_capacitySpin && m_plannedSpin
        && m_capacitySpin->value() < m_plannedSpin->value()) {
        warn(QStringLiteral("最大容量不能小于计划人数。"));
        return false;
    }
    if (m_ownerBox && m_ownerBox->isVisible()
        && m_ownerNameEdit->text().trimmed().isEmpty()) {
        warn(QStringLiteral("所属课程名不能为空。"));
        return false;
    }
    return true;
}

/*
ClassInfoForm::collect - 按表单现值比对数据源，仅产出确有变化的实体

Result:
    Result: courseEdited/classEdited 仅在有实际变化时置位；本函数不落库
*/
ClassInfoForm::Result ClassInfoForm::collect() const
{
    Result r;
    const TeachingClass *old = m_store.teachingClassById(m_classId);
    if (!old)
        return r;

    // 所属课程连带改名（孤儿班无课程段则跳过）
    const Course *owner = m_store.courseById(old->courseId);
    if (owner) {
        Course oc = *owner;
        oc.name = m_ownerNameEdit->text().trimmed();
        oc.depart = m_ownerDepartEdit->text().trimmed();
        if (oc.name != owner->name || oc.depart != owner->depart) {
            r.courseEdited = true;
            r.course = oc;
        }
    }

    TeachingClass k = *old;
    QString teacher = m_teacherCombo->currentText().trimmed();
    if (teacher == QStringLiteral("未安排"))
        teacher.clear();               // 「未安排」占位项 = 空教师
    k.teacherId = teacher;
    k.plannedSize = m_plannedSpin->value();
    k.maxCapacity = m_capacitySpin->value();
    if (k.teacherId != old->teacherId
        || k.plannedSize != old->plannedSize
        || k.maxCapacity != old->maxCapacity) {
        r.classEdited = true;
        r.klass = k;
    }
    return r;
}

/*
ClassInfoForm::warn - 校验失败提示
*/
void ClassInfoForm::warn(const QString &text) const
{
    QMessageBox::warning(const_cast<ClassInfoForm *>(this),
                         QStringLiteral("编辑教学班"), text);
}
