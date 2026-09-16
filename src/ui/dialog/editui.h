#ifndef EDITHUI_H
#define EDITHUI_H

#include <QString>

#include "core/models/models.h"

class QWidget;
class DataStore;

// 基本信息编辑的决策 / 落库助手（editui，供课程详情弹窗与主窗口共用；不触发排课线程）。
// 弹窗（EditInfoDialog / CourseDetailDialog）只产出"想改成什么"，本助手负责预检 + 落库，
// 并把"换师撞车只登记""扩容换大教室要不要对该班 runMovable"这类决策收口在一处，
// 避免两处调用逻辑分叉。设计背景见 docs/architecture.md 第 10 节。
namespace editui {

// 一次班级编辑（applyClassEdit）的结果
struct ApplyOutcome {
    bool changed = false;         // 确有落库改动（含仅登记"拟换师"冲突）
    bool teacherBlocked = false;  // 换师撞车：已登记冲突、教师未应用
    bool roomMoveNeeded = false;  // 扩容超当前教室容量：已按确认落库，需对该班 runMovable
    QString message;              // 摘要 / 提示文案（状态栏、详情内提示用）
};

// 课程模板级改写（课程名/开课学院）；按 id 落库，无实际变化返回 false。
bool applyCourseEdit(DataStore &store, const Course &course, QString *message = nullptr);

// 教学班字段改写（教师/计划人数/最大容量）的预检 + 落库：
//  · 换师撞车 → 只登记进排课冲突列表、不应用换师（teacherBlocked）；
//  · 扩容超出该班当前所在教室容量 → 弹确认框，拒绝则不落库；
//  · 其余情况直接落库（换师成功会顺带清掉该班旧"拟换师"登记）。
ApplyOutcome applyClassEdit(DataStore &store, const TeachingClass &tc,
                            QWidget *promptParent);

} // namespace editui

#endif // EDITHUI_H
