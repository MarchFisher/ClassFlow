/**
 * 文件职责：「从零排入」的整批校验与原子落库实现（manualadd）。
 * 给一个"0 现存课次"的失败教学班新增 N 次课（N = Course.sessionsPerWeek）：每次课
 * 由调用方给出 星期/起始节/教室，本模块按课程模板推导 teacherId / 周范围 / 单次课
 * 跨度并生成 entryId = classId#序号，先做硬约束校验（H1~H5 + 范围 + 批内自撞），
 * 全部通过才逐条 addScheduleEntry 并把该班移出失败列表。与 manualmove 共享
 * manual::Report / manual::Reject 语义：静态失败（教室/容量/类型/范围/前提）先于
 * 冲突校验返回，冲突时用 ConflictTable::busyKeysOf 细分 R/C/T，UI 复用 describeReject。
 * 注意与贪心保持同源口径：单次学时须为 ≥1 的整数节（strategy.cpp 同款谓词），
 * entryId 规则同 strategy.cpp：一个教学班的课次序号从 1 递增（本班 0 条目 → 1..N）。
 */

#include "manualadd.h"

#include <QSet>

#include "core/schedule/conflicttable.h"
#include "core/schedule/roomrules.h"
#include "core/store/datastore.h"

namespace {

/*
hoursToSpan - 单次学时（double）→ 连续节数；仅接受 ≥1 的整数（与贪心同源判据）

Parameter：
    hours: Course.hoursPerSession（double）
    span: 输出换算后的连续节数

Result:
    bool: 学时合法返回 true；非整数 / 小于 1 节返回 false
*/
bool hoursToSpan(double hours, int *span)
{
    if (hours < 1.0 || qAbs(hours - qRound(hours)) > 1e-9)
        return false;
    *span = int(qRound(hours));
    return true;
}

/*
maxSectionOf - 取作息表最大节次号

Parameter：
    store: 数据仓库（作息表）

Result:
    int: 最大节次号；作息表为空返回 0
*/
int maxSectionOf(const DataStore &store)
{
    int maxSection = 0;
    for (const Section &s : store.sections())
        maxSection = qMax(maxSection, s.index);
    return maxSection;
}

/*
fillReport - 把静态失败原因写进 Report（index/entryId/day/section）

Parameter：
    rep: 待填充的报告
    index: 出问题的槽位下标（add 行无真实 entryId，entryId 留空由 UI 只显示行号）
    c: 该槽位（提供 day/section 定位）
*/
void fillReport(manual::Report &rep, int index, const manual::AddSlot &c)
{
    rep.index = index;
    rep.entryId = QString();
    rep.day = c.dayOfWeek;
    rep.section = c.startSection;
}

/*
resolveAdd - 解析整批槽位：前提 / 学时 / 静态校验 + 拼有效条目

Parameter：
    store: 数据仓库（只读）
    classId: 目标教学班（须 0 现存课次）
    adds: 待新增课槽位（须恰为 N 条）
    effective: 输出有效新条目（仅全部通过时有效）
    rep: 失败原因（ok=false 时有效）

Result:
    bool: 全部通过返回 true

Remark:
    失败顺序设计：教学班/课程缺失 → 已有课次（前提）→ 学时非法（无法表达跨度）→
    指定次数不符（防御）→ 静态（教室/容量/类型/范围）→ 冲突交由调用方另表判定。
*/
bool resolveAdd(const DataStore &store, const QString &classId,
                const QVector<manual::AddSlot> &adds,
                QVector<ScheduleEntry> *effective, manual::Report *rep)
{
    const TeachingClass *tc = store.teachingClassById(classId);
    const Course *course = tc ? store.courseById(tc->courseId) : nullptr;
    if (!tc || !course) {
        rep->reject = manual::Reject::EntryMissing;
        rep->entryId = classId;
        return false;
    }

    // 前提：从零排入只对 0 课次的班（失败班）有意义；已有课次请走 manualmove
    for (const ScheduleEntry &e : store.scheduleEntries()) {
        if (e.teachingClassId == classId) {
            rep->reject = manual::Reject::AlreadyPlaced;
            rep->entryId = classId;
            return false;
        }
    }

    // 学时非法（非整数 / 小于 1 节）或每周次数非法 → 无法表达跨度，UI 无从排入
    int span = 1;
    if (course->sessionsPerWeek < 1
        || !hoursToSpan(course->hoursPerSession, &span)) {
        rep->reject = manual::Reject::InvalidHours;
        rep->entryId = classId;
        return false;
    }

    const int need = course->sessionsPerWeek;
    if (int(adds.size()) != need) {          // 防御：需全部 N 次课一起排入才落库
        rep->reject = manual::Reject::SlotCountMismatch;
        rep->entryId = classId;
        return false;
    }

    const int maxSection = maxSectionOf(store);
    for (int i = 0; i < adds.size(); ++i) {
        const manual::AddSlot &c = adds.at(i);
        const Classroom *room = store.classroomById(c.classroomId);
        if (!room) {
            rep->reject = manual::Reject::RoomMissing;
            fillReport(*rep, i, c);
            return false;
        }
        if (!roomrules::capacityOk(room->capacity, tc->plannedSize)) {  // H4 容量
            rep->reject = manual::Reject::CapacityTooSmall;
            fillReport(*rep, i, c);
            return false;
        }
        if (!roomrules::typeOk(room->type, course->requiredRoomType)) {  // H5 类型
            rep->reject = manual::Reject::RoomTypeMismatch;
            fillReport(*rep, i, c);
            return false;
        }
        if (c.dayOfWeek < 1 || c.dayOfWeek > 7 || c.startSection < 1
            || maxSection < 1 || c.startSection + span - 1 > maxSection) {
            rep->reject = manual::Reject::BadRange;
            fillReport(*rep, i, c);
            return false;
        }

        ScheduleEntry e;
        e.entryId          = classId + '#' + QString::number(i + 1);  // 0 条目 → 1..N
        e.teachingClassId  = classId;
        e.teacherId        = tc->teacherId;
        e.classroomId      = c.classroomId;
        e.timeSlot.dayOfWeek   = c.dayOfWeek;
        e.timeSlot.startSection = c.startSection;
        e.timeSlot.endSection   = c.startSection + span - 1;
        e.startWeek = course->startWeek;      // 周范围沿用课程模板（不可由 UI 改）
        e.endWeek   = course->endWeek;
        effective->append(e);
    }
    return true;
}

} // namespace

namespace manual {

Report validateAdd(const DataStore &store, const QString &classId,
                   const QVector<AddSlot> &adds)
{
    Report rep;
    QVector<ScheduleEntry> effective;
    if (!resolveAdd(store, classId, adds, &effective, &rep))
        return rep;

    // 冲突校验：背景 = 全量条目（本班 0 课次，无需剔除自身）
    ConflictTable table;
    for (const ScheduleEntry &e : store.scheduleEntries())
        table.place(e);

    // 新条目逐条 canPlace→place：批内自撞（同班两次课同一时段）也会被后续条目命中
    for (int i = 0; i < effective.size(); ++i) {
        const ScheduleEntry &e = effective.at(i);
        if (!table.canPlace(e)) {
            rep.reject = Reject::Conflict;
            rep.index = i;
            rep.entryId = QString();
            rep.day = e.timeSlot.dayOfWeek;
            rep.section = e.timeSlot.startSection;
            rep.busyTags = table.busyKeysOf(e);
            return rep;
        }
        table.place(e);
    }

    rep.ok = true;
    rep.reject = Reject::None;
    return rep;
}

bool applyAdd(DataStore &store, const QString &classId,
              const QVector<AddSlot> &adds)
{
    Report rep;
    QVector<ScheduleEntry> effective;
    if (!resolveAdd(store, classId, adds, &effective, &rep))
        return false;                     // 校验失败：不写库

    if (!validateAdd(store, classId, adds).ok)
        return false;                     // 冲突校验与 validateAdd 同源（单线程无第三方写入）

    for (const ScheduleEntry &e : effective)
        store.addScheduleEntry(e);
    store.removeScheduleFailureForClass(classId);   // 排够 N 次 → 该班移出失败列表
    return true;
}

} // namespace manual
