#ifndef ADDDIALOG_H
#define ADDDIALOG_H

#include <QDialog>

#include "core/models/models.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QWidget;
class DataStore;

// 新增课程 / 新增教学班弹窗（双标签页）。
// 两个页签相互独立：Tab0「新增课程」只建空课程（不排课，不加班）；Tab1「新增教学班」
// 按课程号选已有课程（含空课程），其余课程信息由选中的课程号自动带出。
// 课程号是识别课程的唯一主键：允许同名不同号，不做课程名反推 / 判重。
// 本弹窗只读数据源，确定后由调用方经 result() 读回本次要落库的内容。
class AddDialog : public QDialog
{
    Q_OBJECT

public:
    // 本次要落库的动作：新增课程（仅 course）或 给已有课程新增教学班（仅 klass）
    enum class Mode { AddCourse, AddClass };

    struct Result {
        Mode mode = Mode::AddCourse;
        Course course;          // mode == AddCourse 时有效（课程号全局唯一）
        TeachingClass klass;    // mode == AddClass 时有效（courseId 指向所选课程）
    };

    // store: 数据仓库（只读；用于课程号/教学班号唯一性、候选课程与教师）
    AddDialog(const DataStore &store, QWidget *parent = nullptr);

    // 表单校验通过并接受后调用；返回当前激活页对应要落库的动作
    Result result() const;

private slots:
    void onOkClicked();        // 校验当前激活页并 accept

private:
    QWidget *buildAddCourseTab();            // 「新增课程」页
    QWidget *buildAddClassTab();             // 「新增教学班」页
    bool validateCourseTab();                // 校验新增课程页
    bool validateClassTab();                 // 校验新增教学班页
    void fillCourse(Course &out);            // 从新增课程页表单填课程
    bool fillClass(TeachingClass &out);      // 从新增教学班页表单填教学班
    void updateDerivedInfo();                // 课程号下拉选中 → 预览该课程其它信息
    void warn(const QString &text);          // 表单校验失败提示

    const DataStore &m_store;
    QTabWidget *m_tabs = nullptr;
    Result m_result;

    // —— 「新增课程」页字段 ——
    QLineEdit      *m_courseIdEdit = nullptr;
    QLineEdit      *m_courseNameEdit = nullptr;
    QDoubleSpinBox *m_creditSpin = nullptr;
    QSpinBox       *m_sessionsSpin = nullptr;
    QSpinBox       *m_hoursSpin = nullptr;
    QLineEdit      *m_departEdit = nullptr;
    QSpinBox       *m_startWeekSpin = nullptr;
    QSpinBox       *m_endWeekSpin = nullptr;
    QComboBox      *m_roomTypeCombo = nullptr;

    // —— 「新增教学班」页字段 ——
    QComboBox *m_courseCombo = nullptr;   // 候选课程（含空课程）
    QLabel    *m_derivedLabel = nullptr;  // 按选中课程号带出的课程信息预览
    QLineEdit *m_classIdEdit = nullptr;
    QComboBox *m_teacherCombo = nullptr;
    QSpinBox  *m_plannedSpin = nullptr;
    QSpinBox  *m_capacitySpin = nullptr;
};

#endif // ADDDIALOG_H
