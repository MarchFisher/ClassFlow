#ifndef MODELS_TIMETABLE_H
#define MODELS_TIMETABLE_H

#include <QString>
#include <QTime>

// 时间与排课结果：作息表节次、时间槽、排课条目。

struct Section {                     // 全局作息表：第几节 → 起止时间
    int   index = 0;
    QTime startTime;
    QTime endTime;
};

struct TimeSlot {                    // 一个可排课的时间窗口（排课键：day × section）
    int dayOfWeek     = 1;           // 1..7（周一..周日）
    int startSection  = 0;           // 起始节
    int endSection    = 0;           // 结束节
};

struct ScheduleEntry {               // 一次每周例行"某班在某时在某教室上课"（周范围 startWeek~endWeek）
    QString entryId;
    QString teachingClassId;
    QString teacherId;               // 冗余，便于按教师查询课表
    TimeSlot timeSlot;               // dayOfWeek + startSection~endSection
    QString classroomId;
    int     startWeek      = 1;      // 起始周
    int     endWeek        = 16;     // 结束周（默认整学期）
};

struct ScheduleFailure {             // 排课失败明细：给用户看"为什么排不上、怎么改"
    QString classId;                 // 教学班 ID
    QString courseId;                // 课程 ID
    QString courseName;              // 课程名
    QString reason;                  // 人类可读的失败原因（容量不足 / 类型不符 / 时间冲突）
};

#endif // MODELS_TIMETABLE_H
