#ifndef CONFLICTTABLE_H
#define CONFLICTTABLE_H

#include <QSet>
#include <QString>
#include <QStringList>

#include "core/models/models.h"

// 占用表：教室 / 教师 / 教学班 × 时间（含周），供排课策略做冲突判定。
// 组合键 '类别|对象id|星期|节次|周' 存入一个 QSet，冲突即键已存在。
// 条目周范围 [startWeek, endWeek] 展开为逐周键 → 不同周的课互不冲突。
//   教室 × 时间    —— 同一教室同一时间（同周）只能一个班
//   教师 × 时间    —— 同一教师同一时间（同周）只能一个班（teacherId 为空则跳过）
//   教学班 × 时间  —— 同班各次课时间互不冲突
class ConflictTable
{
public:
    ConflictTable() = default;

    bool canPlace(const ScheduleEntry &entry) const;  // 是否可排（无冲突）
    // 返回该条目落在 busy 里的占用键（供细分失败原因：R=教室 T=教师 C=教学班）
    QStringList busyKeysOf(const ScheduleEntry &entry) const;
    void place(const ScheduleEntry &entry);           // 占用
    void remove(const ScheduleEntry &entry);          // 释放（供回溯策略用）
    void clear();

private:
    QSet<QString> m_busy;                             // 全部占用键集合

    QStringList keysOf(const ScheduleEntry &entry) const;  // 条目的全部占用键

    static QString keyOf(const QString &tag, const QString &id,
                         int day, int section, int week);
};

#endif // CONFLICTTABLE_H
