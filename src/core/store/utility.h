#ifndef STORE_UTILITY_H
#define STORE_UTILITY_H

#include <QString>
#include <QStringList>

#include "core/models/classroom.h"

// store 层内部共享辅助（namespace store）：
// 教室/课程类型字符串互转、文本行读取、临时 CSV 落盘。
// 供 csv.cpp（loadCsv / exportCsv）与 snapshot.cpp（saveSnapshot / loadSnapshot）共用。

namespace store {

ClassroomType parseClassroomType(const QString &text);          // "Norm"/"Lab"/"PlayGround" → 枚举
QString classroomTypeToString(ClassroomType type);              // 枚举 → CSV 字符串
ClassroomType parseRequiredRoomType(const QString &text);       // 课程所需教室类型列 → 枚举
QString requiredRoomTypeToString(ClassroomType type);           // 枚举 → CSV 字符串
QStringList readAllLines(const QString &path);                  // 读文本文件全部行（剔除 BOM）
QString writeTempCsv(const QStringList &lines, const QString &name); // 写临时 CSV 供复用 loadCsv

} // namespace store

#endif // STORE_UTILITY_H
