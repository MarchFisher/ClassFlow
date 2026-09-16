/**
 * 文件职责：DataStore 基本信息编辑实现——课程模板 / 教学班字段的
 * 原位改写、整班换任课老师（连带已排条目教师副本）、清除某班失败/登记项，以及
 * 换师撞车预检。独立成文件以保持 datastore.cpp 精简（同类拆分还有 csv / snapshot /
 * lookup / utility）。
 */

#include "datastore.h"

/*
DataStore::updateCourse - 按课程 id 原位改写课程模板字段

Parameter：
    updated: 新的课程内容（id 与现有课程一致，仅模板字段变化）

Result:
    bool: 命中并改写返回 true；id 不存在返回 false

Remark:
    不改 id、不改教学班结构；原位替换不改行序与下标 → ID 索引无需失效。
    改名后课表卡片显示由 UI 层触发 refresh 即时生效，本方法不涉及排课。
*/
bool DataStore::updateCourse(const Course &updated)
{
    for (int i = 0; i < m_courses.size(); ++i) {
        if (m_courses.at(i).id == updated.id) {
            m_courses[i] = updated;
            return true;
        }
    }
    return false;
}

/*
DataStore::updateTeachingClass - 按教学班 id 原位改写教学班字段

Parameter：
    updated: 新的教学班内容（classId 与现有班一致，仅字段变化）

Result:
    bool: 命中并改写返回 true；id 不存在返回 false

Remark:
    不改 classId；原位替换不改行序与下标 → ID 索引无需失效。
    改人数/容量是否会令现有落位失效，由调用方自行决策，
    本方法只负责落库字段本身。
*/
bool DataStore::updateTeachingClass(const TeachingClass &updated)
{
    for (int i = 0; i < m_teachingClasses.size(); ++i) {
        if (m_teachingClasses.at(i).classId == updated.classId) {
            m_teachingClasses[i] = updated;
            return true;
        }
    }
    return false;
}

/*
DataStore::reassignClassTeacher - 整班换任课老师（预检通过的"应用路径"收口）

Parameter：
    classId: 教学班 id
    newTeacherId: 新任课老师 id（可为空 = 置为"未安排"）

Result:
    bool: 班存在并改写返回 true；班不存在返回 false

Remark:
    改班 teacherId，并把该班全部已排条目的 teacherId 一并改写（课表卡片显示的是
    条目教师）；条目只动教师字段、不改行序与数量 → ID 索引无需失效。
    换师前是否撞车由调用方先用 teacherSwapClashes 预检，本方法不做判定。
*/
bool DataStore::reassignClassTeacher(const QString &classId, const QString &newTeacherId)
{
    bool found = false;
    for (int i = 0; i < m_teachingClasses.size(); ++i) {
        if (m_teachingClasses.at(i).classId == classId) {
            TeachingClass tc = m_teachingClasses[i];
            tc.teacherId = newTeacherId;
            m_teachingClasses[i] = tc;
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    for (int i = 0; i < m_scheduleEntries.size(); ++i)
        if (m_scheduleEntries.at(i).teachingClassId == classId)
            m_scheduleEntries[i].teacherId = newTeacherId;
    return true;
}

/*
DataStore::removeScheduleFailureForClass - 过滤掉某教学班的失败 / 登记项

Parameter：
    classId: 教学班 id

Result:
    bool: 确有被移除的项返回 true；该班本就无项返回 false

Remark:
    供应用换师成功后清除遗留的"拟换师"冲突登记；删班另有 dropClassRecords 连带过滤。
*/
bool DataStore::removeScheduleFailureForClass(const QString &classId)
{
    bool removed = false;
    QVector<ScheduleFailure> kept;
    kept.reserve(m_scheduleFailures.size());
    for (const ScheduleFailure &f : m_scheduleFailures) {
        if (f.classId == classId) {
            removed = true;
            continue;
        }
        kept.append(f);
    }
    if (!removed)
        return false;
    m_scheduleFailures = kept;
    return true;
}

/*
DataStore::teacherSwapClashes - 换师撞车预检（纯读，不写库）

Parameter：
    classId: 教学班 id
    newTeacherId: 拟换入的任课老师 id

Result:
    bool: 换入后与该班当前各已排条目在教师维相撞返回 true；不撞返回 false

Remark:
    撞车 = 该班某条已排课 (星期, 节次区间, 周区间) 与「其余班中新师已占」的同段课
    时间重叠。重叠口径与引擎 ConflictTable 同一套：同星期 + 节次区间相交 + 周范围
    相交（引擎把周/节逐键展开，等价于本判定）。空 teacherId 视为无占用、永不撞车；
    本班自身条目相互必不重叠（单班已有排课约束），故只与其余班条目比对。
*/
bool DataStore::teacherSwapClashes(const QString &classId, const QString &newTeacherId) const
{
    if (newTeacherId.isEmpty())
        return false;

    // 其余班中由 newTeacherId 占用的条目（本班自身条目除外，自换不撞）
    QVector<const ScheduleEntry *> occupied;
    occupied.reserve(m_scheduleEntries.size());
    for (const ScheduleEntry &f : m_scheduleEntries)
        if (f.teachingClassId != classId && f.teacherId == newTeacherId)
            occupied.append(&f);
    if (occupied.isEmpty())
        return false;

    for (const ScheduleEntry &e : m_scheduleEntries) {
        if (e.teachingClassId != classId)
            continue;
        const int day      = e.timeSlot.dayOfWeek;
        const int secStart = e.timeSlot.startSection;
        const int secEnd   = e.timeSlot.endSection;
        for (const ScheduleEntry *p : occupied) {
            const ScheduleEntry &f = *p;
            if (f.timeSlot.dayOfWeek != day)
                continue;
            if (f.timeSlot.endSection < secStart || secEnd < f.timeSlot.startSection)
                continue;
            if (f.endWeek < e.startWeek || e.endWeek < f.startWeek)
                continue;
            return true;
        }
    }
    return false;
}
