/**
 * 文件职责：排课筛选核心实现——平行于排课核心 core/schedule。
 * ScheduleFilter 是纯条件数据；matchesFilter 是纯判定，
 * 供课表网格（按教师/教室/课程筛选）、导出、统计等消费方复用。
 */

#include "schedulefilter.h"

/*
matchesFilter - 判断一条排课条目是否通过筛选

Parameter：
    filter: 筛选条件（三维度，空 = 不限）
    entry: 待判定的排课条目
    courseOfClass: 教学班 id → 课程 id 查找表

Result:
    bool: 通过返回 true

Remark:
    维度间 AND、维度内 OR。课程判定经 courseOfClass 把教学班换算成课程号；
    条目教师未指定且教师组非空时必然不命中（不属于任何选中教师）。
*/
bool matchesFilter(const ScheduleFilter &filter, const ScheduleEntry &entry,
                   const QHash<QString, QString> &courseOfClass)
{
    if (!filter.teacherIds.isEmpty() && !filter.teacherIds.contains(entry.teacherId))
        return false;

    if (!filter.classroomIds.isEmpty() && !filter.classroomIds.contains(entry.classroomId))
        return false;

    if (!filter.courseIds.isEmpty()) {
        const QString courseId = courseOfClass.value(entry.teachingClassId);
        if (courseId.isEmpty() || !filter.courseIds.contains(courseId))
            return false;
    }

    return true;
}
