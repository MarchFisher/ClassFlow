#ifndef CSV_H
#define CSV_H

#include <QString>
#include <QStringList>

// CSV 最小解析工具：按行拆字段、清洗、拼接。
// 不引第三方库，手写 RFC4180 的简化子集。

namespace Csv {

QStringList parseLine(const QString &line);        // 解析一行 CSV 为字段列表
QString cleanField(const QString &field);          // 去除字段首尾空白
QString joinLine(const QStringList &fields);       // 字段列表拼成一行 CSV

} // namespace Csv

#endif // CSV_H
