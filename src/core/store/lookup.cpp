/**
 * 文件职责：DataStore ID 索引查询实现（courseById / teachingClassById / teacherById / classroomById
 * 及 scheduleEntryById / updateScheduleEntry 共用的懒建索引）。
 * 五张 id → 下标哈希懒建（首次查询构建），数据整表重建（clear / clearScheduleEntries /
 * 删班连带清条目）时失效；查询 O(1)，未命中返回 nullptr。
 * 排课条目尾插不搬旧下标，故 addScheduleEntry 只在已建索引上补插新尾巴；
 * updateScheduleEntry 原位替换不改下标 → 索引始终有效。
 */

#include "datastore.h"

/*
DataStore::invalidateLookupIndexes - 清空全部 ID 索引并复位懒建标记

Remark:
    由 clear() / clearScheduleEntries() / add / remove（课程/班整体增删、
    删班连带过滤排课条目）等"下标可能整体变动"的路径调用，
    保证整表重建后索引必然失效、下次查询时重新构建。
*/
void DataStore::invalidateLookupIndexes()
{
    m_courseIndex.clear();
    m_classIndex.clear();
    m_teacherIndex.clear();
    m_roomIndex.clear();
    m_entryIndex.clear();
    m_indexesBuilt = false;
}

/*
DataStore::ensureLookupIndexes - 懒建五张 id → 下标哈希表

Remark:
    const 方法内通过 mutable 索引成员构建；仅首次查询时执行一次，
    此后 m_indexesBuilt 置位不再重建，直到索引被 invalidateLookupIndexes 失效。
*/
void DataStore::ensureLookupIndexes() const
{
    if (m_indexesBuilt)
        return;
    for (int i = 0; i < m_courses.size(); ++i)
        m_courseIndex.insert(m_courses.at(i).id, i);
    for (int i = 0; i < m_teachingClasses.size(); ++i)
        m_classIndex.insert(m_teachingClasses.at(i).classId, i);
    for (int i = 0; i < m_teachers.size(); ++i)
        m_teacherIndex.insert(m_teachers.at(i).teacherId, i);
    for (int i = 0; i < m_classrooms.size(); ++i)
        m_roomIndex.insert(m_classrooms.at(i).roomNumber, i);
    for (int i = 0; i < m_scheduleEntries.size(); ++i)
        m_entryIndex.insert(m_scheduleEntries.at(i).entryId, i);
    m_indexesBuilt = true;
}

/*
DataStore::courseById - 按课程 id 取课程模板

Parameter：
    id: 课程 id

Result:
    const Course*: 命中返回课程模板指针；未找到返回 nullptr
*/
const Course *DataStore::courseById(const QString &id) const
{
    ensureLookupIndexes();
    const int i = m_courseIndex.value(id, -1);
    return (i >= 0) ? &m_courses.at(i) : nullptr;
}

/*
DataStore::teachingClassById - 按教学班 id 取教学班

Parameter：
    id: 教学班 id

Result:
    const TeachingClass*: 命中返回教学班指针；未找到返回 nullptr
*/
const TeachingClass *DataStore::teachingClassById(const QString &id) const
{
    ensureLookupIndexes();
    const int i = m_classIndex.value(id, -1);
    return (i >= 0) ? &m_teachingClasses.at(i) : nullptr;
}

/*
DataStore::teacherById - 按教师 id 取教师信息

Parameter：
    id: 教师 id

Result:
    const TeacherInfo*: 命中返回教师信息指针；未找到返回 nullptr
*/
const TeacherInfo *DataStore::teacherById(const QString &id) const
{
    ensureLookupIndexes();
    const int i = m_teacherIndex.value(id, -1);
    return (i >= 0) ? &m_teachers.at(i) : nullptr;
}

/*
DataStore::classroomById - 按教室号取教室

Parameter：
    id: 教室号

Result:
    const Classroom*: 命中返回教室指针；未找到返回 nullptr
*/
const Classroom *DataStore::classroomById(const QString &id) const
{
    ensureLookupIndexes();
    const int i = m_roomIndex.value(id, -1);
    return (i >= 0) ? &m_classrooms.at(i) : nullptr;
}
