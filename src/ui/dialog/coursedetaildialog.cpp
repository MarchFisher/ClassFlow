/**
 * 文件职责：课程详情弹窗实现。主窗口点击课表卡片时创建，
 * 依据 ScheduleEntry 解析出课程 / 教学班 / 教师 / 教室等字段，表单式展示。
 * 标题行右端并排三枚纯图标（带悬浮提示）动作按钮：
 *   「锁定/解锁」— 直接切换本班锁定态（未锁「锁定」+关锁 / 已锁「解锁」+开锁）；
 *   「编辑」     — 开 ClassEditDialog（基本信息 + 时间·教室 双页签）一次保存两类改动，
 *                   由其在 onOkClicked 统一校验落库，成功后聚合标志并原地刷新基础行/时间行；
 *   「删除」     — 先弹小选择窗定范围（仅本班 / 连同整门课），再二次确认后改库并关闭。
 * 图标色随明暗主题自动换色（ThemeManager::baseForeground → themeicon::fromFeather）。
 * 变更是否发生经 locksChanged() / edited() / adjusted() / removed() 供外层刷新课表与入环。
 */

#include "coursedetaildialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include "classeditdialog.h"
#include "core/store/datastore.h"
#include "ui/theme/theme.h"
#include "ui/theme/themeicons.h"

/*
CourseDetailDialog - 构造函数：解析条目并搭建详情表单 + 三动作按钮

Parameter：
    store: 数据仓库（含课程 / 教学班 / 排课条目；锁定按钮会改其锁定集）
    entry: 被点击的排课条目
    parent: 父窗口指针

Remark:
    通过 teachingClassId 反查教学班 → courseId → 课程模板，字段缺失（如教师未指定）
    时以占位符显示。教学班查无时「编辑」「删除」禁用；锁定永不禁（锁定集只认 id）。
*/
CourseDetailDialog::CourseDetailDialog(DataStore &store,
                                       const ScheduleEntry &entry,
                                       QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_classId(entry.teachingClassId)
    , m_entry(entry)
{
    setWindowTitle(QStringLiteral("课程详情"));

    // 弹窗整体字号略放大一档（未显式设字体的子控件随之继承，便于读详情）
    QFont baseFont = font();
    baseFont.setPointSizeF(baseFont.pointSizeF() + 1.0);
    setFont(baseFont);

    // 反查教学班与课程模板（O(1) 查表，未命中为 nullptr）
    const TeachingClass *tc = store.teachingClassById(entry.teachingClassId);
    const Course *course = tc ? store.courseById(tc->courseId) : nullptr;

    // 标题：课程名（+ 教学班），加粗放大；编辑后按 store 现读刷新
    m_titleLabel = new QLabel(course ? course->name : entry.teachingClassId, this);
    QFont f = m_titleLabel->font();
    f.setPointSize(15);
    f.setBold(true);
    m_titleLabel->setFont(f);

    // 详情表单（教学班号/学分/周次等静态行；动态行用成员供事后刷新）
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(40);   // 标签列与值列间距拉开（可读性）
    form->setVerticalSpacing(10);     // 行距略大于默认
    form->addRow(QStringLiteral("课程号"),  new QLabel(course ? course->id : QStringLiteral("-"), this));
    form->addRow(QStringLiteral("教学班"),  new QLabel(tc ? tc->classId : QStringLiteral("-"), this));
    form->addRow(QStringLiteral("学分"),    new QLabel(course ? QString::number(course->credit) : QStringLiteral("-"), this));
    form->addRow(QStringLiteral("每周课次"), new QLabel(course ? QString::number(course->sessionsPerWeek) : QStringLiteral("-"), this));
    form->addRow(QStringLiteral("单次学时"), new QLabel(course ? QString::number(course->hoursPerSession) : QStringLiteral("-"), this));
    m_departLabel = new QLabel(course && !course->depart.isEmpty() ? course->depart : QStringLiteral("-"), this);
    form->addRow(QStringLiteral("开课学院"), m_departLabel);
    form->addRow(QStringLiteral("所需教室"), new QLabel(course ? roomTypeName(course->requiredRoomType) : QStringLiteral("-"), this));
    form->addRow(QStringLiteral("周次"),    new QLabel(
        (entry.startWeek == entry.endWeek)
            ? QStringLiteral("第 %1 周").arg(entry.startWeek)
            : QStringLiteral("第 %1~%2 周").arg(entry.startWeek).arg(entry.endWeek), this));
    m_timeLabel = new QLabel(QStringLiteral("%1 第 %2~%3 节")
            .arg(weekdayName(entry.timeSlot.dayOfWeek))
            .arg(entry.timeSlot.startSection)
            .arg(entry.timeSlot.endSection), this);
    form->addRow(QStringLiteral("上课时间"), m_timeLabel);
    m_roomLabel = new QLabel(entry.classroomId, this);
    form->addRow(QStringLiteral("教室"),    m_roomLabel);
    // 教师：teacherById 查表显示姓名；未指定 / 未收录 / 推导（name=id）时回退
    m_teacherLabel = new QLabel(QStringLiteral("未安排"), this);
    form->addRow(QStringLiteral("教师"),    m_teacherLabel);
    m_sizeLabel = new QLabel(QStringLiteral("-"), this);
    form->addRow(QStringLiteral("人数"),    m_sizeLabel);

    // —— 三枚动作按钮（锁定/解锁 · 编辑 · 删除，纯图标随标题行右对齐）——
    m_lockBtn = new QPushButton(this);
    m_editBtn = new QPushButton(this);
    m_deleteBtn = new QPushButton(this);
    const QSize iconBox(34, 34);
    for (QPushButton *btn : {m_lockBtn, m_editBtn, m_deleteBtn}) {
        btn->setProperty("flat", true);   // 纯图标态：透明底 + 无文本内边距（theme.qss）
        btn->setFixedSize(iconBox);
        btn->setFocusPolicy(Qt::NoFocus); // 不做初始焦点落点 → 打开时无焦点环
    }
    m_editBtn->setToolTip(QStringLiteral("编辑"));
    m_deleteBtn->setToolTip(QStringLiteral("删除"));
    connect(m_lockBtn, &QPushButton::clicked, this, [this]() {
        const bool locked = m_store.isClassLocked(m_classId);
        if (locked)
            m_store.unlockClass(m_classId);
        else
            m_store.lockClass(m_classId);
        m_locksChanged = true;
        updateLockButton();
    });

    // 「编辑」：合并基本信息 + 时间·教室双页签，一次保存两类改动（不开新子弹窗逻辑）
    connect(m_editBtn, &QPushButton::clicked, this, [this]() { openClassEdit(); });

    // 「删除」：小选择窗定范围（仅本班 / 连整门课），选定后再二次确认
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() { askDeleteRange(); });

    // 教学班查无 → 无从编辑/删除；锁定只认 id，永不禁用
    if (!tc) {
        m_editBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
    }

    // 底部关闭按钮（同样不做焦点落点，避免打开时默认落在关闭钮）
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (QPushButton *closeBtn = buttons->button(QDialogButtonBox::Close)) {
        closeBtn->setFocusPolicy(Qt::NoFocus);
        closeBtn->setText(QStringLiteral("关闭"));
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 标题行：课程名（左占满）+ 三枚图标按钮右对齐（锁定/解锁 · 编辑 · 删除）
    auto *head = new QHBoxLayout;
    head->addWidget(m_titleLabel, 1);
    head->addWidget(m_lockBtn);
    head->addSpacing(6);
    head->addWidget(m_editBtn);
    head->addSpacing(6);
    head->addWidget(m_deleteBtn);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(head);
    layout->addLayout(form);
    layout->addWidget(buttons);
    setMinimumWidth(400);   // 字号 +1 且两列间距拉开后放宽下限

    // 三钮图标 18px；明暗切换时按新主题前景色重建（context=this 随窗自动断连）
    m_lockBtn->setIconSize(QSize(18, 18));
    m_editBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->setIconSize(QSize(18, 18));
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &CourseDetailDialog::applyThemeIcons);
    applyThemeIcons();
    refreshBasicLabels(); // 把教师/人数等占位行按 store 现读填上（标题/学院同值覆盖）
}

/* removed - 本次删除过返回 true，供外层关闭后原地刷新课表 */
bool CourseDetailDialog::removed() const
{
    return m_removed;
}

/* removalSummary - 删除摘要，如"已删除教学班 C102（属「高等数学」）" */
QString CourseDetailDialog::removalSummary() const
{
    return m_removalSummary;
}

/* edited - 改过基本信息（含仅登记换师撞车冲突）返回 true，外层据此刷新与整段入环 */
bool CourseDetailDialog::edited() const
{
    return m_edited;
}

/* editSummary - 编辑摘要，如"已更新教学班 C102：人数 …；教师 → 王老师" */
QString CourseDetailDialog::editSummary() const
{
    return m_editSummary;
}

/* editRoomMoveNeeded - 确认"扩容换大教室"返回 true，外层据此对该班发起局部重排 */
bool CourseDetailDialog::editRoomMoveNeeded() const
{
    return m_editRoomMove;
}

/* locksChanged - 点过锁定/解锁返回 true，外层据此刷新卡片锁标 */
bool CourseDetailDialog::locksChanged() const
{
    return m_locksChanged;
}

/* adjusted - 经「编辑→时间·教室」确有排课改动并落库返回 true */
bool CourseDetailDialog::adjusted() const
{
    return m_adjusted;
}

/* adjustSummary - 调整摘要，如"已手动调整教学班 C102：1 次课" */
QString CourseDetailDialog::adjustSummary() const
{
    return m_adjustSummary;
}

/*
CourseDetailDialog::openClassEdit - 打开合并编辑弹窗，成功后聚合标志并原地刷新

Remark:
    ClassEditDialog 持非 const store，在自身 onOkClicked 统一校验落库（先排课后
    基本信息）后 accept；本函数只做标志搬运（调整/编辑/扩容重排/换师撞车）与
    exec 返回后的标签刷新——详情保持打开，便于用户接着锁定本班。undo 由外层整段
    一步入环，此处不 push。撞车登记会提示；其余决策（放弃扩容）已在弹窗内说明。
*/
void CourseDetailDialog::openClassEdit()
{
    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    if (!tc)
        return;
    ClassEditDialog dlg(m_store, m_classId, m_entry.entryId, this);
    if (dlg.exec() != QDialog::Accepted)
        return;                                   // 取消或留窗未落：本弹窗不动

    if (dlg.adjusted()) {                         // 时间/教室改动已落库
        m_adjusted = true;
        m_adjustSummary = dlg.adjustSummary();
        refreshEntryLabels();
    }
    if (dlg.edited()) {                           // 基本信息改动已落库（含仅登记撞车）
        m_edited = true;
        m_editSummary = dlg.editSummary();
        refreshBasicLabels();
    }
    if (dlg.editRoomMoveNeeded())
        m_editRoomMove = true;                    // 待外层关闭弹窗后对该班局部重排
    if (dlg.teacherBlocked())                     // 换师撞车：教务必须看到"未应用"
        QMessageBox::information(this, QStringLiteral("编辑教学班"),
                                 dlg.blockedMessage());
}

/*
CourseDetailDialog::askDeleteRange - 「删除」小选择窗：先定删除范围

Remark:
    两个 AcceptRole 按钮（仅删本班 / 连同整门课删）+ 默认「取消」。目标缺失时
    对应选项不提供（教学班查无时整个删除钮已被禁用）。选定后交 confirmDelete 二次
    确认。不引 deletedialog（那是侧边栏整批删除，语义不同）。
*/
void CourseDetailDialog::askDeleteRange()
{
    const TeachingClass *t = m_store.teachingClassById(m_classId);
    if (!t)
        return;
    const Course *c = t ? m_store.courseById(t->courseId) : nullptr;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除"));
    box.setText(QStringLiteral("删除「%1」的哪个范围？").arg(m_classId));
    box.setInformativeText(QStringLiteral("选定后还需二次确认；删除不可撤销。"));
    QPushButton *delClass = box.addButton(QStringLiteral("仅删除本教学班"),
                                          QMessageBox::AcceptRole);
    QPushButton *delCourse = c
        ? box.addButton(QStringLiteral("连同整门课程一起删除"), QMessageBox::AcceptRole)
        : nullptr;
    QPushButton *cancel = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();

    if (box.clickedButton() == delClass)
        confirmDelete(false);
    else if (delCourse && box.clickedButton() == delCourse)
        confirmDelete(true);
}

/*
CourseDetailDialog::confirmDelete - 按范围二次确认并落库

Parameter：
    wholeCourse: true = 连同整门课程一起删（连带全部教学班）；false = 仅删本教学班

Remark:
    沿用原两条删除入口的确认文案与 store 调用；成功后置 removed 标志并关闭本弹窗
    （删除不触发排课）。删单班时课程保留（删成空课程也保留，可去「删除课程」页整门移除）。
*/
void CourseDetailDialog::confirmDelete(bool wholeCourse)
{
    if (!wholeCourse) {                           // 删单班：课程与班相互独立，删班不删课
        const TeachingClass *t = m_store.teachingClassById(m_classId);
        if (!t)
            return;
        const QString cid = t->courseId;
        QString cname = cid;
        if (const Course *pc = m_store.courseById(cid))
            cname = pc->name;
        int total = 0;                            // 该课现有教学班数
        for (const TeachingClass &k : m_store.teachingClasses())
            if (k.courseId == cid)
                ++total;
        QString text = (total <= 1)
            ? QStringLiteral("将删除教学班 %1（它是「%2」最后一个班）。\n"
                             "删除后课程保留为空课程，可在「删除课程」页整门移除。")
                  .arg(m_classId, cname)
            : QStringLiteral("将删除教学班 %1（属「%2」，该课还剩 %3 个班）。")
                  .arg(m_classId).arg(cname).arg(total - 1);
        const auto ans = QMessageBox::warning(this, QStringLiteral("删除教学班"),
            text + QStringLiteral("\n该班现有排课会被移除，此操作不可撤销。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;
        if (!m_store.removeTeachingClass(m_classId))
            return;
        m_removed = true;
        m_removalSummary =
            QStringLiteral("已删除教学班 %1（属「%2」）").arg(m_classId, cname);
        accept();
        return;
    }

    // 删整课：连带其全部教学班与排课
    const TeachingClass *t = m_store.teachingClassById(m_classId);
    const Course *c = t ? m_store.courseById(t->courseId) : nullptr;
    if (!c)
        return;
    const QString cid = c->id;
    const QString cname = c->name;
    int total = 0;
    for (const TeachingClass &k : m_store.teachingClasses())
        if (k.courseId == cid)
            ++total;
    const auto ans = QMessageBox::warning(this, QStringLiteral("删除整门课"),
        QStringLiteral("将删除整门课「%1（%2）」及其全部 %3 个教学班，现有排课一并移除。\n"
                       "此操作不可撤销。")
            .arg(cname, cid).arg(total),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;
    if (!m_store.removeCourse(cid))
        return;
    m_removed = true;
    m_removalSummary =
        QStringLiteral("已删除整门课「%1」及 %2 个教学班").arg(cname).arg(total);
    accept();
}

/*
CourseDetailDialog::refreshEntryLabels - 按 entryId 现读刷新「上课时间/教室」两行

Remark:
    时间/教室调整不改 entryId（保持原值），故按 m_entry.entryId 反查；
    查不到（理论不发生）则置"（已不存在）"。
*/
void CourseDetailDialog::refreshEntryLabels()
{
    const ScheduleEntry *e = m_store.scheduleEntryById(m_entry.entryId);
    if (!e) {
        if (m_timeLabel) m_timeLabel->setText(QStringLiteral("（已不存在）"));
        if (m_roomLabel) m_roomLabel->setText(QStringLiteral("（已不存在）"));
        return;
    }
    m_entry = *e;                                 // 同步锚拷贝，便于连续多次调整
    if (m_timeLabel)
        m_timeLabel->setText(QStringLiteral("%1 第 %2~%3 节")
            .arg(weekdayName(e->timeSlot.dayOfWeek))
            .arg(e->timeSlot.startSection)
            .arg(e->timeSlot.endSection));
    if (m_roomLabel)
        m_roomLabel->setText(e->classroomId);
}

/*
CourseDetailDialog::updateLockButton - 按当前锁定态刷新锁图标与悬浮提示

Remark:
    纯图标按钮：未锁 → 关锁 lock 图标（提示"锁定本班"）；已锁 → 开锁 unlock 图标
    （提示"解锁本班"）。图标描边取当前主题前景色（随明暗自动换色）。
*/
void CourseDetailDialog::updateLockButton()
{
    if (!m_lockBtn)
        return;
    const bool locked = m_store.isClassLocked(m_classId);
    m_lockBtn->setIcon(themeicon::fromFeather(
        locked ? QStringLiteral("unlock") : QStringLiteral("lock"),
        ThemeManager::instance().baseForeground(), m_lockBtn));
    m_lockBtn->setToolTip(locked ? QStringLiteral("解锁") : QStringLiteral("锁定"));
}

/*
CourseDetailDialog::applyThemeIcons - 依当前主题前景色重设三钮 Feather 图标

Remark:
    锁钮图标随锁定态在 updateLockButton 内再定；明暗切换经 themeChanged 触达重设。
*/
void CourseDetailDialog::applyThemeIcons()
{
    const QColor fg = ThemeManager::instance().baseForeground();
    if (m_editBtn)
        m_editBtn->setIcon(themeicon::fromFeather(QStringLiteral("edit-2"), fg, m_editBtn));
    if (m_deleteBtn)
        m_deleteBtn->setIcon(themeicon::fromFeather(QStringLiteral("trash-2"), fg, m_deleteBtn));
    updateLockButton();
}

/*
CourseDetailDialog::refreshBasicLabels - 现读刷新标题/开课学院/教师/人数 四行

Remark:
    编辑后按 store 现读刷新：课程改名换 m_titleLabel，换师（reassignClassTeacher
    会连带改写该班条目 teacherId）优先取条目现读值，人数行按班现读计划/容量。
*/
void CourseDetailDialog::refreshBasicLabels()
{
    const TeachingClass *tc = m_store.teachingClassById(m_classId);
    const Course *course = tc ? m_store.courseById(tc->courseId) : nullptr;
    const ScheduleEntry *e = m_store.scheduleEntryById(m_entry.entryId);
    const QString teacherId = e ? e->teacherId
                                : (tc ? tc->teacherId : m_entry.teacherId);

    if (m_titleLabel)
        m_titleLabel->setText(course ? course->name : m_classId);
    if (m_departLabel)
        m_departLabel->setText(course && !course->depart.isEmpty()
                                   ? course->depart : QStringLiteral("-"));
    if (m_teacherLabel) {
        QString text = QStringLiteral("未安排");
        if (!teacherId.isEmpty()) {
            text = teacherId;                     // 默认回退：id 本身
            const TeacherInfo *t = m_store.teacherById(teacherId);
            if (t && t->name != t->teacherId)
                text = t->name + QStringLiteral("（") + t->teacherId + QStringLiteral("）");
        }
        m_teacherLabel->setText(text);
    }
    if (m_sizeLabel)
        m_sizeLabel->setText(tc
            ? QStringLiteral("计划 %1 / 容量 %2").arg(tc->plannedSize).arg(tc->maxCapacity)
            : QStringLiteral("-"));
}

/*
CourseDetailDialog::weekdayName - 星期号转中文名

Parameter：
    day: 星期号（1..7）

Result:
    QString: "周一".."周日"；越界时原样返回数字
*/
QString CourseDetailDialog::weekdayName(int day)
{
    static const QString names[] = {
        QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
        QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
        QStringLiteral("周日")
    };
    return (day >= 1 && day <= 7) ? names[day - 1] : QString::number(day);
}

/*
CourseDetailDialog::roomTypeName - 教室类型枚举转中文名

Parameter：
    type: ClassroomType 枚举

Result:
    QString: "不限" / "普通教室" / "机房" / "操场"
*/
QString CourseDetailDialog::roomTypeName(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Norm:
        return QStringLiteral("普通教室");
    case ClassroomType::Lab:
        return QStringLiteral("机房");
    case ClassroomType::PlayGround:
        return QStringLiteral("操场");
    default:
        return QStringLiteral("不限");
    }
}
