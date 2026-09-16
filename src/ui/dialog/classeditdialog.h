#ifndef CLASSEDITDIALOG_H
#define CLASSEDITDIALOG_H

#include <QDialog>
#include <QString>

class ClassInfoForm;
class TimeRoomEditor;
class QTabWidget;
class DataStore;

// 教学班合并编辑弹窗：一个双页签「编辑」窗同时承担基本信息与排课调整——
// 页签1「基本信息」嵌 ClassInfoForm（课程名/学院/教师/人数），
// 页签2「时间·教室」嵌 TimeRoomEditor（本节/整班改时间+教室）。一次「保存」
// 即把两类改动一起落库（先排课后基本信息，见 onOkClicked 顺序）。
//
// 本弹窗持**非 const DataStore&**，在自身 onOkClicked() 完成 校验 + 落库 + 标志聚合：
//  · 排课改动（有课次班）：manual::validate（硬约束）→ manual::apply，失败留窗零落库；
//  · 排课改动（0 课次失败班，页签2 变「从零排入」）：manual::validateAdd → manual::applyAdd，
//    全部 N 次课都有空位才落库并从失败列表移除该班，否则留窗精确提示卡点；
//  · 课程/班级改动：editui::applyCourseEdit / applyClassEdit（换师撞车只登记、
//    扩容换大教室确认），本窗沿用其弹框决策。
// 落库后 accept()，结果只经只读 getter 交回外层（详情弹窗 / MainWindow 错误列表编辑），
// 外层只做标志搬运与摘要拼接。本窗内层绝不 push undo（undo 由外层整段一步入环）。
class ClassEditDialog : public QDialog
{
    Q_OBJECT

public:
    // store: 数据仓库（非只读：保存会改排课条目/课程/教学班）
    // classId: 要编辑的教学班（基本信息页对象，兼整班调整对象）
    // anchorEntryId: 打开详情时被点击条目的 id（时间·教室页「本节」模式对象；
    //                0 课次失败班为空白串，页签2 进入「从零排入」）
    ClassEditDialog(DataStore &store, const QString &classId,
                    const QString &anchorEntryId, QWidget *parent = nullptr);

    bool adjusted() const;               // 本次是否落过排课时间/教室改动
    QString adjustSummary() const;       // 如"已手动调整教学班 C102：2 次课"
    bool edited() const;                 // 本次是否落过基本信息改动（含仅登记"拟换师"冲突）
    QString editSummary() const;         // 如"已更新教学班 C102：人数 45/50 → 50/60；…"
    bool classChanged() const;           // 教学班字段确有实际改动（教师/人数/容量应用了；撞车只登记不算）
    bool editRoomMoveNeeded() const;     // 编辑是否确认"扩容换大教室"（待外层对该班局部重排）
    bool teacherBlocked() const;         // 换师撞车：已登记冲突、教师未应用
    QString blockedMessage() const;      // 换师撞车的提示文案（外层弹给用户看）
    bool addedFromScratch() const;       // 本窗对 0 课次失败班"从零排入"成功（外层据此不再 runMovable）

private slots:
    void onOkClicked();                  // 统一顺序校验 + 落库 + accept

private:
    void warn(const QString &text) const;   // 校验失败提示（标题固定）

    DataStore &m_store;
    QString m_classId;
    QString m_anchorEntryId;

    QTabWidget *m_tabs = nullptr;
    ClassInfoForm *m_infoForm = nullptr;    // 页签1 基本信息
    TimeRoomEditor *m_timeRoom = nullptr;   // 页签2 时间·教室

    bool m_adjusted = false;            // 排课改动已落库
    QString m_adjustSummary;
    bool m_edited = false;              // 基本信息改动已落库（含仅登记冲突）
    QString m_editSummary;
    bool m_classChanged = false;        // 教学班字段确有实际改动（撞车只登记不算）
    bool m_editRoomMove = false;        // 确认"扩容换大教室"
    bool m_teacherBlocked = false;      // 换师撞车只登记
    QString m_blockedMessage;
    bool m_added = false;               // 0 课次失败班"从零排入"成功落库
};

#endif // CLASSEDITDIALOG_H
