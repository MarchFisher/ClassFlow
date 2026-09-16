#ifndef SCHEDULEFILTER_H
#define SCHEDULEFILTER_H

#include <QHash>
#include <QSet>
#include <QString>

#include "core/models/models.h"

// 排课筛选条件：三个维度（教师 / 教室 / 课程），空集合表示"不限"。
// 语义：维度间 AND（同时满足教师 ∩ 教室 ∩ 课程），维度内 OR（选中任一人即命中），
// 课程组空 = 全选。筛选是纯数据，由 core/filter 的 matchesFilter 判定。
struct ScheduleFilter {
    QSet<QString> teacherIds;     // 空 = 不限教师
    QSet<QString> classroomIds;   // 空 = 不限教室
    QSet<QString> courseIds;      // 空 = 全选课程

    bool isEmpty() const          // 三维度都空 → 不过滤（显示全部）
    {
        return teacherIds.isEmpty() && classroomIds.isEmpty() && courseIds.isEmpty();
    }
};

// 判断一条排课条目是否通过筛选条件（纯判定，不依赖 UI 与数据层）。
// courseOfClass: 教学班 id → 课程 id 查找表（由调用方从数据构建）。
bool matchesFilter(const ScheduleFilter &filter, const ScheduleEntry &entry,
                   const QHash<QString, QString> &courseOfClass);

#endif // SCHEDULEFILTER_H
