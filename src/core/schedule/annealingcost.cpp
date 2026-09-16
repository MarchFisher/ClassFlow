/**
 * 文件职责：模拟退火目标函数（软约束 S1~S7）与 S5/S6 负载统计。
 * 全量成本供初始化与 best 比较；邻域操作通过 addOccupancy / subOccupancy
 * 增量维护 sumSq，配合「总 load / 总 usage 在 move 中不变」的不变式，
 * 使 S5/S6 的 Δ 精确等于 w·Δ(Σx²)，只涉及被移动的槽位 / 教室。
 */

#include "annealingcost.h"

#include <algorithm>

#include "annealingcandidate.h"
#include "core/store/datastore.h"

/*
chooseRepWeek - 选代表周（覆盖 session 数最多的周，平局取最小）

Parameter：
    cand: 候选解（用其各班周范围与课次）
    semesterWeeks: 学期总周数

Result:
    int: 代表周，范围 [1, semesterWeeks]
*/
int chooseRepWeek(const Candidate &cand, int semesterWeeks)
{
    QVector<int> count(qMax(1, semesterWeeks) + 1, 0);
    for (const QString &classId : cand.classIds()) {
        const ClassInfo &info = cand.infoOf(classId);
        for (int w = info.startWeek; w <= info.endWeek; ++w)
            count[w] += info.sessions;      // 覆盖 session 数（每班每周课次）
    }
    int best = 1;
    for (int w = 2; w <= semesterWeeks; ++w)
        if (count[w] > count[best])
            best = w;
    return best;
}

/*
buildSnapshot - 从候选解构建 S5/S6 负载快照

Parameter：
    cand: 候选解
    store: 数据仓库（教室列表初始化 usage、作息表求节次上限）

Result:
    LoadSnapshot: 含代表周、7 天 × 节数 负载网格、教室占用与 sumSq
*/
LoadSnapshot buildSnapshot(const Candidate &cand, const DataStore &store)
{
    LoadSnapshot snap;
    snap.maxSection = cand.maxSection();
    snap.load = QVector<QVector<int>>(snap.dayCount,
                                      QVector<int>(qMax(1, snap.maxSection), 0));
    for (const Classroom &r : store.classrooms())
        snap.usage.insert(r.roomNumber, 0);
    snap.repWeek = chooseRepWeek(cand, store.semesterWeeks());

    for (const QString &classId : cand.classIds()) {
        const ClassInfo &info = cand.infoOf(classId);
        if (snap.repWeek < info.startWeek || snap.repWeek > info.endWeek)
            continue;                       // 代表周未覆盖该课，不计负载
        for (const Session &s : cand.sessionsOf(classId)) {
            for (int sec = s.section; sec < s.section + info.duration; ++sec) {
                ++snap.load[s.day - 1][sec - 1];
                ++snap.totalLoad;
            }
            snap.usage[s.room] += info.duration;
            snap.totalUsage += info.duration;
        }
    }
    for (int d = 0; d < snap.dayCount; ++d)
        for (int s = 0; s < snap.maxSection; ++s)
            snap.sumSqLoad += qint64(snap.load[d][s]) * snap.load[d][s];
    for (auto it = snap.usage.constBegin(); it != snap.usage.constEnd(); ++it)
        snap.sumSqUsage += qint64(it.value()) * it.value();
    return snap;
}

/*
fullSoftCost - 全量软成本 S1~S7

Parameter：
    cand: 候选解
    snap: 负载快照
    w: 软约束权重

Result:
    double: 软成本。S5/S6 含方差 μ 的小数部分，用 double 表示；
    Δ（增量）则完全整数，见 addOccupancy / subOccupancy。
*/
double fullSoftCost(const Candidate &cand, const LoadSnapshot &snap,
                    const SoftWeights &w)
{
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0, s7 = 0.0;
    for (const QString &courseId : cand.courseIds())
        s1 += splitPenaltyOfCourse(cand, courseId, w.w_split);
    for (const QString &classId : cand.classIds()) {
        s2 += dispersionOfClass(cand, classId, w.w_disp);
        s3 += roomUniformOfClass(cand, classId, w.w_room);
        s4 += wasteOfClass(cand, classId, w.w_waste);
        s7 += weekendPenaltyOfClass(cand, classId, w.w_weekend);
    }

    const double T = double(snap.dayCount) * snap.maxSection;
    const double R = double(cand.roomsByCapacity().size());
    const double varTime = snap.sumSqLoad - double(snap.totalLoad) * snap.totalLoad / T;
    const double varRoom = snap.sumSqUsage - double(snap.totalUsage) * snap.totalUsage / R;
    return s1 + s2 + s3 + s4 + s7 + w.w_time * varTime + w.w_roomload * varRoom;
}

/*
softCostOfEntries - 对写回的排课条目列表直接计算软成本（测试用）

Parameter：
    entries: 排课条目（贪心或 SA 的输出）
    store: 数据仓库

Result:
    double: 软成本；与 SA 内部 fullSoftCost 同口径，可公平比较
*/
double softCostOfEntries(const QVector<ScheduleEntry> &entries, const DataStore &store)
{
    Candidate cand;
    cand.buildFromEntries(entries, store);
    if (cand.empty())
        return 0.0;
    const LoadSnapshot snap = buildSnapshot(cand, store);
    return fullSoftCost(cand, snap, SoftWeights());
}

/*
uniformityOfEntries - 计算 S5/S6 负载方差（测试用）

Parameter：
    entries: 排课条目
    store: 数据仓库

Result:
    UniformityStats: 时间负载方差与教室负载方差
*/
UniformityStats uniformityOfEntries(const QVector<ScheduleEntry> &entries,
                                    const DataStore &store)
{
    UniformityStats stats;
    Candidate cand;
    cand.buildFromEntries(entries, store);
    if (cand.empty())
        return stats;
    const LoadSnapshot snap = buildSnapshot(cand, store);
    const double T = double(snap.dayCount) * snap.maxSection;
    const double R = double(cand.roomsByCapacity().size());
    stats.timeVar = snap.sumSqLoad - double(snap.totalLoad) * snap.totalLoad / T;
    stats.roomVar = snap.sumSqUsage - double(snap.totalUsage) * snap.totalUsage / R;
    return stats;
}

/*
splitPenaltyOfCourse - 单课程拆分罚分（S1）

Parameter：
    cand: 候选解
    courseId: 课程 id
    w_split: 拆分权重

Result:
    int: (该课程不同时间模式组数 − 1) × w_split
*/
int splitPenaltyOfCourse(const Candidate &cand, const QString &courseId, int w_split)
{
    QSet<QString> sigs;
    for (const QString &classId : cand.classesOf(courseId)) {
        QVector<int> days;
        for (const Session &s : cand.sessionsOf(classId))
            days.append(s.day);
        std::sort(days.begin(), days.end());
        days.erase(std::unique(days.begin(), days.end()), days.end());   // 去重
        QStringList ds;
        for (int d : days)
            ds << QString::number(d);
        sigs.insert(ds.join(','));
    }
    return qMax(0, int(sigs.size()) - 1) * w_split;
}

/*
dispersionOfClass - 单班离散度罚分（S2）

Parameter：
    cand: 候选解
    classId: 教学班 id
    w_disp: 离散度权重

Result:
    int: Σ|实际空闲日数 − 理想间隔| × w_disp；N<2 时为 0（防 idealGap 除零）
*/
int dispersionOfClass(const Candidate &cand, const QString &classId, int w_disp)
{
    const ClassInfo &info = cand.infoOf(classId);
    if (info.sessions < 2)
        return 0;
    QVector<Session> ss = cand.sessionsOf(classId);
    std::sort(ss.begin(), ss.end(), [](const Session &a, const Session &b) {
        return a.day != b.day ? a.day < b.day : a.section < b.section;
    });
    const int ideal = qMax(0, (5 - info.sessions) / (info.sessions - 1));
    int sum = 0;
    for (int i = 1; i < ss.size(); ++i) {
        const int gap = qMax(0, ss[i].day - ss[i - 1].day - 1);
        sum += qAbs(gap - ideal);
    }
    return sum * w_disp;
}

/*
roomUniformOfClass - 单班同教室罚分（S3）

Parameter：
    cand: 候选解
    classId: 教学班 id
    w_room: 同教室权重

Result:
    int: (该班所用教室种类数 − 1) × w_room
*/
int roomUniformOfClass(const Candidate &cand, const QString &classId, int w_room)
{
    QSet<QString> rooms;
    for (const Session &s : cand.sessionsOf(classId))
        rooms.insert(s.room);
    return qMax(0, int(rooms.size()) - 1) * w_room;
}

/*
wasteOfClass - 单班教室浪费罚分（S4，按条目即每次课计）

Parameter：
    cand: 候选解
    classId: 教学班 id
    w_waste: 浪费权重

Result:
    int: Σ(教室容量 − 教学班人数) × w_waste
*/
int wasteOfClass(const Candidate &cand, const QString &classId, int w_waste)
{
    const ClassInfo &info = cand.infoOf(classId);
    int sum = 0;
    for (const Session &s : cand.sessionsOf(classId))
        sum += cand.capacityOf(s.room) - info.plannedSize;
    return sum * w_waste;
}

/*
weekendPenaltyOfClass - 单班周末上课罚分（S7）

Parameter：
    cand: 候选解
    classId: 教学班 id
    w_weekend: 周末上课权重

Result:
    int: 该班落在周末（周六/周日）的课次数 × w_weekend
*/
int weekendPenaltyOfClass(const Candidate &cand, const QString &classId, int w_weekend)
{
    const QVector<Session> &ss = cand.sessionsOf(classId);
    int n = 0;
    for (const Session &s : ss)
        if (s.day == 6 || s.day == 7)
            ++n;
    return n * w_weekend;
}

/*
addOccupancy - 负载表增一条排课条目的占用（load +1 / usage +duration）

Parameter：
    snap: 负载快照（引用；同时增量维护 sumSq）
    entry: 要占用的条目

Remark:
    Σx² 增量：load v→v+1 贡献 2v+1；usage u→u+d 贡献 2ud+d²。
*/
void addOccupancy(LoadSnapshot &snap, const ScheduleEntry &entry)
{
    const int day = entry.timeSlot.dayOfWeek;
    for (int sec = entry.timeSlot.startSection; sec <= entry.timeSlot.endSection; ++sec) {
        int &v = snap.load[day - 1][sec - 1];
        snap.sumSqLoad += qint64(2) * v + 1;
        ++v;
    }
    const int d = entry.timeSlot.endSection - entry.timeSlot.startSection + 1;
    int &u = snap.usage[entry.classroomId];
    snap.sumSqUsage += qint64(2) * u * d + qint64(d) * d;
    u += d;
}

/*
subOccupancy - 负载表减去一条排课条目的占用（load −1 / usage −duration）

Parameter：
    snap: 负载快照（引用；同时增量维护 sumSq）
    entry: 要释放的条目

Remark:
    Σx² 增量：load v→v−1 贡献 −2v+1；usage u→u−d 贡献 −2ud+d²。
*/
void subOccupancy(LoadSnapshot &snap, const ScheduleEntry &entry)
{
    const int day = entry.timeSlot.dayOfWeek;
    for (int sec = entry.timeSlot.startSection; sec <= entry.timeSlot.endSection; ++sec) {
        int &v = snap.load[day - 1][sec - 1];
        snap.sumSqLoad += qint64(1) - qint64(2) * v;
        --v;
    }
    const int d = entry.timeSlot.endSection - entry.timeSlot.startSection + 1;
    int &u = snap.usage[entry.classroomId];
    snap.sumSqUsage += qint64(d) * d - qint64(2) * u * d;
    u -= d;
}
