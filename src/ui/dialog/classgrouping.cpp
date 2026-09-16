/**
 * 文件职责：课程/教学班展示类公共小工具实现（namespace courseui）。
 * 收口散落在锁定/删除/详情弹窗与主窗口里的重复逻辑：
 * 按课程分组教学班、课程标签、教师显示名、按课计数。全部只读。
 */

#include "classgrouping.h"

#include "core/models/models.h"
#include "core/store/datastore.h"

/*
courseui::groupClassesByCourse - 按课程顺序收集其下教学班，孤儿班归末尾组

Parameter：
    store: 数据仓库（只读）

Result:
    QVector<CourseGroup>: 每个有教学班的课程一组（course 指针指向 store.courses() 中
    元素，classes 为指向 store.teachingClasses() 元素的指针）；引用不存在的课程号的
    教学班并入末尾 course==nullptr 的「孤儿班」组；无孤儿班时该组不出现。

Remark:
    与旧锁定/删除弹窗的分组算法等价：跳过无教学班的课程，孤儿班单独归组。
*/
QVector<courseui::CourseGroup> courseui::groupClassesByCourse(const DataStore &store)
{
    QVector<CourseGroup> groups;
    QSet<QString> seenCourse;
    for (const Course &c : store.courses()) {
        QVector<const TeachingClass *> classes;
        for (const TeachingClass &tc : store.teachingClasses())
            if (tc.courseId == c.id)
                classes.append(&tc);
        if (classes.isEmpty())
            continue;                 // 空课程不进班列表（删除教学班页只列有班的课）
        CourseGroup g;
        g.course = &c;
        g.classes = classes;
        groups.append(g);
        seenCourse.insert(c.id);
    }
    QVector<const TeachingClass *> dangling;
    for (const TeachingClass &tc : store.teachingClasses())
        if (!seenCourse.contains(tc.courseId))
            dangling.append(&tc);
    if (!dangling.isEmpty()) {
        CourseGroup g;
        g.course = nullptr;
        g.classes = dangling;
        groups.append(g);
    }
    return groups;
}

/*
courseui::classCountOfCourse - 某课程的现有教学班数

Parameter：
    store: 数据仓库（只读）
    courseId: 课程号

Result:
    int: 该课程下的教学班数（0 = 空课程）
*/
int courseui::classCountOfCourse(const DataStore &store, const QString &courseId)
{
    int n = 0;
    for (const TeachingClass &tc : store.teachingClasses())
        if (tc.courseId == courseId)
            ++n;
    return n;
}

/*
courseui::scheduledCountOfCourse - 某课程下已排课的教学班数

Parameter：
    store: 数据仓库（只读）
    courseId: 课程号

Result:
    int: 该课程下已有排课条目的教学班数
*/
int courseui::scheduledCountOfCourse(const DataStore &store, const QString &courseId)
{
    QSet<QString> scheduled;
    for (const ScheduleEntry &e : store.scheduleEntries())
        scheduled.insert(e.teachingClassId);
    int n = 0;
    for (const TeachingClass &tc : store.teachingClasses())
        if (tc.courseId == courseId && scheduled.contains(tc.classId))
            ++n;
    return n;
}

/*
courseui::courseLabel - 统一课程展示格式

Parameter：
    c: 课程

Result:
    QString: "课程名（课程号）"
*/
QString courseui::courseLabel(const Course &c)
{
    return c.name + QStringLiteral("（") + c.id + QStringLiteral("）");
}

/*
courseui::teacherDisplayName - 统一教师显示名

Parameter：
    store: 数据仓库（只读）
    id: 教师 id（可能为空 / 未登记）

Result:
    QString: 空 → "未安排"；查不到 → 原 id；推导表（name==id）→ id；
            否则返回登记姓名
*/
QString courseui::teacherDisplayName(const DataStore &store, const QString &id)
{
    if (id.isEmpty())
        return QStringLiteral("未安排");
    const TeacherInfo *t = store.teacherById(id);
    if (!t)
        return id;
    return (t->name == t->teacherId) ? t->teacherId : t->name;
}
