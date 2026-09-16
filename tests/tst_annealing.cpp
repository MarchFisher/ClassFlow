/**
 * 文件职责：模拟退火排课策略（SimulatedAnnealingStrategy）单元测试。
 * 覆盖：硬约束 H1~H5（教师/教室/班级不撞、容量、教室类型）零违反（小/大两套数据）、
 * 总软成本不劣于贪心 + 负载均匀性不差于贪心（经验属性）、同种子可复现、失败明细沿用、
 * 多学时课程跨节不被打断。SA 统一走 Scheduler 默认策略路径，不直接调策略类。
 */

#include <QtTest>

#include <QFile>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>

#include "core/schedule/annealingcandidate.h"
#include "core/schedule/annealingcost.h"
#include "core/schedule/scheduler.h"
#include "core/schedule/strategy.h"
#include "core/store/datastore.h"

namespace {

/*
loadStore - 载入指定数据集目录下教学班 + 教室 + 作息表的仓库

Parameter：
    tcFile:  数据集目录内的教学班 CSV 文件名
    baseDir: 数据集目录（默认为 DATA_DIR=small/classic；大数据用 DATA_LARGE_DIR）

Result:
    DataStore: 已载入的仓库
*/
DataStore loadStore(const QString &tcFile,
                    const QString &baseDir = QStringLiteral(DATA_DIR))
{
    DataStore store;
    store.loadCsv(baseDir + "/" + tcFile,
                  baseDir + "/classrooms.csv",
                  baseDir + "/sections.csv");
    return store;
}

/*
verifyHardConstraints - 校验排课结果满足硬约束 H1~H5

Parameter：
    store: 已排课的仓库（含课程 / 教学班 / 教室 / 排课结果）

Remark:
    H1 同班互斥 / H2 教室容量足够 / H3 教室不重 / H4 教师不重 / H5 教室类型匹配。
    占用键按周展开，与 ConflictTable 语义一致（不同周可复用教室/教师）。
*/
void verifyHardConstraints(const DataStore &store)
{
    QHash<QString, int> plannedOfClass;
    QHash<QString, ClassroomType> typeOfClass;
    QHash<QString, ClassroomType> typeOfRoom;
    QHash<QString, int> capOfRoom;
    for (const TeachingClass &tc : store.teachingClasses())
        plannedOfClass.insert(tc.classId, tc.plannedSize);
    for (const Course &c : store.courses())
        for (const TeachingClass &tc : store.teachingClasses())
            if (tc.courseId == c.id)
                typeOfClass.insert(tc.classId, c.requiredRoomType);
    for (const Classroom &r : store.classrooms()) {
        typeOfRoom.insert(r.roomNumber, r.type);
        capOfRoom.insert(r.roomNumber, r.capacity);
    }

    QSet<QString> roomKeys, classKeys, teacherKeys;
    qsizetype roomTotal = 0, classTotal = 0, teacherTotal = 0;
    for (const ScheduleEntry &e : store.scheduleEntries()) {
        // H2 容量：教室容量 ≥ 教学班人数
        QVERIFY2(capOfRoom.value(e.classroomId, 0) >= plannedOfClass.value(e.teachingClassId, 0),
                 "H2 教室容量不足");
        // H5 类型：教室类型满足课程需求（Any 表示不限）
        const ClassroomType need = typeOfClass.value(e.teachingClassId, ClassroomType::Any);
        QVERIFY2(need == ClassroomType::Any || typeOfRoom.value(e.classroomId) == need,
                 "H5 教室类型不匹配");

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
    // H1/H3/H4：同班 / 同教室 / 同教师在「同 (day, section, 周)」至多一次
    QCOMPARE(classKeys.size(), classTotal);
    QCOMPARE(roomKeys.size(), roomTotal);
    QCOMPARE(teacherKeys.size(), teacherTotal);
}

} // namespace

class TestAnnealing : public QObject
{
    Q_OBJECT

private slots:
    void hardConstraintsSmall();
    void hardConstraintsLarge();
    void sessionCountPreserved();
    void betterOrEqualThanGreedy();
    void reproducibleSameSeed();
    void failuresCarriedOver();
    void multiSectionPreserved();
    void weekendPenaltyApplied();
    void progressReported();
    void cancelAborts();
};

/*
TestAnnealing::hardConstraintsSmall - 小数据集：SA 结果满足 H1~H5 零违反
*/
void TestAnnealing::hardConstraintsSmall()
{
    DataStore store = loadStore(QStringLiteral("teaching_classes.csv"));
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(store);   // 默认策略 = SA
    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);
    verifyHardConstraints(store);
}

/*
TestAnnealing::hardConstraintsLarge - 大数据集（18 班 / 6 课程）：硬约束零违反
*/
void TestAnnealing::hardConstraintsLarge()
{
    DataStore store = loadStore(QStringLiteral("teaching_classes.csv"),
                                QStringLiteral(DATA_LARGE_DIR));
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(store);
    QVERIFY(r.ok);
    QCOMPARE(r.failedCount, 0);
    verifyHardConstraints(store);
}

/*
TestAnnealing::sessionCountPreserved - 回归：SA 后每班排课条数恒等于 sessionsPerWeek
（防止 M3 把同班两次课压到同一天后，拿「天数不足」的签名当目标导致丢课次）
*/
void TestAnnealing::sessionCountPreserved()
{
    // 两套样例：(小, classic) 与 (大, eighteen)
    const QVector<QPair<QString, QString>> sets{
        { QStringLiteral(DATA_DIR),       QStringLiteral("teaching_classes.csv") },
        { QStringLiteral(DATA_LARGE_DIR), QStringLiteral("teaching_classes.csv") },
    };
    for (const auto &ds : sets) {
        const QString dir = ds.first;
        const QString tc  = ds.second;
        const QString tag = dir + "/" + tc;
        DataStore sa = loadStore(tc, dir);
        Scheduler sched;
        QVERIFY2(sched.schedule(sa).ok, qPrintable(tag + " SA 应排课成功"));

        QHash<QString, int> want;
        for (const Course &c : sa.courses())
            want.insert(c.id, c.sessionsPerWeek);
        QHash<QString, int> got;
        for (const ScheduleEntry &e : sa.scheduleEntries())
            ++got[e.teachingClassId];

        for (const TeachingClass &c : sa.teachingClasses())
            QVERIFY2(got.value(c.classId) == want.value(c.courseId),
                     qPrintable(QStringLiteral("%1 %2 课次数不匹配：want=%3 got=%4")
                                    .arg(tag).arg(c.classId)
                                    .arg(want.value(c.courseId))
                                    .arg(got.value(c.classId))));
    }
}

/*
TestAnnealing::betterOrEqualThanGreedy - 两套数据：SA 总软成本不劣于贪心；
负载均匀性不差于贪心（经验属性）

Remark:
    总成本 ≤ 贪心由 best-so-far 初始化为贪心候选保证（必然成立）；
    单项方差不保证单调，按固定种子放宽到「不劣于贪心 ×1.2 + 1」。
*/
void TestAnnealing::betterOrEqualThanGreedy()
{
    // 两套样例：(小, classic) 与 (大, eighteen)
    const QVector<QPair<QString, QString>> sets{
        { QStringLiteral(DATA_DIR),       QStringLiteral("teaching_classes.csv") },
        { QStringLiteral(DATA_LARGE_DIR), QStringLiteral("teaching_classes.csv") },
    };
    for (const auto &ds : sets) {
        const QString dir = ds.first;
        const QString tc  = ds.second;
        const QString tag = dir + "/" + tc;
        DataStore g = loadStore(tc, dir);
        Scheduler sched;
        GreedyStrategy greedy;
        QVERIFY2(sched.schedule(g, &greedy).ok, qPrintable(tag + " 贪心应排课成功"));
        const double gCost = softCostOfEntries(g.scheduleEntries(), g);
        const UniformityStats gu = uniformityOfEntries(g.scheduleEntries(), g);

        DataStore sa = loadStore(tc, dir);
        Scheduler sched2;
        QVERIFY2(sched2.schedule(sa).ok, qPrintable(tag + " SA 应排课成功"));
        const double saCost = softCostOfEntries(sa.scheduleEntries(), sa);
        const UniformityStats su = uniformityOfEntries(sa.scheduleEntries(), sa);

        QVERIFY2(saCost <= gCost + 1e-9,
                 qPrintable(QStringLiteral("%1 SA 总软成本应不劣于贪心：%2 vs %3")
                                .arg(tag).arg(saCost).arg(gCost)));
        QVERIFY2(su.timeVar <= gu.timeVar * 1.2 + 1.0,
                 qPrintable(QStringLiteral("%1 SA 时间负载方差应不差于贪心：%2 vs %3")
                                .arg(tag).arg(su.timeVar).arg(gu.timeVar)));
        QVERIFY2(su.roomVar <= gu.roomVar * 1.2 + 1.0,
                 qPrintable(QStringLiteral("%1 SA 教室负载方差应不差于贪心：%2 vs %3")
                                .arg(tag).arg(su.roomVar).arg(gu.roomVar)));
    }
}

/*
TestAnnealing::reproducibleSameSeed - 同种子两次运行结果逐条一致
*/
void TestAnnealing::reproducibleSameSeed()
{
    DataStore a = loadStore(QStringLiteral("teaching_classes.csv"));
    DataStore b = loadStore(QStringLiteral("teaching_classes.csv"));
    Scheduler sa1;
    Scheduler sa2;
    QVERIFY(sa1.schedule(a).ok);
    QVERIFY(sa2.schedule(b).ok);
    const QVector<ScheduleEntry> ea = a.scheduleEntries();
    const QVector<ScheduleEntry> eb = b.scheduleEntries();
    QCOMPARE(ea.size(), eb.size());
    for (int i = 0; i < ea.size(); ++i) {
        QCOMPARE(ea[i].teachingClassId, eb[i].teachingClassId);
        QCOMPARE(ea[i].timeSlot.dayOfWeek, eb[i].timeSlot.dayOfWeek);
        QCOMPARE(ea[i].timeSlot.startSection, eb[i].timeSlot.startSection);
        QCOMPARE(ea[i].timeSlot.endSection, eb[i].timeSlot.endSection);
        QCOMPARE(ea[i].classroomId, eb[i].classroomId);
        QCOMPARE(ea[i].startWeek, eb[i].startWeek);
        QCOMPARE(ea[i].endWeek, eb[i].endWeek);
    }
}

/*
TestAnnealing::failuresCarriedOver - 教室不足时失败明细经 SA 沿用贪心上报
*/
void TestAnnealing::failuresCarriedOver()
{
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
    const ScheduleResult r = scheduler.schedule(store);   // SA
    QVERIFY(!r.ok);
    QVERIFY(r.failedCount > 0);
    QVERIFY(!r.failures.isEmpty());
    QCOMPARE(int(store.scheduleFailures().size()), r.failedCount);
}

/*
TestAnnealing::multiSectionPreserved - 多学时课程跨节回归：SA 后 endSection 仍正确
*/
void TestAnnealing::multiSectionPreserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
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
    const ScheduleResult r = scheduler.schedule(store);   // SA
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
TestAnnealing::weekendPenaltyApplied - S7 周末罚分：单班罚分、全量成本含 S7、
周末课挪到工作日后成本恰降一个 w_weekend（其余项不变，差值精确为 2000）
*/
void TestAnnealing::weekendPenaltyApplied()
{
    const int wWeekend = SoftWeights().w_weekend;   // 跟随当前内部权重（×2 整数化）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tcPath   = dir.filePath("tc.csv");
    const QString roomPath = dir.filePath("rooms.csv");
    const QString secPath  = dir.filePath("sections.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,课A,1,1,1,理学院,T001,30,40,1,16,Any\n");
        f.write("C201,C02,课B,1,1,1,理学院,T002,30,40,1,16,Any\n");
        f.close();
    }
    {
        QFile f(roomPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("roomNumber,capacity,type\nA101,30,Norm\n");
        f.close();
    }
    {
        QFile f(secPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("index,startTime,endTime\n1,08:00,08:45\n2,08:55,09:40\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(tcPath, roomPath, secPath));

    // C101 排在周六（周末）、C201 排在工作日，两课节次不重叠
    ScheduleEntry e1, e2;
    e1.entryId = "C101-6-1";
    e1.teachingClassId = "C101";
    e1.teacherId = "T001";
    e1.classroomId = "A101";
    e1.timeSlot.dayOfWeek = 6;
    e1.timeSlot.startSection = 1;
    e1.timeSlot.endSection = 1;
    e1.startWeek = 1; e1.endWeek = 16;
    e2.entryId = "C201-3-1";
    e2.teachingClassId = "C201";
    e2.teacherId = "T002";
    e2.classroomId = "A101";
    e2.timeSlot.dayOfWeek = 3;
    e2.timeSlot.startSection = 1;
    e2.timeSlot.endSection = 1;
    e2.startWeek = 1; e2.endWeek = 16;
    store.addScheduleEntry(e1);
    store.addScheduleEntry(e2);

    Candidate cand;
    cand.buildFromEntries(store.scheduleEntries(), store);
    QCOMPARE(weekendPenaltyOfClass(cand, QLatin1String("C101"), wWeekend), wWeekend);
    QCOMPARE(weekendPenaltyOfClass(cand, QLatin1String("C201"), wWeekend), 0);

    // fullSoftCost 中 S7 项恰为 w_weekend：仅关掉该权重，成本恰降 w_weekend
    const LoadSnapshot snap = buildSnapshot(cand, store);
    SoftWeights wOn, wOff;
    wOff.w_weekend = 0;
    const double cOn = fullSoftCost(cand, snap, wOn);
    const double cOff = fullSoftCost(cand, snap, wOff);
    QVERIFY2(qAbs(cOn - cOff - double(wWeekend)) < 1e-9,
             "fullSoftCost 关掉 w_weekend 应恰降一个 w_weekend");

    // 把周末课挪到周五，其余软约束项不变 → 软成本恰降 w_weekend
    const double cWeekend = softCostOfEntries(store.scheduleEntries(), store);
    e1.timeSlot.dayOfWeek = 5;
    store.clearScheduleEntries();
    store.addScheduleEntry(e1);
    store.addScheduleEntry(e2);
    const double cWeekday = softCostOfEntries(store.scheduleEntries(), store);
    QVERIFY2(qAbs(cWeekend - cWeekday - double(wWeekend)) < 1e-9,
             "周末课挪到工作日软成本应恰降一个 w_weekend");
}

/*
TestAnnealing::progressReported - 注入 onProgress，断言退火阶段有上报且 done 单调不减
*/
void TestAnnealing::progressReported()
{
    DataStore store = loadStore(QStringLiteral("teaching_classes.csv"));
    ScheduleContext ctx;
    QVector<int> annealDones;
    ctx.onProgress = [&](SchedulePhase phase, int done, int total) {
        Q_UNUSED(total);
        if (phase == SchedulePhase::Anneal)
            annealDones.append(done);
    };
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(store, nullptr, &ctx);
    QVERIFY(r.ok);
    QVERIFY(!r.aborted);
    QVERIFY(!annealDones.isEmpty());   // 退火阶段有进度上报
    for (int i = 1; i < annealDones.size(); ++i)
        QVERIFY2(annealDones.at(i) >= annealDones.at(i - 1), "退火进度 done 应单调不减");
}

/*
TestAnnealing::cancelAborts - 首次回调即置取消，断言快速返回且 aborted；对照组不取消则 aborted=false
*/
void TestAnnealing::cancelAborts()
{
    DataStore store = loadStore(QStringLiteral("teaching_classes.csv"));
    ScheduleContext ctx;
    ctx.onProgress = [&](SchedulePhase, int, int) {
        ctx.cancelled = true;   // 首次回调即请求取消
    };
    Scheduler scheduler;
    const ScheduleResult r = scheduler.schedule(store, nullptr, &ctx);
    QVERIFY(r.aborted);

    // 对照组：不取消则 aborted == false
    DataStore store2 = loadStore(QStringLiteral("teaching_classes.csv"));
    Scheduler s2;
    QVERIFY(!s2.schedule(store2).aborted);
}

QTEST_GUILESS_MAIN(TestAnnealing)
#include "tst_annealing.moc"
