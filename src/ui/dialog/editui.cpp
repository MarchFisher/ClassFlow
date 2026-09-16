/**
 * 文件职责：基本信息编辑的决策 / 落库助手（editui 命名空间）。
 * applyCourseEdit / applyClassEdit 收口"课程改名即时生效、换师撞车只登记进冲突列表、
 * 扩容超当前教室弹确认（确认后由调用方对该班 runMovable 换大教室）"的落库决策，
 * 供课程详情弹窗与主窗口（浏览「编辑」）共用。本文件不触发排课线程。
 */

#include "editui.h"

#include <QMessageBox>
#include <QStringList>
#include <QVector>

#include "classgrouping.h"
#include "core/store/datastore.h"

// ---------- 匿名命名空间：班级关联的小工具 ----------
namespace {

/* classHasEntries - 该教学班是否已有排课条目 */
bool classHasEntries(const DataStore &store, const QString &classId)
{
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (e.teachingClassId == classId)
            return true;
    return false;
}

/* classCourseName - 教学班所属课程名（查无回退课程号） */
QString classCourseName(const DataStore &store, const QString &classId)
{
    const TeachingClass *tc = store.teachingClassById(classId);
    if (!tc)
        return classId;
    const Course *c = store.courseById(tc->courseId);
    return c ? c->name : tc->courseId;
}

/* removeSwapMarker - 清除某班遗留的"拟换师"登记（reason 前缀识别，保留引擎失败项） */
void removeSwapMarker(DataStore &store, const QString &classId)
{
    QVector<ScheduleFailure> fs = store.scheduleFailures();
    QVector<ScheduleFailure> kept;
    for (const ScheduleFailure &f : fs) {
        if (f.classId == classId
            && f.reason.startsWith(QStringLiteral("拟换任课教师")))
            continue;
        kept.append(f);
    }
    if (kept.size() != fs.size())
        store.setScheduleFailures(kept);
}

/* registerSwapConflict - 登记"拟换师撞车"待办（先清同类旧登记，避免堆积） */
void registerSwapConflict(DataStore &store, const QString &classId,
                          const QString &newTeacherId)
{
    const TeachingClass *tc = store.teachingClassById(classId);
    const QString cid = tc ? tc->courseId : QString();

    QVector<ScheduleFailure> fs = store.scheduleFailures();
    QVector<ScheduleFailure> kept;
    for (const ScheduleFailure &f : fs) {
        if (f.classId == classId
            && f.reason.startsWith(QStringLiteral("拟换任课教师")))
            continue;
        kept.append(f);
    }
    ScheduleFailure f;
    f.classId    = classId;
    f.courseId   = cid;
    f.courseName = classCourseName(store, classId);
    f.reason = QStringLiteral("拟换任课教师 %1，与教学班 %2 当前上课时段冲突"
                              "（尚未应用）。请先把 %2 的时段手动调整到 %1 空闲处，"
                              "再重新执行「编辑→换师」以应用。")
                   .arg(newTeacherId).arg(classId);
    kept.append(f);
    store.setScheduleFailures(kept);
}

} // namespace

/*
editui::applyCourseEdit - 课程模板级改写（课程名 / 开课学院）

Parameter：
    store: 数据仓库（被改写）
    course: 新的课程内容（id 不变，其余字段完整）
    message: 可空；写入状态栏摘要

Result:
    bool: 确有变化并落库返回 true；课程不存在 / 无实际变化返回 false

Remark:
    只动 Course.name / depart，不动教学班与排课；课表显示由调用方 refresh 生效。
*/
bool editui::applyCourseEdit(DataStore &store, const Course &course,
                             QString *message)
{
    const Course *old = store.courseById(course.id);
    if (!old)
        return false;
    const bool nameChanged   = old->name != course.name;
    const bool departChanged = old->depart != course.depart;
    if (!nameChanged && !departChanged)
        return false;
    store.updateCourse(course);
    if (message) {
        QStringList bits;
        if (nameChanged)
            bits << QStringLiteral("课程名 → %1").arg(course.name);
        if (departChanged)
            bits << QStringLiteral("学院 → %1")
                        .arg(course.depart.isEmpty() ? QStringLiteral("（空）")
                                                     : course.depart);
        *message = QStringLiteral("已更新课程「%1（%2）」：%3")
                       .arg(course.name, course.id, bits.join(QStringLiteral("；")));
    }
    return true;
}

/*
editui::applyClassEdit - 教学班字段改写（教师 / 计划人数 / 容量）的预检与落库

Parameter：
    store: 数据仓库（被改写）
    tc: 期望最终值的教学班（含 classId；字段为表单结果）
    promptParent: 容量溢出确认框的父窗口

Result:
    ApplyOutcome: changed/teacherBlocked/roomMoveNeeded/message

Remark:
    换师撞车 → 只登记冲突（不应用换师）；扩容超出该班当前教室容量 → 弹确认框，
    拒绝则不落库。换师成功会顺带清掉旧"拟换师"登记。本函数不触发排课——
    roomMoveNeeded 时由调用方对该班发起局部重排。
*/
editui::ApplyOutcome editui::applyClassEdit(DataStore &store,
                                            const TeachingClass &tc,
                                            QWidget *promptParent)
{
    ApplyOutcome out;
    const TeachingClass *old = store.teachingClassById(tc.classId);
    if (!old)
        return out;                       // 班已被删：无对象可改

    // 摘要文案要"旧值 → 新值"，但落库会原地改写 store 里的班——先快照旧值
    const QString oldTeacherId = old->teacherId;
    const int oldPlanned = old->plannedSize;
    const int oldCap     = old->maxCapacity;

    const QString newTeacher = tc.teacherId.trimmed();
    const bool teacherChanged = oldTeacherId != newTeacher;
    const bool sizeChanged = old->plannedSize != tc.plannedSize
                             || old->maxCapacity != tc.maxCapacity;
    const bool hasEntries = classHasEntries(store, tc.classId);

    // 撞车预检：新师已在该班当前各时段教别的班
    bool clash = false;
    if (teacherChanged && hasEntries && !newTeacher.isEmpty())
        clash = store.teacherSwapClashes(tc.classId, newTeacher);

    // 扩容预检：计划人数超该班当前所在教室容量（任一教室装不下即需换大教室）
    bool overflow = false;
    QStringList smallRooms;               // 已展示过的装不下教室（去重）
    if (sizeChanged && hasEntries && tc.plannedSize > oldPlanned) {
        for (const ScheduleEntry &e : store.scheduleEntries()) {
            if (e.teachingClassId != tc.classId)
                continue;
            const Classroom *room = store.classroomById(e.classroomId);
            if (!room || tc.plannedSize <= room->capacity)
                continue;
            overflow = true;
            const QString tag = QStringLiteral("%1（容量 %2）")
                                    .arg(room->roomNumber).arg(room->capacity);
            if (!smallRooms.contains(tag))
                smallRooms.append(tag);
        }
    }
    if (overflow) {
        const auto ans = QMessageBox::question(
            promptParent, QStringLiteral("编辑教学班"),
            QStringLiteral("计划人数由 %1 改为 %2，超过该班当前所在教室容量：%3。\n"
                           "需为该班重新安排一间能容纳 %2 人的教室，将对该班做一次"
                           "局部重排（其余班与已锁定班原样保留）。是否继续？")
                .arg(oldPlanned).arg(tc.plannedSize)
                .arg(smallRooms.join(QStringLiteral("、"))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            if (clash) {                  // 撞车登记与容量无关：即使放弃扩容仍告知
                registerSwapConflict(store, tc.classId, newTeacher);
                out.teacherBlocked = true;
                out.changed = true;
                out.message = QStringLiteral("未保存容量改动；已登记换师 %1 撞车（未应用）。")
                                  .arg(newTeacher);
            } else {
                out.message = QStringLiteral("未保存改动：计划人数超过当前教室容量，"
                                             "需先为该班安排更大的教室。");
            }
            return out;
        }
        out.roomMoveNeeded = true;
    }

    // —— 落库：教师撞车只登记不换；否则同步班与已排条目 ——
    if (clash) {
        registerSwapConflict(store, tc.classId, newTeacher);
        out.teacherBlocked = true;
    }

    TeachingClass target = *old;
    target.plannedSize = tc.plannedSize;
    target.maxCapacity = tc.maxCapacity;
    const bool applyTeacher = teacherChanged && !clash;
    if (applyTeacher)
        target.teacherId = newTeacher;

    if (applyTeacher && hasEntries)
        store.reassignClassTeacher(tc.classId, newTeacher);   // 班 + 条目同步教师
    const bool fieldChanged = sizeChanged || applyTeacher;
    if (fieldChanged) {
        store.updateTeachingClass(target);
        if (applyTeacher)
            removeSwapMarker(store, tc.classId);              // 应用成功清旧登记
    }
    out.changed = fieldChanged || clash;

    // —— 摘要文案（旧值取上面的快照，落库已改写 store 原位）——
    QStringList bits;
    if (sizeChanged)
        bits << QStringLiteral("人数 %1/%2 → %3/%4")
                    .arg(oldPlanned).arg(oldCap)
                    .arg(target.plannedSize).arg(target.maxCapacity);
    if (applyTeacher)
        bits << QStringLiteral("教师 → %1")
                    .arg(courseui::teacherDisplayName(store, newTeacher));
    if (clash)
        bits << QStringLiteral("换师 %1 撞车，已登记冲突（未应用）").arg(newTeacher);
    if (out.roomMoveNeeded)
        bits << QStringLiteral("将为本班局部重排换大教室");
    out.message = bits.isEmpty()
        ? QStringLiteral("教学班 %1 无改动。").arg(tc.classId)
        : QStringLiteral("已更新教学班 %1：%2")
              .arg(tc.classId, bits.join(QStringLiteral("；")));
    return out;
}
