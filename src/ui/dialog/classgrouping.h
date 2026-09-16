#ifndef CLASSGROUPING_H
#define CLASSGROUPING_H

#include <QString>
#include <QVector>

class DataStore;
class Course;
class TeachingClass;

// 课程/教学班展示类公共小工具（namespace courseui）。
// 把「按课程分组教学班」「课程标签」「教师显示名」「按课计数」这类在多个弹窗 /
// 主窗口里重复出现的小逻辑收口，保证各处文案与分组顺序一致。
// 全部只读数据源，不修改仓库状态。
namespace courseui {

// 一门课程及其下教学班；course == nullptr 表示「孤儿班」组（引用了不存在的课程号）
struct CourseGroup {
    const Course *course;
    QVector<const TeachingClass *> classes;
};

// 按课程顺序收集其下教学班；无教学班的课程跳过；孤儿班归入末尾 course==nullptr 组。
QVector<CourseGroup> groupClassesByCourse(const DataStore &store);

// 某课程现有教学班数（可为 0 = 空课程）
int classCountOfCourse(const DataStore &store, const QString &courseId);

// 某课程下已排课的教学班数（供删除课程前提示会移除多少条已排课）
int scheduledCountOfCourse(const DataStore &store, const QString &courseId);

// 统一课程展示格式："课程名（课程号）"
QString courseLabel(const Course &c);

// 教师显示名：空 = "未安排"；查不到 = 原 id；推导表（name==id）= 显示 id
QString teacherDisplayName(const DataStore &store, const QString &id);

} // namespace courseui

#endif // CLASSGROUPING_H
