/**
 * 文件职责：教务手动调整（manual::validate / manual::apply）单元测试。
 * 覆盖：单节换时间/换教室、容量/类型/教室缺失/范围拒绝、教室被占与本班自撞
 * 冲突（含 busyKeysOf 细分标签）、整班批量原子落库、整批一节冲突整体拒绝且
 * 不写库、entryId 不存在。
 */

#include <QtTest>

#include <QHash>

#include "core/models/models.h"
#include "core/schedule/conflicttable.h"
#include "core/schedule/manualmove.h"
#include "core/store/datastore.h"

namespace {

/*
makeSession - 构造一条跨 span 节的排课条目（教务手动调整以旧条目跨度为时长锚）

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
    ScheduleEntry: 构造的条目（entryId = classId#班内序号，不内嵌时间）
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
fixture - 造一个可控的排课场景

Remark:
    载入 data 示例的基础数据（课程/班/教室/作息）后清空排课，再手工种入下列条目：
      C103（Norm，占 J1-404 周一 1~2 节）—— 作“教室被占”的干扰项
      C102 两次课：J2-404 周三 1~2 节、J3-404 周五 3~4 节
      C301（Lab，周 9~16）：Lab1 周二 1~2 节 —— 供类型不符测试
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
singleChange - 便捷构造单条变更

Parameter：
    entryId: 旧条目 id
    day: 新星期
    startSection: 新起始节
    room: 新教室

Result:
    QVector<manual::Change>: 仅含一条变更的批
*/
QVector<manual::Change> singleChange(const QString &entryId, int day,
                                     int startSection, const QString &room)
{
    manual::Change c;
    c.entryId = entryId;
    c.dayOfWeek = day;
    c.startSection = startSection;
    c.classroomId = room;
    return {c};
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

class TestManualMove : public QObject
{
    Q_OBJECT

private slots:
    void singleTimeMoveOk();
    void singleRoomMoveOk();
    void roomCapacityRejected();
    void roomTypeRejected();
    void missingRoomRejected();
    void occupiedRoomConflict();
    void sameClassSelfCollision();
    void wholeClassBatchOk();
    void batchConflictRollsBack();
    void unknownEntryId();
    void badRangeRejected();
    void legacyIdStillHits();
    void sameClassEntriesHaveDistinctIds();
};

/*
TestManualMove::singleTimeMoveOk - 单节改星期/起始节成功且原位更新
*/
void TestManualMove::singleTimeMoveOk()
{
    DataStore store = fixture();
    const int n = int(store.scheduleEntries().size());

    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 4, 1, "J2-404"));
    QVERIFY(rep.ok);

    QVERIFY(manual::apply(store, singleChange("C102#1", 4, 1, "J2-404")));
    const ScheduleEntry *e = store.scheduleEntryById(QLatin1String("C102#1"));
    QVERIFY(e);
    QCOMPARE(e->timeSlot.dayOfWeek, 4);
    QCOMPARE(e->timeSlot.startSection, 1);
    QCOMPARE(e->timeSlot.endSection, 2);          // 跨度保持 2 节
    QCOMPARE(int(store.scheduleEntries().size()), n);   // 数量不变
}

/*
TestManualMove::singleRoomMoveOk - 单节换教室（同时间）成功
*/
void TestManualMove::singleRoomMoveOk()
{
    DataStore store = fixture();
    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 3, 1, "J1-404"));
    QVERIFY(rep.ok);
    QVERIFY(manual::apply(store, singleChange("C102#1", 3, 1, "J1-404")));
    QCOMPARE(store.scheduleEntryById(QLatin1String("C102#1"))->classroomId,
             QLatin1String("J1-404"));
}

/*
TestManualMove::roomCapacityRejected - 目标教室容量不足（H4）被拒
*/
void TestManualMove::roomCapacityRejected()
{
    DataStore store = fixture();
    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 3, 1, "A101"));  // 60 < 110
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::CapacityTooSmall);
    QCOMPARE(rep.index, 0);
}

/*
TestManualMove::roomTypeRejected - 目标教室类型不符（H5）被拒
*/
void TestManualMove::roomTypeRejected()
{
    DataStore store = fixture();
    // C301 需 Lab，换成 Norm 教室（容量够）→ 类型不符
    const manual::Report rep =
        manual::validate(store, singleChange("C301#1", 2, 1, "J1-101"));
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::RoomTypeMismatch);
}

/*
TestManualMove::missingRoomRejected - 教室号查无被拒
*/
void TestManualMove::missingRoomRejected()
{
    DataStore store = fixture();
    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 3, 1, "NOPE"));
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::RoomMissing);
}

/*
TestManualMove::occupiedRoomConflict - 挪进被其它班占用的教室时段 → 冲突且带 R 标签
*/
void TestManualMove::occupiedRoomConflict()
{
    DataStore store = fixture();
    // J1-404 周一 1~2 节已被 C103 占（C102 挪入同教室同时段）
    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 1, 1, "J1-404"));
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QVERIFY(hasBusyTag(rep, QLatin1Char('R')));
}

/*
TestManualMove::sameClassSelfCollision - 整批内两条同班课摆到同一时段 → 批内自撞
*/
void TestManualMove::sameClassSelfCollision()
{
    DataStore store = fixture();
    QVector<manual::Change> changes = {
        singleChange("C102#1", 4, 1, "J2-404").at(0),
        singleChange("C102#2", 4, 1, "J3-404").at(0),
    };
    const manual::Report rep = manual::validate(store, changes);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QCOMPARE(rep.index, 1);                       // 第二条自撞
    QVERIFY(hasBusyTag(rep, QLatin1Char('C')));   // 同班时间冲突（C 键）
}

/*
TestManualMove::wholeClassBatchOk - 整班两条课一起换到新时间/教室并原子落库
*/
void TestManualMove::wholeClassBatchOk()
{
    DataStore store = fixture();
    const int n = int(store.scheduleEntries().size());

    QVector<manual::Change> changes = {
        singleChange("C102#1", 1, 5, "J4-404").at(0),
        singleChange("C102#2", 3, 3, "J5-404").at(0),
    };
    QVERIFY(manual::validate(store, changes).ok);
    QVERIFY(manual::apply(store, changes));

    const ScheduleEntry *a = store.scheduleEntryById(QLatin1String("C102#1"));
    const ScheduleEntry *b = store.scheduleEntryById(QLatin1String("C102#2"));
    QVERIFY(a && b);
    QCOMPARE(a->timeSlot.dayOfWeek, 1);
    QCOMPARE(a->timeSlot.startSection, 5);
    QCOMPARE(a->classroomId, QLatin1String("J4-404"));
    QCOMPARE(b->timeSlot.dayOfWeek, 3);
    QCOMPARE(b->timeSlot.startSection, 3);
    QCOMPARE(b->classroomId, QLatin1String("J5-404"));
    QCOMPARE(int(store.scheduleEntries().size()), n);
}

/*
TestManualMove::batchConflictRollsBack - 整批中一节冲突：整体拒绝且不写库
*/
void TestManualMove::batchConflictRollsBack()
{
    DataStore store = fixture();
    const auto before = entryMultiset(store.scheduleEntries());

    QVector<manual::Change> changes = {
        singleChange("C102#1", 1, 1, "J1-404").at(0),   // 撞 C103（J1-404 周一 1~2）
        singleChange("C102#2", 1, 5, "J5-404").at(0),   // 本身可行
    };
    const manual::Report rep = manual::validate(store, changes);
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::Conflict);
    QCOMPARE(rep.index, 0);

    QVERIFY(!manual::apply(store, changes));              // apply 也拒绝
    QVERIFY(entryMultiset(store.scheduleEntries()) == before);  // 零改动（不写库）
}

/*
TestManualMove::unknownEntryId - entryId 在 store 不存在
*/
void TestManualMove::unknownEntryId()
{
    DataStore store = fixture();
    const manual::Report rep =
        manual::validate(store, singleChange("NOPE-9-9", 1, 1, "A101"));
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::EntryMissing);
}

/*
TestManualMove::badRangeRejected - 起始节使结束节超作息最大节（8）被拒
*/
void TestManualMove::badRangeRejected()
{
    DataStore store = fixture();
    // C102#1 跨 2 节：start=8 → end=9 > 8
    const manual::Report rep =
        manual::validate(store, singleChange("C102#1", 3, 8, "J2-404"));
    QVERIFY(!rep.ok);
    QCOMPARE(rep.reject, manual::Reject::BadRange);
}

/*
TestManualMove::legacyIdStillHits - 老格式 entryId（classId-星期-起始节）仍可被命中与原位替换

Remark:
    覆盖 R2 验收点 3：新老格式 id 共存，老格式条目靠字符串相等命中
    scheduleEntryById / updateScheduleEntry，不要求格式一致。
*/
void TestManualMove::legacyIdStillHits()
{
    DataStore store = fixture();
    // 手工种入一条老格式 id 条目（classId-星期-起始节），不走 makeSession
    ScheduleEntry legacy;
    legacy.entryId              = QStringLiteral("C102-4-1");
    legacy.teachingClassId      = QStringLiteral("C102");
    legacy.teacherId            = QStringLiteral("T001");
    legacy.classroomId          = QStringLiteral("J2-404");
    legacy.timeSlot.dayOfWeek     = 4;
    legacy.timeSlot.startSection = 1;
    legacy.timeSlot.endSection   = 2;
    legacy.startWeek = 1;
    legacy.endWeek   = 16;
    store.addScheduleEntry(legacy);

    // 老格式 id 可被 scheduleEntryById 命中
    const ScheduleEntry *p = store.scheduleEntryById(QStringLiteral("C102-4-1"));
    QVERIFY(p != nullptr);
    QCOMPARE(p->classroomId, QStringLiteral("J2-404"));

    // updateScheduleEntry 改教室后仍由老格式 id 命中（原位替换）
    ScheduleEntry updated = *p;
    updated.classroomId = QStringLiteral("J3-404");
    QVERIFY(store.updateScheduleEntry(updated));
    const ScheduleEntry *q = store.scheduleEntryById(QStringLiteral("C102-4-1"));
    QVERIFY(q != nullptr);
    QCOMPARE(q->classroomId, QStringLiteral("J3-404"));
}

/*
TestManualMove::sameClassEntriesHaveDistinctIds - 同班多条目 entryId 两两不同且不内嵌时间

Remark:
    覆盖 R2 验收点 2：同班条目 id 形如 classId#班内序号，序号班内递增、两两不同，
    字面不含星期/节次。
*/
void TestManualMove::sameClassEntriesHaveDistinctIds()
{
    DataStore store = fixture();
    // C102 在 fixture 里两次课，序号 1 / 2
    const ScheduleEntry *a = store.scheduleEntryById(QStringLiteral("C102#1"));
    const ScheduleEntry *b = store.scheduleEntryById(QStringLiteral("C102#2"));
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);
    QVERIFY(a->entryId != b->entryId);
    QCOMPARE(a->entryId, QStringLiteral("C102#1"));
    QCOMPARE(b->entryId, QStringLiteral("C102#2"));
    // id 不内嵌真实时间：C102 两条分别在周三/周五，id 字面不含 '-'（老格式分隔符）
    QVERIFY(!a->entryId.contains(QLatin1String("-")));
    QVERIFY(!b->entryId.contains(QLatin1String("-")));
}

QTEST_GUILESS_MAIN(TestManualMove)
#include "tst_manualmove.moc"
