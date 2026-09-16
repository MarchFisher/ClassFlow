/**
 * 文件职责：DataStore（CSV 解析 / 导出）与 UndoBuffer（会话撤销缓冲回滚）单元测试。
 * 使用 data/ 下示例数据做断言；DATA_DIR 由 CMake 注入。
 */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/store/datastore.h"
#include "core/store/undobuffer.h"

/* loadedSample - 载入 data/ 示例数据作为各用例的基础态 */
static DataStore loadedSample()
{
    DataStore s;
    s.loadCsv(QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
              QStringLiteral(DATA_DIR) + "/classrooms.csv",
              QStringLiteral(DATA_DIR) + "/sections.csv");
    return s;
}

/*
storeBytesEqual - 两个 DataStore 快照逐字节比较

Remark:
    saveSnapshot 段序/行序确定 → 字节相等 = 整仓全字段相等；
    loadWarnings 不入快照，故不需单独比较。
*/
static bool storeBytesEqual(const DataStore &a, const DataStore &b)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return false;
    const QString pa = dir.filePath(QStringLiteral("a.dat"));
    const QString pb = dir.filePath(QStringLiteral("b.dat"));
    if (!a.saveSnapshot(pa) || !b.saveSnapshot(pb))
        return false;
    QFile fa(pa), fb(pb);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    return fa.readAll() == fb.readAll();
}

class TestDataStore : public QObject
{
    Q_OBJECT

private slots:
    void loadSampleData();
    void teachersParsed();
    void teachersDerived();
    void courseDedup();
    void weekRangeParsed();
    void requiredRoomTypeParsed();
    void classroomTypes();
    void sectionTimes();
    void exportRoundTrip();
    void loadMissingFileFails();
    void idLookupHits();
    void idLookupUnknown();
    void idLookupAfterReload();
    void addCourseClassLifecycle();
    void addCourseDuplicateRejected();
    void removeClassCleansRecords();
    void removeLastClassKeepsEmptyCourse();
    void removeCourseWhole();
    void removeEmptyCourseWorks();
    void removeUnknownFails();
    void lockStateLifecycle();
    void updateEntryInPlace();
    void updateUnknownReturnsFalse();
    void entryIndexAfterManyAdds();
    void entryIndexAppendAfterBuild();
    void entryIndexClearInvalidates();
    void entryIndexDropRecordsRebuild();

    // 基本信息编辑：课程/教学班原位改写 + 整班换师 + 换师撞车预检
    void editCourseInPlace();
    void editTeachingClassInPlace();
    void reassignTeacherSyncsEntries();
    void teacherSwapClashDetectsOverlap();
    void teacherSwapClashAllowsFreeSlot();
    void teacherSwapClashRespectsWeekRange();
    void removeFailureForClassOnlyTarget();

    // UndoBuffer 会话撤销缓冲（R7）
    void undoRedoBasicCycle();
    void pushClearsRedoStack();
    void capacityEvictsOldest();
    void undoRestoresStructuralDelete();
    void undoRestoresEntryUpdate();
    void undoRestoresInPlaceOverwrite();
};

/*
TestDataStore::loadSampleData - 载入示例数据，核对各集合数量
*/
void TestDataStore::loadSampleData()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QCOMPARE(int(store.courses().size()), 6);
    QCOMPARE(int(store.teachingClasses().size()), 11);
    QCOMPARE(int(store.classrooms().size()), 23);
    QCOMPARE(int(store.sections().size()), 8);
}

/*
TestDataStore::teachersParsed - 提供教师 CSV 时按表解析姓名与院系
*/
void TestDataStore::teachersParsed()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv",
        QStringLiteral(DATA_DIR) + "/teachers.csv"));

    QCOMPARE(int(store.teachers().size()), 7);

    // 教师表字段：teacherId / name / depart
    for (const TeacherInfo &t : store.teachers()) {
        if (t.teacherId == QLatin1String("T001")) {
            QCOMPARE(t.name, QStringLiteral("张伟"));
            QCOMPARE(t.depart, QStringLiteral("理学院"));
        }
        if (t.teacherId == QLatin1String("T101")) {
            QCOMPARE(t.name, QStringLiteral("赵磊"));
            QCOMPARE(t.depart, QStringLiteral("体育系"));
        }
    }
}

/*
TestDataStore::teachersDerived - 未提供教师 CSV 时从教学班 teacherId 去重推导（name = id）
*/
void TestDataStore::teachersDerived()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QCOMPARE(int(store.teachers().size()), 7);   // 教学班中出现 7 位不同教师
    for (const TeacherInfo &t : store.teachers()) {
        QCOMPARE(t.name, t.teacherId);           // 推导时姓名回退为 ID
        QVERIFY(!t.teacherId.isEmpty());
    }
}

/*
TestDataStore::courseDedup - 同一课程只生成一条课程模板
*/
void TestDataStore::courseDedup()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    int c01 = 0;
    for (const Course &c : store.courses()) {
        if (c.id == QLatin1String("C01")) {
            ++c01;
            QCOMPARE(c.name, QStringLiteral("高等数学"));
            QCOMPARE(c.sessionsPerWeek, 2);
            QCOMPARE(c.startWeek, 1);   // 整学期
            QCOMPARE(c.endWeek, 16);
        }
    }
    QCOMPARE(c01, 1);
}

/*
TestDataStore::weekRangeParsed - 课程周范围（非整学期）正确解析
*/
void TestDataStore::weekRangeParsed()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    for (const Course &c : store.courses()) {
        if (c.id == QLatin1String("C02")) {   // 大学物理：1~8 周
            QCOMPARE(c.startWeek, 1);
            QCOMPARE(c.endWeek, 8);
        }
        if (c.id == QLatin1String("C03")) {   // 程序设计基础：9~16 周
            QCOMPARE(c.startWeek, 9);
            QCOMPARE(c.endWeek, 16);
        }
    }
}

/*
TestDataStore::requiredRoomTypeParsed - 所需教室类型列正确解析为枚举
*/
void TestDataStore::requiredRoomTypeParsed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tcPath = dir.filePath("tc_roomtype.csv");
    {
        QFile f(tcPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,"
                "depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n");
        f.write("C101,C01,实验课,1,1,2,计算机学院,T001,20,30,1,16,Lab\n");
        f.write("C201,C02,体育课,1,1,2,体育部,T002,40,50,1,16,PlayGround\n");
        f.write("C301,C03,普通课,1,1,1,理学院,T003,30,40,1,16,Norm\n");
        f.write("C401,C04,不限课,1,1,1,理学院,T004,30,40,1,16,Any\n");
        f.close();
    }

    DataStore store;
    QVERIFY(store.loadCsv(
        tcPath,
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    for (const Course &c : store.courses()) {
        if (c.id == QLatin1String("C01"))
            QCOMPARE(c.requiredRoomType, ClassroomType::Lab);
        if (c.id == QLatin1String("C02"))
            QCOMPARE(c.requiredRoomType, ClassroomType::PlayGround);
        if (c.id == QLatin1String("C03"))
            QCOMPARE(c.requiredRoomType, ClassroomType::Norm);
        if (c.id == QLatin1String("C04"))
            QCOMPARE(c.requiredRoomType, ClassroomType::Any);
    }
}

/*
TestDataStore::classroomTypes - 教室类型字符串正确映射为枚举
*/
void TestDataStore::classroomTypes()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    for (const Classroom &c : store.classrooms()) {
        if (c.roomNumber == QLatin1String("Lab1"))
            QCOMPARE(c.type, ClassroomType::Lab);
        else if (c.roomNumber == QLatin1String("Play1"))
            QCOMPARE(c.type, ClassroomType::PlayGround);
        else if (c.roomNumber == QLatin1String("A101"))
            QCOMPARE(c.type, ClassroomType::Norm);
    }
}

/*
TestDataStore::sectionTimes - 作息表节次与起止时间解析正确
*/
void TestDataStore::sectionTimes()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QCOMPARE(store.sections().at(0).index, 1);
    QCOMPARE(store.sections().at(0).startTime, QTime(8, 0));
    QCOMPARE(store.sections().last().endTime, QTime(17, 40));
}

/*
TestDataStore::exportRoundTrip - 导出的 CSV 含表头与排课条目
*/
void TestDataStore::exportRoundTrip()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    ScheduleEntry e;
    e.entryId = QLatin1String("E1");
    e.teachingClassId = QLatin1String("C101");
    e.teacherId       = QLatin1String("T001");
    e.classroomId     = QLatin1String("A101");
    e.timeSlot.dayOfWeek   = 1;
    e.timeSlot.startSection = 2;
    e.timeSlot.endSection   = 2;
    store.addScheduleEntry(e);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath("schedule.csv");
    QVERIFY(store.exportCsv(out));

    QFile file(out);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QLatin1String("entryId,teachingClassId")));
    QVERIFY(content.contains(QLatin1String("startWeek,endWeek")));
    QVERIFY(content.contains(QLatin1String("E1,C101,T001,1,2,2,A101,1,16")));
}

/*
TestDataStore::loadMissingFileFails - 文件打不开时返回 false 且数据为空
*/
void TestDataStore::loadMissingFileFails()
{
    DataStore store;
    QVERIFY(!store.loadCsv(QStringLiteral("no/such/file.csv"),
                           QStringLiteral("no/such/file.csv"),
                           QStringLiteral("no/such/file.csv")));
    QCOMPARE(int(store.teachingClasses().size()), 0);
}

/*
TestDataStore::idLookupHits - 四类 ID 查询命中并返回正确字段
*/
void TestDataStore::idLookupHits()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv",
        QStringLiteral(DATA_DIR) + "/teachers.csv"));

    // 课程：C01 高等数学
    const Course *c = store.courseById(QLatin1String("C01"));
    QVERIFY(c);
    QCOMPARE(c->name, QStringLiteral("高等数学"));
    QCOMPARE(c->sessionsPerWeek, 2);

    // 教学班：C101 属 C01、教师 T001
    const TeachingClass *tc = store.teachingClassById(QLatin1String("C101"));
    QVERIFY(tc);
    QCOMPARE(tc->courseId, QLatin1String("C01"));
    QCOMPARE(tc->teacherId, QLatin1String("T001"));

    // 教师：T001 张伟（teachers.csv 真实姓名）
    const TeacherInfo *t = store.teacherById(QLatin1String("T001"));
    QVERIFY(t);
    QCOMPARE(t->name, QStringLiteral("张伟"));

    // 教室：Lab1 机房
    const Classroom *r = store.classroomById(QLatin1String("Lab1"));
    QVERIFY(r);
    QCOMPARE(r->type, ClassroomType::Lab);
}

/*
TestDataStore::idLookupUnknown - 不存在的 id 返回 nullptr
*/
void TestDataStore::idLookupUnknown()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QVERIFY(!store.courseById(QLatin1String("NO_SUCH")));
    QVERIFY(!store.teachingClassById(QLatin1String("NO_SUCH")));
    QVERIFY(!store.teacherById(QLatin1String("NO_SUCH")));
    QVERIFY(!store.classroomById(QLatin1String("NO_SUCH")));
}

/*
TestDataStore::idLookupAfterReload - loadSnapshot 后索引重建，查询仍正确
*/
void TestDataStore::idLookupAfterReload()
{
    DataStore src;
    QVERIFY(src.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("snap_lookup.csv");
    QVERIFY(src.saveSnapshot(path));

    DataStore dst;
    QVERIFY(dst.loadSnapshot(path));
    const Course *c = dst.courseById(QLatin1String("C01"));
    QVERIFY(c);
    QCOMPARE(c->name, QStringLiteral("高等数学"));
}

/*
TestDataStore::addCourseClassLifecycle - 新增课程 + 教学班后计数与查询正确
*/
void TestDataStore::addCourseClassLifecycle()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    Course nc;
    nc.id              = QLatin1String("CC99");
    nc.name            = QStringLiteral("新增测试课");
    nc.credit          = 3.0;
    nc.sessionsPerWeek = 2;
    nc.hoursPerSession = 2;
    nc.depart          = QStringLiteral("测试学院");
    nc.startWeek       = 1;
    nc.endWeek         = 12;
    nc.requiredRoomType = ClassroomType::Norm;
    QVERIFY(store.addCourse(nc));
    QCOMPARE(int(store.courses().size()), nCourses + 1);

    const Course *c = store.courseById(QLatin1String("CC99"));
    QVERIFY(c);
    QCOMPARE(c->name, QStringLiteral("新增测试课"));
    QCOMPARE(c->endWeek, 12);

    TeachingClass k;
    k.classId     = QLatin1String("Z999");
    k.courseId    = QLatin1String("CC99");
    k.teacherId   = QLatin1String("T999");
    k.plannedSize = 20;
    k.maxCapacity = 30;
    QVERIFY(store.addTeachingClass(k));
    QCOMPARE(int(store.teachingClasses().size()), nClasses + 1);

    const TeachingClass *tc = store.teachingClassById(QLatin1String("Z999"));
    QVERIFY(tc);
    QCOMPARE(tc->courseId, QLatin1String("CC99"));
    QCOMPARE(tc->plannedSize, 20);
    QCOMPARE(tc->maxCapacity, 30);
}

/*
TestDataStore::addCourseDuplicateRejected - 课程号/教学班号重复或为空时新增返回 false
*/
void TestDataStore::addCourseDuplicateRejected()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    // 空 id / 已存在 id → false
    Course emptyId;
    emptyId.id = QString();
    QVERIFY(!store.addCourse(emptyId));
    Course dup;
    dup.id = QLatin1String("C01");     // 示例数据中已存在
    QVERIFY(!store.addCourse(dup));
    QCOMPARE(int(store.courses().size()), nCourses);   // 数量不变

    TeachingClass emptyClass;
    emptyClass.classId = QString();
    QVERIFY(!store.addTeachingClass(emptyClass));
    TeachingClass dupClass;
    dupClass.classId  = QLatin1String("C101");          // 示例数据中已存在
    dupClass.courseId = QLatin1String("C01");
    QVERIFY(!store.addTeachingClass(dupClass));
    QCOMPARE(int(store.teachingClasses().size()), nClasses);
}

/*
TestDataStore::removeClassCleansRecords - 删教学班连带清其排课条目/失败明细/锁定
*/
void TestDataStore::removeClassCleansRecords()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    // 造一门含 2 班的课，其中 D1 有排课/失败/锁定
    Course c;
    c.id = QLatin1String("DD99");
    c.name = QStringLiteral("删除测试课");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(store.addCourse(c));
    TeachingClass d1;
    d1.classId = QLatin1String("DEL1");
    d1.courseId = QLatin1String("DD99");
    d1.plannedSize = 20;
    d1.maxCapacity = 30;
    TeachingClass d2 = d1;
    d2.classId = QLatin1String("DEL2");
    QVERIFY(store.addTeachingClass(d1));
    QVERIFY(store.addTeachingClass(d2));

    ScheduleEntry e;
    e.entryId = QLatin1String("E_DEL");
    e.teachingClassId = QLatin1String("DEL1");
    e.classroomId = QLatin1String("A101");
    e.timeSlot.dayOfWeek = 1;
    e.timeSlot.startSection = 1;
    e.timeSlot.endSection = 1;
    store.addScheduleEntry(e);
    ScheduleFailure f;
    f.classId = QLatin1String("DEL1");
    f.courseId = QLatin1String("DD99");
    f.courseName = QStringLiteral("删除测试课");
    f.reason = QStringLiteral("测试");
    store.setScheduleFailures({f});
    store.lockClass(QLatin1String("DEL1"));
    store.lockClass(QLatin1String("DEL2"));

    // 删 D1（课程还有 D2）：班删、其记录全清、课程仍在
    QVERIFY(store.removeTeachingClass(QLatin1String("DEL1")));
    QVERIFY(!store.teachingClassById(QLatin1String("DEL1")));
    QVERIFY(store.teachingClassById(QLatin1String("DEL2")));   // 兄弟班不动
    QVERIFY(store.courseById(QLatin1String("DD99")));           // 课仍在（非末班）
    QCOMPARE(int(store.scheduleEntries().size()), 0);           // D1 条目清
    QCOMPARE(int(store.scheduleFailures().size()), 0);          // D1 失败明细清
    QVERIFY(store.isClassLocked(QLatin1String("DEL2")));        // D2 锁仍在
    QVERIFY(!store.isClassLocked(QLatin1String("DEL1")));       // D1 锁清

    // 再删 D2（最后一个班）：课程保留为空课程，不随末班一并删除
    QVERIFY(store.removeTeachingClass(QLatin1String("DEL2")));
    QVERIFY(!store.teachingClassById(QLatin1String("DEL2")));
    QVERIFY(store.courseById(QLatin1String("DD99")));           // 空课程仍在
    QVERIFY(!store.isClassLocked(QLatin1String("DEL2")));       // D2 锁也随删清
}

/*
TestDataStore::removeLastClassKeepsEmptyCourse - 删某课最后一个班：课程保留为空课程
*/
void TestDataStore::removeLastClassKeepsEmptyCourse()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    Course c;
    c.id = QLatin1String("SOLO");
    c.name = QStringLiteral("单班课");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(store.addCourse(c));
    TeachingClass solo;
    solo.classId = QLatin1String("SOLOC1");
    solo.courseId = QLatin1String("SOLO");
    QVERIFY(store.addTeachingClass(solo));
    QCOMPARE(int(store.courses().size()), nCourses + 1);

    QVERIFY(store.removeTeachingClass(QLatin1String("SOLOC1")));
    QCOMPARE(int(store.courses().size()), nCourses + 1);   // 末班删 → 课程保留为空课程
    QCOMPARE(int(store.teachingClasses().size()), nClasses);
    QVERIFY(store.courseById(QLatin1String("SOLO")));       // 课程仍在（空课程）
    QVERIFY(!store.teachingClassById(QLatin1String("SOLOC1")));
}

/*
TestDataStore::removeCourseWhole - 删整门课连带其全部教学班与记录
*/
void TestDataStore::removeCourseWhole()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    Course c;
    c.id = QLatin1String("EE99");
    c.name = QStringLiteral("整课删除");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(store.addCourse(c));
    TeachingClass t;
    t.courseId = QLatin1String("EE99");
    t.plannedSize = 20;
    t.maxCapacity = 30;
    for (const char *id : {"E1", "E2"}) {
        TeachingClass x = t;
        x.classId = QLatin1String(id);
        QVERIFY(store.addTeachingClass(x));
        store.lockClass(x.classId);
    }
    ScheduleEntry e;
    e.entryId = QLatin1String("E_EE");
    e.teachingClassId = QLatin1String("E1");
    e.classroomId = QLatin1String("A101");
    e.timeSlot.dayOfWeek = 2;
    e.timeSlot.startSection = 2;
    e.timeSlot.endSection = 2;
    store.addScheduleEntry(e);

    QVERIFY(store.removeCourse(QLatin1String("EE99")));
    QVERIFY(!store.courseById(QLatin1String("EE99")));
    QVERIFY(!store.teachingClassById(QLatin1String("E1")));
    QVERIFY(!store.teachingClassById(QLatin1String("E2")));
    QCOMPARE(int(store.scheduleEntries().size()), 0);           // E1 条目清
    QVERIFY(!store.isClassLocked(QLatin1String("E1")));
    QVERIFY(!store.isClassLocked(QLatin1String("E2")));
    QCOMPARE(int(store.courses().size()), nCourses);            // 只少造的那门课
    QCOMPARE(int(store.teachingClasses().size()), nClasses);
}

/*
TestDataStore::removeEmptyCourseWorks - 空课程可单独落库并整门删除
*/
void TestDataStore::removeEmptyCourseWorks()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    Course ghost;
    ghost.id = QLatin1String("GHOST");
    ghost.name = QStringLiteral("预开空课");
    ghost.sessionsPerWeek = 2;
    ghost.hoursPerSession = 1;
    QVERIFY(store.addCourse(ghost));                    // 只建空课程（不建班）
    QVERIFY(store.courseById(QLatin1String("GHOST")));
    QCOMPARE(int(store.courses().size()), nCourses + 1);
    QCOMPARE(int(store.teachingClasses().size()), nClasses);   // 无教学班产生

    // 整门删除空课程：直接生效，不影响其它课程/教学班
    QVERIFY(store.removeCourse(QLatin1String("GHOST")));
    QVERIFY(!store.courseById(QLatin1String("GHOST")));
    QCOMPARE(int(store.courses().size()), nCourses);
    QCOMPARE(int(store.teachingClasses().size()), nClasses);
}

/*
TestDataStore::removeUnknownFails - 删除不存在的课程/教学班返回 false 且数据不变
*/
void TestDataStore::removeUnknownFails()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));
    const int nCourses = int(store.courses().size());
    const int nClasses = int(store.teachingClasses().size());

    QVERIFY(!store.removeTeachingClass(QStringLiteral("NO_SUCH")));
    QVERIFY(!store.removeCourse(QStringLiteral("NO_SUCH")));
    QCOMPARE(int(store.courses().size()), nCourses);
    QCOMPARE(int(store.teachingClasses().size()), nClasses);
}

/*
TestDataStore::lockStateLifecycle - 锁定状态增删/整体替换/clear 生命周期
*/
void TestDataStore::lockStateLifecycle()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    // 初始无锁定
    QVERIFY(store.lockedClassIds().isEmpty());
    QVERIFY(!store.isClassLocked(QLatin1String("C101")));

    // 锁定两个班（幂等）
    store.lockClass(QLatin1String("C101"));
    store.lockClass(QLatin1String("C102"));
    store.lockClass(QLatin1String("C101"));          // 重复锁 = 无操作
    QVERIFY(store.isClassLocked(QLatin1String("C101")));
    QVERIFY(store.isClassLocked(QLatin1String("C102")));
    QCOMPARE(int(store.lockedClassIds().size()), 2);

    // 解锁一个（幂等）
    store.unlockClass(QLatin1String("C102"));
    store.unlockClass(QLatin1String("C102"));
    QVERIFY(!store.isClassLocked(QLatin1String("C102")));
    QCOMPARE(int(store.lockedClassIds().size()), 1);

    // 整体替换
    store.setLockedClasses({QLatin1String("C301"), QLatin1String("C302")});
    QCOMPARE(int(store.lockedClassIds().size()), 2);
    QVERIFY(store.isClassLocked(QLatin1String("C301")));
    QVERIFY(!store.isClassLocked(QLatin1String("C101")));   // 旧锁被覆盖

    // clear 重置锁定
    store.clear();
    QVERIFY(store.lockedClassIds().isEmpty());
}

/*
TestDataStore::updateEntryInPlace - updateScheduleEntry 原位替换：保下标、字段变、其它不动
*/
void TestDataStore::updateEntryInPlace()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    ScheduleEntry e1;
    e1.entryId = QLatin1String("E1");
    e1.teachingClassId = QLatin1String("C101");
    e1.teacherId       = QLatin1String("T001");
    e1.classroomId     = QLatin1String("A101");
    e1.timeSlot.dayOfWeek   = 1;
    e1.timeSlot.startSection = 2;
    e1.timeSlot.endSection   = 2;
    store.addScheduleEntry(e1);
    ScheduleEntry e2 = e1;
    e2.entryId = QLatin1String("E2");
    e2.classroomId = QLatin1String("A201");
    store.addScheduleEntry(e2);
    QCOMPARE(int(store.scheduleEntries().size()), 2);

    ScheduleEntry updated = e1;                 // 改时间 + 换教室，entryId 不变
    updated.classroomId = QLatin1String("J1-404");
    updated.timeSlot.dayOfWeek = 3;
    updated.timeSlot.startSection = 5;
    updated.timeSlot.endSection = 6;
    QVERIFY(store.updateScheduleEntry(updated));

    // 原位替换：E1 仍在原下标、内容已变；E2 不受影响
    const ScheduleEntry &a = store.scheduleEntries().at(0);
    QCOMPARE(a.entryId, QLatin1String("E1"));
    QCOMPARE(a.classroomId, QLatin1String("J1-404"));
    QCOMPARE(a.timeSlot.dayOfWeek, 3);
    QCOMPARE(a.timeSlot.endSection, 6);
    const ScheduleEntry &b = store.scheduleEntries().at(1);
    QCOMPARE(b.entryId, QLatin1String("E2"));
    QCOMPARE(b.classroomId, QLatin1String("A201"));
    QCOMPARE(int(store.scheduleEntries().size()), 2);

    // scheduleEntryById 命中返回更新后内容
    const ScheduleEntry *found = store.scheduleEntryById(QLatin1String("E1"));
    QVERIFY(found);
    QCOMPARE(found->classroomId, QLatin1String("J1-404"));
}

/*
TestDataStore::updateUnknownReturnsFalse - 更新不存在的 entryId 返回 false 且条目不变
*/
void TestDataStore::updateUnknownReturnsFalse()
{
    DataStore store;
    QVERIFY(store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv"));

    ScheduleEntry e;
    e.entryId = QLatin1String("E1");
    e.teachingClassId = QLatin1String("C101");
    e.classroomId = QLatin1String("A101");
    e.timeSlot.dayOfWeek = 1;
    e.timeSlot.startSection = 1;
    e.timeSlot.endSection = 1;
    store.addScheduleEntry(e);

    ScheduleEntry ghost = e;
    ghost.entryId = QLatin1String("NO_SUCH");
    QVERIFY(!store.updateScheduleEntry(ghost));
    QCOMPARE(int(store.scheduleEntries().size()), 1);
    QVERIFY(store.scheduleEntryById(QLatin1String("E1")));
}

/*
TestDataStore::entryIndexAfterManyAdds - 数百条目后 scheduleEntryById 索引命中/未命中（R8）
*/
void TestDataStore::entryIndexAfterManyAdds()
{
    DataStore store;
    for (int i = 0; i < 500; ++i) {
        ScheduleEntry e;
        e.entryId              = QStringLiteral("E%1").arg(i);
        e.teachingClassId      = QStringLiteral("C%1").arg(i % 50);
        e.classroomId          = QStringLiteral("R%1").arg(i);
        e.timeSlot.dayOfWeek    = 1 + i % 7;
        e.timeSlot.startSection = 1 + i % 6;
        e.timeSlot.endSection   = e.timeSlot.startSection;
        store.addScheduleEntry(e);
    }
    QCOMPARE(int(store.scheduleEntries().size()), 500);

    // 懒建索引后按 id 命中首位/末位，未命中返回 nullptr
    const ScheduleEntry *first = store.scheduleEntryById(QLatin1String("E0"));
    QVERIFY(first);
    QCOMPARE(first->classroomId, QLatin1String("R0"));
    const ScheduleEntry *last = store.scheduleEntryById(QLatin1String("E499"));
    QVERIFY(last);
    QCOMPARE(last->classroomId, QLatin1String("R499"));
    QVERIFY(!store.scheduleEntryById(QLatin1String("NO_SUCH")));
}

/*
TestDataStore::entryIndexAppendAfterBuild - 索引已建后再尾插，新条目立即可查、旧序不乱（R8）
*/
void TestDataStore::entryIndexAppendAfterBuild()
{
    DataStore store;
    ScheduleEntry e;
    e.entryId = QLatin1String("A0");
    e.classroomId = QLatin1String("R0");
    e.timeSlot.dayOfWeek = 1;
    e.timeSlot.startSection = 1;
    e.timeSlot.endSection = 1;
    store.addScheduleEntry(e);

    QVERIFY(store.scheduleEntryById(QLatin1String("A0")));   // 首次查询触发懒建

    for (int i = 1; i <= 20; ++i) {                          // 索引已建后尾插 20 条
        ScheduleEntry t = e;
        t.entryId = QStringLiteral("A%1").arg(i);
        store.addScheduleEntry(t);
    }
    // 尾插不搬旧下标：仅补插新尾巴，老条目与追加条目都能命中
    const ScheduleEntry *found = store.scheduleEntryById(QLatin1String("A20"));
    QVERIFY(found);
    QCOMPARE(found->classroomId, QLatin1String("R0"));
    QVERIFY(store.scheduleEntryById(QLatin1String("A0")));
    QCOMPARE(store.scheduleEntries().at(0).entryId, QLatin1String("A0"));  // 卡序未漂移
    QCOMPARE(int(store.scheduleEntries().size()), 21);
}

/*
TestDataStore::entryIndexClearInvalidates - clearScheduleEntries 失效索引：再查 nullptr、重建后正确（R8）
*/
void TestDataStore::entryIndexClearInvalidates()
{
    DataStore store;
    for (int i = 0; i < 100; ++i) {
        ScheduleEntry e;
        e.entryId              = QStringLiteral("E%1").arg(i);
        e.classroomId          = QStringLiteral("R%1").arg(i);
        e.timeSlot.dayOfWeek    = 1;
        e.timeSlot.startSection = 1 + i % 6;
        e.timeSlot.endSection   = e.timeSlot.startSection;
        store.addScheduleEntry(e);
    }
    QVERIFY(store.scheduleEntryById(QLatin1String("E50")));   // 建索引

    store.clearScheduleEntries();                             // 整表清空 → 索引失效
    QVERIFY(store.scheduleEntries().isEmpty());
    QVERIFY(!store.scheduleEntryById(QLatin1String("E50")));

    ScheduleEntry n;
    n.entryId = QLatin1String("NEW");
    n.classroomId = QLatin1String("R0");
    n.timeSlot.dayOfWeek = 1;
    n.timeSlot.startSection = 1;
    n.timeSlot.endSection = 1;
    store.addScheduleEntry(n);
    // 重建（懒建）作用在新条目上，旧 id 不再命中
    const ScheduleEntry *found = store.scheduleEntryById(QLatin1String("NEW"));
    QVERIFY(found);
    QCOMPARE(int(store.scheduleEntries().size()), 1);
    QVERIFY(!store.scheduleEntryById(QLatin1String("E50")));
}

/*
TestDataStore::entryIndexDropRecordsRebuild - 删班过滤条目后索引重建正确（R8）
*/
void TestDataStore::entryIndexDropRecordsRebuild()
{
    DataStore store;
    Course c;
    c.id = QLatin1String("ZZ99");
    c.name = QStringLiteral("索引删除课");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(store.addCourse(c));
    TeachingClass d1;
    d1.classId = QLatin1String("DEL1");
    d1.courseId = QLatin1String("ZZ99");
    d1.plannedSize = 20;
    d1.maxCapacity = 30;
    TeachingClass d2 = d1;
    d2.classId = QLatin1String("DEL2");
    QVERIFY(store.addTeachingClass(d1));
    QVERIFY(store.addTeachingClass(d2));

    for (const char *cid : {"DEL1", "DEL2"}) {
        ScheduleEntry e;
        e.entryId = QLatin1String("E_") + QLatin1String(cid);
        e.teachingClassId = QLatin1String(cid);
        e.classroomId = QLatin1String("A101");
        e.timeSlot.dayOfWeek = 1;
        e.timeSlot.startSection = 1;
        e.timeSlot.endSection = 1;
        store.addScheduleEntry(e);
    }
    QVERIFY(store.scheduleEntryById(QLatin1String("E_DEL1")));   // 建索引

    QVERIFY(store.removeTeachingClass(QLatin1String("DEL1")));
    // 删班过滤整表重建条目下标 → 被删班条目查不到、幸存条目仍可命中
    QVERIFY(!store.scheduleEntryById(QLatin1String("E_DEL1")));
    const ScheduleEntry *kept = store.scheduleEntryById(QLatin1String("E_DEL2"));
    QVERIFY(kept);
    QCOMPARE(kept->teachingClassId, QLatin1String("DEL2"));
    QCOMPARE(int(store.scheduleEntries().size()), 1);
}

/*
TestDataStore::undoRedoBasicCycle - push→动作→undo 回 S0，redo 复现 S1（R7 基本环）
*/
void TestDataStore::undoRedoBasicCycle()
{
    DataStore cur = loadedSample();     // 模拟 MainWindow::m_store
    UndoBuffer buf(20);
    const DataStore s0 = cur;           // 动作前参考
    buf.push(cur);                      // 入环：捕获动作前状态

    Course c;                           // 动作：加一门课 + 锁一个班
    c.id = QLatin1String("ZK1");
    c.name = QStringLiteral("撤销测试课");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(cur.addCourse(c));
    cur.lockClass(QLatin1String("C101"));
    QVERIFY(buf.canUndo());
    QVERIFY(!buf.canRedo());

    DataStore t;
    QVERIFY(buf.undo(cur, &t));
    cur = t;                            // restoreStore 语义：整体赋回
    QVERIFY(!cur.courseById(QLatin1String("ZK1")));
    QVERIFY(!cur.isClassLocked(QLatin1String("C101")));
    QVERIFY(storeBytesEqual(cur, s0));
    QVERIFY(buf.canRedo());
    QVERIFY(!buf.canUndo());

    QVERIFY(buf.redo(cur, &t));
    cur = t;
    QVERIFY(cur.courseById(QLatin1String("ZK1")));
    QVERIFY(cur.isClassLocked(QLatin1String("C101")));
    QVERIFY(buf.canUndo());
    QVERIFY(!buf.canRedo());
}

/*
TestDataStore::pushClearsRedoStack - 撤销后再 push 清空 redo，重做只能走新分支（R7 线性分支）
*/
void TestDataStore::pushClearsRedoStack()
{
    DataStore cur = loadedSample();
    UndoBuffer buf(20);
    buf.push(cur);

    Course ca;                          // 动作 A：加课 KA
    ca.id = QLatin1String("KA");
    ca.name = QStringLiteral("作废分支课");
    ca.sessionsPerWeek = 1;
    ca.hoursPerSession = 1;
    QVERIFY(cur.addCourse(ca));
    DataStore t;
    QVERIFY(buf.undo(cur, &t));
    cur = t;                            // 撤销 A → 回 S0，redo 分支=A 动作后态
    QVERIFY(buf.canRedo());

    buf.push(cur);                      // 新动作 B 前捕获：push 清空 redo 旧分支
    Course cb = ca;
    cb.id = QLatin1String("KB");
    QVERIFY(cur.addCourse(cb));
    QVERIFY(!buf.canRedo());            // A 的分支作废
    QCOMPARE(buf.redoDepth(), 0);

    QVERIFY(buf.undo(cur, &t));
    cur = t;                            // 撤销 B → S0（A、B 皆无）
    QVERIFY(!cur.courseById(QLatin1String("KA")));
    QVERIFY(!cur.courseById(QLatin1String("KB")));
    QVERIFY(buf.redo(cur, &t));
    cur = t;                            // 重做只回 B，不重放已作废的 A
    QVERIFY(cur.courseById(QLatin1String("KB")));
    QVERIFY(!cur.courseById(QLatin1String("KA")));
}

/*
TestDataStore::capacityEvictsOldest - 容量上限挤出最旧：4 次入环只留 3 步、undoDepth==3
*/
void TestDataStore::capacityEvictsOldest()
{
    UndoBuffer buf(3);
    DataStore cur = loadedSample();
    QCOMPARE(buf.capacity(), 3);

    for (int i = 0; i < 4; ++i) {
        const DataStore before = cur;   // S_i
        buf.push(before);
        Course c;
        c.id = QStringLiteral("C%1").arg(i);
        c.name = QStringLiteral("容量测试课");
        c.sessionsPerWeek = 1;
        c.hoursPerSession = 1;
        QVERIFY(cur.addCourse(c));
    }
    QCOMPARE(buf.undoDepth(), 3);       // S0 被挤出

    DataStore t;
    for (int k = 0; k < 3; ++k) {
        QVERIFY(buf.undo(cur, &t));
        cur = t;
    }
    QVERIFY(cur.courseById(QLatin1String("C0")));    // 停在 S1：最旧 S0（无 C0）已不可达
    QVERIFY(!cur.courseById(QLatin1String("C3")));
    QVERIFY(!buf.canUndo());            // 第 4 步不可撤
    QVERIFY(!buf.undo(cur, &t));
}

/*
TestDataStore::undoRestoresStructuralDelete - 删整门课后 undo 全量还原（含班/排课/失败/锁）
*/
void TestDataStore::undoRestoresStructuralDelete()
{
    DataStore cur = loadedSample();
    Course c;
    c.id = QLatin1String("XX99");
    c.name = QStringLiteral("整课撤销");
    c.sessionsPerWeek = 1;
    c.hoursPerSession = 1;
    QVERIFY(cur.addCourse(c));
    TeachingClass t1;
    t1.classId = QLatin1String("XXC1");
    t1.courseId = QLatin1String("XX99");
    t1.plannedSize = 20;
    t1.maxCapacity = 30;
    TeachingClass t2 = t1;
    t2.classId = QLatin1String("XXC2");
    QVERIFY(cur.addTeachingClass(t1));
    QVERIFY(cur.addTeachingClass(t2));
    ScheduleEntry e;
    e.entryId = QLatin1String("E_XX");
    e.teachingClassId = QLatin1String("XXC1");
    e.classroomId = QLatin1String("A101");
    e.timeSlot.dayOfWeek = 1;
    e.timeSlot.startSection = 1;
    e.timeSlot.endSection = 1;
    cur.addScheduleEntry(e);
    ScheduleFailure f;
    f.classId = QLatin1String("XXC1");
    f.courseId = QLatin1String("XX99");
    f.courseName = QStringLiteral("整课撤销");
    f.reason = QStringLiteral("容量不足");
    cur.setScheduleFailures({f});
    cur.lockClass(QLatin1String("XXC1"));
    cur.lockClass(QLatin1String("XXC2"));

    UndoBuffer buf(20);
    const DataStore s0 = cur;
    buf.push(cur);
    QVERIFY(cur.removeCourse(QLatin1String("XX99")));   // 整课删：班/条目/失败/锁随删
    QVERIFY(!cur.courseById(QLatin1String("XX99")));
    QVERIFY(!cur.teachingClassById(QLatin1String("XXC1")));
    QCOMPARE(int(cur.scheduleEntries().size()), 0);
    QCOMPARE(int(cur.scheduleFailures().size()), 0);

    DataStore t;
    QVERIFY(buf.undo(cur, &t));
    cur = t;
    QVERIFY(cur.courseById(QLatin1String("XX99")));
    QVERIFY(cur.teachingClassById(QLatin1String("XXC1")));
    QVERIFY(cur.scheduleEntryById(QLatin1String("E_XX")));
    QVERIFY(cur.isClassLocked(QLatin1String("XXC1")));
    QVERIFY(cur.isClassLocked(QLatin1String("XXC2")));
    QCOMPARE(int(cur.scheduleFailures().size()), 1);
    QVERIFY(storeBytesEqual(cur, s0));
}

/*
TestDataStore::undoRestoresEntryUpdate - 手动调整改教室/时间后 undo 还原原条目（R7 详情落库）
*/
void TestDataStore::undoRestoresEntryUpdate()
{
    DataStore cur = loadedSample();
    ScheduleEntry e1;
    e1.entryId = QLatin1String("E1");
    e1.teachingClassId = QLatin1String("C101");
    e1.teacherId = QLatin1String("T001");
    e1.classroomId = QLatin1String("A101");
    e1.timeSlot.dayOfWeek = 1;
    e1.timeSlot.startSection = 2;
    e1.timeSlot.endSection = 2;
    cur.addScheduleEntry(e1);

    UndoBuffer buf(20);
    const DataStore s0 = cur;
    buf.push(cur);
    ScheduleEntry up = e1;              // 换教室 + 改时段
    up.classroomId = QLatin1String("J1-404");
    up.timeSlot.dayOfWeek = 3;
    up.timeSlot.startSection = 5;
    up.timeSlot.endSection = 6;
    QVERIFY(cur.updateScheduleEntry(up));
    QCOMPARE(cur.scheduleEntryById(QLatin1String("E1"))->classroomId,
             QLatin1String("J1-404"));

    DataStore t;
    QVERIFY(buf.undo(cur, &t));
    cur = t;
    const ScheduleEntry *r = cur.scheduleEntryById(QLatin1String("E1"));
    QVERIFY(r);
    QCOMPARE(r->classroomId, QLatin1String("A101"));
    QCOMPARE(r->timeSlot.dayOfWeek, 1);
    QVERIFY(storeBytesEqual(cur, s0));
}

/*
TestDataStore::undoRestoresInPlaceOverwrite - loadSnapshot 原地 clear+重填后 undo 回旧工作区
*/
void TestDataStore::undoRestoresInPlaceOverwrite()
{
    // 两套不同工作区快照 A / B
    DataStore a = loadedSample();
    Course ca;
    ca.id = QLatin1String("AAA");
    ca.name = QStringLiteral("A 工作区课");
    ca.sessionsPerWeek = 1;
    ca.hoursPerSession = 1;
    QVERIFY(a.addCourse(ca));
    DataStore b = loadedSample();
    Course cb;
    cb.id = QLatin1String("BBB");
    cb.name = QStringLiteral("B 工作区课");
    cb.sessionsPerWeek = 1;
    cb.hoursPerSession = 1;
    QVERIFY(b.addCourse(cb));
    QTemporaryDir dir;
    const QString fA = dir.filePath(QStringLiteral("A.dat"));
    const QString fB = dir.filePath(QStringLiteral("B.dat"));
    QVERIFY(a.saveSnapshot(fA));
    QVERIFY(b.saveSnapshot(fB));

    DataStore cur;                      // 当前工作区 = A 态
    QVERIFY(cur.loadSnapshot(fA));
    UndoBuffer buf(20);
    buf.push(cur);                      // 导入前捕获（须在 loadSnapshot 清空前）
    QVERIFY(cur.loadSnapshot(fB));      // 原地 clear+重填 为 B 态
    QVERIFY(cur.courseById(QLatin1String("BBB")));
    QVERIFY(!cur.courseById(QLatin1String("AAA")));

    DataStore expected;
    QVERIFY(expected.loadSnapshot(fA)); // 与 cur 同源的 A 态参考
    DataStore t;
    QVERIFY(buf.undo(cur, &t));
    cur = t;
    QVERIFY(cur.courseById(QLatin1String("AAA")));
    QVERIFY(!cur.courseById(QLatin1String("BBB")));
    QVERIFY(storeBytesEqual(cur, expected));
}

/*
TestDataStore::editCourseInPlace - updateCourse 原位改写课程名/学院，其它课程不动；未知 id 返回 false
*/
void TestDataStore::editCourseInPlace()
{
    DataStore store = loadedSample();
    const Course *c01 = store.courseById(QLatin1String("C01"));
    QVERIFY(c01);
    QCOMPARE(c01->name, QStringLiteral("高等数学"));

    Course updated = *c01;
    updated.name   = QStringLiteral("高等数学（提高班）");
    updated.depart = QStringLiteral("数理学院");
    QVERIFY(store.updateCourse(updated));

    const Course *again = store.courseById(QLatin1String("C01"));
    QVERIFY(again);
    QCOMPARE(again->name, QStringLiteral("高等数学（提高班）"));
    QCOMPARE(again->depart, QStringLiteral("数理学院"));

    // 其它课程与课程总数不受影响
    const Course *c02 = store.courseById(QLatin1String("C02"));
    QVERIFY(c02);
    QCOMPARE(c02->name, QStringLiteral("大学物理"));
    QCOMPARE(int(store.courses().size()), 6);

    Course ghost = *c01;
    ghost.id = QLatin1String("NO_SUCH");
    QVERIFY(!store.updateCourse(ghost));
}

/*
TestDataStore::editTeachingClassInPlace - updateTeachingClass 原位改写人数/容量；未知 id 返回 false
*/
void TestDataStore::editTeachingClassInPlace()
{
    DataStore store = loadedSample();
    const TeachingClass *c101 = store.teachingClassById(QLatin1String("C101"));
    QVERIFY(c101);
    QCOMPARE(c101->plannedSize, 120);
    QCOMPARE(c101->maxCapacity, 150);

    TeachingClass updated = *c101;
    updated.plannedSize = 140;
    updated.maxCapacity = 160;
    QVERIFY(store.updateTeachingClass(updated));

    const TeachingClass *again = store.teachingClassById(QLatin1String("C101"));
    QVERIFY(again);
    QCOMPARE(again->plannedSize, 140);
    QCOMPARE(again->maxCapacity, 160);

    // 同课其它班不受影响（C102 仍原值）
    const TeachingClass *c102 = store.teachingClassById(QLatin1String("C102"));
    QVERIFY(c102);
    QCOMPARE(c102->plannedSize, 110);

    TeachingClass ghost = updated;
    ghost.classId = QLatin1String("NO_SUCH");
    QVERIFY(!store.updateTeachingClass(ghost));
}

/*
TestDataStore::reassignTeacherSyncsEntries - reassignClassTeacher 改班教师并连带改写该班已排条目教师；未知班返回 false
*/
void TestDataStore::reassignTeacherSyncsEntries()
{
    DataStore store = loadedSample();
    const int baseEntries = int(store.scheduleEntries().size());   // CSV 载入无排课 → 0

    ScheduleEntry e1;
    e1.entryId         = QLatin1String("C101#1");
    e1.teachingClassId = QLatin1String("C101");
    e1.teacherId       = QLatin1String("T001");
    e1.classroomId     = QLatin1String("A101");
    e1.timeSlot.dayOfWeek   = 1;
    e1.timeSlot.startSection = 2;
    e1.timeSlot.endSection   = 2;
    store.addScheduleEntry(e1);
    ScheduleEntry e2 = e1;
    e2.entryId         = QLatin1String("C101#2");
    e2.timeSlot.dayOfWeek   = 3;
    e2.timeSlot.startSection = 5;
    e2.timeSlot.endSection   = 6;
    store.addScheduleEntry(e2);
    QCOMPARE(int(store.scheduleEntries().size()), baseEntries + 2);

    QVERIFY(store.reassignClassTeacher(QLatin1String("C101"), QLatin1String("T002")));

    const TeachingClass *tc = store.teachingClassById(QLatin1String("C101"));
    QVERIFY(tc);
    QCOMPARE(tc->teacherId, QLatin1String("T002"));

    // 两条已排条目教师一并改写，时间/教室原样保留
    const ScheduleEntry *a = store.scheduleEntryById(QLatin1String("C101#1"));
    const ScheduleEntry *b = store.scheduleEntryById(QLatin1String("C101#2"));
    QVERIFY(a && b);
    QCOMPARE(a->teacherId, QLatin1String("T002"));
    QCOMPARE(a->timeSlot.dayOfWeek, 1);
    QCOMPARE(a->classroomId, QLatin1String("A101"));
    QCOMPARE(b->teacherId, QLatin1String("T002"));
    QCOMPARE(b->timeSlot.dayOfWeek, 3);
    QCOMPARE(b->timeSlot.endSection, 6);

    // 未知班返回 false，整体条目数不变
    QVERIFY(!store.reassignClassTeacher(QLatin1String("NO_SUCH"), QLatin1String("T002")));
    QCOMPARE(int(store.scheduleEntries().size()), baseEntries + 2);
}

/*
TestDataStore::teacherSwapClashDetectsOverlap - 新师在同一时段已教别的班 → 预检命中撞车
*/
void TestDataStore::teacherSwapClashDetectsOverlap()
{
    DataStore store = loadedSample();

    ScheduleEntry self;                        // C101 现有课：周一第 2 节
    self.entryId         = QLatin1String("C101#1");
    self.teachingClassId = QLatin1String("C101");
    self.teacherId       = QLatin1String("T001");
    self.classroomId     = QLatin1String("A101");
    self.timeSlot.dayOfWeek   = 1;
    self.timeSlot.startSection = 2;
    self.timeSlot.endSection   = 2;
    store.addScheduleEntry(self);

    ScheduleEntry other;                       // C202 由 T003 授课，同一周一第 2 节
    other.entryId         = QLatin1String("C202#1");
    other.teachingClassId = QLatin1String("C202");
    other.teacherId       = QLatin1String("T003");
    other.classroomId     = QLatin1String("B101");
    other.timeSlot.dayOfWeek   = 1;
    other.timeSlot.startSection = 2;
    other.timeSlot.endSection   = 3;
    store.addScheduleEntry(other);

    // C101 换师到 T003 → 同段相撞
    QVERIFY(store.teacherSwapClashes(QLatin1String("C101"), QLatin1String("T003")));
    // 新师同为本班现师、且无别班占用该时段 → 自换不撞；空教师"未安排"永不撞
    QVERIFY(!store.teacherSwapClashes(QLatin1String("C101"), QLatin1String("T001")));
    QVERIFY(!store.teacherSwapClashes(QLatin1String("C101"), QString()));
}

/*
TestDataStore::teacherSwapClashAllowsFreeSlot - 新师在别班但不在该班当前时段 → 不撞车
*/
void TestDataStore::teacherSwapClashAllowsFreeSlot()
{
    DataStore store = loadedSample();

    ScheduleEntry self;
    self.entryId         = QLatin1String("C101#1");
    self.teachingClassId = QLatin1String("C101");
    self.teacherId       = QLatin1String("T001");
    self.classroomId     = QLatin1String("A101");
    self.timeSlot.dayOfWeek   = 1;
    self.timeSlot.startSection = 2;
    self.timeSlot.endSection   = 2;
    store.addScheduleEntry(self);

    ScheduleEntry other;                       // T003 的课在周三第 5 节，不与 C101 冲突
    other.entryId         = QLatin1String("C202#1");
    other.teachingClassId = QLatin1String("C202");
    other.teacherId       = QLatin1String("T003");
    other.classroomId     = QLatin1String("B101");
    other.timeSlot.dayOfWeek   = 3;
    other.timeSlot.startSection = 5;
    other.timeSlot.endSection   = 5;
    store.addScheduleEntry(other);

    QVERIFY(!store.teacherSwapClashes(QLatin1String("C101"), QLatin1String("T003")));
}

/*
TestDataStore::teacherSwapClashRespectsWeekRange - 同一时段但周范围不交叠 → 不撞车
*/
void TestDataStore::teacherSwapClashRespectsWeekRange()
{
    DataStore store = loadedSample();

    ScheduleEntry self;                        // C101：第 1–8 周 周一第 2 节
    self.entryId         = QLatin1String("C101#1");
    self.teachingClassId = QLatin1String("C101");
    self.teacherId       = QLatin1String("T001");
    self.classroomId     = QLatin1String("A101");
    self.timeSlot.dayOfWeek   = 1;
    self.timeSlot.startSection = 2;
    self.timeSlot.endSection   = 2;
    self.startWeek = 1;
    self.endWeek   = 8;
    store.addScheduleEntry(self);

    ScheduleEntry other;                       // T003 同段但第 9–16 周
    other.entryId         = QLatin1String("C202#1");
    other.teachingClassId = QLatin1String("C202");
    other.teacherId       = QLatin1String("T003");
    other.classroomId     = QLatin1String("B101");
    other.timeSlot.dayOfWeek   = 1;
    other.timeSlot.startSection = 2;
    other.timeSlot.endSection   = 2;
    other.startWeek = 9;
    other.endWeek   = 16;
    store.addScheduleEntry(other);

    QVERIFY(!store.teacherSwapClashes(QLatin1String("C101"), QLatin1String("T003")));
}

/*
TestDataStore::removeFailureForClassOnlyTarget - removeScheduleFailureForClass 只清目标班的失败/登记项
*/
void TestDataStore::removeFailureForClassOnlyTarget()
{
    DataStore store = loadedSample();

    ScheduleFailure fA;
    fA.classId   = QLatin1String("C101");
    fA.courseId  = QLatin1String("C01");
    fA.courseName = QStringLiteral("高等数学");
    fA.reason    = QStringLiteral("拟换师待处理");
    ScheduleFailure fB = fA;
    fB.classId = QLatin1String("C201");
    ScheduleFailure fC = fA;
    fC.classId = QLatin1String("C301");
    QVector<ScheduleFailure> fs{ fA, fB, fC };
    store.setScheduleFailures(fs);

    QVERIFY(store.removeScheduleFailureForClass(QLatin1String("C101")));
    const QVector<ScheduleFailure> &left = store.scheduleFailures();
    QCOMPARE(int(left.size()), 2);
    QVERIFY(left.at(0).classId != QLatin1String("C101"));
    QVERIFY(left.at(1).classId != QLatin1String("C101"));

    // 目标班已无登记项 → 再次调用返回 false
    QVERIFY(!store.removeScheduleFailureForClass(QLatin1String("C101")));
}

QTEST_GUILESS_MAIN(TestDataStore)
#include "tst_datastore.moc"
