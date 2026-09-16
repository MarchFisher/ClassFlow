/**
 * 文件职责：DataStore 实现——内存数据仓库的访问器、清空逻辑、
 * 学期周数与排课失败明细存取。CSV 导入 / 导出见 csv.cpp；工作区快照见 snapshot.cpp；
 * ID 索引查询见 lookup.cpp；类型互转等公共辅助见 utility.cpp。
 */

#include "datastore.h"

/*
DataStore::courses - 取全部课程

Result:
    const QVector<Course>&: 课程列表引用

*/
const QVector<Course> &DataStore::courses() const
{
    return m_courses;
}

/*
DataStore::teachingClasses - 取全部教学班

Result:
    const QVector<TeachingClass>&: 教学班列表引用

*/
const QVector<TeachingClass> &DataStore::teachingClasses() const
{
    return m_teachingClasses;
}

/*
DataStore::classrooms - 取全部教室

Result:
    const QVector<Classroom>&: 教室列表引用

*/
const QVector<Classroom> &DataStore::classrooms() const
{
    return m_classrooms;
}

/*
DataStore::sections - 取全部作息表节次

Result:
    const QVector<Section>&: 节次列表引用

*/
const QVector<Section> &DataStore::sections() const
{
    return m_sections;
}

/*
DataStore::teachers - 取教师表

Result:
    const QVector<TeacherInfo>&: 教师列表引用（未提供教师 CSV 时为推导值）
*/
const QVector<TeacherInfo> &DataStore::teachers() const
{
    return m_teachers;
}

/*
DataStore::scheduleEntries - 取全部排课结果

Result:
    const QVector<ScheduleEntry>&: 排课结果列表引用

*/
const QVector<ScheduleEntry> &DataStore::scheduleEntries() const
{
    return m_scheduleEntries;
}

/*
DataStore::semesterWeeks - 取学期总周数

Result:
    int: 学期总周数（默认 16）

*/
int DataStore::semesterWeeks() const
{
    return m_semesterWeeks;
}

/*
DataStore::setSemesterWeeks - 设置学期总周数

Parameter：
    weeks: 新的学期总周数

*/
void DataStore::setSemesterWeeks(int weeks)
{
    m_semesterWeeks = weeks;
}

/*
DataStore::clear - 清空仓库内全部数据

Remark:
    学期总周数一并重置为默认值 16；ID 索引一并失效（见 lookup.cpp）。
*/
void DataStore::clear()
{
    invalidateLookupIndexes();
    m_courses.clear();
    m_teachingClasses.clear();
    m_classrooms.clear();
    m_sections.clear();
    m_teachers.clear();
    m_scheduleEntries.clear();
    m_scheduleFailures.clear();
    m_lockedClasses.clear();
    m_loadWarnings.clear();
    m_semesterWeeks = 16;
}

/*
DataStore::loadWarnings - 取快照加载期间收集的一致性告警

Result:
    const QVector<QString>&: 告警文本列表（空 = 无不一致）

Remark:
    由 loadSnapshot 填充（[TeachingClasses] 内嵌课程列与权威 [Courses] 段比对），
    clear() 清空；不阻断加载。
*/
const QVector<QString> &DataStore::loadWarnings() const
{
    return m_loadWarnings;
}

/*
DataStore::addScheduleEntry - 追加一条排课结果

Parameter：
    entry: 要追加的排课条目

Remark:
    尾插不搬动既有条目下标（快照/单元格卡序由此保持）；若懒建索引已生效，
    只需把新条目 id → 末尾下标补进 m_entryIndex，旧映射继续有效。
*/
void DataStore::addScheduleEntry(const ScheduleEntry &entry)
{
    m_scheduleEntries.append(entry);
    if (m_indexesBuilt)
        m_entryIndex.insert(entry.entryId, m_scheduleEntries.size() - 1);
}

/*
DataStore::clearScheduleEntries - 仅清空排课结果

Remark:
    重新排课前调用；不影响课程 / 教学班 / 教室 / 作息表。
    清空属"整表重建"，失效全部懒建索引（见 lookup.cpp）——否则条目索引
    会残留指向已清空/被重排后新条目的旧下标。
*/
void DataStore::clearScheduleEntries()
{
    m_scheduleEntries.clear();
    invalidateLookupIndexes();
}

/*
DataStore::scheduleEntryById - 按 entryId 查单条排课条目

Parameter：
    entryId: 排课条目 id

Result:
    const ScheduleEntry*: 命中条目指针；不存在返回 nullptr

Remark:
    走懒建索引 O(1)（课程/班/教师/教室/条目五表同批构建，见 lookup.cpp）；
    供教务手动调整定位/刷新用。
*/
const ScheduleEntry *DataStore::scheduleEntryById(const QString &entryId) const
{
    ensureLookupIndexes();
    const int i = m_entryIndex.value(entryId, -1);
    return (i >= 0) ? &m_scheduleEntries.at(i) : nullptr;
}

/*
DataStore::updateScheduleEntry - 按 entryId 原位替换一条排课条目

Parameter：
    updated: 新条目内容（entryId 与旧条目一致，视为不透明锚点）

Result:
    bool: 命中并替换返回 true；entryId 不存在返回 false

Remark:
    索引定位 O(1)；原位替换不改下标、entryId 不变 → 条目索引无需失效
    （保住 scheduleEntries() 遍历序 / 快照写出序 / 单元格卡序，整班搬场卡片不漂移）。
*/
bool DataStore::updateScheduleEntry(const ScheduleEntry &updated)
{
    ensureLookupIndexes();
    const int i = m_entryIndex.value(updated.entryId, -1);
    if (i < 0)
        return false;
    m_scheduleEntries[i] = updated;
    return true;
}

/*
DataStore::scheduleFailures - 取排课失败明细

Result:
    const QVector<ScheduleFailure>&: 失败明细列表（教学班/课程/原因）
*/
const QVector<ScheduleFailure> &DataStore::scheduleFailures() const
{
    return m_scheduleFailures;
}

/*
DataStore::setScheduleFailures - 设置排课失败明细（每次排课后由 Scheduler 写入）

Parameter：
    failures: 本次排课的失败明细列表
*/
void DataStore::setScheduleFailures(const QVector<ScheduleFailure> &failures)
{
    m_scheduleFailures = failures;
}

/*
DataStore::clearScheduleFailures - 清空排课失败明细

Remark:
    重新排课前调用，与 clearScheduleEntries 配套。
*/
void DataStore::clearScheduleFailures()
{
    m_scheduleFailures.clear();
}

/*
DataStore::addCourse - 追加一门课程模板（id 唯一校验）

Parameter：
    course: 要新增的课程

Result:
    bool: 追加成功返回 true；id 为空或与现有课程重复返回 false

Remark:
    新增会失效 ID 索引（下次查询懒重建）。
*/
bool DataStore::addCourse(const Course &course)
{
    if (course.id.isEmpty())
        return false;
    for (const Course &c : m_courses)
        if (c.id == course.id)
            return false;
    m_courses.append(course);
    invalidateLookupIndexes();
    return true;
}

/*
DataStore::addTeachingClass - 追加一个教学班（classId 唯一校验）

Parameter：
    tc: 要新增的教学班

Result:
    bool: 追加成功返回 true；classId 为空或与现有教学班重复返回 false

Remark:
    新增会失效 ID 索引（下次查询懒重建）。
*/
bool DataStore::addTeachingClass(const TeachingClass &tc)
{
    if (tc.classId.isEmpty())
        return false;
    for (const TeachingClass &t : m_teachingClasses)
        if (t.classId == tc.classId)
            return false;
    m_teachingClasses.append(tc);
    invalidateLookupIndexes();
    return true;
}

/*
DataStore::dropClassRecords - 排课条目 / 失败明细 / 锁定记录过滤掉某教学班

Parameter：
    classId: 要被清除记录的教学班 id

Remark:
    只清与该班相关的三类记录，不改 m_teachingClasses / m_courses；
    删除班本身前调用，保证课表与错误列表里不留指向已删班的悬空条目。
*/
void DataStore::dropClassRecords(const QString &classId)
{
    QVector<ScheduleEntry> keptEntries;
    keptEntries.reserve(m_scheduleEntries.size());
    for (const ScheduleEntry &e : m_scheduleEntries)
        if (e.teachingClassId != classId)
            keptEntries.append(e);
    m_scheduleEntries = keptEntries;

    QVector<ScheduleFailure> keptFailures;
    keptFailures.reserve(m_scheduleFailures.size());
    for (const ScheduleFailure &f : m_scheduleFailures)
        if (f.classId != classId)
            keptFailures.append(f);
    m_scheduleFailures = keptFailures;

    m_lockedClasses.remove(classId);

    invalidateLookupIndexes();   // 条目下标整体变动；调用方 remove* 亦会失效，幂等无妨
}

/*
DataStore::removeTeachingClass - 删除一个教学班（含其全部记录；课程保留，允许空课程）

Parameter：
    classId: 要删除的教学班 id

Result:
    bool: 删除成功返回 true；id 不存在返回 false

Remark:
    先清该班的排课条目 / 失败明细 / 锁定，再删班行。课程与教学班是两个独立实体，
    删班不删所属课程——即使删成空课程也保留；删整门课请用 removeCourse。
*/
bool DataStore::removeTeachingClass(const QString &classId)
{
    int idx = -1;
    for (int i = 0; i < m_teachingClasses.size(); ++i)
        if (m_teachingClasses.at(i).classId == classId) { idx = i; break; }
    if (idx < 0)
        return false;

    dropClassRecords(classId);
    m_teachingClasses.removeAt(idx);

    invalidateLookupIndexes();
    return true;
}

/*
DataStore::removeCourse - 删除整门课及其全部教学班

Parameter：
    courseId: 要删除的课程 id

Result:
    bool: 删除成功返回 true；id 不存在返回 false

Remark:
    收集该课全部教学班，逐个清记录与锁定后整体移除班行，再删课程模板；
    一次调用对单门课原子生效，供详情弹窗"删除整门课"使用。
*/
bool DataStore::removeCourse(const QString &courseId)
{
    int ci = -1;
    for (int i = 0; i < m_courses.size(); ++i)
        if (m_courses.at(i).id == courseId) { ci = i; break; }
    if (ci < 0)
        return false;

    QVector<QString> classIds;
    for (const TeachingClass &t : m_teachingClasses)
        if (t.courseId == courseId)
            classIds.append(t.classId);
    for (const QString &cid : classIds)
        dropClassRecords(cid);

    QVector<TeachingClass> keptClasses;
    keptClasses.reserve(m_teachingClasses.size());
    for (const TeachingClass &t : m_teachingClasses)
        if (t.courseId != courseId)
            keptClasses.append(t);
    m_teachingClasses = keptClasses;

    m_courses.removeAt(ci);
    invalidateLookupIndexes();
    return true;
}

/*
DataStore::lockedClassIds - 取全部锁定的教学班 id

Result:
    const QSet<QString>&: 锁定集合引用
*/
const QSet<QString> &DataStore::lockedClassIds() const
{
    return m_lockedClasses;
}

/*
DataStore::isClassLocked - 判断某教学班是否被锁定

Parameter：
    classId: 教学班 id

Result:
    bool: 已锁定返回 true
*/
bool DataStore::isClassLocked(const QString &classId) const
{
    return m_lockedClasses.contains(classId);
}

/*
DataStore::lockClass - 锁定一个教学班（幂等）

Parameter：
    classId: 教学班 id
*/
void DataStore::lockClass(const QString &classId)
{
    m_lockedClasses.insert(classId);
}

/*
DataStore::unlockClass - 解锁一个教学班（幂等）

Parameter：
    classId: 教学班 id
*/
void DataStore::unlockClass(const QString &classId)
{
    m_lockedClasses.remove(classId);
}

/*
DataStore::setLockedClasses - 整体替换锁定集合（锁定弹窗确认后写回）

Parameter：
    classIds: 新的锁定教学班集合
*/
void DataStore::setLockedClasses(const QSet<QString> &classIds)
{
    m_lockedClasses = classIds;
}
