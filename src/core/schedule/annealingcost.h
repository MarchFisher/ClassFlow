#ifndef ANNEALINGCOST_H
#define ANNEALINGCOST_H

#include <QHash>
#include <QString>
#include <QVector>

#include "core/models/models.h"

class Candidate;
class DataStore;

// 软约束权重（内部 ×2 整数化；名义值见 docs/algorithms/simulated-annealing.md §7）。
// ×2 后 w_waste 由 0.5 变为 1，全部权重为整数，S5/S6 的平方差累加无浮点。
struct SoftWeights {
    int w_split    = 500;   // S1 课程拆分（名义 1000）
    int w_disp     = 100;    // S2 离散度（名义 100）
    int w_room     = 50;    // S3 同班同教室（名义 50）
    int w_waste    = 1;      // S4 教室浪费（名义 0.5）
    int w_time     = 60;    // S5 时间负载（名义 60）
    int w_roomload = 40;     // S6 教室负载（名义 40）
    int w_weekend  = 800;   // S7 周末上课（名义 1000）
};

// S5/S6 负载统计快照（代表周）：load[day-1][section-1] = 并发上课班数，
// usage[room] = 代表周内该教室占用节数。sumSq 为 Σx²，由邻域操作增量维护。
struct LoadSnapshot {
    int repWeek = 1;                    // 代表周（覆盖 session 最多的周，平局取最小）
    int dayCount = 7;                   // S5 网格天数（含周末，对齐 UI 7 列）
    int maxSection = 0;                 // 节次上限（作息表最大节次号）
    int totalLoad = 0;                  // Σ load（move 不变 → μ_time 不变）
    int totalUsage = 0;                 // Σ usage（move 不变 → μ_room 不变）
    qint64 sumSqLoad = 0;               // Σ load²（增量维护）
    qint64 sumSqUsage = 0;              // Σ usage²（增量维护）
    QVector<QVector<int>> load;         // [day-1][section-1]
    QHash<QString, int> usage;          // room → 占用节数
};

// 选代表周：覆盖 session 数最多的周，平局取最小周。
int chooseRepWeek(const Candidate &cand, int semesterWeeks);

// 从候选解构建负载快照（含代表周选择、全部教室初始化为 0）。
LoadSnapshot buildSnapshot(const Candidate &cand, const DataStore &store);

// 全量软成本 S1~S7（候选解当前状态）。
double fullSoftCost(const Candidate &cand, const LoadSnapshot &snap,
                    const SoftWeights &w);

// 供测试：对写回的排课条目列表直接计算软成本（同一公式口径）。
double softCostOfEntries(const QVector<ScheduleEntry> &entries,
                         const DataStore &store);

// 供测试：对写回的排课条目列表计算 S5/S6 方差（均匀性断言）。
struct UniformityStats {
    double timeVar = 0.0;   // Σ_t (load(t) − μ_time)²
    double roomVar = 0.0;   // Σ_r (usage(r) − μ_room)²
};
UniformityStats uniformityOfEntries(const QVector<ScheduleEntry> &entries,
                                    const DataStore &store);

// 单课程拆分罚分（S1，move 增量用）。
int splitPenaltyOfCourse(const Candidate &cand, const QString &courseId, int w_split);

// 单班离散度罚分（S2，move 增量用）。
int dispersionOfClass(const Candidate &cand, const QString &classId, int w_disp);

// 单班同教室罚分（S3，move 增量用）。
int roomUniformOfClass(const Candidate &cand, const QString &classId, int w_room);

// 单班教室浪费罚分（S4，move 增量用；按条目即每次课计）。
int wasteOfClass(const Candidate &cand, const QString &classId, int w_waste);

// 单班周末上课罚分（S7，move 增量用；周末=周六/日，每课次计）。
int weekendPenaltyOfClass(const Candidate &cand, const QString &classId, int w_weekend);

// 负载表增量更新：一条排课条目对 S5/S6 的占用增减（move 应用/回滚用）。
void addOccupancy(LoadSnapshot &snap, const ScheduleEntry &entry);
void subOccupancy(LoadSnapshot &snap, const ScheduleEntry &entry);

#endif // ANNEALINGCOST_H
