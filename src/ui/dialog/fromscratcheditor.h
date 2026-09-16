#ifndef UI_DIALOG_FROMSCRATCHEDITOR_H
#define UI_DIALOG_FROMSCRATCHEDITOR_H

#include <QString>
#include <QVector>
#include <QWidget>

#include "core/schedule/manualadd.h"

class QComboBox;
class QLabel;
class QVBoxLayout;
class DataStore;

// 「从零排入」编辑区（可嵌入 widget，供 0 课次的失败教学班使用）。
// 该班按课程模板应排 N 次课（N = Course.sessionsPerWeek、每次跨单次学时整节、周范围
// 沿用课程模板），本组件把它展开成 N 行 星期/起始节/教室 三下拉供教务从零指定。
// 本组件只负责"要排入什么"——collectSlots() 组整批 manual::AddSlot，落库由宿主经
// manual::validateAdd / manual::applyAdd 完成；自身绝不改 store、绝不触发排课。
// 进入页面时（TimeRoomEditor::showEvent 触发）autoSuggest() 会按"首个不冲突空位"
// 顺序给每行填默认值：找得到的填上、找不到的行保持「（未指定）」并注明卡点。
class FromScratchEditor : public QWidget
{
    Q_OBJECT

public:
    // store: 数据仓库（只读本组件；落库由宿主负责）
    // classId: 目标教学班（须 0 现存课次）
    FromScratchEditor(const DataStore &store, const QString &classId,
                      QWidget *parent = nullptr);

    // 应排课次数 N；0 = 该班无法从零排入（学时非法 / 课程缺失，此时仅显示说明）
    int needCount() const;
    // 读各行当前选择组整批槽位；specified = 三下拉均为真实值的行数（0 = 没填任何课）
    void collectSlots(QVector<manual::AddSlot> *out, int *specified) const;
    // 按"首个不冲突空位"给每行填默认（幂等：仅当尚无任何真实选择时执行一次）
    void autoSuggest();

private:
    struct Row {
        int ordinal = 0;          // 第几次课（1..N）
        QComboBox *day = nullptr;
        QComboBox *start = nullptr;
        QComboBox *room = nullptr;
        QLabel *reason = nullptr; // 自动填入失败时行尾的卡点说明（可为空）
        QString hint;             // 最近一次卡点文案（空 = 可排/未尝试）
    };

    void buildHeader();                    // 顶部说明（班/次数/周范围/跨度）
    QWidget *buildRowWidget(Row &row);     // 单行控件：说明 + 星期/起始节/教室 + 卡点
    void fillCombos(Row &row);             // 三下拉候选 + 每下拉首项「（未指定）」
    bool rowFilled(const Row &row) const;  // 三下拉是否均为真实值
    int filledCount() const;               // 已真实指定的行数
    void setRowValue(Row &row, int day, int start, const QString &room);  // 写入一次课
    void refreshSummary();                 // 底部小结：已自动填入 M/N、未填行号

    const DataStore &m_store;
    QString m_classId;
    int m_need = 0;                 // 应排次数（needCount 结果）
    int m_span = 1;                 // 单次课连续节数
    int m_startWeek = 1, m_endWeek = 16;
    QString m_blockReason;          // 无法从零排入的整体原因（空 = 可排）
    bool m_suggested = false;       // autoSuggest 已执行过一次（幂等）
    QVBoxLayout *m_rowsLay = nullptr;   // 行容器布局
    QLabel *m_summary = nullptr;    // 底部小结标签
    QVector<Row> m_rows;
};

#endif // UI_DIALOG_FROMSCRATCHEDITOR_H
