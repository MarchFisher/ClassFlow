/**
 * 文件职责：DataStore 工作区快照序列化实现（snapshotText / saveSnapshot / loadSnapshot）。
 * 单文件 CSV 分节（[Term]/[Courses]/[TeachingClasses]/[Teachers]/[Classrooms]/
 * [Sections]/[ScheduleEntries]/[ScheduleErrors]/[Locks] 九段）；[Courses] 与 [Locks]
 * 允许缺失（老快照容错：缺 [Courses] 时由教学班行推导课程）。
 * 加载回填一律走 ID 查询而非行下标，故各段内的行序可以自由调整。
 */

#include "datastore.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

#include "csv.h"
#include "utility.h"

/*
DataStore::snapshotText - 把整个工作区序列化为单文件 CSV 分节快照文本

Result:
    QString: 九段快照文本（与 saveSnapshot 落盘内容逐字节一致）

Remark:
    九段依次为 [Term] / [Courses] / [TeachingClasses] / [Teachers] / [Classrooms] /
    [Sections] / [ScheduleEntries] / [ScheduleErrors] / [Locks]；[Courses] 承载独立课程
    实体（含空课程）；教学班行仍按 courseId 回填课程字段（兼容老版本由班行推导课程）。
    saveSnapshot 写盘、UI 自动保存比对 / 手动保存脏标记判定都复用它；
    [Locks] 段锁定 id 先排序再写，保证同一内容的文本逐字节确定。
*/
QString DataStore::snapshotText() const
{
    QString text;
    QTextStream out(&text);

    // [Term]：学期总周数
    out << "[Term]\n";
    out << "semesterWeeks\n";
    out << m_semesterWeeks << '\n';

    // [Courses]：独立课程实体（含空课程；课程名/学分等以本段为准）。教学班行下方
    // 仍内嵌课程字段，是为了让没有本段的老版本构建能由班行推导出课程。
    out << "[Courses]\n";
    out << "courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,startWeek,endWeek,requiredRoomType\n";
    for (const Course &c : m_courses) {
        const QStringList fields = {
            c.id,
            c.name,
            QString::number(c.credit),
            QString::number(c.sessionsPerWeek),
            QString::number(c.hoursPerSession),
            c.depart,
            QString::number(c.startWeek),
            QString::number(c.endWeek),
            store::requiredRoomTypeToString(c.requiredRoomType)
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [TeachingClasses]：课程字段按 courseId 回填（含周范围、所需教室类型）
    out << "[TeachingClasses]\n";
    out << "classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType\n";
    for (const TeachingClass &tc : m_teachingClasses) {
        const Course *c = courseById(tc.courseId);
        const QStringList fields = {
            tc.classId,
            tc.courseId,
            c ? c->name : QString(),
            c ? QString::number(c->credit) : QStringLiteral("0"),
            c ? QString::number(c->sessionsPerWeek) : QStringLiteral("0"),
            c ? QString::number(c->hoursPerSession) : QStringLiteral("0"),
            c ? c->depart : QString(),
            tc.teacherId,
            QString::number(tc.plannedSize),
            QString::number(tc.maxCapacity),
            c ? QString::number(c->startWeek) : QStringLiteral("1"),
            c ? QString::number(c->endWeek)   : QStringLiteral("16"),
            c ? store::requiredRoomTypeToString(c->requiredRoomType) : QStringLiteral("Any")
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [Teachers]：教师表（随工作区持久化，供筛选按教师搜索）
    out << "[Teachers]\n";
    out << "teacherId,name,depart\n";
    for (const TeacherInfo &t : m_teachers) {
        const QStringList fields = { t.teacherId, t.name, t.depart };
        out << Csv::joinLine(fields) << '\n';
    }

    // [Classrooms]
    out << "[Classrooms]\n";
    out << "roomNumber,capacity,type\n";
    for (const Classroom &r : m_classrooms) {
        const QStringList fields = {
            r.roomNumber,
            QString::number(r.capacity),
            store::classroomTypeToString(r.type)
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [Sections]
    out << "[Sections]\n";
    out << "index,startTime,endTime\n";
    for (const Section &s : m_sections) {
        const QStringList fields = {
            QString::number(s.index),
            s.startTime.toString("HH:mm"),
            s.endTime.toString("HH:mm")
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [ScheduleEntries]
    out << "[ScheduleEntries]\n";
    out << "entryId,teachingClassId,teacherId,dayOfWeek,startSection,endSection,classroomId,startWeek,endWeek\n";
    for (const ScheduleEntry &e : m_scheduleEntries) {
        const QStringList fields = {
            e.entryId,
            e.teachingClassId,
            e.teacherId,
            QString::number(e.timeSlot.dayOfWeek),
            QString::number(e.timeSlot.startSection),
            QString::number(e.timeSlot.endSection),
            e.classroomId,
            QString::number(e.startWeek),
            QString::number(e.endWeek)
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [ScheduleErrors]：排课失败明细（随工作区持久化，供用户对照修改课程）
    out << "[ScheduleErrors]\n";
    out << "classId,courseId,courseName,reason\n";
    for (const ScheduleFailure &fail : m_scheduleFailures) {
        const QStringList fields = {
            fail.classId,
            fail.courseId,
            fail.courseName,
            fail.reason
        };
        out << Csv::joinLine(fields) << '\n';
    }

    // [Locks]：锁定的教学班 id（每行一个；空集 = 无锁定，锁定重排/最小排据此冻结）
    out << "[Locks]\n";
    out << "classId\n";
    QStringList lockIds = m_lockedClasses.values();
    lockIds.sort();   // QSet 迭代无序 → 排序保证同内容的序列化确定（脏态比对依赖）
    for (const QString &id : lockIds)
        out << Csv::joinLine({id}) << '\n';

    return text;
}

/*
DataStore::saveSnapshot - 把 snapshotText() 序列化结果写入文件

Parameter：
    path: 输出快照文件路径

Result:
    bool: 写成功返回 true；文件打不开返回 false
*/
bool DataStore::saveSnapshot(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << snapshotText();
    return true;
}

/*
DataStore::loadSnapshot - 从单文件 CSV 分节快照恢复整个工作区

Parameter：
    path: 输入快照文件路径

Result:
    bool: 恢复成功返回 true；文件打不开 / 快照损坏返回 false

Remark:
    教学班 / 教室 / 作息三段先落临时文件再调 loadCsv 复用现有解析
    （含 Course 按 courseId 去重）；[Courses] / [Term] 段与排课段单独解析。
    [Courses] 若存在则以它重建课程表（允许空课程）；[Term] 需在 loadCsv 之后处理，
    避免被其内部 clear() 重置学期周数。
    R4：[Courses] 覆盖后对 [TeachingClasses] 内嵌课程列做一致性校验，不一致以
    [Courses] 为准并记入 loadWarnings()（不阻断加载）；老快照无 [Courses] 段则跳过。
*/
bool DataStore::loadSnapshot(const QString &path)
{
    const QStringList lines = store::readAllLines(path);
    if (lines.isEmpty())
        return false;

    // 1. 按 [段名] 分节，各行归入对应段（首行为表头）
    //    [Courses] 与 [Locks] 允许缺失（老快照无这两段 → courseLines/lockLines 空，容错：
    //    缺 [Courses] 时课程由教学班行推导，见下方 2.25）。
    QStringList termLines, courseLines, tcLines, roomLines, secLines, schedLines, errLines,
                teacherLines, lockLines;
    QStringList *cur = nullptr;
    for (const QString &raw : lines) {
        if (raw.startsWith('[')) {
            const QString name = raw.mid(1, raw.indexOf(']') - 1);
            cur = nullptr;
            if (name == "Term")                 cur = &termLines;
            else if (name == "Courses")         cur = &courseLines;
            else if (name == "TeachingClasses") cur = &tcLines;
            else if (name == "Teachers")        cur = &teacherLines;
            else if (name == "Classrooms")      cur = &roomLines;
            else if (name == "Sections")        cur = &secLines;
            else if (name == "ScheduleEntries") cur = &schedLines;
            else if (name == "ScheduleErrors")  cur = &errLines;
            else if (name == "Locks")           cur = &lockLines;
        } else if (cur && !raw.trimmed().isEmpty()) {
            cur->append(raw);
        }
    }

    // 2. 三段落临时文件，复用 loadCsv（内部 clear + 解析）
    const QString tcPath   = store::writeTempCsv(tcLines, "cf_tc.csv");
    const QString roomPath = store::writeTempCsv(roomLines, "cf_room.csv");
    const QString secPath  = store::writeTempCsv(secLines, "cf_sec.csv");
    const bool ok = loadCsv(tcPath, roomPath, secPath);
    QFile::remove(tcPath);
    QFile::remove(roomPath);
    QFile::remove(secPath);
    if (!ok)
        return false;

    // 2.25 [Courses]：如存在本段，则以它整体重建课程表（含空课程），覆盖 loadCsv 由
    //       教学班行去重推导出的课程；老快照缺此段 → 保留推导结果（回退，同旧版行为）。
    //       行格式 9 列：courseId,courseName,credit,sessionsPerWeek,hoursPerSession,
    //       depart,startWeek,endWeek,requiredRoomType。
    if (!courseLines.isEmpty()) {
        QVector<Course> parsed;
        parsed.reserve(courseLines.size());
        for (int i = 1; i < courseLines.size(); ++i) {
            const QStringList f = Csv::parseLine(courseLines.at(i));
            if (f.size() < 9)
                continue;
            Course c;
            c.id               = Csv::cleanField(f.at(0));
            if (c.id.isEmpty())
                continue;
            c.name             = Csv::cleanField(f.at(1));
            c.credit           = Csv::cleanField(f.at(2)).toDouble();
            c.sessionsPerWeek  = Csv::cleanField(f.at(3)).toInt();
            c.hoursPerSession  = Csv::cleanField(f.at(4)).toDouble();
            c.depart           = Csv::cleanField(f.at(5));
            c.startWeek        = Csv::cleanField(f.at(6)).toInt();
            c.endWeek          = Csv::cleanField(f.at(7)).toInt();
            c.requiredRoomType = store::parseRequiredRoomType(f.at(8));
            parsed.append(c);
        }
        m_courses = parsed;
        invalidateLookupIndexes();
    }

    // 2.3 R4 一致性校验：仅当存在权威 [Courses] 段时，把 [TeachingClasses] 每行内嵌
    //     课程列与 [Courses] 逐字段比对；不一致以 [Courses] 为准（上一步已覆盖）并记入
    //     m_loadWarnings + qWarning。老快照无 [Courses] 段则跳过（课程由班行推导，无权威可比）。
    if (!courseLines.isEmpty()) {
        for (int i = 1; i < tcLines.size(); ++i) {
            const QStringList f = Csv::parseLine(tcLines.at(i));
            if (f.size() < 13)
                continue;   // 坏行交由 loadCsv 的 13 列校验处理，此处不重复报
            const QString classId  = Csv::cleanField(f.at(0));
            const QString courseId = Csv::cleanField(f.at(1));
            const Course *c = courseById(courseId);   // 覆盖后 m_courses 为权威
            if (!c) {
                // 悬空班：班行引用的 courseId 在 [Courses] 段无对应课程
                const QString msg = QStringLiteral(
                    "[snapshot] 教学班 %1 引用的 courseId=%2 在 [Courses] 段无对应课程")
                    .arg(classId, courseId);
                m_loadWarnings.append(msg);
                qWarning() << msg;
                continue;
            }
            // 逐字段比对班行内嵌课程列 vs 权威 [Courses]；收集不一致字段
            QStringList bad;
            if (Csv::cleanField(f.at(2)) != c->name)
                bad << QStringLiteral("courseName=%1/%2").arg(Csv::cleanField(f.at(2)), c->name);
            if (qAbs(Csv::cleanField(f.at(3)).toDouble() - c->credit) > 1e-9)
                bad << QStringLiteral("credit=%1/%2").arg(Csv::cleanField(f.at(3)), QString::number(c->credit));
            if (Csv::cleanField(f.at(4)).toInt() != c->sessionsPerWeek)
                bad << QStringLiteral("sessionsPerWeek=%1/%2").arg(Csv::cleanField(f.at(4)), QString::number(c->sessionsPerWeek));
            if (qAbs(Csv::cleanField(f.at(5)).toDouble() - c->hoursPerSession) > 1e-9)
                bad << QStringLiteral("hoursPerSession=%1/%2").arg(Csv::cleanField(f.at(5)), QString::number(c->hoursPerSession));
            if (Csv::cleanField(f.at(6)) != c->depart)
                bad << QStringLiteral("depart=%1/%2").arg(Csv::cleanField(f.at(6)), c->depart);
            if (Csv::cleanField(f.at(10)).toInt() != c->startWeek)
                bad << QStringLiteral("startWeek=%1/%2").arg(Csv::cleanField(f.at(10)), QString::number(c->startWeek));
            if (Csv::cleanField(f.at(11)).toInt() != c->endWeek)
                bad << QStringLiteral("endWeek=%1/%2").arg(Csv::cleanField(f.at(11)), QString::number(c->endWeek));
            if (store::parseRequiredRoomType(f.at(12)) != c->requiredRoomType)
                bad << QStringLiteral("requiredRoomType=%1/%2")
                       .arg(Csv::cleanField(f.at(12)), store::requiredRoomTypeToString(c->requiredRoomType));
            if (!bad.isEmpty()) {
                const QString msg = QStringLiteral(
                    "[snapshot] 教学班 %1(courseId=%2) 内嵌课程列与 [Courses] 不一致：%3")
                    .arg(classId, courseId, bad.join(QStringLiteral("; ")));
                m_loadWarnings.append(msg);
                qWarning() << msg;
            }
        }
    }

    // 2.5 [Term]：学期总周数（loadCsv 内部 clear 已重置，此处再覆盖）
    for (const QString &raw : termLines) {
        const QString t = raw.trimmed();
        if (t.isEmpty() || t.compare(QStringLiteral("semesterWeeks"), Qt::CaseInsensitive) == 0)
            continue;
        m_semesterWeeks = t.toInt();
        break;   // 只取第一个非表头值
    }

    // 2.75 [Teachers]：教师表（loadCsv 已推导为 name=id；有真实表则覆盖）
    if (!teacherLines.isEmpty()) {
        m_teachers.clear();
        for (int i = 1; i < teacherLines.size(); ++i) {
            const QStringList f = Csv::parseLine(teacherLines.at(i));
            if (f.size() < 2)
                continue;
            TeacherInfo t;
            t.teacherId = Csv::cleanField(f.at(0));
            t.name      = Csv::cleanField(f.at(1));
            t.depart    = (f.size() >= 3) ? Csv::cleanField(f.at(2)) : QString();
            m_teachers.append(t);
        }
    }

    // 3. 排课段单独解析（新 9 列含周范围；旧 7 列回退整学期）
    for (int i = 1; i < schedLines.size(); ++i) {
        const QStringList f = Csv::parseLine(schedLines.at(i));
        if (f.size() < 7)
            continue;
        ScheduleEntry e;
        e.entryId               = Csv::cleanField(f.at(0));
        e.teachingClassId       = Csv::cleanField(f.at(1));
        e.teacherId             = Csv::cleanField(f.at(2));
        e.timeSlot.dayOfWeek    = Csv::cleanField(f.at(3)).toInt();
        e.timeSlot.startSection = Csv::cleanField(f.at(4)).toInt();
        e.timeSlot.endSection   = Csv::cleanField(f.at(5)).toInt();
        e.classroomId           = Csv::cleanField(f.at(6));
        if (f.size() >= 9) {
            e.startWeek = Csv::cleanField(f.at(7)).toInt();
            e.endWeek   = Csv::cleanField(f.at(8)).toInt();
        }
        addScheduleEntry(e);
    }

    // 4. [ScheduleErrors]：排课失败明细（教学班/课程/原因）
    m_scheduleFailures.clear();
    for (int i = 1; i < errLines.size(); ++i) {
        const QStringList f = Csv::parseLine(errLines.at(i));
        if (f.size() < 4)
            continue;
        ScheduleFailure fail;
        fail.classId    = Csv::cleanField(f.at(0));
        fail.courseId   = Csv::cleanField(f.at(1));
        fail.courseName = Csv::cleanField(f.at(2));
        fail.reason     = Csv::cleanField(f.at(3));
        m_scheduleFailures.append(fail);
    }

    // 5. [Locks]：锁定的教学班 id（每行一个；首行表头跳过）
    m_lockedClasses.clear();
    for (int i = 1; i < lockLines.size(); ++i) {
        const QStringList f = Csv::parseLine(lockLines.at(i));
        if (f.isEmpty())
            continue;
        const QString id = Csv::cleanField(f.at(0));
        if (!id.isEmpty())
            m_lockedClasses.insert(id);
    }

    return true;
}
