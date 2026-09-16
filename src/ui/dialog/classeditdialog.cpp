/**
 * 文件职责：教学班合并编辑弹窗实现。
 * 单弹窗双页签：基本信息（ClassInfoForm）与
 * 时间·教室（TimeRoomEditor）。时间·教室页按该班课次多寡分两种形态：
 *   · 有课次：本节/整班改 时间+教室，先经 manual::validate/apply 生效；
 *   · 0 课次（排课失败的班）：「从零排入」，先经 manual::validateAdd/applyAdd 落库，
 *     全部 N 次课有空位才落库并从失败列表移除该班（外层据此不再 runMovable）。
 * 统一「先落排课、后落基本信息」顺序：applyClassEdit 据此读到最终时段与教室——
 * "同窗移时段 + 换新师且不撞"一次保存即成立；撞车只登记不应用；扩计划人数超最终
 * 所在教室才弹确认。undo 由外层整段一步入环，本窗内绝不 push。
 */

#include "classeditdialog.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "classinfoform.h"
#include "core/schedule/manualadd.h"
#include "core/schedule/manualmove.h"
#include "core/store/datastore.h"
#include "editui.h"
#include "timeroomeditor.h"

/*
ClassEditDialog - 构造：双页签（基本信息 / 时间·教室）+ 保存/取消

Parameter：
    store: 数据仓库（非只读：保存会落库）
    classId: 要编辑的教学班
    anchorEntryId: 打开详情时被点击条目的 id（时间·教室「本节」默认对象）
    parent: 父窗口指针

Remark:
    页签2 嵌入 TimeRoomEditor：该班有课次时是可调整的「本节/整班」，0 课次（排课失败
    的班）时自动进入「从零排入」形态，不再禁用。基本信息页在构造时即按 classId 预填。
*/
ClassEditDialog::ClassEditDialog(DataStore &store, const QString &classId,
                                 const QString &anchorEntryId, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_classId(classId)
    , m_anchorEntryId(anchorEntryId)
{
    setWindowTitle(QStringLiteral("编辑教学班"));
    setMinimumSize(620, 460);

    auto *v = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    // —— 页签1：基本信息（可连带改所属课程）——
    auto *infoPage = new QWidget(this);
    auto *ilay = new QVBoxLayout(infoPage);
    ilay->setContentsMargins(8, 8, 8, 8);
    m_infoForm = new ClassInfoForm(m_store, infoPage);
    ilay->addWidget(m_infoForm);
    m_tabs->addTab(infoPage, QStringLiteral("基本信息"));

    // —— 页签2：时间·教室（本节/整班调整）——
    auto *timePage = new QWidget(this);
    auto *tlay = new QVBoxLayout(timePage);
    tlay->setContentsMargins(8, 8, 8, 8);
    m_timeRoom = new TimeRoomEditor(m_store, m_classId, m_anchorEntryId, timePage);
    tlay->addWidget(m_timeRoom);
    m_tabs->addTab(timePage, QStringLiteral("时间·教室"));
    v->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ClassEditDialog::onOkClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &ClassEditDialog::reject);
    v->addWidget(buttons);

    m_infoForm->populate(m_classId);           // 基本信息页按当前班预填
}

/*
ClassEditDialog::onOkClicked - 校验 + 统一顺序落库 + 聚合标志

Remark:
    时间·教室页分两形态，落排课分支随之分叉：
      · 编辑形态（该班有课次）：整批移动走 manual::validate/apply；
      · 从零排入（0 课次失败班）：须把全部 N 次课都指定才走 manual::validateAdd/applyAdd
        ——只填一部分提示留窗；填满则整批 H1~H5 校验，全部有空位才落库并把该班移出
        失败列表（m_added 供外层据此不再 runMovable），否则留窗精确提示卡点。
    顺序关键：先落排课（时间/教室）再落基本信息——applyClassEdit 的换师撞车检测与
    扩容判断都读"最终排课"，故"移时段/从零排入 + 换师"可一次成立。落库决策沿用
    editui：换师撞车只登记（accept 后由外层弹提示）、扩容超当前所在教室容量弹确认
    （拒绝 → 无任何改动则留窗，已落排课则照常 accept）。无实际改动提示留窗。
*/
void ClassEditDialog::onOkClicked()
{
    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    if (!tc) {
        warn(QStringLiteral("教学班 %1 已不存在。").arg(m_classId));
        return;
    }

    // 0. 基本信息自身校验（容量 ≥ 计划人数、所属课程名非空），失败留窗
    if (!m_infoForm->validate())
        return;

    // 1. 收集：基本信息 diff（collect 只置确有变化的实体）+ 排课改动批
    const ClassInfoForm::Result r = m_infoForm->collect();
    QVector<manual::Change> changes;
    int changedCount = 0;
    QVector<manual::AddSlot> addSlots;
    int addCount = 0;
    const bool adding = m_timeRoom->addMode();      // 从零排入形态（0 课次失败班）
    if (adding)
        m_timeRoom->collectAdd(&addSlots, &addCount);
    else
        m_timeRoom->collectChanges(&changes, &changedCount);
    const int need = m_timeRoom->addNeedCount();    // 从零排入应排次数（非 add 形态为 0）

    if (!r.courseEdited && !r.classEdited && addCount == 0 && changedCount == 0) {
        warn(QStringLiteral("没有改动任何字段。"));
        return;
    }

    // 从零排入只支持"整批排满 N 次课"：只填了一部分（N>0 时）→ 提示留窗
    if (adding && need > 0 && addCount > 0 && addCount < need) {
        warn(QStringLiteral("只指定了 %1/%2 次课。需把全部 %2 次课都指定（时间 + 教室）"
                            "才能从零排入并从错误列表移除；若只想保存基本信息改动，"
                            "请把已指定行改回「（未指定）」。")
                 .arg(addCount).arg(need));
        return;
    }
    // 0 课次班但一行都没填（未切页签2 / 该班无法从零排入）→ 本步无排课改动
    const bool scheduleIntent = adding ? (need > 0 && addCount == need)
                                       : (changedCount > 0);

    // 2. 排课硬约束预检：此时尚未落任何库，失败直接留窗、零副作用
    if (scheduleIntent) {
        const manual::Report rep = adding
            ? manual::validateAdd(m_store, m_classId, addSlots)
            : manual::validate(m_store, changes);
        if (!rep.ok) {
            warn(m_timeRoom->describeReject(rep));
            return;
        }
    }

    // 3. 先落排课（时间/教室 / 从零新增）；失败兜底提示留窗
    bool scheduleApplied = false;
    if (scheduleIntent) {
        if (adding) {
            if (!manual::applyAdd(m_store, m_classId, addSlots)) {
                warn(QStringLiteral("落库失败，请重试。"));
                return;
            }
            m_added = true;                       // 供外层：本班已就位、失败项已移除
        } else if (!manual::apply(m_store, changes)) {
            warn(QStringLiteral("落库失败，请重试。"));
            return;
        }
        scheduleApplied = true;
        m_adjusted = true;
        m_adjustSummary = adding
            ? QStringLiteral("已为教学班 %1 从零排入 %2 次课。").arg(m_classId).arg(need)
            : QStringLiteral("已手动调整教学班 %1：%2 次课")
                  .arg(m_classId).arg(changedCount);
    }

    // 4. 课程模板级改动（改名/开课学院即时生效）
    QStringList notes;
    bool courseApplied = false;
    if (r.courseEdited) {
        QString msg;
        courseApplied = editui::applyCourseEdit(m_store, r.course, &msg);
        if (courseApplied && !msg.isEmpty())
            notes << msg;
    }

    // 5. 教学班字段改动（教师/人数；换师撞车登记、扩容确认均在 applyClassEdit 内部决策）
    editui::ApplyOutcome out;
    if (r.classEdited) {
        out = editui::applyClassEdit(m_store, r.klass, this);
        if (out.roomMoveNeeded)
            m_editRoomMove = true;        // 待外层关闭弹窗后对该班局部重排
        if (out.changed && !out.message.isEmpty())
            notes << out.message;
        if (out.changed && !out.teacherBlocked)
            m_classChanged = true;        // 人数/容量/教师确有实际改动（撞车只登记不算）
        if (out.teacherBlocked) {
            m_teacherBlocked = true;
            m_blockedMessage =
                QStringLiteral("换师暂未应用，已登记到冲突列表：\n\n%1\n\n"
                               "请重新打开「编辑」，先在「时间·教室」把本班移到"
                               "该教师空闲时段，再保存换师。").arg(out.message);
        }
    }

    // 6. 有无实际落库决定去留：完全没落（唯一路径 = info 改动被放弃扩容拒绝）
    //    → 提示并留窗，用户可改容量或去页签2换大教室后重存；
    //    已有任何落库（含仅登记撞车）→ 聚合标志 accept，外层负责刷新与提示。
    const bool anyApplied = scheduleApplied || courseApplied || out.changed;
    if (!anyApplied) {
        if (r.classEdited && !out.message.isEmpty())
            QMessageBox::information(this, QStringLiteral("编辑教学班"), out.message);
        return;
    }

    if (courseApplied || out.changed) {
        m_edited = true;
        m_editSummary = notes.join(QStringLiteral("；"));
        if (m_editSummary.isEmpty())
            m_editSummary = QStringLiteral("已更新教学班 %1 信息。").arg(m_classId);
    }
    accept();
}

/*
ClassEditDialog::adjusted - 本次是否落过排课时间/教室改动

Result:
    bool: 有排课改动并 manual::apply 成功返回 true
*/
bool ClassEditDialog::adjusted() const
{
    return m_adjusted;
}

/*
ClassEditDialog::adjustSummary - 排课调整摘要

Result:
    QString: 如"已手动调整教学班 C102：2 次课"
*/
QString ClassEditDialog::adjustSummary() const
{
    return m_adjustSummary;
}

/*
ClassEditDialog::edited - 本次是否落过基本信息改动

Result:
    bool: 课程/班级确有落库（含仅登记"拟换师"冲突）返回 true
*/
bool ClassEditDialog::edited() const
{
    return m_edited;
}

/*
ClassEditDialog::editSummary - 基本信息编辑摘要

Result:
    QString: 课程改名/学院/人数/教师 的旧值→新值拼接
*/
QString ClassEditDialog::editSummary() const
{
    return m_editSummary;
}

/*
ClassEditDialog::classChanged - 教学班字段是否确有实际改动

Result:
    bool: applyClassEdit 确认教师/人数/容量真的落了库返回 true（"拟换师"撞车只登记不算）

Remark:
    供外层判断"要不要对该失败班发起局部重排"：只改课程名/学院或仅登记换师撞车
    不会改变排课可行性，无需引擎重排。
*/
bool ClassEditDialog::classChanged() const
{
    return m_classChanged;
}

/*
ClassEditDialog::addedFromScratch - 本窗是否对 0 课次失败班"从零排入"成功

Result:
    bool: applyAdd 成功返回 true（该班已落库 N 次课、失败记录已移除）

Remark:
    供外层（错误列表编辑）判定：本班已手动就位，不必再 runMovable 让引擎重排
    （否则引擎会把刚手排的 N 次课整班清掉重排）；仅"扩容换大教室"另论。
*/
bool ClassEditDialog::addedFromScratch() const
{
    return m_added;
}

/*
ClassEditDialog::editRoomMoveNeeded - 本次编辑是否确认了"扩容换大教室"

Result:
    bool: 为 true 时外层需在弹窗关闭后对该班发起一次局部重排
*/
bool ClassEditDialog::editRoomMoveNeeded() const
{
    return m_editRoomMove;
}

/*
ClassEditDialog::teacherBlocked - 换师撞车是否只登记而未应用

Result:
    bool: 为 true 时外层弹 blockedMessage() 提示
*/
bool ClassEditDialog::teacherBlocked() const
{
    return m_teacherBlocked;
}

/*
ClassEditDialog::blockedMessage - 换师撞车提示文案

Result:
    QString: 含冲突详情与引导（外层弹窗展示）
*/
QString ClassEditDialog::blockedMessage() const
{
    return m_blockedMessage;
}

/*
ClassEditDialog::warn - 校验失败提示

Parameter：
    text: 提示正文
*/
void ClassEditDialog::warn(const QString &text) const
{
    QMessageBox::warning(const_cast<ClassEditDialog *>(this),
                         QStringLiteral("编辑教学班"), text);
}
