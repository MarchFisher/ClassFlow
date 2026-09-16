#ifndef COURSEDETAILDIALOG_H
#define COURSEDETAILDIALOG_H

#include <QDialog>
#include <QString>

#include "core/models/models.h"

class QLabel;
class QPushButton;
class DataStore;

// 课程详情弹窗：由主窗口在点击课表卡片时创建。
// 依据排课条目解析出课程 / 教学班 / 教师 / 教室等信息，分组展示。
// 标题行右端并排三枚纯图标动作按钮（带主题色 Feather 图标与悬浮提示）：
//   「锁定/解锁」— 直接切换本班锁定态（关锁/开锁图标）；
//   「编辑」     — 开 ClassEditDialog（基本信息 + 时间·教室 双页签）一次保存两类改动，
//                   成功后原地刷新标题/基础行与时间/教室两行；
//   「删除」     — 先弹小选择窗定删除范围（仅本班 / 连同整门课），再二次确认后落库。
// 是否发生变更经 locksChanged() / edited() / adjusted() / removed() 供外层刷新课表与
// 入环；编辑若确认了"扩容换大教室"，经 editRoomMoveNeeded() 告知外层做局部重排。
class CourseDetailDialog : public QDialog
{
    Q_OBJECT

public:
    // store: 数据仓库（非只读：锁定/编辑/删除会改其状态）；entry: 被点击的排课条目
    CourseDetailDialog(DataStore &store, const ScheduleEntry &entry,
                       QWidget *parent = nullptr);

    bool locksChanged() const;   // 本次打开期间是否切换过锁定状态（供外层刷新卡片锁标）
    bool edited() const;         // 本次是否改过课程/教学班基本信息（含仅登记撞车冲突）
    QString editSummary() const; // 编辑摘要，如"已更新教学班 C102：人数 …；教师 → …"
    bool editRoomMoveNeeded() const;  // 编辑是否确认了"扩容换大教室"（供外层对该班局部重排）
    bool adjusted() const;       // 本次是否做过手动调整（供外层关闭后刷新课表）
    QString adjustSummary() const;   // 调整摘要，如"已手动调整教学班 C102：1 次课"
    bool removed() const;        // 本次是否删除过课程/教学班（供外层关闭后刷新课表）
    QString removalSummary() const;  // 删除摘要，如"已删除教学班 C102（属「高等数学」）"

private:
    void updateLockButton();     // 按当前锁定态刷新锁图标与提示（"锁定"/"解锁"）
    void openClassEdit();        // 开 ClassEditDialog；成功后聚合标志、刷新基础行与时间行
    void askDeleteRange();       // 「删除」小选择窗：仅本班 / 连整门课 两选项定范围
    void confirmDelete(bool wholeCourse);  // 按范围二次确认并落库（整门课 or 单教学班）
    void applyThemeIcons();      // 依当前主题前景色重设三钮 Feather 图标（随明暗换色）
    void refreshEntryLabels();   // 按 entryId 现读刷新 m_timeLabel / m_roomLabel
    void refreshBasicLabels();   // 现读刷新标题/开课学院/教师/人数 四行（编辑后）

    static QString weekdayName(int day);                 // 星期号 1..7 → "周一".."周日"
    static QString roomTypeName(ClassroomType type);     // 教室类型 → 中文名（Any=不限）

    DataStore &m_store;
    QString m_classId;               // 本条目所属教学班 id（锁定/调整/编辑/删除对象）
    ScheduleEntry m_entry;           // 被点击条目的拷贝（锚：entryId 恒定，余字段可刷新）
    QLabel *m_titleLabel = nullptr;  // 标题（课程名；编辑后原地刷新）
    QLabel *m_departLabel = nullptr; // 详情「开课学院」值标签（编辑后原地刷新）
    QLabel *m_teacherLabel = nullptr;// 详情「教师」值标签（换师后原地刷新）
    QLabel *m_sizeLabel = nullptr;   // 详情「人数」值标签（编辑后原地刷新）
    QLabel *m_timeLabel = nullptr;   // 详情「上课时间」值标签（调整后原地刷新）
    QLabel *m_roomLabel = nullptr;   // 详情「教室」值标签（调整后原地刷新）
    QPushButton *m_lockBtn = nullptr;   // 「锁定/解锁」（动作式，随态换文案与锁图标）
    QPushButton *m_editBtn = nullptr;   // 「编辑」（基本信息 + 时间·教室 双页签）
    QPushButton *m_deleteBtn = nullptr; // 「删除」（小选择窗定范围）
    bool m_locksChanged = false;
    bool m_edited = false;           // 本次是否改过课程/教学班基本信息（含仅登记撞车冲突）
    QString m_editSummary;           // 编辑摘要（关闭后供外层状态栏）
    bool m_editRoomMove = false;     // 本次编辑是否确认"扩容换大教室"（待外层对该班局部重排）
    bool m_adjusted = false;         // 本次是否做过成功的手动调整
    QString m_adjustSummary;         // 调整摘要（关闭后供外层状态栏）
    bool m_removed = false;          // 本次是否删除过课程/教学班
    QString m_removalSummary;        // 删除摘要（关闭后供外层状态栏）
};

#endif // COURSEDETAILDIALOG_H
