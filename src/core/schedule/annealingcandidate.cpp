/**
 * 文件职责：模拟退火内部候选解（Candidate）的核心例程。
 * 候选解 = classId → N 个 Session；本文件负责：从排课条目反建候选、
 * 按序写回 store、以及被邻域操作复用的 entryFor / commit / rollback / deltaOf。
 * 邻域操作 M1~M5 见 annealingmoves.cpp（同一类的成员，可跨翻译单元定义）。
 * Δcost 只算被移动班/课程及受影响槽位/教室（S5/S6 依赖总 load/usage 不变的不变式）。
 */

#include "annealingcandidate.h"

#include <algorithm>

#include "conflicttable.h"
#include "core/store/datastore.h"

/*
Candidate::buildFromEntries - 从排课条目反建候选解

Parameter：
    entries: 排课条目（贪心写回后的 store.scheduleEntries()）
    store: 数据仓库（教室容量/类型、作息表、课程/教学班信息）
*/
void Candidate::buildFromEntries(const QVector<ScheduleEntry> &entries,
                                 const DataStore &store)
{
    m_sessions.clear();
    m_info.clear();
    m_classIds.clear();
    m_classesOfCourse.clear();
    m_courseIds.clear();
    m_roomCapacity.clear();
    m_roomType.clear();
    m_roomsByCapacity.clear();
    m_journalOld.clear();
    m_journalNew.clear();
    m_journalSessions.clear();
    m_locked.clear();
    m_movable.clear();

    for (const Classroom &r : store.classrooms()) {
        m_roomCapacity.insert(r.roomNumber, r.capacity);
        m_roomType.insert(r.roomNumber, r.type);
        m_roomsByCapacity.append(r.roomNumber);
    }
    std::sort(m_roomsByCapacity.begin(), m_roomsByCapacity.end(),
              [this](const QString &a, const QString &b) {
                  return m_roomCapacity.value(a) < m_roomCapacity.value(b);
              });

    m_maxSection = 0;
    for (const Section &s : store.sections())
        m_maxSection = qMax(m_maxSection, s.index);

    QHash<QString, TeachingClass> tcById;
    for (const TeachingClass &tc : store.teachingClasses())
        tcById.insert(tc.classId, tc);
    QHash<QString, Course> courseById;
    for (const Course &c : store.courses())
        courseById.insert(c.id, c);

    QHash<QString, QVector<ScheduleEntry>> byClass;
    for (const ScheduleEntry &e : entries)
        byClass[e.teachingClassId].append(e);
    const QList<QString> keys = byClass.keys();
    m_classIds = QVector<QString>(keys.cbegin(), keys.cend());
    std::sort(m_classIds.begin(), m_classIds.end());

    for (const QString &classId : m_classIds) {
        const QVector<ScheduleEntry> &es = byClass.value(classId);
        QVector<Session> ss;
        ss.reserve(es.size());
        for (const ScheduleEntry &e : es) {
            Session s;
            s.day = e.timeSlot.dayOfWeek;
            s.section = e.timeSlot.startSection;
            s.room = e.classroomId;
            ss.append(s);
        }
        m_sessions.insert(classId, ss);

        const TeachingClass tc = tcById.value(classId);
        const Course course = courseById.value(tc.courseId);
        ClassInfo info;
        info.courseId = tc.courseId;
        info.teacherId = tc.teacherId;
        info.startWeek = es.first().startWeek;      // 周范围取条目（贪心由课程写入）
        info.endWeek = es.first().endWeek;
        info.duration = es.first().timeSlot.endSection
                        - es.first().timeSlot.startSection + 1;
        info.plannedSize = tc.plannedSize;
        info.sessions = int(es.size());
        info.requiredType = course.requiredRoomType;
        m_info.insert(classId, info);
        m_classesOfCourse[tc.courseId].append(classId);
    }
    const QList<QString> courseKeys = m_classesOfCourse.keys();
    m_courseIds = QVector<QString>(courseKeys.cbegin(), courseKeys.cend());
    std::sort(m_courseIds.begin(), m_courseIds.end());
    for (auto it = m_classesOfCourse.begin(); it != m_classesOfCourse.end(); ++it)
        std::sort(it.value().begin(), it.value().end());

    m_locked.clear();               // 刚反建默认全可动；由 SA 层按需 setLockedClasses
    m_movable = m_classIds;
}

/*
Candidate::applyToStore - 把当前候选写回 store（先清空旧结果，条目按序）

Parameter：
    store: 数据仓库（排课结果写回其中）
*/
void Candidate::applyToStore(DataStore &store) const
{
    store.clearScheduleEntries();
    QVector<ScheduleEntry> all;
    for (const QString &classId : m_classIds) {
        const QVector<Session> &sessions = m_sessions.value(classId);
        for (int i = 0; i < sessions.size(); ++i)
            all.append(entryFor(classId, sessions.at(i), i + 1));
    }
    std::sort(all.begin(), all.end(), [](const ScheduleEntry &a, const ScheduleEntry &b) {
        if (a.teachingClassId != b.teachingClassId)
            return a.teachingClassId < b.teachingClassId;
        if (a.timeSlot.dayOfWeek != b.timeSlot.dayOfWeek)
            return a.timeSlot.dayOfWeek < b.timeSlot.dayOfWeek;
        return a.timeSlot.startSection < b.timeSlot.startSection;
    });
    for (const ScheduleEntry &e : all)
        store.addScheduleEntry(e);
}

// ---- 访问器 ----

bool Candidate::empty() const { return m_classIds.isEmpty(); }
int Candidate::classCount() const { return int(m_classIds.size()); }
const QVector<QString> &Candidate::classIds() const { return m_classIds; }
const QVector<QString> &Candidate::courseIds() const { return m_courseIds; }

/*
Candidate::setLockedClasses - 冻结部分教学班，重建可动班抽样列表

Parameter：
    locked: 冻结（锁定）的教学班集合

Remark:
    冻结班会话仍保留在候选里（计入负载/成本，写回时原样），只是不被邻域移动。
*/
void Candidate::setLockedClasses(const QSet<QString> &locked)
{
    m_locked = locked;
    m_movable.clear();
    m_movable.reserve(int(m_classIds.size()));
    for (const QString &cid : m_classIds)
        if (!m_locked.contains(cid))
            m_movable.append(cid);
}

/*
Candidate::movableClassIds - 可动教学班列表（排序），邻域抽样用

Result:
    const QVector<QString>&: 候选内非冻结班的排序列表
*/
const QVector<QString> &Candidate::movableClassIds() const { return m_movable; }

/*
Candidate::movableClassCount - 可动教学班数量

Result:
    int: 非冻结班数量（0 = 全部冻结，无需退火）
*/
int Candidate::movableClassCount() const { return int(m_movable.size()); }

/*
Candidate::isLocked - 某班是否被冻结

Parameter：
    classId: 教学班 id

Result:
    bool: true 表示该班当前被冻结、不会被移动
*/
bool Candidate::isLocked(const QString &classId) const
{
    return m_locked.contains(classId);
}
int Candidate::maxSection() const { return m_maxSection; }
int Candidate::capacityOf(const QString &room) const { return m_roomCapacity.value(room, 0); }
ClassroomType Candidate::typeOf(const QString &room) const
{ return m_roomType.value(room, ClassroomType::Any); }
const QVector<QString> &Candidate::roomsByCapacity() const { return m_roomsByCapacity; }

const QVector<QString> &Candidate::classesOf(const QString &courseId) const
{
    static const QVector<QString> kEmpty;
    const auto it = m_classesOfCourse.constFind(courseId);
    return it == m_classesOfCourse.constEnd() ? kEmpty : it.value();
}

const QVector<Session> &Candidate::sessionsOf(const QString &classId) const
{
    static const QVector<Session> kEmpty;
    const auto it = m_sessions.constFind(classId);
    return it == m_sessions.constEnd() ? kEmpty : it.value();
}

const ClassInfo &Candidate::infoOf(const QString &classId) const
{
    static const ClassInfo kEmpty;
    const auto it = m_info.constFind(classId);
    return it == m_info.constEnd() ? kEmpty : it.value();
}

/*
Candidate::entryFor - 由 Session 构造排课条目

Parameter：
    classId: 教学班 id
    s: 会话（时间槽块 + 教室）
    seq: 班内递增序号（从 1 起，用于 entryId；默认 0 = 占位，仅校验用）

Result:
    ScheduleEntry: 条目（entryId = classId#班内序号；teacherId / 周范围 / duration 取自 ClassInfo）

Remark:
    entryId 形如 classId#seq（如 C102#2），绝不内嵌时间；时间信息另存 timeSlot。
*/
ScheduleEntry Candidate::entryFor(const QString &classId, const Session &s, int seq) const
{
    const ClassInfo &info = m_info.value(classId);
    ScheduleEntry e;
    e.entryId = classId + '#' + QString::number(seq);
    e.teachingClassId = classId;
    e.teacherId = info.teacherId;
    e.classroomId = s.room;
    e.timeSlot.dayOfWeek = s.day;
    e.timeSlot.startSection = s.section;
    e.timeSlot.endSection = s.section + info.duration - 1;
    e.startWeek = info.startWeek;
    e.endWeek = info.endWeek;
    return e;
}

/*
Candidate::commit - 统一落子：旧条目出表 → 新条目逐条校验落子 → 增量更新负载快照

Parameter：
    table: 冲突表（引用；先 remove 旧、再逐条 canPlace+place）
    snap: 负载快照（引用；旧出新入，增量维护 sumSq）
    oldEntries: 移动前的旧条目
    newEntries: 移动后的新条目
    affectedClasses: 受影响教学班（用于回滚日志）
    dLoadSq: 输出，Σload² 的增量
    dUsageSq: 输出，Σusage² 的增量

Result:
    bool: 新条目全部可排返回 true；任一冲突已回滚并返回 false

Remark:
    逐条 canPlace→place：先落子的新条目进入表，后查的新条目彼此冲突也能发现。
    失败回滚 = 移除已落新条目 + 恢复全部旧条目（旧条目移动前可行，无需再校验）。
*/
bool Candidate::commit(ConflictTable &table, LoadSnapshot &snap,
                       const QVector<ScheduleEntry> &oldEntries,
                       const QVector<ScheduleEntry> &newEntries,
                       const QVector<QString> &affectedClasses,
                       qint64 &dLoadSq, qint64 &dUsageSq)
{
    m_journalOld = oldEntries;
    m_journalNew = newEntries;
    m_journalSessions.clear();
    for (const QString &cid : affectedClasses)
        m_journalSessions.insert(cid, m_sessions.value(cid));

    for (const ScheduleEntry &e : oldEntries)
        table.remove(e);
    QVector<ScheduleEntry> placed;
    for (const ScheduleEntry &e : newEntries) {
        if (!table.canPlace(e)) {
            for (const ScheduleEntry &p : placed)
                table.remove(p);
            for (const ScheduleEntry &o : oldEntries)
                table.place(o);
            m_journalOld.clear();
            m_journalNew.clear();
            m_journalSessions.clear();
            return false;
        }
        table.place(e);
        placed.append(e);
    }

    const qint64 sl0 = snap.sumSqLoad;
    const qint64 su0 = snap.sumSqUsage;
    for (const ScheduleEntry &e : oldEntries)
        subOccupancy(snap, e);
    for (const ScheduleEntry &e : newEntries)
        addOccupancy(snap, e);
    dLoadSq = snap.sumSqLoad - sl0;
    dUsageSq = snap.sumSqUsage - su0;
    return true;
}

/*
Candidate::rollback - 撤销最近一次已应用 move

Parameter：
    table: 冲突表（引用；移除新落条目、恢复旧条目）
    snap: 负载快照（引用；新出旧入，与 commit 相反）
*/
void Candidate::rollback(ConflictTable &table, LoadSnapshot &snap)
{
    for (auto it = m_journalSessions.constBegin(); it != m_journalSessions.constEnd(); ++it)
        m_sessions.insert(it.key(), it.value());
    for (const ScheduleEntry &e : m_journalNew)
        subOccupancy(snap, e);
    for (const ScheduleEntry &e : m_journalOld)
        addOccupancy(snap, e);
    for (const ScheduleEntry &e : m_journalNew)
        table.remove(e);
    for (const ScheduleEntry &e : m_journalOld)
        table.place(e);
    m_journalOld.clear();
    m_journalNew.clear();
    m_journalSessions.clear();
}

/*
Candidate::deltaOf - 计算一次移动的 Δcost（S1~S4、S7 前后差 + S5/S6 的 sumSq 差）

Parameter：
    s1b..s4b, s7b: 移动前的 S1~S4、S7 部分罚分（会话更新前计算）
    courseId / classId: 受影响课程与教学班（移动后重算 after）
    w: 软约束权重
    dLoadSq / dUsageSq: S5/S6 的 Σx² 增量（commit 输出）

Result:
    int: Δcost = (S1a−S1b)+…+(S7a−S7b)+w_time·dLoadSq+w_roomload·dUsageSq
*/
int Candidate::deltaOf(int s1b, int s2b, int s3b, int s4b, int s7b,
                       const QString &courseId, const QString &classId,
                       const SoftWeights &w, qint64 dLoadSq, qint64 dUsageSq) const
{
    const long long s1 = splitPenaltyOfCourse(*this, courseId, w.w_split) - s1b;
    const long long s2 = dispersionOfClass(*this, classId, w.w_disp) - s2b;
    const long long s3 = roomUniformOfClass(*this, classId, w.w_room) - s3b;
    const long long s4 = wasteOfClass(*this, classId, w.w_waste) - s4b;
    const long long s7 = weekendPenaltyOfClass(*this, classId, w.w_weekend) - s7b;
    return static_cast<int>(s1 + s2 + s3 + s4 + s7
                            + static_cast<long long>(w.w_time) * dLoadSq
                            + static_cast<long long>(w.w_roomload) * dUsageSq);
}
