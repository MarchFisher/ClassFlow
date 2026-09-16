/**
 * 文件职责：占用表实现——以组合键集合维护「教室/教师/教学班 × 时间（含周）」的占用，
 * 提供 canPlace / place / remove / clear 供排课策略做冲突判定。
 */

#include "conflicttable.h"

/*
ConflictTable::keyOf - 生成单个占用键

Parameter：
    tag: 类别标记（R=教室，T=教师，C=教学班）
    id: 对象 id
    day: 星期（1..7）
    section: 节次
    week: 周次（1..16）

Result:
    QString: 组合键文本

*/
QString ConflictTable::keyOf(const QString &tag, const QString &id,
                             int day, int section, int week)
{
    return tag + '|' + id + '|' + QString::number(day)
           + '|' + QString::number(section) + '|' + QString::number(week);
}

/*
ConflictTable::keysOf - 生成条目覆盖的全部占用键

Parameter：
    entry: 排课条目

Result:
    QStringList: 占用键列表

Remark:
    覆盖周范围 [startWeek, endWeek] × 节次区间 [startSection, endSection]；
    教师键仅当 teacherId 非空时生成。
    周范围展开为逐周键：不同周的课在键上天然错开，互不冲突。
*/
QStringList ConflictTable::keysOf(const ScheduleEntry &entry) const
{
    QStringList keys;
    const int day = entry.timeSlot.dayOfWeek;
    for (int w = entry.startWeek; w <= entry.endWeek; ++w) {
        for (int s = entry.timeSlot.startSection; s <= entry.timeSlot.endSection; ++s) {
            keys << keyOf("R", entry.classroomId, day, s, w);
            keys << keyOf("C", entry.teachingClassId, day, s, w);
            if (!entry.teacherId.isEmpty())
                keys << keyOf("T", entry.teacherId, day, s, w);
        }
    }
    return keys;
}

/*
ConflictTable::canPlace - 判断某条排课是否无冲突

Parameter：
    entry: 待判定的排课条目（教学班 × 时间 × 教室）

Result:
    bool: 任一占用键已存在返回 false，否则 true

*/
bool ConflictTable::canPlace(const ScheduleEntry &entry) const
{
    const QStringList keys = keysOf(entry);
    for (const QString &k : keys) {
        if (m_busy.contains(k))
            return false;
    }
    return true;
}

/*
ConflictTable::busyKeysOf - 返回该条目命中的全部已占键

Parameter：
    entry: 待判定的排课条目

Result:
    QStringList: 落在 busy 里的占用键列表（可能为空 = 无冲突）

Remark:
    供「手动调整」等需要向用户说明冲突来源的场合：按键前缀可区分
    R=教室被占 / C=教学班时间冲突 / T=教师被占。
*/
QStringList ConflictTable::busyKeysOf(const ScheduleEntry &entry) const
{
    QStringList busy;
    const QStringList keys = keysOf(entry);
    for (const QString &k : keys) {
        if (m_busy.contains(k))
            busy << k;
    }
    return busy;
}

/*
ConflictTable::place - 写入占用

Parameter：
    entry: 要占用的排课条目

*/
void ConflictTable::place(const ScheduleEntry &entry)
{
    const QStringList keys = keysOf(entry);
    for (const QString &k : keys)
        m_busy.insert(k);
}

/*
ConflictTable::remove - 释放占用

Parameter：
    entry: 要释放的排课条目

Remark:
    供回溯策略在回退时调用。
*/
void ConflictTable::remove(const ScheduleEntry &entry)
{
    const QStringList keys = keysOf(entry);
    for (const QString &k : keys)
        m_busy.remove(k);
}

/*
ConflictTable::clear - 清空全部占用

*/
void ConflictTable::clear()
{
    m_busy.clear();
}
