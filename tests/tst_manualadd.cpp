/**
 * 文件职责：「从零排入」（manual::validateAdd / manual::applyAdd）单元测试。
 * 给 0 课次的失败教学班整批新增 N 次课。覆盖：整批全部落库并从失败列表移除该班
 * （其余班失败保留）、entryId = 班#1..N 与字段（教师/周范围/跨度）正确、批内自撞、
 * 教室被占（R）与教师忙（T）冲突细分、容量（H4）/类型（H5）拒绝、前提（该班已有
 * 课次）、学时非法、指定次数与应排次数不符，以及 apply 失败零写库（条目与失败项
 * 均不变）。样式对齐 tst_manualmove。
 */

#include <QtTest>

#include <QHash>

#include "core/models/models.h"
#include "core/schedule/conflicttable.h"
#include "core/schedule/manualadd.h"
#include "core/store/datastore.h"

namespace {

/*
makeSession - 构造一条跨 span 节的排课条目（干扰项用，非本次被测新增路径）

Parameter：
    classId: 教学班 id
    room: 教室号
    day: 星期
    startSection: 起始节
    teacher: 教师 id
    span: 单次课跨节数（end = start + span - 1）
    seq: 班内递增序号（从 1 起，用于 entryId）
    startWeek: 起始周（默认 1）
    endWeek: 结束周（默认 16）

Result:
    ScheduleEntry: 构造的条目（entryId = classId#班内序号）
*/
ScheduleEntry makeSession(const QString &classId, const QString &room,
                          int day, int startSection, const QString &teacher,
                          int span, int seq, int startWeek = 1, int endWeek = 16)
{
    ScheduleEntry e;
    e.entryId = classId + '#' + QString::number(seq);
    e.teachingClassId   = classId;
    e.teacherId         = teacher;
    e.classroomId       = room;
    e.timeSlot.dayOfWeek   = day;
    e.timeSlot.startSection = startSection;
    e.timeSlot.endSection   = startSection + span - 1;
    e.startWeek = startWeek;
    e.endWeek   = endWeek;
    return e;
}

/*
makeAdd - 便捷构造一条新增槽位

Parameter：
    day: 星期
    start: 起始节
    room: 教室号

Result:
    manual::AddSlot: 一条待新增槽位
*/
manual::AddSlot makeAdd(int day, int start, const QString &room)
{
    manual::AddSlot s;
    s.dayOfWeek = day;
    s.startSection = start;
    s.classroomId = room;
    return s;
}

/*
fixture - 造一个可控的排课场景

Remark:
    载入 data 示例的基础数据（课程/班/教室/作息）后清空排课，再手工种入干扰条目
    （与被测目标班 C101 不同班）：
      C103（Norm，占 J1-404 周一 1~2 节）—— 教室/教师被占干扰
      C102 两次课（T001）：J2-404 周三 1~2、J3-404 周五 3~4 —— 教师 T001 忙段
      C301（Lab，周 9~16）：Lab1 周二 1~2 —— 机房占用干扰
    目标班 C101（C01 高等数学，每周 2 次 × 2 节、计划 120 人、Norm）零课次，
    其可用教室为 J1..J6-404（容量 200）。
*/
DataStore fixture()
{
    DataStore store;
    store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv");
    store.clearScheduleEntries();

    store.addScheduleEntry(makeSession("C103", "J1-404", 1, 1, "T002", 2, 1));
    store.addScheduleEntry(makeSession("C102", "J2-404", 3, 1, "T001", 2, 1));
    store.addScheduleEntry(makeSession("C102", "J3-404", 5, 3, "T001", 2, 2));
    store.addScheduleEntry(makeSession("C301", "Lab1",   2, 1, "T004", 2, 1, 9, 16));
    return store;
}

/*
failureOf - 便捷构造一条失败明细（默认目标班 C101）

Parameter：
    classId: 教学班 id（默认 "C101"）

Result:
    ScheduleFailure: 失败明细
*/
ScheduleFailure failureOf(const QString &classId = QStringLiteral("C101"))
{
    ScheduleFailure f;
    f.classId = classId;
    f.courseId = QStringLiteral("C01");
    f.courseName = QStringLiteral("高等数学");
    f.reason = QStringLiteral("测试：无处可排");
    return f;
}

/*
entryKey - 条目规范化键（逐字段可比；ScheduleEntry 无 operator==）

Parameter：
    e: 排课条目

Result:
    QString: id/教学班/教室/时段/周范围 全字段键
*/
QString entryKey(const ScheduleEntry &e)
{
    return e.entryId + '|' + e.teachingClassId + '|' + e.classroomId + '|'
           + QString::number(e.timeSlot.dayOfWeek) + '|'
           + QString::number(e.timeSlot.startSection) + '|'
           + QString::number(e.timeSlot.endSection) + '|'
           + QString::number(e.startWeek) + '|' + QString::number(e.endWeek)
           + '|' + e.teacherId;
}

/*
entryMultiset - 条目列表转多重集合（比较两组排课是否逐条一致）

Parameter：
    entries: 排课条目列表

Result:
    QHash<QString,int>: 规范化键 → 出现次数
*/
QHash<QString, int> entryMultiset(const QVector<ScheduleEntry> &entries)
{
    QHash<QString, int> bag;
    for (const ScheduleEntry &e : entries)
        ++bag[entryKey(e)];
    return bag;
}

/*
hasBusyTag - busyTags 里是否出现指定前缀（R=教室 C=教学班 T=教师）

Parameter：
    report: 校验报告
    prefix: 前缀字符（'R'/'C'/'T'）

Result:
    bool: 出现返回 true
*/
bool hasBusyTag(const manual::Report &report, const QChar &prefix)
{
    const QString head = QString(prefix) + QLatin1Char('|');
    for (const QString &k : report.busyTags)
        if (k.startsWith(head))
            return true;
    return false;
}

} // namespace

class TestManualAdd : public QObject
{
    Q_OBJECT

private slots:
    void addFullOkAndClearsOnlyTargetFailure();
    void selfCollisionInBatch();
    void occupiedRoomConflict();
    void teacherBusyConflict();
    void capacityRejected();
    void typeRejected();
    void alreadyPlacedRejected();
    void invalidHoursRejected();
    void countMismatchRejected();
    void applyFailKeepsStoreUntouched();
};

/*
TestManualAdd::addFullOkAndClearsOnlyTargetFailure - 两槽全部落库、entryId 与字段正确、
目标班失败移除而其它班失败保留
*/
void TestManualAdd::addFullOkAndClearsOnlyTargetFailure()
{
    DataStore store = fixture();
    store.setScheduleFailures({failureOf(), failureOf(QStringLiteral("C202"))});
    const int n = int(store.scheduleEntries().size());

    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J2-404"),   // C101 周一 1~2（C103 占的是 J1-404，不冲突）
        makeAdd(4, 1, "J4-404"),   // C101 周四 1~2
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(rep.ok);

    QVERIFY(manual::applyAdd(store, QStringLiteral("C101"), adds));
    QCOMPARE(int(store.scheduleEntries().size()), n + 2);

    const ScheduleEntry *a = store.scheduleEntryById(QStringLiteral("C101#1"));
    const ScheduleEntry *b = store.scheduleEntryById(QStringLiteral("C101#2"));
    QVERIFY(a && b);
    QCOMPARE(a->timeSlot.dayOfWeek, 1);
    QCOMPARE(a->timeSlot.startSection, 1);
    QCOMPARE(a->classroomId, QStringLiteral("J2-404"));
    QCOMPARE(b->timeSlot.dayOfWeek, 4);
    QCOMPARE(a->teacherId, QStringLiteral("T001"));     // 教师沿教学班
    QCOMPARE(a->startWeek, 1);                          // 周范围沿课程模板
    QCOMPARE(a->endWeek, 16);
    QCOMPARE(a->timeSlot.endSection - a->timeSlot.startSection + 1, 2);  // 跨度 2 节

    // 目标班失败移除、C202 的失败保留
    const QVector<ScheduleFailure> &fails = store.scheduleFailures();
    QCOMPARE(fails.size(), 1);
    QCOMPARE(fails.at(0).classId, QStringLiteral("C202"));
}

/*
TestManualAdd::selfCollisionInBatch - 批内两次课同日同起始节 → 同班自撞（C 键）
*/
void TestManualAdd::selfCollisionInBatch()
{
    DataStore store = fixture();
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J2-404"),
        makeAdd(1, 1, "J4-404"),   // 与上一行同一时段（不同教室）→ 本班自撞
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QCOMPARE(rep.index, 1);
    QVERIFY(hasBusyTag(rep, QLatin1Char('C')));
}

/*
TestManualAdd::occupiedRoomConflict - 排进被其它班占用的教室时段 → 教室冲突（R 键）
*/
void TestManualAdd::occupiedRoomConflict()
{
    DataStore store = fixture();
    // J1-404 周一 1~2 已被 C103 占（目标班 C101 挪入同教室同时段）
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J1-404"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QCOMPARE(rep.index, 0);
    QVERIFY(hasBusyTag(rep, QLatin1Char('R')));
}

/*
TestManualAdd::teacherBusyConflict - 排到该班教师已忙时段 → 教师冲突（T 键）
*/
void TestManualAdd::teacherBusyConflict()
{
    DataStore store = fixture();
    // C101 教师 T001 周三 1~2 已在 J2-404 给 C102 上课；换空教室 J5-404 仍撞教师
    const QVector<manual::AddSlot> adds = {
        makeAdd(3, 1, "J5-404"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QVERIFY(hasBusyTag(rep, QLatin1Char('T')));
}

/*
TestManualAdd::capacityRejected - 目标教室容量不足（H4）被拒
*/
void TestManualAdd::capacityRejected()
{
    DataStore store = fixture();
    // C101 计划 120 人，A101 容量 60
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "A101"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::CapacityTooSmall);
    QCOMPARE(rep.index, 0);
}

/*
TestManualAdd::typeRejected - 目标教室类型不符（H5）被拒
*/
void TestManualAdd::typeRejected()
{
    DataStore store = fixture();
    // C211 需 Lab（计划 22 人），给普通教室 B201（容量 30 ≥ 22）→ 类型不符
    const QVector<manual::AddSlot> adds = {
        makeAdd(4, 1, "B201"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C211"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::RoomTypeMismatch);
}

/*
TestManualAdd::alreadyPlacedRejected - 该班已有课次 → 从零排入前提不满足
*/
void TestManualAdd::alreadyPlacedRejected()
{
    DataStore store = fixture();
    store.addScheduleEntry(makeSession("C101", "J2-404", 2, 1, "T001", 2, 9));
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J2-404"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::AlreadyPlaced);
}

/*
TestManualAdd::invalidHoursRejected - 课程单次学时非法（非整数）→ 无法从零排入
*/
void TestManualAdd::invalidHoursRejected()
{
    DataStore store = fixture();
    Course c = *store.courseById(QStringLiteral("C01"));
    c.hoursPerSession = 1.5;
    QVERIFY(store.updateCourse(c));

    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J2-404"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::InvalidHours);
}

/*
TestManualAdd::countMismatchRejected - 指定次数 ≠ 每周应排次数（防御）
*/
void TestManualAdd::countMismatchRejected()
{
    DataStore store = fixture();
    // C101 每周 2 次，只给 1 条
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J2-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::SlotCountMismatch);
}

/*
TestManualAdd::applyFailKeepsStoreUntouched - applyAdd 遇冲突返回 false 且条目与失败项不变
*/
void TestManualAdd::applyFailKeepsStoreUntouched()
{
    DataStore store = fixture();
    store.setScheduleFailures({failureOf()});
    const auto before = entryMultiset(store.scheduleEntries());
    const auto failsBefore = store.scheduleFailures();

    // 第一槽撞 C103（J1-404 周一 1~2），第二槽本身可行
    const QVector<manual::AddSlot> adds = {
        makeAdd(1, 1, "J1-404"),
        makeAdd(4, 1, "J4-404"),
    };
    const manual::Report rep = manual::validateAdd(store, QStringLiteral("C101"), adds);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);

    QVERIFY(!manual::applyAdd(store, QStringLiteral("C101"), adds));  // 不写库
    QVERIFY(entryMultiset(store.scheduleEntries()) == before);
    const QVector<ScheduleFailure> &fails = store.scheduleFailures();
    QCOMPARE(fails.size(), failsBefore.size());      // 失败明细也不动
    for (int i = 0; i < fails.size(); ++i)
        QCOMPARE(fails.at(i).classId, failsBefore.at(i).classId);
}

QTEST_GUILESS_MAIN(TestManualAdd)
#include "tst_manualadd.moc"
