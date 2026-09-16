#ifndef MODELS_CLASSROOM_H
#define MODELS_CLASSROOM_H

#include <QString>

// 教室类型。
// Any 为课程「所需教室类型」的不限哨兵（教室本体不会标注 Any）。
enum class ClassroomType { Any, Norm, Lab, PlayGround };

struct Classroom {
    QString roomNumber;
    int     capacity     = 0;
    ClassroomType type   = ClassroomType::Norm;
};

#endif // MODELS_CLASSROOM_H
