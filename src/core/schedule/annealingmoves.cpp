/**
 * 文件职责：模拟退火邻域操作 M1~M5 的实现（Candidate 成员函数）。
 * 每个 move 走统一 commit 流程（见 annealingcandidate.cpp）：
 * 「旧条目出表 → 新条目逐条 canPlace+place → 增量更新负载快照」，冲突即回滚，
 * 保证硬约束全程零违反；Δcost 只算被移动班/课程及受影响槽位/教室。
 * idealGap / buildPatterns 镜像 strategy.cpp（为不动贪心基线故复制）。
 */

#include "annealingcandidate.h"

#include <algorithm>

#include "conflicttable.h"
#include "core/store/datastore.h"
#include "roomrules.h"

namespace {

/*
idealGap - 一周 N 次课的理想间隔（镜像 strategy.cpp，为不动贪心基线故复制）

Parameter：
    n: 每周课次（≥2）

Result:
    int: 理想间隔天数 (5-n)/(n-1)，下限 0
*/
int idealGap(int n)
{
    return qMax(0, (5 - n) / (n - 1));
}

/*
buildPatterns - 生成候选时间模式（镜像 strategy.cpp；间隔从理想到最密）

Parameter：
    n: 每周课次

Result:
    QVector<QVector<int>>: 每个元素是一组 n 个"星期"
*/
QVector<QVector<int>> buildPatterns(int n)
{
    QVector<QVector<int>> patterns;
    if (n <= 1) {
        for (int d = 1; d <= 7; ++d)
            patterns.append(QVector<int>{d});
        return patterns;
    }
    for (int gap = idealGap(n); gap >= 0; --gap) {
        const int step = gap + 1;
        QVector<int> pat;
        pat.reserve(n);
        for (int i = 0; i < n; ++i) {
            const int day = 1 + i * step;
            if (day > 7)
                break;
            pat.append(day);
        }
        if (pat.size() == n)
            patterns.append(pat);
    }
    return patterns;
}

} // namespace

/*
Candidate::moveClassToDays - M2/M3 共用：整班换到一组目标天

Parameter：
    table: 冲突表（引用）
    snap: 负载快照（引用）
    w: 软约束权重
    classId: 目标教学班
    targetDays: 目标星期集（M2 为随机模式，M3 为多数派模式）
    delta: 输出，Δcost

Result:
    bool: 应用成功返回 true；无空位、模式未变或天数不符返回 false

Remark:
    对每个目标天 first-fit 找可排节（对照当前表，避开本班其它课），
    保持当前教室；目标模式与当前完全相同则跳过（无意义）。
    强制「目标天数 == 该班课次数」：保证整班平移后课次数不变
    （防止被 M1 压到同一天的课被 M3 拿去当目标导致丢课次）。
*/
bool Candidate::moveClassToDays(ConflictTable &table, LoadSnapshot &snap,
                                const SoftWeights &w, const QString &classId,
                                const QVector<int> &targetDays, int &delta)
{
    const ClassInfo &info = m_info.value(classId);
    const QVector<Session> &ss = m_sessions.value(classId);
    if (ss.isEmpty())
        return false;
    const QString room = ss.first().room;

    QVector<int> curDays;
    for (const Session &s : ss)
        curDays.append(s.day);
    std::sort(curDays.begin(), curDays.end());
    curDays.erase(std::unique(curDays.begin(), curDays.end()), curDays.end());
    QVector<int> tgt = targetDays;
    std::sort(tgt.begin(), tgt.end());
    if (curDays == tgt)
        return false;
    // 目标天数必须等于该班每周课次：M1 可能把某班两次课挪到同一天，
    // 使其去重签名天数少于课次（如「1,4」变「1」）；若拿这种签名当目标，
    // 下面的 news 只会生成 targetDays.size() 个 Session，课次会被吞。
    if (int(tgt.size()) != int(ss.size()))
        return false;

    QVector<Session> news;
    news.reserve(targetDays.size());
    for (int day : targetDays) {
        bool found = false;
        for (int s = 1; s + info.duration - 1 <= m_maxSection; ++s) {
            Session ns;
            ns.day = day;
            ns.section = s;
            ns.room = room;
            if (table.canPlace(entryFor(classId, ns))) {
                news.append(ns);
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    QVector<ScheduleEntry> oldE, newE;
    oldE.reserve(ss.size());
    for (const Session &s : ss)
        oldE.append(entryFor(classId, s));
    for (const Session &s : news)
        newE.append(entryFor(classId, s));
    qint64 dL = 0, dU = 0;
    if (!commit(table, snap, oldE, newE, {classId}, dL, dU))
        return false;

    const int s1b = splitPenaltyOfCourse(*this, info.courseId, w.w_split);
    const int s2b = dispersionOfClass(*this, classId, w.w_disp);
    const int s3b = roomUniformOfClass(*this, classId, w.w_room);
    const int s4b = wasteOfClass(*this, classId, w.w_waste);
    const int s7b = weekendPenaltyOfClass(*this, classId, w.w_weekend);
    m_sessions[classId] = news;
    delta = deltaOf(s1b, s2b, s3b, s4b, s7b, info.courseId, classId, w, dL, dU);
    return true;
}

/*
Candidate::tryMoveM1 - M1 单课平移：随机班随机一次课，试至多 8 个随机空位

Parameter：
    rng: 随机数源
    table: 冲突表（引用；经 commit 维护）
    snap: 负载快照（引用）
    w: 软约束权重
    delta: 输出，Δcost

Result:
    bool: 应用成功返回 true；无可行空位返回 false
*/
bool Candidate::tryMoveM1(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                          const SoftWeights &w, int &delta)
{
    if (m_movable.isEmpty())          // 全部冻结 / 无候选时不产生动作
        return false;
    std::uniform_int_distribution<int> pick(0, int(m_movable.size()) - 1);
    const QString cid = m_movable.at(pick(rng));
    QVector<Session> &ss = m_sessions[cid];
    if (ss.isEmpty())
        return false;
    std::uniform_int_distribution<int> pickS(0, int(ss.size()) - 1);
    const int idx = pickS(rng);
    const Session old = ss.at(idx);
    const ClassInfo &info = m_info.value(cid);
    const int maxStart = m_maxSection - info.duration + 1;
    if (maxStart < 1)
        return false;
    std::uniform_int_distribution<int> dayD(1, 7);
    std::uniform_int_distribution<int> secD(1, maxStart);

    for (int k = 0; k < 8; ++k) {                   // 至多试 8 个随机候选
        const int day = dayD(rng);
        const int sec = secD(rng);
        if (day == old.day && sec == old.section)
            continue;                               // 退化：位置未变
        Session ns;
        ns.day = day;
        ns.section = sec;
        ns.room = old.room;
        if (!table.canPlace(entryFor(cid, ns)))     // 粗筛：冲突（含本班其它课）
            continue;
        qint64 dL = 0, dU = 0;
        if (!commit(table, snap, {entryFor(cid, old)}, {entryFor(cid, ns)}, {cid}, dL, dU))
            continue;
        const int s1b = splitPenaltyOfCourse(*this, info.courseId, w.w_split);
        const int s2b = dispersionOfClass(*this, cid, w.w_disp);
        const int s3b = roomUniformOfClass(*this, cid, w.w_room);
        const int s4b = wasteOfClass(*this, cid, w.w_waste);
        const int s7b = weekendPenaltyOfClass(*this, cid, w.w_weekend);
        ss[idx] = ns;                               // 更新候选会话
        delta = deltaOf(s1b, s2b, s3b, s4b, s7b, info.courseId, cid, w, dL, dU);
        return true;
    }
    return false;
}

/*
Candidate::tryMoveM2 - M2 整班平移：随机班换到一组随机新时间模式
*/
bool Candidate::tryMoveM2(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                          const SoftWeights &w, int &delta)
{
    if (m_movable.isEmpty())          // 全部冻结 / 无候选时不产生动作
        return false;
    std::uniform_int_distribution<int> pick(0, int(m_movable.size()) - 1);
    const QString cid = m_movable.at(pick(rng));
    const ClassInfo &info = m_info.value(cid);
    if (info.sessions < 1)
        return false;
    const QVector<QVector<int>> patterns = buildPatterns(info.sessions);
    if (patterns.isEmpty())
        return false;
    std::uniform_int_distribution<int> pickP(0, int(patterns.size()) - 1);
    return moveClassToDays(table, snap, w, cid, patterns.at(pickP(rng)), delta);
}

/*
Candidate::tryMoveM3 - M3 课程归并：被拆分课程的少数派班移回多数派模式

Remark:
    若随机选到的课程未被拆分（只有一组模式），返回 false（本轮无操作）。
*/
bool Candidate::tryMoveM3(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                          const SoftWeights &w, int &delta)
{
    if (m_courseIds.isEmpty())
        return false;
    std::uniform_int_distribution<int> pick(0, int(m_courseIds.size()) - 1);
    const QString courseId = m_courseIds.at(pick(rng));
    const QVector<QString> classes = m_classesOfCourse.value(courseId);
    if (classes.size() < 2)
        return false;

    QHash<QString, QVector<QString>> groups;        // day 签名 → 教学班
    QHash<QString, QVector<int>> sigDays;
    for (const QString &cid : classes) {
        QVector<int> days;
        for (const Session &s : m_sessions.value(cid))
            days.append(s.day);
        std::sort(days.begin(), days.end());
        days.erase(std::unique(days.begin(), days.end()), days.end());
        QStringList ds;
        for (int d : days)
            ds << QString::number(d);
        const QString sig = ds.join(',');
        groups[sig].append(cid);
        sigDays[sig] = days;
    }
    if (groups.size() <= 1)
        return false;

    QString majoritySig;
    int maxSize = -1;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        if (it.value().size() > maxSize) {
            maxSize = int(it.value().size());
            majoritySig = it.key();
        }
    // 少数派里只挑可动班作被并对象（冻结班不能被 M3 移动）
    QVector<QString> minority;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        if (it.key() != majoritySig)
            minority += it.value();
    QVector<QString> movableMinority;
    for (const QString &cid : minority)
        if (!m_locked.contains(cid))
            movableMinority.append(cid);
    if (movableMinority.isEmpty())
        return false;
    std::uniform_int_distribution<int> pickM(0, int(movableMinority.size()) - 1);
    const QString target = movableMinority.at(pickM(rng));
    return moveClassToDays(table, snap, w, target, sigDays.value(majoritySig), delta);
}

/*
Candidate::tryMoveM4 - M4 换教室：随机班换一个满足 H4/H5 的教室（时间不变）

Remark:
    按容量升序遍历教室，选第一个「容量/类型匹配且换后不冲突」的；
    教室维冲突由 commit 的逐条 canPlace 兜底（本班旧条目已出表）。
*/
bool Candidate::tryMoveM4(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                          const SoftWeights &w, int &delta)
{
    if (m_movable.isEmpty())          // 全部冻结 / 无候选时不产生动作
        return false;
    std::uniform_int_distribution<int> pick(0, int(m_movable.size()) - 1);
    const QString cid = m_movable.at(pick(rng));
    const ClassInfo &info = m_info.value(cid);
    const QVector<Session> &ss = m_sessions.value(cid);
    if (ss.isEmpty())
        return false;
    const QString curRoom = ss.first().room;

    for (const QString &room : m_roomsByCapacity) {
        if (room == curRoom)
            continue;
        if (!roomrules::roomOk(m_roomCapacity.value(room), m_roomType.value(room),
                               info.plannedSize, info.requiredType))
            continue;                               // H4/H5 共享判定（roomrules 单源）

        QVector<Session> news = ss;
        for (Session &s : news)
            s.room = room;
        QVector<ScheduleEntry> oldE, newE;
        for (const Session &s : ss)
            oldE.append(entryFor(cid, s));
        for (const Session &s : news)
            newE.append(entryFor(cid, s));
        qint64 dL = 0, dU = 0;
        if (!commit(table, snap, oldE, newE, {cid}, dL, dU))
            continue;

        const int s1b = splitPenaltyOfCourse(*this, info.courseId, w.w_split);
        const int s2b = dispersionOfClass(*this, cid, w.w_disp);
        const int s3b = roomUniformOfClass(*this, cid, w.w_room);
        const int s4b = wasteOfClass(*this, cid, w.w_waste);
        const int s7b = weekendPenaltyOfClass(*this, cid, w.w_weekend);
        m_sessions[cid] = news;
        delta = deltaOf(s1b, s2b, s3b, s4b, s7b, info.courseId, cid, w, dL, dU);
        return true;
    }
    return false;
}

/*
Candidate::tryMoveM5 - M5 双班教室对调：交换两班教室（时间不变）

Remark:
    先做容量/类型互检（H4/H5），再双方旧条目出表、逐条校验落子；
    换后教室维冲突（与第三方）由 commit 兜底。仅动教室 → S5 与 S7 Δ=0。
*/
bool Candidate::tryMoveM5(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                          const SoftWeights &w, int &delta)
{
    if (m_movable.size() < 2)         // 全部冻结 / 可动班不足两班时无可对调对象
        return false;
    std::uniform_int_distribution<int> pick(0, int(m_movable.size()) - 1);
    const QString a = m_movable.at(pick(rng));
    QString b = a;
    for (int i = 0; i < 10 && b == a; ++i)
        b = m_movable.at(pick(rng));
    if (b == a)
        return false;

    const ClassInfo &ia = m_info.value(a);
    const ClassInfo &ib = m_info.value(b);
    const QVector<Session> &sa = m_sessions.value(a);
    const QVector<Session> &sb = m_sessions.value(b);
    if (sa.isEmpty() || sb.isEmpty())
        return false;
    const QString ra = sa.first().room;
    const QString rb = sb.first().room;
    if (ra == rb)
        return false;

    if (!roomrules::roomOk(m_roomCapacity.value(rb), m_roomType.value(rb),
                           ia.plannedSize, ia.requiredType)
        || !roomrules::roomOk(m_roomCapacity.value(ra), m_roomType.value(ra),
                              ib.plannedSize, ib.requiredType))
        return false;                               // H4/H5 共享判定（roomrules 单源）

    QVector<Session> na = sa, nb = sb;
    for (Session &s : na)
        s.room = rb;
    for (Session &s : nb)
        s.room = ra;
    QVector<ScheduleEntry> oldE, newE;
    for (const Session &s : sa)
        oldE.append(entryFor(a, s));
    for (const Session &s : sb)
        oldE.append(entryFor(b, s));
    for (const Session &s : na)
        newE.append(entryFor(a, s));
    for (const Session &s : nb)
        newE.append(entryFor(b, s));
    qint64 dL = 0, dU = 0;
    if (!commit(table, snap, oldE, newE, {a, b}, dL, dU))
        return false;

    const int s1Ab = splitPenaltyOfCourse(*this, ia.courseId, w.w_split);
    const int s2Ab = dispersionOfClass(*this, a, w.w_disp);
    const int s3Ab = roomUniformOfClass(*this, a, w.w_room);
    const int s4Ab = wasteOfClass(*this, a, w.w_waste);
    const int s1Bb = splitPenaltyOfCourse(*this, ib.courseId, w.w_split);
    const int s2Bb = dispersionOfClass(*this, b, w.w_disp);
    const int s3Bb = roomUniformOfClass(*this, b, w.w_room);
    const int s4Bb = wasteOfClass(*this, b, w.w_waste);
    m_sessions[a] = na;
    m_sessions[b] = nb;

    const long long da = (splitPenaltyOfCourse(*this, ia.courseId, w.w_split) - s1Ab)
                         + (dispersionOfClass(*this, a, w.w_disp) - s2Ab)
                         + (roomUniformOfClass(*this, a, w.w_room) - s3Ab)
                         + (wasteOfClass(*this, a, w.w_waste) - s4Ab);
    const long long db = (splitPenaltyOfCourse(*this, ib.courseId, w.w_split) - s1Bb)
                         + (dispersionOfClass(*this, b, w.w_disp) - s2Bb)
                         + (roomUniformOfClass(*this, b, w.w_room) - s3Bb)
                         + (wasteOfClass(*this, b, w.w_waste) - s4Bb);
    delta = static_cast<int>(da + db + static_cast<long long>(w.w_time) * dL
                             + static_cast<long long>(w.w_roomload) * dU);
    return true;
}

/*
Candidate::tryRandomMove - 均匀随机选一个邻域操作执行

Parameter：
    rng: 随机数源
    table: 冲突表（引用）
    snap: 负载快照（引用）
    w: 软约束权重
    delta: 输出，Δcost

Result:
    bool: 选中的操作成功应用返回 true，否则 false
*/
bool Candidate::tryRandomMove(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                              const SoftWeights &w, int &delta)
{
    std::uniform_int_distribution<int> m(1, 5);
    switch (m(rng)) {
    case 1:  return tryMoveM1(rng, table, snap, w, delta);
    case 2:  return tryMoveM2(rng, table, snap, w, delta);
    case 3:  return tryMoveM3(rng, table, snap, w, delta);
    case 4:  return tryMoveM4(rng, table, snap, w, delta);
    default: return tryMoveM5(rng, table, snap, w, delta);
    }
}
