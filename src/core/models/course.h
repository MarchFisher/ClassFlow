#ifndef MODELS_COURSE_H
#define MODELS_COURSE_H

#include <QString>

#include "classroom.h"

// 课程体系：课程模板 与 教学班。

struct Course {                      // 课程模板
    QString id;
    QString name;
    double  credit          = 0.0;   // 学分
    int     sessionsPerWeek = 0;     // 每周课程次数
    double  hoursPerSession = 0.0;   // 单次课所需学时
    QString depart;                  // 开课学院
    int     startWeek       = 1;     // 起始周
    int     endWeek         = 16;    // 结束周（默认整学期）
    ClassroomType requiredRoomType = ClassroomType::Any;  // 所需教室类型（Any = 不限，H5）
};

struct TeachingClass {               // 教学班 = 课程的一个具体班次
    QString classId;
    QString courseId;                // 组合：关联 Course.id
    QString teacherId;               // 关联教师（可为空，未指定教师也允许排课）
    int     plannedSize = 0;         // 排课时预期人数（约束教室容量）
    int     maxCapacity = 0;         // 最大容纳人数（选课上限）
    // 注意：不含 classroom、不含授课时间 —— 它们属于 ScheduleEntry
};

#endif // MODELS_COURSE_H
