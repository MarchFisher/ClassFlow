#ifndef CORE_SCHEDULE_MANUALADD_H
#define CORE_SCHEDULE_MANUALADD_H

#include <QString>
#include <QVector>

#include "core/schedule/manualmove.h"

class DataStore;

// 「从零排入」：整批为某个「0 现存课次」的教学班新增 N 次课（供错误列表里排不下的
// 失败班从零手动指定时段+教室）。与 manualmove 的锚点式"移动已有课"相反，本模块
// 表达"新增"语义：每次课只需给 星期/起始节/教室，其余字段（teacherId / 周范围 /
// 单次课跨度 / entryId）一律由 core 从 Course / TeachingClass 现读推导并生成——
// "不能乱改学时/周次/教师"由本模块结构性强制。硬约束判定与 manualmove / 引擎同一套
// (ConflictTable + roomrules)。校验通过且恰排够 N 条才原子落库并移除该班失败记录。
namespace manual {

// 一次待新增课（只表达 在哪天几点用哪个教室）
struct AddSlot {
    int dayOfWeek = 1;          // 星期（1..7）
    int startSection = 1;       // 起始节（结束节 = 起始节 + 单次学时 − 1）
    QString classroomId;        // 教室号
};

// 整批纯校验（只读，不写库）：N 次课都通过 H1~H5 且互不冲突返回 ok；
// 否则 rep 定位到出问题的那一行（index/entryId/busyTags 供 UI 精确文案）。
Report validateAdd(const DataStore &store, const QString &classId,
                   const QVector<AddSlot> &adds);

// 先校验（内部再 resolve 一次），全部通过且恰排够 N 条才逐条 addScheduleEntry 落库，
// 并 removeScheduleFailureForClass(classId) 把该班移出失败列表；失败返回 false 不写库。
bool applyAdd(DataStore &store, const QString &classId,
              const QVector<AddSlot> &adds);

} // namespace manual

#endif // CORE_SCHEDULE_MANUALADD_H
