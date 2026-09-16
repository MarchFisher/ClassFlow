#ifndef MODELS_TEACHER_H
#define MODELS_TEACHER_H

#include <QString>

// 教师表（排课域）：一师一行（教师 ID 为唯一键）。
// 教学班 / 排课条目的 teacherId 引用此表；筛选按教师查看课表时供姓名搜索。
// 注意：与 user.h 的登录权限模型 Teacher（uid/courseList）不同名，避免冲突。
struct TeacherInfo {
    QString teacherId;   // 教师 ID（如 T001），唯一
    QString name;        // 教师姓名
    QString depart;      // 所属学院 / 院系
};

#endif // MODELS_TEACHER_H
