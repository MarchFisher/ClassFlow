#ifndef CORE_SCHEDULE_ROOMRULES_H
#define CORE_SCHEDULE_ROOMRULES_H

#include "core/models/classroom.h"

// 硬约束 H4（容量）与 H5（类型）判定的单一事实来源。
// 贪心 best-fit / 退火 M4（换教室）、M5（双班教室对调）/ 教务手动调整 manualmove
// 都调用本模块，故口径（Any 语义、容量比较）只需在 roomrules.cpp 改一处，
// 即可全引擎生效。
namespace roomrules {

// H4：教室容量是否足够装下教学班（roomCapacity >= plannedSize）
bool capacityOk(int roomCapacity, int plannedSize);

// H5：教室类型是否匹配课程所需类型（requiredType == Any 时任意教室均可）
bool typeOk(ClassroomType roomType, ClassroomType requiredType);

// H4 与 H5 同时成立（教室对某班可行）
bool roomOk(int roomCapacity, ClassroomType roomType,
            int plannedSize, ClassroomType requiredType);

} // namespace roomrules

#endif // CORE_SCHEDULE_ROOMRULES_H
