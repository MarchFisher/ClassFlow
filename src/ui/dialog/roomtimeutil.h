#ifndef UI_DIALOG_ROOMTIMEUTIL_H
#define UI_DIALOG_ROOMTIMEUTIL_H

#include <QString>
#include <QVector>

#include "core/models/classroom.h"

class DataStore;
class Classroom;

// 「时间·教室」类编辑器的共享展示 / 候选 helper（plan：TimeRoomEditor 与
// FromScratchEditor 共用，避免逐文件拷贝同一段星期/节次/教室文案与过滤逻辑）。
// 均为纯函数：不持有状态、不落库，只把作息表 / 教室表翻译成可展示的候选。
namespace slotui {

QString weekdayName(int day);              // 星期号(1..7) → "周一".."周日"
QString sectionRangeText(int start, int end);  // 单节/连续多节区间文案
QString roomTypeName(ClassroomType type);  // 教室类型枚举 → 中文名
int maxSectionOf(const DataStore &store);  // 作息表最大节次号（空表 = 0）

// 教室候选：容量 ≥ plannedSize 且 (requiredType==Any || 类型匹配)，按容量升序、
// 容量相同时按教室号稳定排序（best-fit 语义；供 星期/起始节/教室 下拉使用）。
QVector<const Classroom *> candidateRooms(const DataStore &store,
                                          int plannedSize,
                                          ClassroomType requiredType);

} // namespace slotui

#endif // UI_DIALOG_ROOMTIMEUTIL_H
