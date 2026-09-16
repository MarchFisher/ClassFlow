/**
 * 文件职责：store 层内部共享辅助实现（namespace store）。
 * 类型字符串互转、文本行读取（剔 BOM）、临时 CSV 落盘。
 */

#include "utility.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace store {

/*
parseClassroomType - 将类型字符串映射为 ClassroomType 枚举

Parameter：
    text: "Norm" / "Lab" / "PlayGround"（大小写不敏感）

Result:
    ClassroomType: 对应枚举；未知字符串返回 Norm

*/
ClassroomType parseClassroomType(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == "lab")
        return ClassroomType::Lab;
    if (t == "playground")
        return ClassroomType::PlayGround;
    return ClassroomType::Norm;
}

/*
classroomTypeToString - 将教室类型枚举映射为 CSV 字符串

Parameter：
    type: ClassroomType 枚举

Result:
    QString: "Any" / "Norm" / "Lab" / "PlayGround"

Remark:
    parseClassroomType 的逆操作，用于快照序列化（教室本体不会出现 Any）。
*/
QString classroomTypeToString(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Any:
        return QStringLiteral("Any");
    case ClassroomType::Lab:
        return QStringLiteral("Lab");
    case ClassroomType::PlayGround:
        return QStringLiteral("PlayGround");
    default:
        return QStringLiteral("Norm");
    }
}

/*
parseRequiredRoomType - 解析课程所需教室类型列

Parameter：
    text: "Any" / "Norm" / "Lab" / "PlayGround"（大小写不敏感；空/未知按不限）

Result:
    ClassroomType: 对应枚举；空或未知返回 Any（不限）
*/
ClassroomType parseRequiredRoomType(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == "norm")
        return ClassroomType::Norm;
    if (t == "lab")
        return ClassroomType::Lab;
    if (t == "playground")
        return ClassroomType::PlayGround;
    return ClassroomType::Any;   // "Any" / 空 / 未知 → 不限
}

/*
requiredRoomTypeToString - 将课程所需教室类型枚举映射为 CSV 字符串

Parameter：
    type: ClassroomType 枚举

Result:
    QString: "Any" / "Norm" / "Lab" / "PlayGround"

Remark:
    parseRequiredRoomType 的逆操作，用于快照序列化。
*/
QString requiredRoomTypeToString(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Norm:
        return QStringLiteral("Norm");
    case ClassroomType::Lab:
        return QStringLiteral("Lab");
    case ClassroomType::PlayGround:
        return QStringLiteral("PlayGround");
    default:
        return QStringLiteral("Any");
    }
}

/*
readAllLines - 读取一个文本文件的所有行

Parameter：
    path: 文件路径

Result:
    QStringList: 行列表；文件打不开时返回空列表

Remark:
    若文件带 UTF-8 BOM，第一行行首 BOM 会被剔除。
*/
QStringList readAllLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd())
        lines.append(in.readLine());

    if (!lines.isEmpty() && lines.first().startsWith(QChar(0xFEFF)))
        lines[0] = lines.first().mid(1);
    return lines;
}

/*
writeTempCsv - 把若干行写到系统临时目录的一个 CSV 文件

Parameter：
    lines: 要写入的行（首行为表头）
    name:  临时文件名

Result:
    QString: 临时文件完整路径；写失败返回空串

Remark:
    供 loadSnapshot 落盘三段、复用 loadCsv 现有解析。
*/
QString writeTempCsv(const QStringList &lines, const QString &name)
{
    const QString path = QDir::tempPath() + '/' + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    QTextStream out(&f);
    for (const QString &l : lines)
        out << l << '\n';
    out.flush();
    f.close();
    return path;
}

} // namespace store
