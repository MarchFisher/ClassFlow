/**
 * 文件职责：CSV 最小解析工具实现（namespace Csv）——按行拆字段、清洗、拼接；
 * 以及 DataStore 的 CSV 导入 / 导出（loadCsv / exportCsv）。
 * 解析工具支持字段首尾空白、双引号包裹、引号内逗号不拆分、"" 转义引号。
 */

#include "datastore.h"

#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QTime>

#include "csv.h"
#include "utility.h"

namespace Csv {

/*
parseLine - 解析一行 CSV 文本为字段列表

Parameter：
    line: 一行 CSV 文本（不含行尾换行）

Result:
    QStringList: 字段列表

Remark:
    RFC4180 简化子集：双引号包裹的字段内逗号不拆分，
    引号内 "" 表示一个字面引号。
*/
QStringList parseLine(const QString &line)
{
    QStringList fields;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line.at(i + 1) == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.append(field);
                field.clear();
            } else {
                field += c;
            }
        }
    }
    fields.append(field);
    return fields;
}

/*
cleanField - 去除字段首尾空白

Parameter：
    field: 原始字段文本

Result:
    QString: 去除首尾空白后的字段

*/
QString cleanField(const QString &field)
{
    return field.trimmed();
}

/*
joinLine - 将字段列表拼接为一行 CSV

Parameter：
    fields: 字段列表

Result:
    QString: 拼接后的一行 CSV 文本（无行尾换行）

Remark:
    含逗号、引号、换行的字段自动加双引号包裹。
*/
QString joinLine(const QStringList &fields)
{
    QStringList out;
    for (const QString &f : fields) {
        if (f.contains(',') || f.contains('"') || f.contains('\n')) {
            QString q = f;
            q.replace('"', "\"\"");
            out.append('"' + q + '"');
        } else {
            out.append(f);
        }
    }
    return out.join(',');
}

} // namespace Csv

/*
DataStore::loadCsv - 从 CSV 文件载入教学班、教室、作息表（可选教师表）

Parameter：
    teachingClassPath: 教学班 CSV 文件路径（含内嵌课程字段）
    classroomPath: 教室 CSV 文件路径
    sectionPath: 作息表 CSV 文件路径
    teacherPath: 教师表 CSV 文件路径（可选；空或缺文件时从教学班 teacherId 去重推导）

Result:
    bool: 教学班/教室/作息三文件均成功解析返回 true；任一打不开返回 false

Remark:
    教学班行内嵌课程字段，按 courseId 去重生成 Course 列表。
    调用前会 clear() 清空旧数据。
*/
bool DataStore::loadCsv(const QString &teachingClassPath,
                        const QString &classroomPath,
                        const QString &sectionPath,
                        const QString &teacherPath)
{
    clear();

    const QStringList sectionLines = store::readAllLines(sectionPath);
    const QStringList roomLines    = store::readAllLines(classroomPath);
    const QStringList classLines   = store::readAllLines(teachingClassPath);

    // 任一文件打不开或为空 → 导入失败
    if (sectionLines.isEmpty() || roomLines.isEmpty() || classLines.isEmpty())
        return false;

    // 1. 作息表：index,startTime,endTime
    for (int i = 1; i < sectionLines.size(); ++i) {   // 跳过表头
        const QStringList f = Csv::parseLine(sectionLines.at(i));
        if (f.size() < 3)
            continue;
        Section s;
        s.index     = Csv::cleanField(f.at(0)).toInt();
        s.startTime = QTime::fromString(Csv::cleanField(f.at(1)), "HH:mm");
        s.endTime   = QTime::fromString(Csv::cleanField(f.at(2)), "HH:mm");
        m_sections.append(s);
    }

    // 2. 教室：roomNumber,capacity,type
    for (int i = 1; i < roomLines.size(); ++i) {
        const QStringList f = Csv::parseLine(roomLines.at(i));
        if (f.size() < 3)
            continue;
        Classroom c;
        c.roomNumber = Csv::cleanField(f.at(0));
        c.capacity   = Csv::cleanField(f.at(1)).toInt();
        c.type       = store::parseClassroomType(f.at(2));
        m_classrooms.append(c);
    }

    // 3. 教师表（可选）：teacherId,name,depart；文件为空或缺省则后续推导
    if (!teacherPath.isEmpty()) {
        const QStringList teacherLines = store::readAllLines(teacherPath);
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

    // 4. 教学班：classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,
    //    depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType（固定 13 列）
    for (int i = 1; i < classLines.size(); ++i) {
        const QStringList f = Csv::parseLine(classLines.at(i));
        if (f.size() != 13)
            continue;

        const QString classId  = Csv::cleanField(f.at(0));
        const QString courseId = Csv::cleanField(f.at(1));

        // Course 去重：同一 courseId 只生成一条课程模板
        bool hasCourse = false;
        for (const Course &c : m_courses) {
            if (c.id == courseId) {
                hasCourse = true;
                break;
            }
        }
        if (!hasCourse) {
            Course course;
            course.id              = courseId;
            course.name            = Csv::cleanField(f.at(2));
            course.credit          = Csv::cleanField(f.at(3)).toDouble();
            course.sessionsPerWeek = Csv::cleanField(f.at(4)).toInt();
            course.hoursPerSession = Csv::cleanField(f.at(5)).toDouble();
            course.depart          = Csv::cleanField(f.at(6));
            course.startWeek       = Csv::cleanField(f.at(10)).toInt();
            course.endWeek         = Csv::cleanField(f.at(11)).toInt();
            course.requiredRoomType = store::parseRequiredRoomType(f.at(12));
            m_courses.append(course);
        }

        TeachingClass tc;
        tc.classId     = classId;
        tc.courseId    = courseId;
        tc.teacherId   = Csv::cleanField(f.at(7));
        tc.plannedSize = Csv::cleanField(f.at(8)).toInt();
        tc.maxCapacity = Csv::cleanField(f.at(9)).toInt();
        m_teachingClasses.append(tc);
    }

    // 5. 未提供教师表时，从教学班 teacherId 去重推导（name = id，depart 留空）
    if (m_teachers.isEmpty()) {
        QSet<QString> seen;
        for (const TeachingClass &tc : m_teachingClasses) {
            if (tc.teacherId.isEmpty() || seen.contains(tc.teacherId))
                continue;
            TeacherInfo t;
            t.teacherId = tc.teacherId;
            t.name      = tc.teacherId;
            m_teachers.append(t);
            seen.insert(tc.teacherId);
        }
    }

    return true;
}

/*
DataStore::exportCsv - 将排课结果导出为 CSV 文件

Parameter：
    path: 输出文件路径

Result:
    bool: 导出成功返回 true；文件打不开返回 false

Remark:
    每行一条 ScheduleEntry：entryId,teachingClassId,teacherId,
    dayOfWeek,startSection,endSection,classroomId,startWeek,endWeek
*/
bool DataStore::exportCsv(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
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
    return true;
}
