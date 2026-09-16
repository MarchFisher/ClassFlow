#ifndef UNDOBUFFER_H
#define UNDOBUFFER_H

#include <QVector>

#include "core/store/datastore.h"

/*
会话级撤销/重做缓冲（动作边界的整值快照环）。

纯逻辑、无 QObject。持有有界的"动作前"DataStore 历史值；依赖 Qt 值容器
（QVector/QSet/QHash）的隐式共享 COW——push 前的 `DataStore before = current;`
是 O(1) 级共享引用，之后任一处 mutation 才 detach，故存 N 份历史代价极低、
无需序列化；索引等内部自洽由整值拷贝天然保证，历史库还原无需失效处理。

"当前态"由外部持有（MainWindow::m_store 在类外），故 undo/redo 把当前态以参数
传入：undo(current) 先把 current 存入 redo 栈、再从 undo 栈顶弹回"应恢复的动作前
状态"；redo 对称。调用方负责把返回的 target 赋回权威 store 并做 UI 刷新。
*/
class UndoBuffer
{
public:
    explicit UndoBuffer(int capacity = 20);

    void clear();                 // 清空 undo/redo 两栈（新会话起点用）
    int capacity() const;         // 容量上限（现役 undo 栈最大长度）

    // 入环：记录一次"动作前状态"；自动清空 redo 栈（新分支）、容量超限挤最旧项。
    void push(const DataStore &before);

    bool canUndo() const;         // 是否有可撤销步
    bool canRedo() const;         // 是否有可重做步
    int undoDepth() const;        // 可撤销步数（<= capacity）
    int redoDepth() const;

    // 撤销一步：把 current（动作后态）存入 redo 栈；成功返回 true 且 *target =
    // 应恢复的动作前状态（调用方赋回权威 store 并刷新 UI）。无可撤销步返回 false，
    // *target 保持不变。
    bool undo(const DataStore &current, DataStore *target);

    // 重做一步：把 current（撤销后态）存入 undo 栈；成功返回 true 且 *target =
    // 应恢复的动作后状态。无可重做步返回 false，*target 保持不变。
    bool redo(const DataStore &current, DataStore *target);

private:
    int m_capacity;
    QVector<DataStore> m_undo;   // 栈底(0)=最旧可撤销动作前状态，栈顶(尾)=最近一次 push
    QVector<DataStore> m_redo;   // 栈底(0)=最旧，栈顶(尾)=最近一次 undo 让出的动作后态
};

#endif // UNDOBUFFER_H
