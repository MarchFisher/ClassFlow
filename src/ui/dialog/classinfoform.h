#ifndef CLASSINFOFORM_H
#define CLASSINFOFORM_H

#include <QString>
#include <QWidget>

#include "core/models/models.h"

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class DataStore;

// 单个教学班「基本信息」编辑表单（可嵌入任意 QWidget）。
// 编辑：所属课程（课程名/开课学院，孤儿班隐藏）+ 任课教师（下拉「未安排」+现有教师，
// NoInsert 可键入）+ 计划人数/最大容量。持 const DataStore&（只读数据源），
// 只产出 collect() 结果不落库；落库决策由调用方走 editui（换师撞车/扩容溢出）。
// 复用方：EditInfoDialog（钉选班级 / 浏览班级页）、ClassEditDialog 基本信息页签。
class ClassInfoForm : public QWidget
{
    Q_OBJECT

public:
    // 本次「想改成什么」：与数据源现值比对，仅确有变化的实体置位
    struct Result {
        bool courseEdited = false;
        Course course;            // courseEdited 为真时有效（含 id 等全字段）
        bool classEdited = false;
        TeachingClass klass;      // classEdited 为真时有效
    };

    explicit ClassInfoForm(const DataStore &store, QWidget *parent = nullptr);

    QString classId() const { return m_classId; }     // 当前编辑目标教学班 id
    void populate(const QString &classId);            // 切目标并预填表单
    bool validate();                                  // 容量 ≥ 计划人数等校验（失败弹提示）
    Result collect() const;                           // 按表单现值比对数据源，仅含确有变化项

private:
    void connectTeacherCombo();   // 重建教师下拉：空项「未安排」+ 现有教师（NoInsert 可键入）
    void warn(const QString &text) const;             // 校验失败提示

    const DataStore &m_store;
    QString m_classId;            // 当前编辑目标（空 = 尚未定位教学班）

    QLabel    *m_caption = nullptr;       // "教学班 C101（课程 C01）"
    QGroupBox *m_ownerBox = nullptr;      // 所属课程信息（孤儿班隐藏该段）
    QLabel    *m_ownerCaption = nullptr;  // "课程号：C01"
    QLineEdit *m_ownerNameEdit = nullptr;
    QLineEdit *m_ownerDepartEdit = nullptr;
    QComboBox *m_teacherCombo = nullptr;
    QSpinBox  *m_plannedSpin = nullptr;
    QSpinBox  *m_capacitySpin = nullptr;
};

#endif // CLASSINFOFORM_H
