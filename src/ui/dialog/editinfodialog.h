#ifndef EDITINFODIALOG_H
#define EDITINFODIALOG_H

#include <QDialog>
#include <QString>

#include "core/models/models.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTreeWidget;
class QWidget;
class ClassInfoForm;
class DataStore;

// 落库决策助手（课程改名即时生效 / 换师撞车登记 / 扩容预检）在 editui.h 声明，
// 由 editui.cpp 实现；本文件只声明弹窗本身（弹窗不落库，OK 后调用方读 result() 决策）。

// 课程/教学班基本信息编辑弹窗。打开形态由构造参数决定：
//   courseId 非空（classId 空）→ 只编辑该门课程（课程名/开课学院）；
//   classId  非空            → 编辑该教学班（教师/计划人数/最大容量）及其所属课程，
//                               表单复用共享的 ClassInfoForm；
//   两者皆空                 → 浏览形态：顶部「课程 / 教学班」两个页签选目标，下方编辑。
// 只读数据源；本弹窗不做任何落库，OK 后调用方读 result() 统一决策（含 undo 入环）。
class EditInfoDialog : public QDialog
{
    Q_OBJECT

public:
    // 本次"想改成什么"：仅含确有变化的实体；courseEdited=只改课程，classEdited=改教学班
    // （两者可同时为真——在班级形态里连带改了所属课程）。
    struct Result {
        bool courseEdited = false;
        Course course;            // courseEdited 为真时有效（含 id 等全字段）
        bool classEdited = false;
        TeachingClass klass;      // classEdited 为真时有效
    };

    EditInfoDialog(const DataStore &store, const QString &courseId,
                   const QString &classId, QWidget *parent = nullptr);

    Result result() const;        // 校验通过后调用；仅含确有变化的实体

private slots:
    void onOkClicked();           // 校验当前目标并 accept（结果写回 m_result）

private:
    QWidget *buildCoursePage();   // 课程编辑页（浏览模式含课程下拉；钉选模式只表单）
    QWidget *buildClassPage();    // 教学班编辑页（浏览模式含分组树 + 共享 ClassInfoForm）
    void setupBrowse();           // 浏览模式：两页签 + 选中联动 populate
    void populateCourse();        // 按 m_curCourseId 填充课程字段
    void pickCourse(const QString &courseId);   // 切到课程目标并 populate
    void pickClass(const QString &classId);     // 切到班级目标并 populate（委托 ClassInfoForm）
    void warn(const QString &text) const;       // 校验失败提示（标题固定）

    const DataStore &m_store;
    QString m_pinCourseId;        // 钉选课程 id（非空 = 课程形态）
    QString m_pinClassId;         // 钉选教学班 id（非空 = 班级形态）
    QString m_curCourseId;        // 当前编辑目标课程 id（浏览随选中更新）
    QString m_curClassId;         // 当前编辑目标教学班 id（空 = 课程形态）
    Result  m_result;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_coursePage = nullptr;
    QWidget *m_classPage = nullptr;

    // —— 课程页字段 ——
    QComboBox *m_courseCombo = nullptr;   // 浏览模式课程下拉（行数据 = courseId）
    QLabel    *m_courseIdCaption = nullptr;   // "课程号 C01"
    QLineEdit *m_courseNameEdit = nullptr;
    QLineEdit *m_courseDepartEdit = nullptr;

    // —— 班级页 ——
    QTreeWidget *m_classTree = nullptr;   // 浏览模式分组树（叶数据 = classId）
    ClassInfoForm *m_infoForm = nullptr;  // 共享教学班基本信息表单（钉选/浏览班级页共用）
};

#endif // EDITINFODIALOG_H
