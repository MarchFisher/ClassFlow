/**
 * 文件职责：教务手动调整的整批校验与原子落库实现。把一批「只动 时间+教室」的
 * 最小变更（manual::Change）逐条反查旧条目、回填 classId/teacherId/周范围/跨度
 * 拼出完整新条目，先做硬约束校验（H1~H5 + 范围），全程只读；apply 先校验再
 * 逐条 updateScheduleEntry 原子落库。与排课引擎解耦：不查软约束、不改周范围、
 * 不改教师、不动锁定、不经线程/退火。
 */

#include "manualmove.h"

#include <QSet>

#include "core/schedule/conflicttable.h"
#include "core/schedule/roomrules.h"
#include "core/store/datastore.h"

namespace {

// 单条 Change 的解析结果
struct Attempt {
    bool ok = false;
    manual::Reject reject = manual::Reject::None;
    ScheduleEntry effective;        // ok=true 时为新条目内容
};

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
makeAttempt - 解析单条 Change：反查旧条目回填并做静态校验

Parameter：
    store: 数据仓库（只读）
    c: 最小变更

Result:
    Attempt: ok=false 时 reject 为具体原因（教室/容量/类型/范围/缺失）

Remark:
    静态校验（教室存在、H4 容量、H5 类型、星期/节次范围）先于冲突校验，
    让容量/类型类错误优先返回、可读性更好。单次课跨度取自旧条目
    end-start+1，周范围与教师沿用旧条目，entryId 保持原值不变。
*/
Attempt makeAttempt(const DataStore &store, const manual::Change &c)
{
    Attempt a;
    const ScheduleEntry *old = store.scheduleEntryById(c.entryId);
    if (!old) { a.reject = manual::Reject::EntryMissing; return a; }

    const TeachingClass *tc = store.teachingClassById(old->teachingClassId);
    const Course *course = tc ? store.courseById(tc->courseId) : nullptr;
    if (!tc || !course) { a.reject = manual::Reject::EntryMissing; return a; }

    const Classroom *room = store.classroomById(c.classroomId);
    if (!room) { a.reject = manual::Reject::RoomMissing; return a; }

    if (!roomrules::capacityOk(room->capacity, tc->plannedSize)) {  // H4 容量
        a.reject = manual::Reject::CapacityTooSmall;
        return a;
    }
    if (!roomrules::typeOk(room->type, course->requiredRoomType)) {  // H5 类型
        a.reject = manual::Reject::RoomTypeMismatch;
        return a;
    }

    const int span = old->timeSlot.endSection - old->timeSlot.startSection + 1;
    const int maxSection = maxSectionOf(store);
    if (c.dayOfWeek < 1 || c.dayOfWeek > 7 || c.startSection < 1
        || maxSection < 1 || c.startSection + span - 1 > maxSection) {
        a.reject = manual::Reject::BadRange;
        return a;
    }

    ScheduleEntry e = *old;                 // 保留 entryId/教学班/教师/周范围
    e.classroomId = c.classroomId;
    e.timeSlot.dayOfWeek = c.dayOfWeek;
    e.timeSlot.startSection = c.startSection;
    e.timeSlot.endSection = c.startSection + span - 1;   // 保持单次课跨度
    a.ok = true;
    a.effective = e;
    return a;
}

/*
fillReport - 把静态失败原因写进 Report（index/entryId/day/section）

Parameter：
    rep: 待填充的报告
    index: 出问题的 Change 下标
    c: 该条 Change
*/
void fillReport(manual::Report &rep, int index, const manual::Change &c)
{
    rep.index = index;
    rep.entryId = c.entryId;
    rep.day = c.dayOfWeek;
    rep.section = c.startSection;
}

/*
resolveAll - 解析整批 Change：查重 + 逐条静态校验

Parameter：
    store: 数据仓库（只读）
    changes: 待校验的整批变更
    attempts: 输出解析结果（仅全部通过时有效）

Result:
    bool: 全部解析通过返回 true；否则 false 且 rep 填为失败原因

Remark:
    批内重复 entryId（结果将只保留最后一次）视为非法直接拒绝；
    该异常由调用方 UI 的两模式（每行一个 id）天然避免，此处仅防御。
*/
bool resolveAll(const DataStore &store, const QVector<manual::Change> &changes,
                QVector<Attempt> *attempts, manual::Report *rep)
{
    attempts->reserve(changes.size());
    QSet<QString> seen;
    for (int i = 0; i < changes.size(); ++i) {
        const manual::Change &c = changes.at(i);
        if (seen.contains(c.entryId)) {
            rep->reject = manual::Reject::Conflict;   // 语义异常，非时间冲突
            fillReport(*rep, i, c);
            return false;
        }
        seen.insert(c.entryId);

        const Attempt a = makeAttempt(store, c);
        if (!a.ok) {
            rep->reject = a.reject;
            fillReport(*rep, i, c);
            return false;
        }
        attempts->append(a);
    }
    return true;
}

} // namespace

namespace manual {

Report validate(const DataStore &store, const QVector<Change> &changes)
{
    Report rep;
    QVector<Attempt> attempts;
    if (!resolveAll(store, changes, &attempts, &rep))
        return rep;

    // 冲突校验：背景 = 全量条目剔除本批将被替换的旧条目
    ConflictTable table;
    QSet<QString> replacedIds;
    for (const Attempt &a : attempts)
        replacedIds.insert(a.effective.entryId);
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (!replacedIds.contains(e.entryId))
            table.place(e);

    // 新条目逐条 canPlace→place：批内自撞也会被后续条目命中
    for (int i = 0; i < attempts.size(); ++i) {
        const ScheduleEntry &eff = attempts.at(i).effective;
        if (!table.canPlace(eff)) {
            rep.reject = Reject::Conflict;
            rep.index = i;
            rep.entryId = changes.at(i).entryId;
            rep.day = eff.timeSlot.dayOfWeek;
            rep.section = eff.timeSlot.startSection;
            rep.busyTags = table.busyKeysOf(eff);
            return rep;
        }
        table.place(eff);
    }

    rep.ok = true;
    rep.reject = Reject::None;
    return rep;
}

bool apply(DataStore &store, const QVector<Change> &changes)
{
    QVector<Attempt> attempts;
    Report rep;
    if (!resolveAll(store, changes, &attempts, &rep))
        return false;                       // 校验失败：不写库

    // 冲突校验与 validate 同源（modal 单线程下 validate→apply 间无第三方写入）
    if (!validate(store, changes).ok)
        return false;

    for (const Attempt &a : attempts)
        store.updateScheduleEntry(a.effective);
    return true;
}

} // namespace manual
