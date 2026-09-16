#ifndef ANNEALINGCANDIDATE_H
#define ANNEALINGCANDIDATE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <random>

#include "core/models/models.h"
#include "annealingcost.h"

class ConflictTable;
class DataStore;

// 候选解内一个时间槽块：一次课（占 duration 个连续节）。
struct Session {
    int day = 1;          // 星期 1..7
    int section = 1;      // 起始节
    QString room;         // 教室
};

// 每班元数据（构建时从 store 缓存；邻域操作不改变这些值）。
struct ClassInfo {
    QString courseId;
    QString teacherId;
    int startWeek = 1;
    int endWeek = 16;
    int duration = 1;     // 一次课占连续节数（由条目 endSection−startSection+1 推导）
    int plannedSize = 0;
    int sessions = 0;     // 每周课次 N
    ClassroomType requiredType = ClassroomType::Any;
};

// 模拟退火内部候选解：classId → N 个 Session（同班共用教室）。
// 邻域操作就地应用（同步冲突表与负载快照），成功返回 Δcost；
// Metropolis 拒绝时调用 rollback 撤销最近一次已应用 move。
class Candidate
{
public:
    // 从排课条目（贪心初始解写回后的 store.scheduleEntries()）反建候选。
    void buildFromEntries(const QVector<ScheduleEntry> &entries, const DataStore &store);
    // 把当前候选按 (classId, day, section) 排序写回 store（先清空旧结果）。
    void applyToStore(DataStore &store) const;

    bool empty() const;
    int classCount() const;
    // 以下访问器供成本模块与邻域操作使用；返回排序后的列表保证可复现。
    const QVector<QString> &classIds() const;
    const QVector<QString> &courseIds() const;
    const QVector<QString> &classesOf(const QString &courseId) const;
    const QVector<Session> &sessionsOf(const QString &classId) const;
    const ClassInfo &infoOf(const QString &classId) const;
    int maxSection() const;
    int capacityOf(const QString &room) const;
    ClassroomType typeOf(const QString &room) const;
    const QVector<QString> &roomsByCapacity() const;

    // 冻结（锁定）部分教学班：这些班的会话参与成本/负载统计但永不被邻域移动；
    // 可动班 = 候选内全部班扣除冻结班。局部排课/最小排用同一入口。
    void setLockedClasses(const QSet<QString> &locked);
    const QVector<QString> &movableClassIds() const;   // 可动班（排序），邻域抽样用
    int movableClassCount() const;
    bool isLocked(const QString &classId) const;

    // 邻域操作：应用成功返回 true 并给出 delta（整数 Δcost，含 S1~S7）；
    // 冲突/不可行则已回滚并返回 false（不会留下半截修改）。
    bool tryMoveM1(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                   const SoftWeights &w, int &delta);
    bool tryMoveM2(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                   const SoftWeights &w, int &delta);
    bool tryMoveM3(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                   const SoftWeights &w, int &delta);
    bool tryMoveM4(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                   const SoftWeights &w, int &delta);
    bool tryMoveM5(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                   const SoftWeights &w, int &delta);
    bool tryRandomMove(std::mt19937 &rng, ConflictTable &table, LoadSnapshot &snap,
                       const SoftWeights &w, int &delta);
    // 撤销最近一次已应用（未被接受）的 move：还原会话 / 冲突表 / 负载快照。
    void rollback(ConflictTable &table, LoadSnapshot &snap);

private:
    QHash<QString, QVector<Session>> m_sessions;
    QHash<QString, ClassInfo> m_info;
    QVector<QString> m_classIds;                  // 排序，迭代可复现
    QHash<QString, QVector<QString>> m_classesOfCourse;
    QVector<QString> m_courseIds;                 // 排序
    QHash<QString, int> m_roomCapacity;
    QHash<QString, ClassroomType> m_roomType;
    QVector<QString> m_roomsByCapacity;           // 容量升序（best-fit 语义）
    QSet<QString> m_locked;                       // 冻结班（不参与任何 move）
    QVector<QString> m_movable;                   // 可动班排序列表（move 抽样用）
    int m_maxSection = 0;

    // 最近一次已应用 move 的日志（rollback 用）。
    QVector<ScheduleEntry> m_journalOld;          // 移动前在表中的旧条目
    QVector<ScheduleEntry> m_journalNew;          // 移动后落子的新条目
    QHash<QString, QVector<Session>> m_journalSessions;  // 受影响班的移动前会话

    // 由 Session 构造排课条目（teacherId / 周范围 / duration 取自 ClassInfo）。
    // seq 为班内递增序号（从 1 起，用于 entryId；默认 0 = 占位，仅校验用）。
    ScheduleEntry entryFor(const QString &classId, const Session &s, int seq = 0) const;
    // 统一落子：旧条目出表 → 新条目逐条 canPlace+place → 增量更新负载快照。
    // 失败时回滚冲突表并返回 false；成功返回 load/usage 的 sumSq 增量。
    bool commit(ConflictTable &table, LoadSnapshot &snap,
                const QVector<ScheduleEntry> &oldEntries,
                const QVector<ScheduleEntry> &newEntries,
                const QVector<QString> &affectedClasses,
                qint64 &dLoadSq, qint64 &dUsageSq);
    // M2/M3 共用：把某班整班换到目标 days 集（first-fit 找节，保持当前教室）。
    bool moveClassToDays(ConflictTable &table, LoadSnapshot &snap,
                         const SoftWeights &w, const QString &classId,
                         const QVector<int> &targetDays, int &delta);
    // 计算一次移动的 Δ：受影响课程/班的 S1~S4、S7 前后差 + S5/S6 的 sumSq 差。
    int deltaOf(int s1b, int s2b, int s3b, int s4b, int s7b,
                const QString &courseId, const QString &classId,
                const SoftWeights &w, qint64 dLoadSq, qint64 dUsageSq) const;
};

#endif // ANNEALINGCANDIDATE_H
