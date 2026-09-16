/**
 * 文件职责：DataStore 工作区快照（saveSnapshot / loadSnapshot）单元测试。
 * 验证九段（[Term] 起至 [Locks]）往返一致、含排课结果恢复、异常输入返回 false。
 * 课程事实双写收口：校验 [Courses] 与 [TeachingClasses] 内嵌课程列的一致性、
 * 篡改回退、老快照容错、悬空班告警（loadWarnings）。
 * DATA_DIR 由 CMake 注入。
 */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/store/datastore.h"

class TestSnapshot : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void addedCourseRoundTrip();
    void removedDataRoundTrip();
    void emptyCourseRoundTrip();
    void roundTripWithSchedule();
    void errorsRoundTrip();
    void locksRoundTrip();
    void snapshotTextStable();       // 快照文本 = 内容稳定指纹（自动保存的脏标记判定依赖）
    void missingLocksSectionLoads();
    void corruptFileFails();
    // 课程事实双写收口
    void coursesConsistentAfterRoundTrip();
    void tamperedClassRowFallsBackToCourses();
    void legacySnapshotWithoutCoursesOpens();
    void danglingCourseIdWarns();
};

/*
TestSnapshot::roundTrip - 快照往返后四段数据数量与逐字段一致
*/
void TestSnapshot::roundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snapshot.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));

    // 数量一致
    QCOMPARE(int(dst.courses().size()), int(src.courses().size()));
    QCOMPARE(int(dst.teachingClasses().size()), int(src.teachingClasses().size()));
    QCOMPARE(int(dst.classrooms().size()), int(src.classrooms().size()));
    QCOMPARE(int(dst.sections().size()), int(src.sections().size()));
    QCOMPARE(int(dst.teachers().size()), int(src.teachers().size()));   // [Teachers] 段往返
    QCOMPARE(dst.semesterWeeks(), src.semesterWeeks());   // [Term] 段往返

    // 逐字段一致
    for (int i = 0; i < src.courses().size(); ++i) {
        const Course &a = src.courses().at(i);
        const Course &b = dst.courses().at(i);
        QCOMPARE(b.id, a.id);
        QCOMPARE(b.name, a.name);
        QCOMPARE(b.credit, a.credit);
        QCOMPARE(b.sessionsPerWeek, a.sessionsPerWeek);
        QCOMPARE(b.hoursPerSession, a.hoursPerSession);
        QCOMPARE(b.depart, a.depart);
        QCOMPARE(b.startWeek, a.startWeek);
        QCOMPARE(b.endWeek, a.endWeek);
        QCOMPARE(b.requiredRoomType, a.requiredRoomType);
    }
    for (int i = 0; i < src.teachingClasses().size(); ++i) {
        const TeachingClass &a = src.teachingClasses().at(i);
        const TeachingClass &b = dst.teachingClasses().at(i);
        QCOMPARE(b.classId, a.classId);
        QCOMPARE(b.courseId, a.courseId);
        QCOMPARE(b.teacherId, a.teacherId);
        QCOMPARE(b.plannedSize, a.plannedSize);
        QCOMPARE(b.maxCapacity, a.maxCapacity);
    }
    for (int i = 0; i < src.classrooms().size(); ++i) {
        const Classroom &a = src.classrooms().at(i);
        const Classroom &b = dst.classrooms().at(i);
        QCOMPARE(b.roomNumber, a.roomNumber);
        QCOMPARE(b.capacity, a.capacity);
        QCOMPARE(b.type, a.type);
    }
    for (int i = 0; i < src.teachers().size(); ++i) {
        const TeacherInfo &a = src.teachers().at(i);
        const TeacherInfo &b = dst.teachers().at(i);
        QCOMPARE(b.teacherId, a.teacherId);
        QCOMPARE(b.name, a.name);
        QCOMPARE(b.depart, a.depart);
    }
    for (int i = 0; i < src.sections().size(); ++i) {
        const Section &a = src.sections().at(i);
        const Section &b = dst.sections().at(i);
        QCOMPARE(b.index, a.index);
        QCOMPARE(b.startTime, a.startTime);
        QCOMPARE(b.endTime, a.endTime);
    }
}

/*
TestSnapshot::addedCourseRoundTrip - 新增课程+教学班经快照往返后数据完整
*/
void TestSnapshot::addedCourseRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    Course nc;
    nc.id              = QLatin1String("CC99");
    nc.name            = QStringLiteral("新增测试课");
    nc.credit          = 3.0;
    nc.sessionsPerWeek = 2;
    nc.hoursPerSession = 2;
    nc.startWeek       = 1;
    nc.endWeek         = 16;
    QVERIFY(src.addCourse(nc));
    TeachingClass k;
    k.classId     = QLatin1String("Z999");
    k.courseId    = QLatin1String("CC99");
    k.teacherId   = QLatin1String("T999");
    k.plannedSize = 20;
    k.maxCapacity = 30;
    QVERIFY(src.addTeachingClass(k));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_added.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(int(dst.courses().size()), int(src.courses().size()));
    QCOMPARE(int(dst.teachingClasses().size()), int(src.teachingClasses().size()));

    const Course *c = dst.courseById(QLatin1String("CC99"));
    QVERIFY(c);
    QCOMPARE(c->name, QStringLiteral("新增测试课"));
    QCOMPARE(c->sessionsPerWeek, 2);

    const TeachingClass *tc = dst.teachingClassById(QLatin1String("Z999"));
    QVERIFY(tc);
    QCOMPARE(tc->courseId, QLatin1String("CC99"));
    QCOMPARE(tc->maxCapacity, 30);
}

/*
TestSnapshot::removedDataRoundTrip - 删光某课全部班后快照不残留其锁定/排课，课程保留为空课
*/
void TestSnapshot::removedDataRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    // 给 C101 一条排课并锁定
    ScheduleEntry e;
    e.entryId = QLatin1String("E_REM");
    e.teachingClassId = QLatin1String("C101");
    e.classroomId = QLatin1String("A101");
    e.timeSlot.dayOfWeek = 1;
    e.timeSlot.startSection = 1;
    e.timeSlot.endSection = 1;
    src.addScheduleEntry(e);
    src.lockClass(QLatin1String("C101"));

    // 删除 C01 的全部教学班（示例数据 C01 下含 C101/C102/C103）→ 课程变成空课程
    QStringList c01Classes;
    for (const TeachingClass &tc : src.teachingClasses())
        if (tc.courseId == QLatin1String("C01"))
            c01Classes.append(tc.classId);
    for (const QString &cid : c01Classes)
        QVERIFY(src.removeTeachingClass(cid));
    QVERIFY(src.courseById(QLatin1String("C01")));        // 课程保留为空（不随末班删）

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_removed.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(int(dst.courses().size()), int(src.courses().size()));
    QCOMPARE(int(dst.teachingClasses().size()), int(src.teachingClasses().size()));
    QVERIFY(dst.courseById(QLatin1String("C01")));          // 空课程跨快照往返不丢
    QVERIFY(!dst.teachingClassById(QLatin1String("C101"))); // 班不在
    QVERIFY(!dst.isClassLocked(QLatin1String("C101")));     // 锁不在
    QCOMPARE(int(dst.scheduleEntries().size()), 0);         // 排课条目不残留
    QVERIFY(!dst.teachingClasses().isEmpty());              // 其余数据完整
}

/*
TestSnapshot::emptyCourseRoundTrip - 只建不带教学班的空课程也能快照往返
*/
void TestSnapshot::emptyCourseRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    Course ghost;
    ghost.id              = QLatin1String("GHOST");
    ghost.name            = QStringLiteral("预开空课");
    ghost.credit          = 2.0;
    ghost.sessionsPerWeek = 2;
    ghost.hoursPerSession = 1;
    ghost.depart          = QStringLiteral("计算机学院");
    ghost.startWeek       = 5;
    ghost.endWeek         = 16;
    ghost.requiredRoomType = ClassroomType::Lab;
    QVERIFY(src.addCourse(ghost));                        // 只建空课，不加任何班
    QCOMPARE(int(src.teachingClasses().size()), 11);      // 无教学班被连带创建

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_empty.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(int(dst.teachingClasses().size()), 11);
    const Course *c = dst.courseById(QLatin1String("GHOST"));
    QVERIFY(c);                                           // 空课程在 [Courses] 段保留
    QCOMPARE(c->name, QStringLiteral("预开空课"));
    QCOMPARE(c->credit, 2.0);
    QCOMPARE(c->sessionsPerWeek, 2);
    QCOMPARE(c->hoursPerSession, 1.0);
    QCOMPARE(c->depart, QStringLiteral("计算机学院"));
    QCOMPARE(c->startWeek, 5);
    QCOMPARE(c->endWeek, 16);
    QCOMPARE(c->requiredRoomType, ClassroomType::Lab);
}

/*
TestSnapshot::roundTripWithSchedule - 快照含排课结果时恢复后课表完整
*/
void TestSnapshot::roundTripWithSchedule()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    ScheduleEntry e;
    e.entryId               = QLatin1String("E1");
    e.teachingClassId       = QLatin1String("C101");
    e.teacherId             = QLatin1String("T001");
    e.classroomId           = QLatin1String("A101");
    e.timeSlot.dayOfWeek    = 3;
    e.timeSlot.startSection = 2;
    e.timeSlot.endSection   = 3;
    e.startWeek             = 5;   // 排课条目周范围一并往返
    e.endWeek               = 12;
    src.addScheduleEntry(e);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));

    QCOMPARE(int(dst.scheduleEntries().size()), 1);
    const ScheduleEntry &r = dst.scheduleEntries().first();
    QCOMPARE(r.entryId, e.entryId);
    QCOMPARE(r.teachingClassId, e.teachingClassId);
    QCOMPARE(r.teacherId, e.teacherId);
    QCOMPARE(r.classroomId, e.classroomId);
    QCOMPARE(r.timeSlot.dayOfWeek, 3);
    QCOMPARE(r.timeSlot.startSection, 2);
    QCOMPARE(r.timeSlot.endSection, 3);
    QCOMPARE(r.startWeek, 5);
    QCOMPARE(r.endWeek, 12);
}

/*
TestSnapshot::errorsRoundTrip - 排课失败明细随快照 [ScheduleErrors] 段往返一致
*/
void TestSnapshot::errorsRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    ScheduleFailure f;
    f.classId    = QLatin1String("C301");
    f.courseId   = QLatin1String("C03");
    f.courseName = QLatin1String("程序设计基础");
    f.reason     = QLatin1String("「机房」类型教室最大容量 30 人，仍小于本班 50 人");
    src.setScheduleFailures({f});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_errors.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(int(dst.scheduleFailures().size()), 1);
    const ScheduleFailure &r = dst.scheduleFailures().first();
    QCOMPARE(r.classId, f.classId);
    QCOMPARE(r.courseId, f.courseId);
    QCOMPARE(r.courseName, f.courseName);
    QCOMPARE(r.reason, f.reason);
}

/*
TestSnapshot::locksRoundTrip - 锁定教学班随 [Locks] 段往返一致
*/
void TestSnapshot::locksRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    src.setLockedClasses({QLatin1String("C101"), QLatin1String("C102"),
                          QLatin1String("C301")});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_locks.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(dst.lockedClassIds(), src.lockedClassIds());
    QVERIFY(dst.isClassLocked(QLatin1String("C101")));
    QVERIFY(!dst.isClassLocked(QLatin1String("C201")));
}

/*
TestSnapshot::snapshotTextStable - 快照文本是内容的稳定指纹

Remark:
    自动保存判变 / 手动保存脏标记 ● 都依赖「同一内容 → 同一 snapshotText 逐字节稳定」：
    存盘再读回文本不变；锁集反序插入仍一致（[Locks] 排序后确定）；
    内容一变（解锁另锁 / 改名）文本即变。
*/
void TestSnapshot::snapshotTextStable()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    src.setLockedClasses({QLatin1String("C101"), QLatin1String("C102"),
                          QLatin1String("C301")});
    const QString text = src.snapshotText();
    QVERIFY(!text.isEmpty());
    QVERIFY(text.contains(QLatin1String("[Locks]")));

    // 同一锁定集、反序插入的另一个仓库 → 文本逐字节一致（[Locks] 已排序）
    DataStore other;
    QVERIFY(other.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    other.setLockedClasses({QLatin1String("C301"), QLatin1String("C102"),
                            QLatin1String("C101")});
    QCOMPARE(other.snapshotText(), text);

    // 存盘再读回 → 文本指纹不变（UI 据此判是否要重写自动恢复文件 / 标 ●）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_text.csv");
    QVERIFY(src.saveSnapshot(path));
    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QCOMPARE(dst.snapshotText(), text);

    // 内容一变 → 文本即变（脏态要能感知）
    DataStore changed = src;
    changed.unlockClass(QLatin1String("C101"));
    changed.lockClass(QLatin1String("C201"));
    QVERIFY(changed.snapshotText() != text);
}

/*
TestSnapshot::missingLocksSectionLoads - 老快照缺 [Locks] 段仍能加载（容错）
*/
void TestSnapshot::missingLocksSectionLoads()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    src.lockClass(QLatin1String("C101"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_old.csv");
    QVERIFY(src.saveSnapshot(path));

    // 删掉 [Locks] 段整块文本 → 模拟旧格式快照
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    const int pos = content.indexOf(QLatin1String("[Locks]"));
    QVERIFY(pos >= 0);
    content.truncate(pos);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(content.toUtf8());
    f.close();

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));                     // 缺段不报错
    QVERIFY(!dst.isClassLocked(QLatin1String("C101")));  // 无锁
    QCOMPARE(int(dst.teachingClasses().size()), 11);     // 其余数据完整
}

/*
TestSnapshot::corruptFileFails - 不存在 / 空 / 乱码文件返回 false
*/
void TestSnapshot::corruptFileFails()
{
    DataStore store;

    // 不存在的文件
    QVERIFY(!store.loadSnapshot(QStringLiteral("no/such/snapshot.csv")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 空文件
    const QString empty = dir.filePath("empty.csv");
    {
        QFile f(empty);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.close();
    }
    QVERIFY(!store.loadSnapshot(empty));

    // 无分节标记的乱码
    const QString garbage = dir.filePath("garbage.csv");
    {
        QFile f(garbage);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("hello\nworld\n");
        f.close();
    }
    QVERIFY(!store.loadSnapshot(garbage));
}

/*
TestSnapshot::coursesConsistentAfterRoundTrip - 正常快照往返后无一致性告警（R4）

Remark:
    保存端 [TeachingClasses] 内嵌课程列从 m_courses 现取，与 [Courses] 段同源，
    故 loadSnapshot 的 R4 校验不应产生任何告警。
*/
void TestSnapshot::coursesConsistentAfterRoundTrip()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_clean.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    QVERIFY(dst.loadWarnings().isEmpty());   // 同源写出，不应有不一致告警
}

/*
TestSnapshot::tamperedClassRowFallsBackToCourses - 篡改班行课程列后以 [Courses] 为准并告警（R4）

Remark:
    手工把 [TeachingClasses] 段 C101 行的 courseName 改坏；loadSnapshot 应以权威
    [Courses] 段为准（name 仍为"高等数学"），且 loadWarnings 记录该不一致。
*/
void TestSnapshot::tamperedClassRowFallsBackToCourses()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_tampered.csv");
    QVERIFY(src.saveSnapshot(path));

    // 篡改 [TeachingClasses] 段 C101 行的 courseName（第 3 列）
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("C101,C01,高等数学")));
    content.replace(QStringLiteral("C101,C01,高等数学"), QStringLiteral("C101,C01,被篡改的课名"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(content.toUtf8());
    f.close();

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    const Course *c = dst.courseById(QLatin1String("C01"));
    QVERIFY(c);
    QCOMPARE(c->name, QStringLiteral("高等数学"));   // 以 [Courses] 为准，未被班行篡改影响
    QVERIFY(!dst.loadWarnings().isEmpty());           // 产生可见告警
    bool hit = false;
    for (const QString &w : dst.loadWarnings())
        if (w.contains(QLatin1String("C101"))) { hit = true; break; }
    QVERIFY(hit);                                     // 告警定位到 C101
}

/*
TestSnapshot::legacySnapshotWithoutCoursesOpens - 老快照缺 [Courses] 段仍能加载（R4）

Remark:
    删掉 [Courses] 段整块模拟老格式快照；loadSnapshot 应回退为由教学班行推导课程，
    且因无权威 [Courses] 段不触发 R4 一致性校验（loadWarnings 为空）。
*/
void TestSnapshot::legacySnapshotWithoutCoursesOpens()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_legacy.csv");
    QVERIFY(src.saveSnapshot(path));

    // 删掉 [Courses] 段整块（从 [Courses] 到 [TeachingClasses] 之前），模拟老格式快照
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    const int begin = content.indexOf(QLatin1String("[Courses]"));
    const int end = content.indexOf(QLatin1String("[TeachingClasses]"));
    QVERIFY(begin >= 0 && end > begin);
    content.remove(begin, end - begin);   // 移除 [Courses] 段，保留 [TeachingClasses] 起
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(content.toUtf8());
    f.close();

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));                  // 老快照不报错
    const Course *c = dst.courseById(QLatin1String("C01"));
    QVERIFY(c);                                       // 课程由教学班行推导回退
    QCOMPARE(c->name, QStringLiteral("高等数学"));
    QVERIFY(dst.loadWarnings().isEmpty());            // 无 [Courses] 段不校验，无告警
}

/*
TestSnapshot::danglingCourseIdWarns - 班行 courseId 不在 [Courses] 段时悬空班告警（R4）

Remark:
    篡改 C101 行的 courseId 为 [Courses] 段不存在的 C99；loadSnapshot 后该班成悬空班，
    loadWarnings 应含"无对应课程"告警，且加载不阻断。
*/
void TestSnapshot::danglingCourseIdWarns()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_dangling.csv");
    QVERIFY(src.saveSnapshot(path));

    // 篡改 C101 行的 courseId（第 2 列）：C01 -> C99（[Courses] 段无 C99）
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QLatin1String("C101,C01,")));
    content.replace(QLatin1String("C101,C01,"), QLatin1String("C101,C99,"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(content.toUtf8());
    f.close();

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));                  // 悬空班不阻断加载
    QVERIFY(!dst.loadWarnings().isEmpty());
    bool hit = false;
    for (const QString &w : dst.loadWarnings())
        if (w.contains(QStringLiteral("无对应课程")) && w.contains(QLatin1String("C101"))) { hit = true; break; }
    QVERIFY(hit);
}

QTEST_GUILESS_MAIN(TestSnapshot)
#include "tst_snapshot.moc"
