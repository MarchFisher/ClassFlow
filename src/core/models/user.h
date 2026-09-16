#ifndef MODELS_USER_H
#define MODELS_USER_H

#include <QString>
#include <QVector>

// 用户相关：权限枚举、基础用户、教师。
// 教师与 User 采用组合而非继承。

enum class Permission { Admin, Teacher };

struct User {                        // 基础用户信息
    QString    uid;
    QString    name;
    QString    password;
    Permission permission = Permission::Teacher;
};

struct Teacher {                     // 教师
    QString uid;
    QString name;
    QString depart;                  // 所属学院
    QVector<QString> courseList;     // 课程 id 列表
};

#endif // MODELS_USER_H
