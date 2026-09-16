/**
 * 文件职责：Scheduler / GreedyStrategy 排课正确性单元测试。
 * 覆盖：无冲突、容量满足、同班时间互斥、次数精确、失败上报、重复排课不累积，
 * 以及周范围错开的课程复用同一教室 / 教师。
 */

#include <QtTest>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include "core/schedule/scheduler.h"
#include "core/schedule/strategy.h"
#include "core/store/datastore.h"

namespace {

/*
sampleStore - 载入示例数据的数据仓库

Result:
    DataStore: 已载入教学班 / 教室 / 作息表的仓库
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

} // namespace

class TestScheduler : public QObject
{
    Q_OBJECT

private slots:
    void schedulesSampleData();
    void noConflictAndCapacityOk();
    void eachClassGetsSessions();
    void failureWhenRoomsInsufficient();
    void disjointWeeksReuseRoom();
    void sameCourseShareDays();
    void sameClassSameRoom();
    void dispersionTwoSessionsPerWeek();
    void classroomTypeMatching();
    void failureReasonCapacityInsufficient();
    void multiSectionSessions();
    void nonIntegerHoursRejected();
    void rescheduleDoesNotAccumulate();
};

/*
TestScheduler::schedulesSampleData - 示例数据全部排入：11 班 × 周课次 = 20 条
*/
void TestScheduler::schedulesSampleData()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);

    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);
    QCOMPARE(r.scheduledCount, 20);
    QCOMPARE(int(store.scheduleEntries().size()), 20);
}

/*
TestScheduler::noConflictAndCapacityOk - 无教室/教学班/教师冲突，容量满足
*/
void TestScheduler::noConflictAndCapacityOk()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    scheduler.schedule(store, &greedy);

    QSet<QString> roomKeys, classKeys, teacherKeys;
    qsizetype roomTotal = 0, classTotal = 0, teacherTotal = 0;
    for (const ScheduleEntry &e : store.scheduleEntries()) {
        // 容量：教室容量 ≥ 教学班人数
        int planned = 0, cap = 0;
        for (const TeachingClass &tc : store.teachingClasses())
            if (tc.classId == e.teachingClassId) { planned = tc.plannedSize; break; }
        for (const Classroom &c : store.classrooms())
            if (c.roomNumber == e.classroomId) { cap = c.capacity; break; }
        QVERIFY2(planned <= cap, "教室容量不足");

        // 占用键按周展开，与 ConflictTable 语义一致：
        // 同教室 / 同班 / 同教师在「同 (day, section, 周)」只能出现一次。
        // 不同周的课键不同，可复用教室 / 教师，故不再要求键数与条目数相等。
        const int day = e.timeSlot.dayOfWeek;
        const int sec = e.timeSlot.startSection;
        for (int w = e.startWeek; w <= e.endWeek; ++w) {
            const QString ws = QString::number(w);
            roomKeys.insert(e.classroomId + '|' + QString::number(day) + '|'
                            + QString::number(sec) + '|' + ws);
            classKeys.insert(e.teachingClassId + '|' + QString::number(day) + '|'
                             + QString::number(sec) + '|' + ws);
            if (!e.teacherId.isEmpty())
                teacherKeys.insert(e.teacherId + '|' + QString::number(day) + '|'
                                   + QString::number(sec) + '|' + ws);
            ++roomTotal; ++classTotal; ++teacherTotal;
        }
    }

    // 无重复键即无冲突（同教室同周同时刻只一班 / 同班互斥 / 同教师互斥）
    QCOMPARE(roomKeys.size(), roomTotal);
    QCOMPARE(classKeys.size(), classTotal);
    QCOMPARE(teacherKeys.size(), teacherTotal);
}

/*
TestScheduler::eachClassGetsSessions - 每班恰排够 sessionsPerWeek 次
*/
void TestScheduler::eachClassGetsSessions()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    scheduler.schedule(store, &greedy);

    QHash<QString, int> sessionsOf;
    for (const Course &c : store.courses())
        sessionsOf.insert(c.id, c.sessionsPerWeek);

    for (const TeachingClass &tc : store.teachingClasses()) {
        int got = 0;
        for (const ScheduleEntry &e : store.scheduleEntries())
            if (e.teachingClassId == tc.classId)
                ++got;
        QCOMPARE(got, sessionsOf.value(tc.courseId));
    }
}

/*
TestScheduler::failureWhenRoomsInsufficient - 教室不足时失败可上报
*/
void TestScheduler::failureWhenRoomsInsufficient()
{
    // 造一个只有 1 间 30 人教室的作息表
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString roomPath = dir.filePath("rooms.csv");
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,30,Norm\n");
    }

    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        roomPath,
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(!r.ok);
    QVERIFY(r.failedCount > 0);
    QVERIFY(!r.failedClassIds.isEmpty());
}

/*
TestScheduler::disjointWeeksReuseRoom - 周范围不重叠的两门课复用同一教室 / 教师同一时段
*/
void TestScheduler::disjointWeeksReuseRoom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 仅 1 间教室、1 个节次；两门课周范围错开（1~3 周 与 5~8 周），同教师
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,课A,1,1,1,理学院,T001,30,40,1,3,Any\n");
        f.write("C201,C02,课B,1,1,1,理学院,T001,30,40,5,8,Any\n");
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
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(r.ok);
    QCOMPARE(r.scheduledCount, 2);

    // 两门课都落在同一 (教室, 星期, 节次)，仅靠周范围错开
    bool placedA = false, placedB = false;
    for (const ScheduleEntry &e : store.scheduleEntries()) {
        QCOMPARE(e.classroomId, QStringLiteral("A101"));
        QCOMPARE(e.timeSlot.dayOfWeek, 1);
        QCOMPARE(e.timeSlot.startSection, 1);
        if (e.teachingClassId == QLatin1String("C101")) {
            placedA = true;
            QCOMPARE(e.startWeek, 1);
            QCOMPARE(e.endWeek, 3);
        }
        if (e.teachingClassId == QLatin1String("C201")) {
            placedB = true;
            QCOMPARE(e.startWeek, 5);
            QCOMPARE(e.endWeek, 8);
        }
    }
    QVERIFY(placedA);
    QVERIFY(placedB);
}

/*
TestScheduler::sameCourseShareDays - 同课程的教学班共用同一套时间模式（同天）
*/
void TestScheduler::sameCourseShareDays()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 一门课 N=2，两个教学班，仅一间教室 → 同一天、不同节次
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,课A,2,2,1,理学院,T001,30,40,1,16,Any\n");
        f.write("C102,C01,课A,2,2,1,理学院,T002,30,40,1,16,Any\n");
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
        f.write("index,startTime,endTime\n"
                "1,08:00,08:45\n2,08:55,09:40\n3,10:00,10:45\n4,10:55,11:40\n"
                "5,14:00,14:45\n6,14:55,15:40\n7,16:00,16:45\n8,16:55,17:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(r.ok);
    QCOMPARE(r.scheduledCount, 4);

    // 两班都在 周一/周五（同课程共享同一时间模式）
    QHash<QString, QSet<int>> daysOf;
    for (const ScheduleEntry &e : store.scheduleEntries())
        daysOf[e.teachingClassId].insert(e.timeSlot.dayOfWeek);
    QCOMPARE(daysOf.value(QStringLiteral("C101")), QSet<int>({1, 5}));
    QCOMPARE(daysOf.value(QStringLiteral("C102")), QSet<int>({1, 5}));
}

/*
TestScheduler::sameClassSameRoom - 同一教学班每次课固定同一教室（best-fit 取最小容量）
*/
void TestScheduler::sameClassSameRoom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 一门课 N=2，一个班；两间教室 A101(40) < A102(60) → 应固定用 A101
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,课A,2,2,1,理学院,T001,30,40,1,16,Any\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,40,Norm\nA102,60,Norm\n");
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
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(r.ok);
    QCOMPARE(int(store.scheduleEntries().size()), 2);
    for (const ScheduleEntry &e : store.scheduleEntries())
        QCOMPARE(e.classroomId, QStringLiteral("A101"));   // 同班同教室 + 最小容量
}

/*
TestScheduler::dispersionTwoSessionsPerWeek - 每周 2 次的课落在 周一/周五（间隔 3 天）
*/
void TestScheduler::dispersionTwoSessionsPerWeek()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    QVERIFY(scheduler.schedule(store, &greedy).ok);

    QHash<QString, QString> courseOfClass;
    QHash<QString, int> sessionsOf;
    for (const Course &c : store.courses())
        sessionsOf.insert(c.id, c.sessionsPerWeek);
    for (const TeachingClass &tc : store.teachingClasses())
        courseOfClass.insert(tc.classId, tc.courseId);

    QHash<QString, QSet<int>> daysOf;
    for (const ScheduleEntry &e : store.scheduleEntries())
        daysOf[e.teachingClassId].insert(e.timeSlot.dayOfWeek);

    // 每周 2 次的每个教学班都必须落在 {周一, 周五}，而不是同一天连堂
    for (auto it = daysOf.begin(); it != daysOf.end(); ++it) {
        const QString classId = it.key();
        const QString courseId = courseOfClass.value(classId);
        if (sessionsOf.value(courseId, 0) == 2)
            QCOMPARE(it.value(), QSet<int>({1, 5}));
    }
}

/*
TestScheduler::classroomTypeMatching - H5：教室类型匹配课程所需类型
*/
void TestScheduler::classroomTypeMatching()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 实验课 → 只能进机房；体育课 → 无操场教室 → 失败；普通课(Any) → 不受限
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,实验课,1,1,2,计算机学院,T001,30,40,1,16,Lab\n");
        f.write("C201,C02,体育课,1,1,2,体育部,T002,30,50,1,16,PlayGround\n");
        f.write("C301,C03,普通课,1,1,1,理学院,T003,30,40,1,16,Any\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        // 普通教室容量更小——验证类型匹配优先于容量（不做"装下就算"）
        f.write("roomNumber,capacity,type\nA101,30,Norm\nLab1,30,Lab\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n"
                "1,08:00,08:45\n2,08:55,09:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(!r.ok);                                   // 体育课排不下 → 整体失败
    QCOMPARE(r.scheduledCount, 2);                    // 实验课 + 普通课成功
    QVERIFY(r.failedClassIds.contains(QStringLiteral("C201")));

    for (const ScheduleEntry &e : store.scheduleEntries()) {
        if (e.teachingClassId == QLatin1String("C101"))
            QCOMPARE(e.classroomId, QStringLiteral("Lab1"));   // 类型匹配优先，不落普通教室
        if (e.teachingClassId == QLatin1String("C301"))
            QCOMPARE(e.classroomId, QStringLiteral("A101"));   // Any 不受限，取最小容量
    }

    // 失败明细：C201 无操场教室 → 原因点名「操场」，且已写入 store 供持久化
    QCOMPARE(r.failures.size(), 1);
    QCOMPARE(r.failures.first().classId, QStringLiteral("C201"));
    QVERIFY(r.failures.first().reason.contains(QStringLiteral("操场")));
    QCOMPARE(int(store.scheduleFailures().size()), 1);
    QCOMPARE(store.scheduleFailures().first().reason, r.failures.first().reason);
}

/*
TestScheduler::failureReasonCapacityInsufficient - 失败原因归因：类型匹配但容量不足
*/
void TestScheduler::failureReasonCapacityInsufficient()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 实验课 50 人，但机房最大仅 30 人 → 归因为容量不足；普通课 20 人可排
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,实验课,1,1,2,理学院,T001,50,60,1,16,Lab\n");
        f.write("C201,C02,普通课,1,1,1,理学院,T002,20,40,1,16,Norm\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nLab1,30,Lab\nA101,40,Norm\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n"
                "1,08:00,08:45\n2,08:55,09:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(!r.ok);
    QCOMPARE(r.scheduledCount, 1);                        // 普通课成功
    QCOMPARE(r.failures.size(), 1);
    QCOMPARE(r.failures.first().classId, QStringLiteral("C101"));
    QVERIFY(r.failures.first().reason.contains(QStringLiteral("容量")));
    QVERIFY(r.failures.first().reason.contains(QStringLiteral("30")));

    // 失败明细已写入 store，供快照持久化后重看
    QCOMPARE(int(store.scheduleFailures().size()), 1);
    QCOMPARE(store.scheduleFailures().first().reason, r.failures.first().reason);
}

/*
TestScheduler::multiSectionSessions - 多学时课程一次课跨多个连续节次（endSection 正确）
*/
void TestScheduler::multiSectionSessions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 3 学时 / 2 学时各一门课，单教学班，N=1：验证 endSection 覆盖连续节次
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,三学时课,1,1,3,理学院,T001,30,40,1,16,Any\n");
        f.write("C201,C02,两学时课,1,1,2,理学院,T002,20,30,1,16,Any\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,40,Norm\nA102,40,Norm\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n"
                "1,08:00,08:45\n2,08:55,09:40\n3,10:00,10:45\n4,10:55,11:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(r.ok);
    QCOMPARE(int(store.scheduleEntries().size()), 2);

    for (const ScheduleEntry &e : store.scheduleEntries()) {
        if (e.teachingClassId == QLatin1String("C101"))
            QCOMPARE(e.timeSlot.endSection, e.timeSlot.startSection + 2);   // 3 学时 → 3 节
        else
            QCOMPARE(e.timeSlot.endSection, e.timeSlot.startSection + 1);   // 2 学时 → 2 节
    }
}

/*
TestScheduler::nonIntegerHoursRejected - 非整数学时课程整门记失败（不静默丢学时）
*/
void TestScheduler::nonIntegerHoursRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 1.5 学时课程 → 该课程教学班全部失败，原因点名「学时」；合法课程不受影响
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,物理课,3,2,1.5,理学院,T001,30,40,1,16,Any\n");
        f.write("C201,C02,数学课,4,2,2,理学院,T002,30,40,1,16,Any\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,40,Norm\nA102,40,Norm\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n"
                "1,08:00,08:45\n2,08:55,09:40\n3,10:00,10:45\n4,10:55,11:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));
    Scheduler scheduler;
    GreedyStrategy greedy;
    const ScheduleResult r = scheduler.schedule(store, &greedy);
    QVERIFY(!r.ok);
    QCOMPARE(r.failedCount, 1);
    QVERIFY(r.failedClassIds.contains(QStringLiteral("C101")));
    QCOMPARE(r.failures.size(), 1);
    QCOMPARE(r.failures.first().classId, QStringLiteral("C101"));
    QVERIFY(r.failures.first().reason.contains(QStringLiteral("学时")));
    // 合法学时课程正常排入（数学课 N=2 → 2 条）
    QCOMPARE(r.scheduledCount, 2);
}

/*
TestScheduler::rescheduleDoesNotAccumulate - 重复排课不累积旧结果
*/
void TestScheduler::rescheduleDoesNotAccumulate()
{
    DataStore store = sampleStore();
    Scheduler scheduler;
    GreedyStrategy greedy;
    scheduler.schedule(store, &greedy);
    scheduler.schedule(store, &greedy);
    QCOMPARE(int(store.scheduleEntries().size()), 20);
}

QTEST_GUILESS_MAIN(TestScheduler)
#include "tst_scheduler.moc"
