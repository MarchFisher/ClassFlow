/**
 * 文件职责：硬约束 H4（容量）与 H5（类型）判定的共享实现。
 * 供贪心 best-fit / 退火 M4（换教室）、M5（双班教室对调）/ 教务手动调整
 * manualmove 统一调用，约束口径全引擎单源定义。
 */

#include "roomrules.h"

/*
roomrules::capacityOk - H4：教室容量是否足够装下教学班

Parameter：
    roomCapacity: 教室容量
    plannedSize: 教学班预期人数

Result:
    bool: 容量足够返回 true
*/
bool roomrules::capacityOk(int roomCapacity, int plannedSize)
{
    return roomCapacity >= plannedSize;
}

/*
roomrules::typeOk - H5：教室类型是否匹配课程所需类型

Parameter：
    roomType: 教室类型
    requiredType: 课程所需类型（Any = 不限，恒通过）

Result:
    bool: 类型匹配返回 true
*/
bool roomrules::typeOk(ClassroomType roomType, ClassroomType requiredType)
{
    return requiredType == ClassroomType::Any || roomType == requiredType;
}

/*
roomrules::roomOk - H4 与 H5 同时成立（该教室对某教学班可行）

Parameter：
    roomCapacity: 教室容量
    roomType: 教室类型
    plannedSize: 教学班预期人数
    requiredType: 课程所需类型

Result:
    bool: 容量与类型均满足返回 true
*/
bool roomrules::roomOk(int roomCapacity, ClassroomType roomType,
                       int plannedSize, ClassroomType requiredType)
{
    return capacityOk(roomCapacity, plannedSize) && typeOk(roomType, requiredType);
}
