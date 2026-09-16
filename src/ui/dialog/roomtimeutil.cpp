/**
 * 文件职责：「时间·教室」类编辑器的共享 helper 实现（roomtimeutil）。
 * 从原 TimeRoomEditor 内部匿名函数抽离，供其与「从零排入」编辑器 FromScratchEditor
 * 共用同一套 星期名 / 节次区间 / 教室类型文案 与「容量 ≥ 班人数 + 类型匹配、容量升序」
 * 教室候选口径——避免两份实现各自演化、口径漂移。纯函数、只读 store，不落库。
 */

#include "roomtimeutil.h"

#include <algorithm>

#include "core/store/datastore.h"

namespace slotui {

/*
slotui::weekdayName - 星期号转中文名

Parameter：
    day: 星期号（1..7）

Result:
    QString: "周一".."周日"；越界返回数字
*/
QString weekdayName(int day)
{
    static const QString names[] = {
        QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
        QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
        QStringLiteral("周日")
    };
    return (day >= 1 && day <= 7) ? names[day - 1] : QString::number(day);
}

/*
slotui::sectionRangeText - 节次区间展示（单节/连续多节）

Parameter：
    start: 起始节
    end: 结束节

Result:
    QString: "第 3 节" 或 "第 1~2 节"
*/
QString sectionRangeText(int start, int end)
{
    return (start == end)
        ? QStringLiteral("第 %1 节").arg(start)
        : QStringLiteral("第 %1~%2 节").arg(start).arg(end);
}

/*
slotui::roomTypeName - 教室类型枚举转中文名

Parameter：
    type: ClassroomType 枚举

Result:
    QString: "普通教室"/"机房"/"操场"/"不限"
*/
QString roomTypeName(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Norm:       return QStringLiteral("普通教室");
    case ClassroomType::Lab:        return QStringLiteral("机房");
    case ClassroomType::PlayGround: return QStringLiteral("操场");
    default:                        return QStringLiteral("不限");
    }
}

/*
slotui::maxSectionOf - 作息表最大节次号

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
slotui::candidateRooms - 可行的教室候选（容量 + 类型过滤，容量升序稳定排序）

Parameter：
    store: 数据仓库（只读）
    plannedSize: 教学班计划人数（H4：容量须 ≥ 它）
    requiredType: 课程所需教室类型（Any = 不限，H5）

Result:
    QVector<const Classroom *>: 容量升序、同容量按教室号升序的候选（可能为空）
*/
QVector<const Classroom *> candidateRooms(const DataStore &store, int plannedSize,
                                          ClassroomType requiredType)
{
    QVector<const Classroom *> cand;
    const QVector<Classroom> &rooms = store.classrooms();
    cand.reserve(rooms.size());
    for (const Classroom &r : rooms) {
        if (r.capacity < plannedSize)
            continue;
        if (requiredType != ClassroomType::Any && r.type != requiredType)
            continue;
        cand.append(&r);
    }
    std::sort(cand.begin(), cand.end(), [](const Classroom *a, const Classroom *b) {
        if (a->capacity != b->capacity)
            return a->capacity < b->capacity;
        return a->roomNumber < b->roomNumber;
    });
    return cand;
}

} // namespace slotui
