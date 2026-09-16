#ifndef MODELS_TERM_H
#define MODELS_TERM_H

#include <QDate>
#include <QString>

// 学期（预留结构：当前排课只用 DataStore 的「学期总周数」一个整数，
// 本结构尚未被任何代码引用）

struct Term {
    QString id;
    QString name;
    QDate   startDate;
    QDate   endDate;
};

#endif // MODELS_TERM_H
