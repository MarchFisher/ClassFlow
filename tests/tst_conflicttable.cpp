/**
 * 文件职责：ConflictTable 冲突检测单元测试，另含 roomrules（H4 容量 / H5 类型）
 * 共享判定的纯函数用例。
 * 覆盖教室 / 教学班 / 教师 × 时间（含周）的占用、释放与清空，
 * 以及「不同周范围不冲突」的跨周复用语义。
 * roomrules 用例并入本套件（沿用 R3 决定：不新增测试目标，保持 9 个）。
 */

#include <QtTest>

#include "core/schedule/conflicttable.h"
#include "core/schedule/roomrules.h"

namespace {

/*
makeEntry - 构造一条占单节的排课条目

Parameter：
    classId: 教学班 id
    room: 教室号
    day: 星期
    section: 节次
    teacher: 教师 id（可空）
    startWeek: 起始周（默认 1）
    endWeek: 结束周（默认 16，整学期）

Result:
    ScheduleEntry: 构造的条目
*/
ScheduleEntry makeEntry(const QString &classId, const QString &room,
                        int day, int section,
                        const QString &teacher = QString(),
                        int startWeek = 1, int endWeek = 16)
{
    ScheduleEntry e;
    e.entryId = classId + '-' + QString::number(day) + '-' + QString::number(section);
    e.teachingClassId = classId;
    e.teacherId       = teacher;
    e.classroomId     = room;
    e.timeSlot.dayOfWeek   = day;
    e.timeSlot.startSection = section;
    e.timeSlot.endSection   = section;
    e.startWeek = startWeek;
    e.endWeek   = endWeek;
    return e;
}

} // namespace

class TestConflictTable : public QObject
{
    Q_OBJECT

private slots:
    void sameRoomSameTimeConflicts();
    void differentRoomSameTimeOk();
    void sameClassSameTimeConflicts();
    void sameTeacherSameTimeConflicts();
    void removeFreesSlot();
    void clearFreesAll();
    void adjacentSectionsOk();
    void differentWeeksShareRoomOk();
    void differentWeeksShareTeacherOk();
    void overlappingWeeksConflict();

    // roomrules（R3 并入）：H4/H5 共享判定的纯函数用例
    void roomRulesCapacityOk();
    void roomRulesTypeOk();
    void roomRulesRoomOk();
};

/*
TestConflictTable::sameRoomSameTimeConflicts - 同教室同时间只能一个班
*/
void TestConflictTable::sameRoomSameTimeConflicts()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2));
    QVERIFY(!t.canPlace(makeEntry("C201", "A101", 1, 2)));
}

/*
TestConflictTable::differentRoomSameTimeOk - 不同教室同时间可排
*/
void TestConflictTable::differentRoomSameTimeOk()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2));
    QVERIFY(t.canPlace(makeEntry("C201", "A201", 1, 2)));
}

/*
TestConflictTable::sameClassSameTimeConflicts - 同班各次课时间互斥
*/
void TestConflictTable::sameClassSameTimeConflicts()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2));
    QVERIFY(!t.canPlace(makeEntry("C101", "A201", 1, 2)));
}

/*
TestConflictTable::sameTeacherSameTimeConflicts - 同教师同一时间只能一个班
*/
void TestConflictTable::sameTeacherSameTimeConflicts()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2, "T001"));
    QVERIFY(!t.canPlace(makeEntry("C201", "A201", 1, 2, "T001")));
}

/*
TestConflictTable::removeFreesSlot - remove 释放后恢复可排
*/
void TestConflictTable::removeFreesSlot()
{
    ConflictTable t;
    const ScheduleEntry e = makeEntry("C101", "A101", 1, 2);
    t.place(e);
    t.remove(e);
    QVERIFY(t.canPlace(e));
}

/*
TestConflictTable::clearFreesAll - clear 清空全部占用
*/
void TestConflictTable::clearFreesAll()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2));
    t.place(makeEntry("C201", "A201", 1, 2));
    t.clear();
    QVERIFY(t.canPlace(makeEntry("C101", "A101", 1, 2)));
    QVERIFY(t.canPlace(makeEntry("C201", "A201", 1, 2)));
}

/*
TestConflictTable::adjacentSectionsOk - 相邻节次（同教室）互不冲突
*/
void TestConflictTable::adjacentSectionsOk()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 1));
    QVERIFY(t.canPlace(makeEntry("C201", "A101", 1, 2)));
}

/*
TestConflictTable::differentWeeksShareRoomOk - 周范围不重叠时同教室同时刻可复用
*/
void TestConflictTable::differentWeeksShareRoomOk()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2, QString(), 1, 3));        // 1~3 周
    QVERIFY(t.canPlace(makeEntry("C201", "A101", 1, 2, QString(), 5, 8)));  // 5~8 周
}

/*
TestConflictTable::differentWeeksShareTeacherOk - 周范围不重叠时同教师同时刻可复用
*/
void TestConflictTable::differentWeeksShareTeacherOk()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2, "T001", 1, 3));
    QVERIFY(t.canPlace(makeEntry("C201", "A201", 1, 2, "T001", 5, 8)));
}

/*
TestConflictTable::overlappingWeeksConflict - 周范围重叠时同教室同时刻冲突
*/
void TestConflictTable::overlappingWeeksConflict()
{
    ConflictTable t;
    t.place(makeEntry("C101", "A101", 1, 2, QString(), 1, 5));
    QVERIFY(!t.canPlace(makeEntry("C201", "A101", 1, 2, QString(), 3, 8)));
}

/*
TestConflictTable::roomRulesCapacityOk - H4：教室容量 ≥ 教学班人数（roomrules 单源）
*/
void TestConflictTable::roomRulesCapacityOk()
{
    QVERIFY(roomrules::capacityOk(45, 40));    // 容量足够
    QVERIFY(roomrules::capacityOk(40, 40));    // 恰等
    QVERIFY(!roomrules::capacityOk(39, 40));   // 容量不足
    QVERIFY(roomrules::capacityOk(120, 0));    // 空班恒可
}

/*
TestConflictTable::roomRulesTypeOk - H5：教室类型匹配课程所需类型（roomrules 单源）
*/
void TestConflictTable::roomRulesTypeOk()
{
    // 同类型匹配
    QVERIFY(roomrules::typeOk(ClassroomType::Norm, ClassroomType::Norm));
    QVERIFY(roomrules::typeOk(ClassroomType::Lab, ClassroomType::Lab));
    QVERIFY(roomrules::typeOk(ClassroomType::PlayGround, ClassroomType::PlayGround));
    // 类型不符
    QVERIFY(!roomrules::typeOk(ClassroomType::Norm, ClassroomType::Lab));
    QVERIFY(!roomrules::typeOk(ClassroomType::PlayGround, ClassroomType::Norm));
    // 课程要求 Any（不限）恒通过
    QVERIFY(roomrules::typeOk(ClassroomType::Norm, ClassroomType::Any));
    QVERIFY(roomrules::typeOk(ClassroomType::Lab, ClassroomType::Any));
}

/*
TestConflictTable::roomRulesRoomOk - H4 与 H5 同时成立才通过（roomrules 单源）
*/
void TestConflictTable::roomRulesRoomOk()
{
    // 容量与类型都满足
    QVERIFY(roomrules::roomOk(50, ClassroomType::Lab, 40, ClassroomType::Lab));
    // 课程不限类型时只看容量
    QVERIFY(roomrules::roomOk(50, ClassroomType::Lab, 40, ClassroomType::Any));
    // 容量不足
    QVERIFY(!roomrules::roomOk(30, ClassroomType::Lab, 40, ClassroomType::Lab));
    // 类型不符
    QVERIFY(!roomrules::roomOk(50, ClassroomType::Norm, 40, ClassroomType::Lab));
}

QTEST_GUILESS_MAIN(TestConflictTable)
#include "tst_conflicttable.moc"
