/**
 * 文件职责：movable/冻结「局部排课」引擎单元测试。
 * 覆盖：默认全量排课零回归；锁定子集重排（冻结班条目原样保留、全表零冲突）；
 * 新增课程最小排（存量条目零扰动）；拥挤场景最小排放不下时正确上报失败（升格判据）。
 */

#include <QtTest>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include "core/models/models.h"
#include "core/schedule/conflicttable.h"
#include "core/schedule/scheduler.h"
#include "core/schedule/strategy.h"
#include "core/store/datastore.h"

namespace {

/*
sampleStore - 载入 data 示例数据的数据仓库

Result:
    DataStore: 已载入教学班 / 教室 / 作息表的仓库（11 班 → 20 条排课）
*/
DataStore sampleStore()
{
    DataStore store;
    store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv");
    return store;
}

/*
entryKey - 条目的规范化键（排课结果逐字段可比）

Parameter：
    e: 排课条目

Result:
    QString: 教学班/教室/时段/周范围的键
*/
QString entryKey(const ScheduleEntry &e)
{
    return e.teachingClassId + '|' + e.classroomId + '|'
           + QString::number(e.timeSlot.dayOfWeek) + '|'
           + QString::number(e.timeSlot.startSection) + '|'
           + QString::number(e.timeSlot.endSection) + '|'
           + QString::number(e.startWeek) + '|' + QString::number(e.endWeek);
}

/*
entryMultiset - 条目列表转多重集合（用于比较两组排课是否逐条一致）

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
expectedSessions - 某批教学班应排的课次总数

Parameter：
    store: 数据仓库（含课程周课次）
    classIds: 关注的班级集合

Result:
    int: 这些班 sessionsPerWeek 之和
*/
int expectedSessions(const DataStore &store, const QSet<QString> &classIds)
{
    QHash<QString, int> sessionsOf;
    for (const Course &c : store.courses())
        sessionsOf.insert(c.id, c.sessionsPerWeek);
    int n = 0;
    for (const TeachingClass &tc : store.teachingClasses())
        if (classIds.contains(tc.classId))
            n += sessionsOf.value(tc.courseId, 0);
    return n;
}

/*
scheduleValid - 校验整表硬约束（无冲突 + 容量/类型匹配）

Parameter：
    store: 数据仓库（含排课结果）

Result:
    bool: 全表合法返回 true
*/
bool scheduleValid(const DataStore &store)
{
    QHash<QString, QString> courseOfClass;
    QHash<QString, int> planned;
    for (const TeachingClass &tc : store.teachingClasses()) {
        courseOfClass.insert(tc.classId, tc.courseId);
        planned.insert(tc.classId, tc.plannedSize);
    }
    QHash<QString, ClassroomType> needOf;
    for (const Course &c : store.courses())
        needOf.insert(c.id, c.requiredRoomType);
    QHash<QString, int> capOf;
    QHash<QString, ClassroomType> typeOf;
    for (const Classroom &r : store.classrooms()) {
        capOf.insert(r.roomNumber, r.capacity);
        typeOf.insert(r.roomNumber, r.type);
    }

    ConflictTable table;
    for (const ScheduleEntry &e : store.scheduleEntries()) {
        if (planned.value(e.teachingClassId, 0) > capOf.value(e.classroomId, 0))
            return false;                                  // H4 容量
        const ClassroomType need = needOf.value(courseOfClass.value(e.teachingClassId));
        if (need != ClassroomType::Any && typeOf.value(e.classroomId) != need)
            return false;                                  // H5 类型
        if (!table.canPlace(e))
            return false;                                  // H1~H3 冲突
        table.place(e);
    }
    return true;
}

} // namespace

class TestMovable : public QObject
{
    Q_OBJECT

private slots:
    void fullRescheduleStillAllMovable();
    void lockedSubsetPreservedBySa();
    void incrementalAddZeroDisturbance();
    void crowdedInsertReportsFailure();
};

/*
TestMovable::fullRescheduleStillAllMovable - movable 为空 = 全量排课（零回归）
*/
void TestMovable::fullRescheduleStillAllMovable()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(store);    // movable 空 → 所有班可动
    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);
    QCOMPARE(int(store.scheduleEntries().size()), 20);
    QVERIFY(scheduleValid(store));
}

/*
TestMovable::lockedSubsetPreservedBySa - 锁定子集后局部重排：冻结班条目原样保留

Remark:
    基线用贪心排成"按难度序"的确定布局；锁定排在前面的 C01 整课 + C301，
    部分重排走默认 SA，仅可动班被退火移动，冻结班（含 SA 内部贪心重放）必须逐条一致。
*/
void TestMovable::lockedSubsetPreservedBySa()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    QVERIFY(scheduler.schedule(store, &greedy).ok);

    const QSet<QString> locked = { QStringLiteral("C101"), QStringLiteral("C102"),
                                   QStringLiteral("C103"), QStringLiteral("C301") };
    QVector<ScheduleEntry> lockedBefore;
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (locked.contains(e.teachingClassId))
            lockedBefore.append(e);
    const QHash<QString, int> lockedBag = entryMultiset(lockedBefore);

    QSet<QString> movable;
    for (const TeachingClass &tc : store.teachingClasses())
        if (!locked.contains(tc.classId))
            movable.insert(tc.classId);
    QVERIFY(!movable.isEmpty());
    const int movableSessions = expectedSessions(store, movable);

    const ScheduleResult r = scheduler.schedule(store, nullptr, nullptr, movable);
    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);
    QCOMPARE(r.scheduledCount, movableSessions);            // 可动班全部排上
    QCOMPARE(int(store.scheduleEntries().size()), 20);

    QVector<ScheduleEntry> lockedAfter;
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (locked.contains(e.teachingClassId))
            lockedAfter.append(e);
    QCOMPARE(entryMultiset(lockedAfter), lockedBag);        // 冻结班逐条一致
    QVERIFY(scheduleValid(store));
}

/*
TestMovable::incrementalAddZeroDisturbance - 新增课程最小排：存量条目零扰动
*/
void TestMovable::incrementalAddZeroDisturbance()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    QVERIFY(scheduler.schedule(store, &greedy).ok);
    const QVector<ScheduleEntry> before = store.scheduleEntries();
    const QHash<QString, int> beforeBag = entryMultiset(before);

    // 新增一门新课 + 一个教学班
    Course c;
    c.id = QStringLiteral("C99");
    c.name = QStringLiteral("线性代数");
    c.credit = 2;
    c.sessionsPerWeek = 2;
    c.hoursPerSession = 1;
    c.depart = QStringLiteral("理学院");
    c.startWeek = 1;
    c.endWeek = 16;
    c.requiredRoomType = ClassroomType::Any;
    QVERIFY(store.addCourse(c));
    TeachingClass tc;
    tc.classId = QStringLiteral("C990");
    tc.courseId = QStringLiteral("C99");
    tc.teacherId = QStringLiteral("T001");
    tc.plannedSize = 30;
    tc.maxCapacity = 60;
    QVERIFY(store.addTeachingClass(tc));

    // 最小排：只排新班（其余冻结为背景）
    const QSet<QString> movable = { QStringLiteral("C990") };
    const ScheduleResult r = scheduler.schedule(store, &greedy, nullptr, movable);
    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);

    QVector<ScheduleEntry> kept;
    for (const ScheduleEntry &e : store.scheduleEntries())
        if (e.teachingClassId != QStringLiteral("C990"))
            kept.append(e);
    QCOMPARE(entryMultiset(kept), beforeBag);               // 存量逐条不变
    QCOMPARE(int(store.scheduleEntries().size()) - int(kept.size()), 2);  // 新班 2 次课
    QVERIFY(scheduleValid(store));
}

/*
TestMovable::crowdedInsertReportsFailure - 表已排满时最小排失败正确上报（升格判据）
*/
void TestMovable::crowdedInsertReportsFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
               "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n";
        for (int i = 1; i <= 7; ++i) {                      // 7 个班占满 7 天唯一空位
            out << "C10" << i << ",C01,课A,1,1,1,理学院,T0" << i
                << ",30,40,1,16,Any\n";
        }
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,40,Norm\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n1,08:00,08:45\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult first = scheduler.schedule(store, &greedy);
    QVERIFY(first.ok);
    QCOMPARE(int(store.scheduleEntries().size()), 7);

    Course c2;
    c2.id = QStringLiteral("C02");
    c2.name = QStringLiteral("课B");
    c2.credit = 1;
    c2.sessionsPerWeek = 1;
    c2.hoursPerSession = 1;
    c2.depart = QStringLiteral("理学院");
    c2.startWeek = 1;
    c2.endWeek = 16;
    c2.requiredRoomType = ClassroomType::Any;
    QVERIFY(store.addCourse(c2));
    TeachingClass tc;
    tc.classId = QStringLiteral("C201");
    tc.courseId = QStringLiteral("C02");
    tc.teacherId = QStringLiteral("T008");
    tc.plannedSize = 30;
    tc.maxCapacity = 40;
    QVERIFY(store.addTeachingClass(tc));

    const QSet<QString> movable = { QStringLiteral("C201") };
    const ScheduleResult r = scheduler.schedule(store, &greedy, nullptr, movable);
    QVERIFY(!r.ok);                                         // 排不下 → 升格判据
    QCOMPARE(r.failedCount, 1);
    QVERIFY(r.failedClassIds.contains(QStringLiteral("C201")));
    QCOMPARE(int(store.scheduleEntries().size()), 7);       // 存量原样保留
    QVERIFY(scheduleValid(store));
}

QTEST_GUILESS_MAIN(TestMovable)
#include "tst_movable.moc"
