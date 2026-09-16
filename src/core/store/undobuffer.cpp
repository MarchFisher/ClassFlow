/**
 * 文件职责：会话级撤销/重做缓冲（UndoBuffer）实现。
 * 持有有界"动作前"DataStore 历史值（COW 隐式共享，push 前拷贝 O(1)）；
 * 纯逻辑、无 QObject，供 MainWindow 在顶层动作边界入环、undo/redo 时整体换源。
 */

#include <QtGlobal>

#include "core/store/undobuffer.h"

/*
UndoBuffer::UndoBuffer - 构造撤销缓冲

Parameter：
    capacity: 可撤销步数上限（默认 20；超出挤最旧）

Remark:
    空实现成员的默认构造由 DataStore 提供；容量钳制到至少 1。
*/
UndoBuffer::UndoBuffer(int capacity)
    : m_capacity(qMax(capacity, 1))
{
}

/*
UndoBuffer::clear - 清空 undo / redo 两栈

Remark:
    用于新会话起点（如启动自动恢复后），历史不跨工作区边界。
*/
void UndoBuffer::clear()
{
    m_undo.clear();
    m_redo.clear();
}

int UndoBuffer::capacity() const { return m_capacity; }

/*
UndoBuffer::push - 压入一次"动作前状态"

Parameter：
    before: 动作即将发生前的 DataStore（一般 = 当前权威 store 的 O(1) 拷贝）

Remark:
    push 即开新分支：清空 redo 栈（已撤销的步骤作废）；超出容量把最旧项挤出。
*/
void UndoBuffer::push(const DataStore &before)
{
    m_undo.append(before);
    m_redo.clear();
    while (m_undo.size() > m_capacity)
        m_undo.removeAt(0);       // 挤最旧（cap 小、removeAt 成本可忽略）
}

bool UndoBuffer::canUndo() const { return !m_undo.isEmpty(); }
bool UndoBuffer::canRedo() const { return !m_redo.isEmpty(); }
int  UndoBuffer::undoDepth() const { return int(m_undo.size()); }
int  UndoBuffer::redoDepth() const { return int(m_redo.size()); }

/*
UndoBuffer::undo - 撤销一步

Parameter：
    current: 当前权威 store 状态（将被撤销的"动作后态"）
    target: 出参，成功时 = 应恢复的动作前状态

Result:
    bool: true = 已产生一步撤销，*target 待赋回；false = 无历史可撤销

Remark:
    undo 把 current 存入 redo 栈（供随后 redo 前进），再从 undo 栈顶弹出恢复目标。
*/
bool UndoBuffer::undo(const DataStore &current, DataStore *target)
{
    if (m_undo.isEmpty() || !target)
        return false;
    m_redo.append(current);       // 记住将撤销的"动作后态"
    *target = m_undo.takeLast();  // 待恢复目标（动作前状态）
    return true;
}

/*
UndoBuffer::redo - 重做一步

Parameter：
    current: 当前权威 store 状态（撤销后的状态，将被重做丢弃）
    target: 出参，成功时 = 应恢复的动作后状态

Result:
    bool: true = 已产生一步重做，*target 待赋回；false = 无重做分支
*/
bool UndoBuffer::redo(const DataStore &current, DataStore *target)
{
    if (m_redo.isEmpty() || !target)
        return false;
    m_undo.append(current);       // 当前态重新可撤销（此分支仍可回退）
    *target = m_redo.takeLast();
    return true;
}
