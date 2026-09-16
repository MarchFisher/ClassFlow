#ifndef TIMEROOMEDITOR_H
#define TIMEROOMEDITOR_H

#include <QString>
#include <QVector>
#include <QWidget>

#include "core/models/models.h"
#include "core/schedule/manualadd.h"

class QComboBox;
class QRadioButton;
class QShowEvent;
class DataStore;
class FromScratchEditor;

// 「时间·教室」调整编辑区（可嵌入任意 QWidget）。
// 教务手动调整的编辑区：本节 / 整班两种互斥模式，每行 星期/起始节/教室 三下拉，
// 教室按「容量 ≥ 班人数 + 类型匹配」过滤。
// 本组件只负责「要改成什么」——collectChanges() 组整批 manual::Change 供宿主经
// manual::validate / manual::apply 落库，自身不落库、不触发排课。据此合并编辑弹窗
// 才能编排成「先落排课、后落基本信息」的统一顺序。
// 数据以构造传入的锚（classId + 被点击 entryId）现读 store，绝不信点击瞬间旧拷贝。
class TimeRoomEditor : public QWidget
{
    Q_OBJECT

public:
    // store: 数据仓库（读现状；调整的实际落库由宿主负责）
    // classId: 本班（整班模式对象）
    // anchorEntryId: 被点击条目的 id（本节模式对象）
    TimeRoomEditor(DataStore &store, const QString &classId,
                   const QString &anchorEntryId, QWidget *parent = nullptr);

    bool hasSessions() const;             // 该班现存课次是否非空（供宿主区分两形态）
    int sessionCount() const;
    // 读各行当前选择组整批变更；changedCount = 与现状确实不同的行数（0 = 无实际改动）
    bool collectChanges(QVector<manual::Change> *out, int *changedCount) const;
    // 校验拒绝报告 → 可读文案（宿主 manual::validate / manual::validateAdd 失败时展示）
    QString describeReject(const manual::Report &rep) const;

    // ——「从零排入」形态（该班 0 课次）：内部为 FromScratchEditor，见其类注释 ——
    bool addMode() const;                  // 是否处于"从零排入"形态（无现存课次的失败班）
    int addNeedCount() const;              // 应排课次数 N（addMode 时）；0 = 该班无法从零排入
    // 读从零排入各行当前选择组整批槽位；specified = 真实指定的行数（0 = 没填任何课）
    void collectAdd(QVector<manual::AddSlot> *out, int *specified) const;

protected:
    void showEvent(QShowEvent *event) override;  // 首次可见（切到时间页）→ 自动建议空位

private slots:
    void onModeSwitched();                // 本节 / 整班 切换 → 重建编辑行

private:
    // 一行待编辑的课次
    struct EditRow {
        QString entryId;          // 该行对应旧条目 id（锚）
        int baseSpan = 1;         // 该行单次课跨度（结束节 = 起始节 + 跨度 − 1）
        QComboBox *day = nullptr;
        QComboBox *start = nullptr;
        QComboBox *room = nullptr;
    };

    void rebuildRows();                   // 按当前模式重建编辑区
    QWidget *buildRowWidget(const EditRow &row, const QString &caption);
    void fillCombos(const EditRow &row, const ScheduleEntry &cur);
    int maxSection() const;               // 作息表最大节次号

    DataStore &m_store;
    QString m_classId;              // 操作的教学班（整班模式对象）
    QString m_anchorEntryId;        // 被点击条目的 id（本节模式对象）
    QVector<ScheduleEntry> m_sessions;   // 该班现存全部课次（按 星期→起始节 排序）

    QRadioButton *m_singleRadio = nullptr;
    QRadioButton *m_batchRadio = nullptr;
    QWidget *m_rowsHost = nullptr;  // 编辑行容器（内容随模式重建）

    FromScratchEditor *m_addEditor = nullptr;  // 从零排入形态（0 课次时非空）
    bool m_addSuggested = false;    // 自动建议空位已跑过一次（见 showEvent）

    QVector<EditRow> m_rows;        // 当前模式的各编辑行
};

#endif // TIMEROOMEDITOR_H
