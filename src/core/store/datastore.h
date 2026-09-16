#ifndef DATASTORE_H
#define DATASTORE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include "core/models/models.h"

// 内存数据仓库：课程 / 教学班 / 教室 / 作息表 / 排课结果。
// 作为单一数据源，UI 与排课核心都从它读写。
// 工作区快照（saveSnapshot / loadSnapshot）也在此落盘与恢复。
class DataStore
{
public:
    DataStore() = default;

    // ---- CSV 导入 / 导出（QTextStream 手写解析，不引第三方库）----
    bool loadCsv(const QString &teachingClassPath,
                 const QString &classroomPath,
                 const QString &sectionPath,
                 const QString &teacherPath = QString());
    bool exportCsv(const QString &path) const;

    // 工作区快照：单文件 CSV 分节（教学班 / 教室 / 作息 / 排课结果）
    bool saveSnapshot(const QString &path) const;
    bool loadSnapshot(const QString &path);
    // 快照序列化文本（saveSnapshot 的写盘内容；同一内容逐字节确定，
    // 供 UI 自动保存比对 / 手动保存脏标记复用）
    QString snapshotText() const;

    // ---- 数据访问 ----
    const QVector<Course>        &courses() const;
    const QVector<TeachingClass> &teachingClasses() const;
    const QVector<Classroom>     &classrooms() const;
    const QVector<Section>       &sections() const;
    const QVector<TeacherInfo>   &teachers() const;
    const QVector<ScheduleEntry> &scheduleEntries() const;

    // 学期总周数（默认 16），随快照 [Term] 段持久化
    int semesterWeeks() const;
    void setSemesterWeeks(int weeks);

    // 排课失败明细（教学班 + 原因），随快照 [ScheduleErrors] 段持久化
    const QVector<ScheduleFailure> &scheduleFailures() const;
    void setScheduleFailures(const QVector<ScheduleFailure> &failures);
    void clearScheduleFailures();

    // 快照加载告警（loadSnapshot 期间检测到的不一致，如 [TeachingClasses] 内嵌课程列
    // 与权威 [Courses] 段不符）；不阻断加载，供 UI / 测试读取
    const QVector<QString> &loadWarnings() const;

    // ---- ID 索引查询（O(1)，懒建；nullptr = 未找到）----
    const Course*        courseById(const QString &id) const;
    const TeachingClass* teachingClassById(const QString &id) const;
    const TeacherInfo*   teacherById(const QString &id) const;
    const Classroom*     classroomById(const QString &id) const;
    // 排课条目单条读取（教务手动调整定位/详情刷新用；并入懒建索引，O(1)）
    const ScheduleEntry *scheduleEntryById(const QString &entryId) const;  // nullptr = 不存在

    void clear();
    void clearScheduleEntries();   // 仅清空排课结果（重新排课前调用）
    void addScheduleEntry(const ScheduleEntry &entry);
    // 按 entryId 原位替换（教务手动调整落库用）；存在返回 true，不存在返回 false
    bool updateScheduleEntry(const ScheduleEntry &updated);

    // ---- 数据增改（编辑功能：新增/删除课程/教学班时用）----
    bool addCourse(const Course &course);              // 课程 id 重复返回 false
    bool addTeachingClass(const TeachingClass &tc);    // 教学班 id 重复返回 false
    // 删除一个教学班：连带清其排课条目 / 失败明细 / 锁定。课程与班相互独立，
    // 删班不删课（允许课程变成 0 班）；删整门课请用 removeCourse
    bool removeTeachingClass(const QString &classId);  // 不存在返回 false
    // 删除整门课：删课程模板及全部教学班（含各自的排课条目 / 失败明细 / 锁定）
    bool removeCourse(const QString &courseId);        // 不存在返回 false

    // ---- 基本信息编辑（课程/教学班原位改写 + 整班换师）----
    // 原位改写课程模板 / 教学班（按 id 定位）；id 不存在返回 false。只动字段、
    // 不改行序与 id → ID 索引无需失效；改名后课表显示由 UI 层 refresh 生效。
    bool updateCourse(const Course &updated);
    bool updateTeachingClass(const TeachingClass &updated);
    // 整班换任课老师（预检通过的"应用路径"收口）：改班 teacherId 并连带改写该班
    // 全部已排条目的 teacherId（保时间/教室/周范围）。班不存在返回 false。
    bool reassignClassTeacher(const QString &classId, const QString &newTeacherId);
    // 清除某教学班的失败/登记项（应用换师成功后去掉遗留"拟换师"登记）；无该项返回 false
    bool removeScheduleFailureForClass(const QString &classId);
    // 换师撞车预检（纯读，换师前的决策依据）：新师与该班当前各已排条目时段重叠返回 true
    bool teacherSwapClashes(const QString &classId, const QString &newTeacherId) const;

    // ---- 锁定状态（锁定重排/新增课程最小排时用；随快照 [Locks] 段持久化）----
    // 锁定语义：被锁定的教学班在「局部排课」中原样保留；
    // 未排上课的班即使被锁，重排仍会尝试安排（否则变"永不排"）。
    const QSet<QString> &lockedClassIds() const;       // 全部锁定的教学班 id
    bool isClassLocked(const QString &classId) const;
    void lockClass(const QString &classId);            // 幂等：已锁则无操作
    void unlockClass(const QString &classId);          // 幂等：未锁则无操作
    void setLockedClasses(const QSet<QString> &classIds); // 整体替换（锁定弹窗确认后写回）

private:
    void invalidateLookupIndexes();             // 整表重建后清空索引（clear/clearScheduleEntries/dropClassRecords 内调用）
    void ensureLookupIndexes() const;           // 懒建五张 id → 下标表（课程/班/教师/教室/排课条目，mutable 索引）
    void dropClassRecords(const QString &classId);  // 排课条目/失败明细/锁定过滤掉某班

    QVector<Course>          m_courses;
    QVector<TeachingClass>   m_teachingClasses;
    QVector<Classroom>       m_classrooms;
    QVector<Section>         m_sections;
    QVector<TeacherInfo>     m_teachers;
    QVector<ScheduleEntry>   m_scheduleEntries;
    QVector<ScheduleFailure> m_scheduleFailures;
    QSet<QString>            m_lockedClasses;         // 锁定的教学班 id（[Locks] 段持久化）
    int                      m_semesterWeeks = 16;   // 学期总周数
    QVector<QString>         m_loadWarnings;        // 快照加载告警（loadSnapshot 收集，clear 清空）

    mutable QHash<QString, int> m_courseIndex;       // 课程 id → 下标
    mutable QHash<QString, int> m_classIndex;        // 教学班 id → 下标
    mutable QHash<QString, int> m_teacherIndex;      // 教师 id → 下标
    mutable QHash<QString, int> m_roomIndex;         // 教室号 → 下标
    mutable QHash<QString, int> m_entryIndex;        // 排课条目 entryId → 下标（尾插不搬下标，可增量维护）
    mutable bool m_indexesBuilt = false;             // 索引是否已建（懒建标记）
};

#endif // DATASTORE_H
