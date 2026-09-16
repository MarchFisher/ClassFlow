/**
 * 文件职责：筛选核心（core/filter/schedulefilter）单元测试。
 * 覆盖：空条件放行、维度间 AND、维度内 OR、课程经教学班换算、教师未指定时不命中。
 */

#include <QSet>
#include <QtTest>

#include "core/filter/schedulefilter.h"
#include "core/models/models.h"

class TstFilter : public QObject
{
    Q_OBJECT

private slots:
    // 空条件 = 不过滤，任意条目都放行
    void emptyFilterPassesAll();

    // 维度间 AND：教师与教室必须同时命中
    void andAcrossGroups();

    // 维度内 OR：教师选中两人，命中任一人即通过
    void orWithinGroup();

    // 课程维度经教学班 → 课程号换算
    void courseMatchViaTeachingClass();

    // 条目教师未指定且教师组非空 → 不命中
    void unknownTeacherExcluded();
};

void TstFilter::emptyFilterPassesAll()
{
    const ScheduleFilter f;
    QVERIFY(f.isEmpty());

    ScheduleEntry e;
    e.teacherId = QStringLiteral("T001");
    e.classroomId = QStringLiteral("J1-101");
    e.teachingClassId = QStringLiteral("C101-01");

    QHash<QString, QString> courseOfClass;
    courseOfClass.insert(QStringLiteral("C101-01"), QStringLiteral("C01"));

    QVERIFY(matchesFilter(f, e, courseOfClass));
}

void TstFilter::andAcrossGroups()
{
    ScheduleEntry e;
    e.teacherId = QStringLiteral("T001");
    e.classroomId = QStringLiteral("J1-101");
    e.teachingClassId = QStringLiteral("C101-01");
    QHash<QString, QString> courseOfClass;
    courseOfClass.insert(QStringLiteral("C101-01"), QStringLiteral("C01"));

    // 教师对、教室错 → 不通过
    ScheduleFilter f;
    f.teacherIds = { QStringLiteral("T001") };
    f.classroomIds = { QStringLiteral("A1-999") };
    QVERIFY(!matchesFilter(f, e, courseOfClass));

    // 教师错、教室对 → 不通过
    f.teacherIds = { QStringLiteral("T999") };
    f.classroomIds = { QStringLiteral("J1-101") };
    QVERIFY(!matchesFilter(f, e, courseOfClass));

    // 两维都对 → 通过
    f.teacherIds = { QStringLiteral("T001") };
    f.classroomIds = { QStringLiteral("J1-101") };
    QVERIFY(matchesFilter(f, e, courseOfClass));
}

void TstFilter::orWithinGroup()
{
    ScheduleEntry e;
    e.teacherId = QStringLiteral("T002");
    e.classroomId = QStringLiteral("J1-101");
    e.teachingClassId = QStringLiteral("C101-01");
    QHash<QString, QString> courseOfClass;
    courseOfClass.insert(QStringLiteral("C101-01"), QStringLiteral("C01"));

    // 教师维度内选中 T001 / T002，命中其一即可
    ScheduleFilter f;
    f.teacherIds = { QStringLiteral("T001"), QStringLiteral("T002") };
    QVERIFY(matchesFilter(f, e, courseOfClass));

    // 教室维度内选中 J1-100 / J1-102，都未命中 → 不通过
    f.classroomIds = { QStringLiteral("J1-100"), QStringLiteral("J1-102") };
    QVERIFY(!matchesFilter(f, e, courseOfClass));
}

void TstFilter::courseMatchViaTeachingClass()
{
    ScheduleEntry e;
    e.teacherId = QStringLiteral("T001");
    e.classroomId = QStringLiteral("J1-101");
    e.teachingClassId = QStringLiteral("C201-02");   // 属于课程 C02
    QHash<QString, QString> courseOfClass;
    courseOfClass.insert(QStringLiteral("C101-01"), QStringLiteral("C01"));
    courseOfClass.insert(QStringLiteral("C201-02"), QStringLiteral("C02"));

    // 选 C02 → 命中
    ScheduleFilter f;
    f.courseIds = { QStringLiteral("C02") };
    QVERIFY(matchesFilter(f, e, courseOfClass));

    // 选 C01 → 不命中
    f.courseIds = { QStringLiteral("C01") };
    QVERIFY(!matchesFilter(f, e, courseOfClass));
}

void TstFilter::unknownTeacherExcluded()
{
    ScheduleEntry e;
    e.teacherId = QString();             // 条目未指定教师
    e.classroomId = QStringLiteral("J1-101");
    e.teachingClassId = QStringLiteral("C101-01");
    QHash<QString, QString> courseOfClass;
    courseOfClass.insert(QStringLiteral("C101-01"), QStringLiteral("C01"));

    // 教师组非空而条目教师为空 → 不属于任何选中教师，不命中
    ScheduleFilter f;
    f.teacherIds = { QStringLiteral("T001") };
    QVERIFY(!matchesFilter(f, e, courseOfClass));

    // 教师组空 → 不受该维度限制，命中
    f.teacherIds.clear();
    QVERIFY(matchesFilter(f, e, courseOfClass));
}

QTEST_GUILESS_MAIN(TstFilter)
#include "tst_filter.moc"
