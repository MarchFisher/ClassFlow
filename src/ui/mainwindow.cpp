/**
 * 文件职责：主窗口实现。顶部栏（保存/撤销/重做 图标钮 + 周选择器 + 自动排课/主题）
 * + 左侧侧边栏（新建 / 导入快照 / 导出快照 / 编辑…）+ 课表网格。只负责窗口拼装与
 * 文件级动作：排课生命周期（后台线程/进度/换源）委托给 ScheduleController，读课表/
 * 点卡/筛选/错误查看委托给 TimetableController。保存分两层：改动防抖
 * 自动写 .classflow/workspace.dat（L1，启动恢复现场）；Ctrl+S 手动写教务选定的项目
 * 文件（L2，首次即选路径）；侧栏「导出快照」独立另存一份副本，不改项目文件身份。
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QSpinBox>
#include <QTimer>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>
#include <QVector>

#include "ui/emptystate.h"
#include "ui/dialog/adddialog.h"
#include "ui/dialog/classeditdialog.h"
#include "ui/dialog/classgrouping.h"
#include "ui/dialog/deletedialog.h"
#include "ui/dialog/editinfodialog.h"
#include "ui/dialog/editui.h"
#include "ui/dialog/importdialog.h"
#include "ui/dialog/lockdialog.h"
#include "ui/dialog/snapshotimportdialog.h"
#include "ui/schedule/schedulecontroller.h"
#include "ui/theme/theme.h"
#include "ui/theme/themeicons.h"
#include "ui/timetable/timetabledelegate.h"
#include "ui/timetable/timetablecontroller.h"
#include "ui/timetable/timetableview.h"

/*
MainWindow - 主窗口构造函数：搭建顶部栏、侧边栏、课表网格，接线两个控制器并自动恢复上次工作区

Parameter：
    parent: 父窗口指针，默认 nullptr

Remark:
    布局：顶部左「保存」右「自动排课」；左侧 1/4 侧边栏（新建/导入/导出），右侧 3/4 课表。
    排课/课表交互分别由 ScheduleController / TimetableController 承担，本构造只做拼装与接线。
*/
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_model(this)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("ClassFlow - 自动排课"));
    resize(1100, 760);              // 默认窗口放大，保证课表格子够大
    setMinimumSize(900, 600);

    // —— 顶部栏：左「保存/撤销/重做」（图标+文字钮）+ 中间「周选择器」+
    //            右「自动排课 / 排课错误 / 主题」 ——
    m_saveBtn = new QPushButton(QStringLiteral("保存"), this);
    m_undoBtn = new QPushButton(QStringLiteral("撤销"), this);
    m_redoBtn = new QPushButton(QStringLiteral("重做"), this);
    auto *schedBtn = new QPushButton(QStringLiteral("自动排课"), this);
    m_errBtn = new QPushButton(QStringLiteral("排课错误"), this);
    m_lockBtn = new QPushButton(QStringLiteral("锁定"), this);
    auto *themeBtn = new QPushButton(QStringLiteral("主题"), this);
    // 历史动作钮三件套：图标统一 18px 且等高 40；等宽在 applyTopButtonIcons 后按
    // 三者 sizeHint 之最补齐（见下方），保证 保存/撤销/重做 观感一致
    for (QPushButton *b : {m_saveBtn, m_undoBtn, m_redoBtn}) {
        b->setIconSize(QSize(18, 18));
        b->setMinimumHeight(40);
    }
    schedBtn->setMinimumHeight(40);
    m_errBtn->setMinimumHeight(40);
    m_lockBtn->setMinimumHeight(40);
    themeBtn->setMinimumHeight(40);
    schedBtn->setProperty("primary", true);   // 主操作按钮：用主题 accent 强调色

    // 周选择器：切换显示哪一周的课表
    auto *weekLabel = new QLabel(QStringLiteral("周次:"), this);
    m_weekSpin = new QSpinBox(this);
    m_weekSpin->setRange(1, 16);          // 默认 16 周，载入数据后由 syncWeekSelector 对齐
    m_weekSpin->setValue(1);
    m_weekSpin->setMinimumWidth(72);

    // —— 「主题」按钮：弹出明暗方案 + accent 菜单 ——
    ThemeManager &tm = ThemeManager::instance();
    auto *themeMenu = new QMenu(themeBtn);
    auto *schemeGroup = new QActionGroup(this);
    schemeGroup->setExclusive(true);
    auto addSchemeAction = [this, themeMenu, schemeGroup, &tm](
                               const QString &text, ThemeManager::Scheme s) {
        QAction *act = themeMenu->addAction(text);
        act->setCheckable(true);
        act->setChecked(tm.scheme() == s);
        schemeGroup->addAction(act);
        connect(act, &QAction::triggered, this,
                [&tm, s]() { tm.setScheme(s); });
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    addSchemeAction(QStringLiteral("跟随系统"), ThemeManager::FollowSystem);
#endif
    addSchemeAction(QStringLiteral("亮色"), ThemeManager::Light);
    addSchemeAction(QStringLiteral("暗色"), ThemeManager::Dark);
    themeMenu->addSeparator();
    auto *accentGroup = new QActionGroup(this);
    accentGroup->setExclusive(true);
    for (const auto &acc : tm.accents()) {
        QAction *act = themeMenu->addAction(acc.second);
        act->setCheckable(true);
        act->setChecked(tm.accentId() == acc.first);
        accentGroup->addAction(act);
        connect(act, &QAction::triggered, this,
                [&tm, id = acc.first]() { tm.setAccent(id); });
    }
    themeBtn->setMenu(themeMenu);

    // 主题变化（明暗 / accent）→ 刷新课表卡片配色盘 + 顶栏三钮图标
    connect(&tm, &ThemeManager::themeChanged, this, &MainWindow::applyCardPalette);
    connect(&tm, &ThemeManager::themeChanged, this, &MainWindow::applyTopButtonIcons);
    applyTopButtonIcons();   // 初始：给 保存/撤销/重做 上主题色图标（含禁用灰态）
    // 三钮等宽：取三者 sizeHint 最宽者统一设最小宽，观感一致
    int histW = 0;
    for (QPushButton *b : {m_saveBtn, m_undoBtn, m_redoBtn})
        histW = qMax(histW, b->sizeHint().width());
    for (QPushButton *b : {m_saveBtn, m_undoBtn, m_redoBtn})
        b->setMinimumWidth(histW);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_saveBtn);
    topRow->addWidget(m_undoBtn);
    topRow->addWidget(m_redoBtn);
    topRow->addStretch();
    topRow->addWidget(weekLabel);
    topRow->addWidget(m_weekSpin);
    topRow->addSpacing(12);
    topRow->addWidget(schedBtn);
    topRow->addSpacing(4);
    topRow->addWidget(m_errBtn);
    topRow->addSpacing(4);
    topRow->addWidget(m_lockBtn);
    topRow->addSpacing(4);
    topRow->addWidget(themeBtn);

    // Ctrl+S 触发保存
    auto *shortcut = new QShortcut(QKeySequence::StandardKey::Save, this);
    connect(shortcut, &QShortcut::activated, this, &MainWindow::onSaveClicked);
    // Ctrl+Z / Ctrl+Shift+Z 触发撤销/重做（弹窗全模态，主窗快捷键在弹窗期间不激活）
    auto *undoShortcut = new QShortcut(QKeySequence::StandardKey::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::onUndoClicked);
    auto *redoShortcut = new QShortcut(QKeySequence::StandardKey::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, &MainWindow::onRedoClicked);

    // —— 左侧侧边栏：分「文件 / 课表」两节，各带次要文字小节标题 ——
    auto *newBtn = new QPushButton(QStringLiteral("新建"), this);
    auto *importBtn = new QPushButton(QStringLiteral("导入快照"), this);
    auto *exportBtn = new QPushButton(QStringLiteral("导出快照"), this);
    m_editBtn = new QPushButton(QStringLiteral("编辑"), this);
    auto *addCourseBtn = new QPushButton(QStringLiteral("新增"), this);
    auto *deleteBtn = new QPushButton(QStringLiteral("删除"), this);
    m_filterBtn = new QPushButton(QStringLiteral("筛选"), this);
    // 统一按钮观感：min-height 44 + 11pt（不再 60 高的"大按钮"）
    for (QPushButton *btn : {newBtn, importBtn, exportBtn, m_editBtn,
                             addCourseBtn, deleteBtn, m_filterBtn}) {
        btn->setMinimumHeight(44);
        QFont f = btn->font();
        f.setPointSize(11);
        btn->setFont(f);
    }
    auto sectionTitle = [this](const QString &text) {
        auto *lab = new QLabel(text, this);
        lab->setProperty("secondary", true);   // 次要文字（QSS QLabel[secondary=true]）
        lab->setContentsMargins(4, 0, 0, 0);
        return lab;
    };

    auto *sidebar = new QVBoxLayout;
    sidebar->addWidget(sectionTitle(QStringLiteral("文件")));
    sidebar->addWidget(newBtn);
    sidebar->addWidget(importBtn);
    sidebar->addWidget(exportBtn);
    sidebar->addSpacing(12);
    sidebar->addWidget(sectionTitle(QStringLiteral("课表")));
    sidebar->addWidget(m_editBtn);
    // 紧随「编辑」之后：新增 / 删除 / 筛选 三个按钮等宽并排成一行
    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(addCourseBtn, 1);   // 拉伸系数相同 → 等宽
    bottomRow->addWidget(deleteBtn, 1);
    bottomRow->addWidget(m_filterBtn, 1);
    sidebar->addLayout(bottomRow);
    sidebar->addStretch();

    // —— 右侧课表：只读，行列撑满、格子大；卡片由 delegate 自绘 ——
    m_view = new TimetableView(this);
    m_view->setModel(&m_model);
    m_view->setItemDelegate(new TimetableDelegate(m_view));   // 逐卡自绘 + 像素命中
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);  // 点击卡片时高亮当前格
    m_view->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 行撑满高度

    // —— 右侧画布：空态引导页 / 课表网格 用 QStackedWidget 切换；外圈留白让画布浮起 ——
    auto *rightPane = new QVBoxLayout;
    rightPane->setContentsMargins(12, 12, 12, 12);
    m_empty = new EmptyState(this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_empty);    // page 0：无数据引导
    m_stack->addWidget(m_view);     // page 1：课表网格
    rightPane->addWidget(m_stack, 1);

    // —— 主体：左 1/4 侧边栏 + 右 3/4 课表 ——
    auto *body = new QHBoxLayout;
    body->addLayout(sidebar, 1);
    body->addLayout(rightPane, 3);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addLayout(topRow);
    layout->addLayout(body, 1);
    setCentralWidget(central);
    // 点主窗空白（布局留白/两控件之间等无子控件区）→ 取消残留选中格与按钮焦点
    central->installEventFilter(this);

    // —— 状态栏：单条富文本标签 stretch 承接主窗全部状态（右端留给 Qt 原生提示区）——
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::RichText);
    statusBar()->addWidget(m_status, 1);

    // —— 控制器：排课生命周期 / 课表交互（各持引用/指针，本类只接线） ——
    // 排课期间需禁用的入口按钮：自动排课 / 撤销 / 重做 / 锁定 / 新建 / 导出 /
    // 新增课程教学班 / 删除 / 编辑 / 导入 / 筛选（导出会在线程写盘途中抓到半截结果；
    // 编辑改的是 worker 正在排的那份副本的源数据，冲突；筛选会让模型状态与排课结果错开；
    // undo/redo 会在排课换源竞态中撤错目标）
    m_schedule = new ScheduleController(m_store, m_model,
                                        {schedBtn, m_undoBtn, m_redoBtn, m_lockBtn,
                                         newBtn, exportBtn, addCourseBtn, deleteBtn,
                                         m_editBtn, importBtn, m_filterBtn},
                                        this, this);
    m_grid = new TimetableController(m_store, m_model, m_weekSpin, &m_undo, this, this);
    // 课表交互产生的状态文案接到主窗富文本状态标签（不再直写 statusBar）
    m_grid->setStatusSink([this](const QString &text) { showStatusMsg(text); });

    // 文件级动作留在本类
    connect(newBtn,         &QPushButton::clicked, this, &MainWindow::onNewClicked);
    connect(addCourseBtn,   &QPushButton::clicked, this, &MainWindow::onAddCourseClicked);
    connect(deleteBtn,      &QPushButton::clicked, this, &MainWindow::onDeleteClicked);
    connect(m_editBtn,      &QPushButton::clicked, this, &MainWindow::onEditClicked);
    connect(importBtn,      &QPushButton::clicked, this, &MainWindow::onImportClicked);
    connect(exportBtn,  &QPushButton::clicked, this, &MainWindow::onExportClicked);
    connect(m_saveBtn,  &QPushButton::clicked, this, &MainWindow::onSaveClicked);
    // 查看 / 交互类动作交给 TimetableController；筛选生效后点亮「筛选」按钮
    connect(m_filterBtn, &QPushButton::clicked, this, [this] {
        m_grid->openFilter();
        refreshFilterVisual();
    });
    connect(m_errBtn,   &QPushButton::clicked,
            m_grid, &TimetableController::showScheduleErrors);
    // 自动排课经中转槽入环（捕获动作前状态，成功才 push）；撤销/重做本类处理
    connect(schedBtn,   &QPushButton::clicked, this, &MainWindow::onRunSchedulingClicked);
    connect(m_undoBtn,  &QPushButton::clicked, this, &MainWindow::onUndoClicked);
    connect(m_redoBtn,  &QPushButton::clicked, this, &MainWindow::onRedoClicked);
    // 详情弹窗内发生撤销级变更（锁定/编辑/调整/删除）→ 刷新撤销/重做按钮可用态
    connect(m_grid, &TimetableController::undoStackChanged,
            this, &MainWindow::updateUndoActions);
    // 详情内「编辑信息」确认扩容换大教室 → 本类对该班发起局部重排（腾不出落错误列表）
    connect(m_grid, &TimetableController::rescheduleNeeded,
            this, &MainWindow::onRescheduleNeeded);
    // 排课错误列表某行「编辑」→ 钉选该班编辑，改完对失败班整体局部重排重试
    connect(m_grid, &TimetableController::editClassRequested,
            this, &MainWindow::onEditClassRequested);
    // 「锁定」由本类弹窗维护锁定集，不触发排课
    connect(m_lockBtn,  &QPushButton::clicked, this, &MainWindow::onLockClicked);
    // 周选择器切换 → 刷新课表模型
    connect(m_weekSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            m_grid, &TimetableController::setWeek);
    // 排课结束（成功/失败/取消）→ 本类做界面收尾
    connect(m_schedule, &ScheduleController::scheduleDone,
            this, &MainWindow::onSchedulingDone);
    // 点卡片（像素命中某门课）→ 直达该课详情；点「更多」→ 弹菜单列被折叠课
    connect(m_view, &TimetableView::entryClicked,
            m_grid, &TimetableController::showCourseDetailAt);
    connect(m_view, &TimetableView::moreClicked,
            m_grid, &TimetableController::showMoreCourses);
    // 空态引导页的 CTA 直连文件级动作
    connect(m_empty, &EmptyState::newRequested, this, &MainWindow::onNewClicked);
    connect(m_empty, &EmptyState::importRequested, this, &MainWindow::onImportClicked);

    // —— 启动时自动恢复上次工作区 ——
    const QString mem = memoryPath();
    if (!mem.isEmpty() && QFileInfo::exists(mem) && m_store.loadSnapshot(mem)) {
        m_model.setDataStore(&m_store);
        m_grid->syncWeekSelector();
        showStatusMsg(QStringLiteral("已恢复上次工作区"));
    } else {
        showStatusMsg(QStringLiteral("请先新建或导入"));
    }

    // —— 会话续接：记住的最近项目文件（L2）→ 标题名与脏标记基准；自动写基准 = 刚恢复内容 ——
    QSettings settings(QStringLiteral("ClassFlow"), QStringLiteral("ClassFlow"));
    const QString remembered = settings.value(QStringLiteral("projectFile/last")).toString();
    if (!remembered.isEmpty() && QFileInfo::exists(remembered)) {
        m_projectFile = remembered;
        DataStore baseline;                        // 只读该文件求 ● 基准，不碰当前态
        if (baseline.loadSnapshot(remembered))
            m_projectText = baseline.snapshotText();
    }
    m_autoText = m_store.snapshotText();
    updateWindowTitle();

    // —— 自动恢复防抖：内容与上次写盘不同 → 1.5s 内自动写 L1，并顺手刷新标题脏标记 ——
    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(1500);
    connect(m_autoTimer, &QTimer::timeout, this, [this] {
        const QString text = m_store.snapshotText();
        if (!m_store.teachingClasses().isEmpty() && text != m_autoText) {
            writeWorkspace();
            m_autoText = text;
        }
        updateWindowTitle();
    });
    m_autoTimer->start();

    updateUndoActions();   // 恢复会话为全新：撤销/重做均不可用（无历史跨越启动边界）
    applyCardPalette();   // 按当前主题（已由 main 应用）注入课表卡片配色盘
    updateEmptyState();   // 启动时按是否有教学班切 空态页 / 课表网格
}

/*
MainWindow::applyCardPalette - 按当前主题给课表模型注入卡片配色盘

Remark:
    主题切换（themeChanged）与数据源更换（setDataStore）后调用；
    明暗切换时卡片底色与文字色随主题盘刷新，保证可读。
*/
void MainWindow::applyCardPalette()
{
    ThemeManager &tm = ThemeManager::instance();
    m_model.setCardPalette(tm.cardBackgrounds(), tm.cardForegrounds(),
                           tm.moreBackground(), tm.moreForeground());
}

/*
MainWindow::applyTopButtonIcons - 按当前主题给顶栏 保存/撤销/重做 上图标

Remark:
    初始建钮与主题切换（themeChanged）时调用。图标用 buttonIcon 带禁用态：
    正常态描边取 baseForeground，禁用态取 disabledForeground（与 QSS 禁用文字同灰），
    撤销/重做不可用（updateUndoActions setEnabled(false)）时由 QIcon 自动落禁用态，
    无需每次开关重建。
*/
void MainWindow::applyTopButtonIcons()
{
    ThemeManager &tm = ThemeManager::instance();
    const QColor fg  = tm.baseForeground();
    const QColor dim = tm.disabledForeground();
    if (m_saveBtn) m_saveBtn->setIcon(themeicon::buttonIcon(QStringLiteral("save"), fg, dim, m_saveBtn));
    if (m_undoBtn) m_undoBtn->setIcon(themeicon::buttonIcon(QStringLiteral("rotate-ccw"), fg, dim, m_undoBtn));
    if (m_redoBtn) m_redoBtn->setIcon(themeicon::buttonIcon(QStringLiteral("rotate-cw"), fg, dim, m_redoBtn));
}

/*
MainWindow::updateEmptyState - 按数据有无切换右栏页面

Remark:
    判据为"是否有教学班"（课表网格只承载教学班排课；仅课程无班也视为空）。
    数据源整体更换 / 删除后 / 撤销重做换源后调用。
*/
void MainWindow::updateEmptyState()
{
    const bool hasData = !m_store.teachingClasses().isEmpty();
    m_stack->setCurrentWidget(hasData ? static_cast<QWidget *>(m_view)
                                      : static_cast<QWidget *>(m_empty));
    refreshFilterVisual();                       // 换源后筛选必重置 → 熄灭「筛选」
    updateErrorBadge(m_store.scheduleFailures().size());  // 快照携带的失败随数据源反映到徽标
}

/*
MainWindow::showStatusMsg - 状态栏纯文本（自动 HTML 转义后写入富文本标签）
*/
void MainWindow::showStatusMsg(const QString &text)
{
    if (m_status)
        m_status->setText(text.toHtmlEscaped());
}

/*
MainWindow::showStatusColored - 状态栏：开头语义词加粗着色 + 其余纯文本
*/
void MainWindow::showStatusColored(const QString &lead, const QColor &leadColor,
                                   const QString &tail)
{
    showStatusHtml(QStringLiteral("<span style=\"color:%1;\"><b>%2</b></span>%3")
                       .arg(leadColor.name(), lead.toHtmlEscaped(), tail.toHtmlEscaped()));
}

/*
MainWindow::showStatusHtml - 状态栏富文本直写（调用方负责转义与自构标记）
*/
void MainWindow::showStatusHtml(const QString &html)
{
    if (m_status)
        m_status->setText(html);
}

/*
MainWindow::refreshFilterVisual - 「筛选」按钮生效态：有生效筛选 → accent 点亮 + 尾缀条件数
*/
void MainWindow::refreshFilterVisual()
{
    if (!m_filterBtn)
        return;
    const ScheduleFilter f = m_model.filter();
    const bool active = !f.isEmpty();
    m_filterBtn->setProperty("active", active);
    m_filterBtn->setText(active
        ? QStringLiteral("筛选 · %1").arg(f.teacherIds.size() + f.classroomIds.size()
                                           + f.courseIds.size())
        : QStringLiteral("筛选"));
    m_filterBtn->style()->unpolish(m_filterBtn);
    m_filterBtn->style()->polish(m_filterBtn);
    m_filterBtn->update();
}

/*
MainWindow::updateErrorBadge - 「排课错误」徽标：失败数 >0 → danger 态 + 计数
*/
void MainWindow::updateErrorBadge(int failedCount)
{
    if (!m_errBtn)
        return;
    const bool danger = failedCount > 0;
    m_errBtn->setProperty("danger", danger);
    m_errBtn->setText(danger
        ? QStringLiteral("排课错误 (%1)").arg(failedCount)
        : QStringLiteral("排课错误"));
    m_errBtn->style()->unpolish(m_errBtn);
    m_errBtn->style()->polish(m_errBtn);
    m_errBtn->update();
}

/*
~MainWindow - 主窗口析构函数，释放 ui 对象

*/
MainWindow::~MainWindow()
{
    delete ui;
}

/*
MainWindow::eventFilter - 主窗空白（无子控件承接）点一下 → 取消残留高亮

Remark:
    过滤对象为构造时创建的 central（仅安装于它）；事件只有落在布局留白 /
    控件间隙等"空白"处才由 central 承接，按钮/课表格等子控件各自处理不入此过滤。
*/
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress)
        clearTransientHighlights();
    return QMainWindow::eventFilter(watched, event);
}

/*
MainWindow::clearTransientHighlights - 取消课表选中格与当前焦点控件的按钮高亮

Remark:
    点击空白后让被点过的课表格/按钮不再"粘着"高亮；焦点环（QSS :focus）
    随 clearFocus 消失，选中格随 clearSelection 消失。
*/
void MainWindow::clearTransientHighlights()
{
    if (m_view)
        m_view->clearSelection();
    if (QWidget *f = QApplication::focusWidget())
        f->clearFocus();
}

/*
MainWindow::closeEvent - 关闭时先收尾后台排课，有数据则自动保存工作区

Parameter：
    event: 关闭事件

Remark:
    后台仍在排课：先经 ScheduleController 取消并等线程退出，再保存，
    避免半截结果落盘 / 后台线程残留。
*/
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_schedule)
        m_schedule->cancelAndWait();
    if (!m_store.teachingClasses().isEmpty())
        writeWorkspace();   // 关窗 flush：防抖未触发时补写自动恢复文件（L1）
    event->accept();
}

/*
MainWindow::memoryPath - 定位记忆文件完整路径

Result:
    QString: <项目根>/.classflow/workspace.dat；找不到项目根返回空串

Remark:
    从可执行文件目录向上寻找含 data/README.md（数据集根标记）的目录，
    兼容 build 与 build/Desktop_* 两种构建目录位置。
*/
QString MainWindow::memoryPath() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("data/README.md"))) {
        if (!dir.cdUp())
            break;
    }
    if (!dir.exists(QStringLiteral("data/README.md")))
        return QString();
    return dir.absolutePath() + QStringLiteral("/.classflow/workspace.dat");
}

/*
MainWindow::writeWorkspace - 写自动恢复文件（L1），目录不存在时自动创建

Remark:
    防抖定时器 / 关窗 / 新建前调用；不弹状态栏，避免频繁自动写入刷屏。
*/
void MainWindow::writeWorkspace()
{
    const QString path = memoryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_store.saveSnapshot(path);
}

/*
MainWindow::persistProjectFile - 手动保存工作区到教务选定的项目文件（L2）

Remark:
    已有当前项目文件 → 直接覆盖写；尚无（未命名）→ 弹路径选择（首次即导出语义）。
    写成功 → 该项目文件成为当前项目文件（跨会话记住），并把刚写入的快照文本记为
    脏标记 ● 的基准，标题/状态栏随之刷新。取消文件对话框不改当前项目文件。
    侧栏「导出快照」不经过这里（那是独立副本，见 onExportClicked）。
*/
void MainWindow::persistProjectFile()
{
    QString path = m_projectFile;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存工作区到文件"), QString(),
            QStringLiteral("快照文件 (*.csv *.dat)"));
        if (path.isEmpty())
            return;
    }
    if (!m_store.saveSnapshot(path)) {
        QMessageBox::warning(this, QStringLiteral("保存"), QStringLiteral("保存失败。"));
        return;
    }
    m_projectFile = path;
    m_projectText = m_store.snapshotText();
    QSettings(QStringLiteral("ClassFlow"), QStringLiteral("ClassFlow"))
        .setValue(QStringLiteral("projectFile/last"), m_projectFile);
    updateWindowTitle();
    showStatusMsg(QStringLiteral("已保存到：%1").arg(QFileInfo(path).fileName()));
}

/*
MainWindow::clearProjectIdentity - 把当前会话重置为「未命名」新文档

Remark:
    新建（从 CSV 导入新工作区）后调用：清当前项目文件并抹掉跨会话记忆，
    使新文档首次 Ctrl+S 走另存为；不触碰已写入的自动恢复文件（旧内容仍在 L1）。
*/
void MainWindow::clearProjectIdentity()
{
    m_projectFile.clear();
    m_projectText.clear();
    QSettings(QStringLiteral("ClassFlow"), QStringLiteral("ClassFlow"))
        .remove(QStringLiteral("projectFile/last"));
    updateWindowTitle();
}

/*
MainWindow::updateWindowTitle - 窗口标题 = 项目文件名（基名）+ 脏标记 ●

Remark:
    ● = 当前内容相对最近一次手动保存/导入到项目文件的文本有差异；尚无项目文件且有
    数据时显示「未命名 ●」，提示该做第一次保存。空数据不装饰，维持原标题。
*/
void MainWindow::updateWindowTitle()
{
    if (m_store.teachingClasses().isEmpty()) {
        setWindowTitle(QStringLiteral("ClassFlow - 自动排课"));
        return;
    }
    const QString name = m_projectFile.isEmpty()
        ? QStringLiteral("未命名")
        : QFileInfo(m_projectFile).fileName();
    const bool dirty = m_store.snapshotText() != m_projectText;
    setWindowTitle(QStringLiteral("ClassFlow · %1%2").arg(
        name, dirty ? QStringLiteral(" ●") : QString()));
}

/*
MainWindow::onNewClicked - 新建工作区：先自动保存原工作区，再载入三份源文件并立即自动排课

Remark:
    排课由 ScheduleController::run 承担；本槽只负责换源与刷新。
*/
void MainWindow::onNewClicked()
{
    // 新建前把原工作区 flush 到自动恢复文件（有数据才需要）
    if (!m_store.teachingClasses().isEmpty())
        writeWorkspace();

    ImportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const DataStore before = m_store;               // 入环捕获：新建=整工作区替换，算一步可撤销
    m_store = dialog.dataStore();                   // 拷贝（隐式共享，代价低）
    m_model.setDataStore(&m_store);
    applyCardPalette();                             // 新数据源用当前主题卡片盘
    updateEmptyState();                             // 新数据源可能为空 → 切空态页
    clearProjectIdentity();                         // 新建 = 「未命名」新文档（L2 身份清零）
    m_undo.push(before);                            // 新建入环；随后 run 换源不重复入环
    updateUndoActions();
    m_schedule->run();                              // 新建后立即自动排课
}

/*
MainWindow::onImportClicked - 从快照文件恢复工作区并刷新课表
*/
void MainWindow::onImportClicked()
{
    SnapshotImportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // loadSnapshot 会原地 clear+重填，捕获必须在它执行之前
    const DataStore before = m_store;               // 入环捕获：导入可能清空重填
    if (!m_store.loadSnapshot(dialog.path())) {
        QMessageBox::warning(this, QStringLiteral("导入"),
                             QStringLiteral("恢复失败：快照文件损坏或格式不正确。"));
        m_undo.push(before);                        // 失败也可能已清库，留一步 Ctrl+Z 找回
        updateUndoActions();
        return;
    }
    m_undo.push(before);                            // 恢复成功，旧工作区可一键回退
    updateUndoActions();
    m_model.setDataStore(&m_store);
    applyCardPalette();                         // 新数据源用当前主题卡片盘
    updateEmptyState();                         // 快照可能为空 → 切空态页
    m_grid->syncWeekSelector();
    m_projectFile = dialog.path();                  // 导入≈打开：该文件成为当前项目文件
    m_projectText = m_store.snapshotText();         // 内容==文件 → ● 熄灭
    QSettings(QStringLiteral("ClassFlow"), QStringLiteral("ClassFlow"))
        .setValue(QStringLiteral("projectFile/last"), m_projectFile);
    updateWindowTitle();
    showStatusMsg(
        QStringLiteral("已恢复：课程 %1 门，教学班 %2 个，教室 %3 间，排课 %4 条")
            .arg(m_store.courses().size())
            .arg(m_store.teachingClasses().size())
            .arg(m_store.classrooms().size())
            .arg(m_store.scheduleEntries().size()));
}

/*
MainWindow::onExportClicked - 「导出快照」：选路径另存一份独立快照副本

Remark:
    与 Ctrl+S/「保存」互不影响：导出不改当前项目文件、不挪 Ctrl+S 落点、不更新
    标题脏标记 —— 副本即当前现场的一份快照，可分发给他人 / 备份。取消对话框不落盘。
*/
void MainWindow::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出快照到文件"), QString(),
        QStringLiteral("快照文件 (*.csv *.dat)"));
    if (path.isEmpty())
        return;
    if (!m_store.saveSnapshot(path)) {
        QMessageBox::warning(this, QStringLiteral("导出快照"),
                             QStringLiteral("导出失败，无法写入该文件。"));
        return;
    }
    showStatusMsg(QStringLiteral("已导出快照：%1").arg(QFileInfo(path).fileName()));
}

/*
MainWindow::onSaveClicked - 手动保存到项目文件（Ctrl+S / 顶部「保存」）

Remark:
    已有当前项目文件 → 直接覆盖；尚无（未命名）→ 首次弹路径（导出语义）。
    保存的是 L2 项目文件，与自动恢复文件（L1）相互独立。
*/
void MainWindow::onSaveClicked()
{
    persistProjectFile();
}

/*
MainWindow::onAddCourseClicked - 「新增课程/教学班」入口：弹双标签表单 → 按当前激活页落库

Remark:
    AddDialog 两个页签相互独立。当前在「新增课程」页 → 只建空课程（不排课、课表不动，
    状态栏提示下一步可加教学班）；当前在「新增教学班」页 → 给所选课程号下的课程增班，
    只对新班做最小排（存量班冻结零扰动插入），排不下由 ScheduleController 内部询问是否
    升格局部重排，本槽不关心 runKind。全程不再触发全量重排。
*/
void MainWindow::onAddCourseClicked()
{
    if (m_store.teachingClasses().isEmpty() && m_store.courses().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("新增课程/教学班"),
                                 QStringLiteral("请先新建或导入数据，再新增课程。"));
        return;
    }
    if (m_schedule->isRunning())
        return;                       // 已在排课（入口按钮本就禁用，此处双保险）

    AddDialog dlg(m_store, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const DataStore before = m_store;               // 入环捕获：一次新增（课程或班）= 一步
    const AddDialog::Result res = dlg.result();
    if (res.mode == AddDialog::Mode::AddCourse) {
        if (!m_store.addCourse(res.course)) {       // 课程号重复会返回 false（弹窗已校验）
            QMessageBox::warning(this, QStringLiteral("新增课程"),
                                 QStringLiteral("保存失败：课程号与现有数据冲突。"));
            return;
        }
        m_undo.push(before);                        // 建空课程成功才入环
        updateUndoActions();
        showStatusMsg(
            QStringLiteral("已新增课程「%1（%2）」，可到「新增教学班」页为其添加班级。")
                .arg(res.course.name, res.course.id));
        return;                       // 只建空课程：不排课、课表无需刷新
    }

    if (!m_store.addTeachingClass(res.klass)) {
        QMessageBox::warning(this, QStringLiteral("新增教学班"),
                             QStringLiteral("保存失败：教学班号与现有数据冲突。"));
        return;
    }
    m_undo.push(before);                            // 增班入环；随后最小排换源不重复入环
    updateUndoActions();
    m_schedule->scheduleNewClass(res.klass.classId);   // 最小排→排不下由控制器内询问升格
}

/*
MainWindow::onDeleteClicked - 「删除课程/教学班」入口：双标签勾选 → 确认摘要 → 落库删除

Remark:
    DeleteDialog 两页各自选择合并成 DeletionRequest：课程页勾的整门课（连其班/排课/锁删）
    与班页勾的单班（删班不删课，删成空课程也保留）。确认摘要把整门删与单班删分开列；
    落库先 removeCourse（连带班删）再 removeTeachingClass，随后原地刷新课表。删除不触发排课。
*/
void MainWindow::onDeleteClicked()
{
    if (m_store.teachingClasses().isEmpty() && m_store.courses().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("删除"),
                                 QStringLiteral("当前没有数据，请先新建或导入。"));
        return;
    }
    if (m_schedule->isRunning())
        return;                       // 已在排课（入口按钮本就禁用，此处双保险）

    DeleteDialog dlg(m_store, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const DeleteDialog::DeletionRequest req = dlg.request();
    if (req.courseIds.isEmpty() && req.classIds.isEmpty()) {
        showStatusMsg(QStringLiteral("未选择任何要删除的课程/教学班。"));
        return;
    }

    // 确认摘要：整门删的课程与单独删的班分开列（属整删课程的班不重复计数）
    QStringList lines;
    for (const QString &cid : req.courseIds) {
        if (const Course *c = m_store.courseById(cid))
            lines << QStringLiteral("整门课「%1（%2）」及全部 %3 个教学班")
                         .arg(c->name, c->id)
                         .arg(courseui::classCountOfCourse(m_store, cid));
    }
    for (const QString &cid : req.classIds) {
        const TeachingClass *tc = m_store.teachingClassById(cid);
        if (!tc || req.courseIds.contains(tc->courseId))
            continue;                 // 属整删课程：已在上面列出
        const Course *c = m_store.courseById(tc->courseId);
        lines << QStringLiteral("教学班 %1（%2）").arg(cid, c ? c->name : cid);
    }

    const auto ans = QMessageBox::warning(this, QStringLiteral("删除"),
        QStringLiteral("将删除以下内容，此操作不可撤销：\n\n%1\n\n"
                       "注：删除教学班不会删除课程（删成空课程也保留）；"
                       "要连课程一起删，请勾选「删除课程」页。\n\n确定删除？")
            .arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    const DataStore before = m_store;               // 入环捕获：确认落库前（实删才 push）
    int removedClasses = 0;
    for (const QString &cid : req.courseIds) {          // 先整门删（连带其班）
        removedClasses += courseui::classCountOfCourse(m_store, cid);
        m_store.removeCourse(cid);
    }
    for (const QString &cid : req.classIds) {           // 再删单独勾选的班
        const TeachingClass *tc = m_store.teachingClassById(cid);
        if (tc && !req.courseIds.contains(tc->courseId)) {
            m_store.removeTeachingClass(cid);
            ++removedClasses;
        }
    }
    if (removedClasses > 0) {                           // 实删才入环，避免空步
        m_undo.push(before);
        updateUndoActions();
    }
    m_model.refresh();                                   // 课表里少掉的卡即时消失
    updateEmptyState();                                  // 删空 → 切回空态引导页
    if (req.courseIds.isEmpty())
        showStatusMsg(
            QStringLiteral("已删除 %1 个教学班。").arg(removedClasses));
    else
        showStatusMsg(
            QStringLiteral("已删除 %1 门课程、共移除 %2 个教学班。")
                .arg(int(req.courseIds.size())).arg(removedClasses));
}

/*
MainWindow::onEditClicked - 「编辑」入口：浏览弹窗改课程/教学班基本信息，必要时局部重排

Remark:
    侧边栏「编辑」开浏览形态的 EditInfoDialog（课程/教学班两页签）。结果经 editui
    助手统一决策落库：课程改名/学院即时生效；换师撞车 → 只登记冲突列表、不应用并弹提示；
    扩容超出当前教室容量 → 确认后本槽对该班 runMovable 换大教室。一次编辑 = 一步入环
    （含仅登记冲突）；roomMove 的局部重排换源不重复入环，随「编辑」一步整体可 Ctrl+Z 回退。
*/
void MainWindow::onEditClicked()
{
    if (m_store.teachingClasses().isEmpty() && m_store.courses().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("编辑信息"),
                                 QStringLiteral("请先新建或导入数据，再编辑课程/教学班。"));
        return;
    }
    if (m_schedule->isRunning())
        return;                       // 已在排课（入口按钮本就禁用，此处双保险）

    EditInfoDialog dlg(m_store, QString(), QString(), this);   // 浏览形态：课程/教学班两页签
    if (dlg.exec() != QDialog::Accepted)
        return;

    const EditApplyResult r = applyAcceptedEdit(dlg.result());
    if (r.changed && r.roomMoveNeeded && !r.classId.isEmpty())
        m_schedule->runMovable(QSet<QString>{r.classId},
                               QStringLiteral("局部重排"));
}

/*
MainWindow::applyAcceptedEdit - 侧栏浏览「编辑」弹窗 OK 后的落库/入环/界面收尾
（该弹窗只产出"想改成什么"，落库统一走 editui；错误列表入口走的是另一套自含落库的
 ClassEditDialog，见 onEditClassRequested）

Parameter：
    res: 编辑弹窗 result()（仅含确有变化的课程 / 教学班实体）

Result:
    EditApplyResult: 落库摘要。changed=本次是否入环；classApplied/classId=教学班字段是否
    已实际改动（换师撞车只登记不算）；roomMoveNeeded=扩容超当前教室容量、需对该班局部重排

Remark:
    换师撞车、扩容确认等决策由 editui 在 apply* 内完成（弹窗本身不落库）；本方法统一做
    落库 + 一步入环 + 刷新课表 + 状态栏 / 提示文案，返回摘要供调用方决定是否局部重排
    （浏览入口=仅扩容换大教室时对该班 runMovable）。
*/
MainWindow::EditApplyResult MainWindow::applyAcceptedEdit(const EditInfoDialog::Result &res)
{
    EditApplyResult r;
    const DataStore before = m_store;               // 入环捕获：一次编辑 = 一步
    QStringList notes;
    bool changed = false;
    if (res.courseEdited) {
        QString msg;
        if (editui::applyCourseEdit(m_store, res.course, &msg)) {
            changed = true;
            if (!msg.isEmpty())
                notes << msg;
        }
    }
    editui::ApplyOutcome out;                        // 默认全 false；未改班时无需 applyClassEdit
    if (res.classEdited) {
        out = editui::applyClassEdit(m_store, res.klass, this);
        if (out.changed) {
            changed = true;
            if (!out.message.isEmpty())
                notes << out.message;
            if (!out.teacherBlocked) {               // 撞车只登记、未实际换师 → 不算"班已改"
                r.classApplied = true;
                r.classId = res.klass.classId;
            }
        }
    }
    r.roomMoveNeeded = out.roomMoveNeeded;
    if (!changed) {                    // 放弃扩容等：无落库（可能有提示文案）
        if (res.classEdited && !out.message.isEmpty())
            QMessageBox::information(this, QStringLiteral("编辑教学班"), out.message);
        return r;
    }

    m_undo.push(before);
    updateUndoActions();
    m_model.refresh();                 // 改名/换师/改人数即时反映到课表卡片
    showStatusMsg(notes.isEmpty()
        ? QStringLiteral("已更新信息。")
        : notes.join(QStringLiteral("；")));
    if (out.teacherBlocked)
        QMessageBox::information(this, QStringLiteral("编辑教学班"),
            QStringLiteral("换师撞车：未应用，已登记到冲突列表。\n\n%1\n\n"
                           "请先把该班时段手动调整到新教师的空闲处，"
                           "再重新执行「编辑→换师」以应用。").arg(out.message));
    return r;
}

/*
MainWindow::onEditClassRequested - 排课错误列表某行「编辑」→ 用课表详情同款双页签编辑窗
（基本信息 + 时间·教室）改该失败班，确有改动后对该班局部重排重试

Parameter：
    classId: 排课错误列表中点击「编辑」的失败教学班 id

Remark:
    复用「点卡片 → 详情 → 编辑」的同一个窗口 ClassEditDialog（基本信息 + 时间·教室双页签），
    落库由弹窗内部统一完成（manual::apply / manual::applyAdd / editui），本槽只做外层
    一步入环 + 刷新 + 摘要，与详情弹窗口径一致。时间·教室页按该班课次分两形态：
      · 有课次（如"拟换师撞车"行）：两页都可用，可先挪时段再保存换师；
      · 0 课次的失败班：「从零排入」——手动把应排的 N 次课填进空位，保存即落库并从
        错误列表移除该班（弹窗内 applyAdd 完成）。该班已就位，**不应再 runMovable**
        （引擎会把刚手排的 N 次课整班清掉重建），仅"确认扩容换大教室"才需要整班重建；
        否则刷新失败徽标、若仍剩其它失败班则续开错误列表继续逐个处理。
    仅当"该班此前没排上且教学班字段确有应用（人数/容量/教师真的改了；撞车只登记不算）"
    才对该失败班发起局部重排：局部排课只对可动班重新判定失败，其它失败班没有条目、
    无法作冻结背景会被引擎略过——故先把重排前的失败明细快照进 m_errRetryPrior，
    onSchedulingDone 收尾时把未参与本次重排的失败记录补回，避免其余失败静默消失。
*/
void MainWindow::onEditClassRequested(const QString &classId)
{
    if (!m_schedule || m_schedule->isRunning())
        return;                       // 排课中不插队（错误列表是模态的，此处双保险）
    const TeachingClass *klass = m_store.teachingClassById(classId);
    if (!klass) {
        QMessageBox::information(this, QStringLiteral("编辑教学班"),
                                 QStringLiteral("教学班 %1 已不存在。").arg(classId));
        return;
    }

    // 锚点 = 该班现存第一条课次的 entryId（无则空）。错误列表没有"被点中的那一节"；
    // 真·失败班零课次，时间·教室页进入「从零排入」形态（见 ClassEditDialog 注释）。
    QString anchorEntryId;
    for (const ScheduleEntry &e : m_store.scheduleEntries()) {
        if (e.teachingClassId == classId) {
            anchorEntryId = e.entryId;
            break;
        }
    }
    const bool wasUnplaced = anchorEntryId.isEmpty();   // 编辑前该班是否真·没排上

    ClassEditDialog dlg(m_store, classId, anchorEntryId, this);
    const DataStore before = m_store;        // 入环捕获：一次编辑 = 一步
    dlg.exec();
    if (!dlg.edited() && !dlg.adjusted())
        return;                              // 取消 / 无改动：不入环、不重排

    m_undo.push(before);
    updateUndoActions();
    m_model.refresh();

    if (dlg.teacherBlocked())
        QMessageBox::information(this, QStringLiteral("编辑教学班"), dlg.blockedMessage());

    // 状态栏摘要：与课表详情口径一致（手动调整优先于基本信息）
    if (dlg.adjusted()) {
        QString text = dlg.adjustSummary();
        if (!m_store.isClassLocked(classId))
            text += QStringLiteral("；如需保留请锁定本班");
        showStatusMsg(text);
    } else if (dlg.edited()) {
        showStatusMsg(dlg.editSummary());
    }

    // 从零排入成功：本班已手动就位、失败项已由 applyAdd 移除——不再 runMovable（引擎
    // 会把刚手排的课次整班清掉重建）。仅"确认扩容换大教室"才需整班重建换大房；否则
    // 刷新失败徽标，若仍剩其它失败班则续开错误列表继续逐个「编辑」处理。
    if (dlg.addedFromScratch()) {
        if (dlg.editRoomMoveNeeded()) {
            m_errRetryMovable.insert(classId);
            m_errRetryPrior = m_store.scheduleFailures();
            m_errRetryPending = true;
            m_schedule->runMovable(m_errRetryMovable, QStringLiteral("局部重排"));
            return;
        }
        const int remain = int(m_store.scheduleFailures().size());
        updateErrorBadge(remain);
        if (remain > 0)
            m_grid->showScheduleErrors();       // 还有别的失败班 → 续看列表
        return;
    }

    // 自动重排仅当：编辑前真·没排上（零条目）且教学班字段确有实际改动（否则改了也
    // 排不上，白跑一遍又弹错误列表）。已有课次的冲突行本身已排上，靠弹窗内挪时段 +
    // 换师解决，无需引擎重排；扩容换大教室对本路径的失败班不适用（无当前教室）。
    if (!wasUnplaced || !dlg.classChanged())
        return;

    m_errRetryMovable.insert(classId);
    m_errRetryPrior = m_store.scheduleFailures();   // 快照：收尾时补回其余失败
    m_errRetryPending = true;
    m_schedule->runMovable(m_errRetryMovable, QStringLiteral("局部重排"));
}

/*
MainWindow::onRescheduleNeeded - 详情内编辑确认扩容换大教室 → 对该班局部重排

Parameter：
    classId: 计划人数被扩过当前教室容量的教学班

Remark:
    详情弹窗关闭（exec 返回）后才触发；runMovable 只排该班，其余班冻结原样保留，
    与手动调整/新增最小排同基建；换源后 onSchedulingDone 汇总（不重复入环）。
*/
void MainWindow::onRescheduleNeeded(const QString &classId)
{
    if (!m_schedule || m_schedule->isRunning())
        return;
    m_schedule->runMovable(QSet<QString>{classId}, QStringLiteral("局部重排"));
}

/*
MainWindow::onLockClicked - 「锁定」入口：弹锁定勾选弹窗，OK 仅写回锁定集并刷新锁标

Remark:
    不触发排课：真正的排课由「自动排课」发起（ScheduleController::run 内部按锁定集
    把已锁有课班冻结为背景、只重排其余班）。本槽只维护 m_store 的锁定集并即时刷新
    课表锁标，让用户看到哪些班将在下次自动排课时被原样保留。
*/
void MainWindow::onLockClicked()
{
    if (m_store.teachingClasses().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("锁定"),
                                 QStringLiteral("请先新建或导入数据。"));
        return;
    }

    LockDialog dlg(m_store, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QSet<QString> next = dlg.selectedLocked();
    if (next == m_store.lockedClassIds())
        return;                                        // 净零变化不入环（避免空步）
    const DataStore before = m_store;                  // 入环捕获：锁定集整体替换
    m_store.setLockedClasses(next);
    m_undo.push(before);
    updateUndoActions();
    m_model.refresh();                     // 即时更新卡片锁标（不动筛选/周次）
    showStatusMsg(
        QStringLiteral("已更新锁定。点「自动排课」将保留已锁定的班，只重排其余班。"));
}

/*
MainWindow::onSchedulingDone - 排课结束（成功/失败/取消）后的界面收尾

Parameter：
    ok: 排课是否全部成功
    scheduledCount: 成功排出的条目数
    failedCount: 失败的教学班数
    aborted: 是否被取消（此时未换源，保留旧课表）

Remark:
    换源与刷新已由 ScheduleController 完成（含保留筛选的原地 refresh）；本槽只刷主题卡色、
    对齐周选择器、写状态栏；失败时调 TimetableController 弹出错误列表。
    文案用 ScheduleController::runKind（自动排课 / 新增教学班最小排 / 局部重排 之一）。
    升格询问（新增教学班排不下 → 局部重排）已收进 ScheduleController 内部，
    此处不再按 runKind 做控制流分支，runKind 仅用于文案。
*/
void MainWindow::onSchedulingDone(bool ok, int scheduledCount, int failedCount,
                                  bool aborted)
{
    // 自动排课入环提交：pending 只在中转槽 onRunSchedulingClicked 里置位，正常一槽对应一次
    // scheduleDone。取消（aborted）不换源、不成一步，不 push；非取消已由 onWorkerFinished
    // 换过源，算一步。新建/新增班自带 push、走 pending=false 路径，绝不 double-push。
    if (m_schedPending) {
        if (!aborted)
            m_undo.push(m_schedBefore);
        m_schedPending = false;
        m_schedBefore = DataStore();
        updateUndoActions();
    }

    // 错误列表「编辑」→局部重排的收尾：取走现场并清 pending（取消时未换源，旧失败明细
    // 原样还在，无需补回；非取消见下方 merge）。
    const bool errRetry = m_errRetryPending;
    const QSet<QString> retryMovable = m_errRetryMovable;
    const QVector<ScheduleFailure> retryPrior = m_errRetryPrior;
    m_errRetryPending = false;
    m_errRetryMovable.clear();
    m_errRetryPrior.clear();

    const QString kind = m_schedule ? m_schedule->runKind() : QStringLiteral("排课");
    if (aborted) {
        showStatusMsg(QStringLiteral("已取消%1，保留原课表").arg(kind));
        return;
    }

    // 局部排课只对可动班重新判定失败；其它失败班没有排课条目、无法作冻结背景，会被引擎
    // 略过。错误列表编辑后的重排只把被编辑班放进可动集，因此要把"未参与本次重排的班"
    // 在重排前仍成立的失败记录补回，避免其余失败从错误列表静默消失；ok 同步重算。
    if (errRetry) {
        QVector<ScheduleFailure> merged = m_store.scheduleFailures();
        for (const ScheduleFailure &f : retryPrior)
            if (!retryMovable.contains(f.classId))
                merged.append(f);
        if (merged.size() != int(m_store.scheduleFailures().size()))
            m_store.setScheduleFailures(merged);
        failedCount = int(merged.size());
        ok = (failedCount == 0);
    }

    applyCardPalette();               // 新数据源用当前主题卡片盘
    m_grid->syncWeekSelector();       // 新周数对齐周选择器范围
    updateErrorBadge(failedCount);    // 非取消：按本次失败数刷新 danger 徽标

    const QColor accent = ThemeManager::instance().accentColor();
    const QColor danger = ThemeManager::instance().dangerColor();
    if (ok) {
        showStatusHtml(QStringLiteral("%1完成：<span style=\"color:%2;\"><b>成功 %3 条</b></span>。")
                           .arg(kind.toHtmlEscaped(), accent.name()).arg(scheduledCount));
        return;
    }

    showStatusHtml(QStringLiteral("%1完成：<span style=\"color:%2;\"><b>成功 %3 条</b></span>，"
                                  "<span style=\"color:%4;\"><b>失败 %5 个教学班</b></span>"
                                  "（详见错误列表）")
                       .arg(kind.toHtmlEscaped(), accent.name()).arg(scheduledCount)
                       .arg(danger.name()).arg(failedCount));
    m_grid->showScheduleErrors();
}

/*
MainWindow::onUndoClicked - 撤销一步：整体还原动作前状态（Ctrl+Z / 顶栏「撤销」）

Remark:
    按钮在排课 busy 期间禁用，此处 isRunning 只作快捷键兜底。
*/
void MainWindow::onUndoClicked()
{
    if (m_schedule && m_schedule->isRunning())
        return;
    DataStore target;
    if (m_undo.undo(m_store, &target))           // 把当前态存进 redo，弹回恢复目标
        restoreStore(target, QStringLiteral("已撤销上一步操作"));
}

/*
MainWindow::onRedoClicked - 重做一步：沿 redo 分支恢复动作后状态（Ctrl+Shift+Z / 顶栏「重做」）
*/
void MainWindow::onRedoClicked()
{
    if (m_schedule && m_schedule->isRunning())
        return;
    DataStore target;
    if (m_undo.redo(m_store, &target))
        restoreStore(target, QStringLiteral("已重做上一步操作"));
}

/*
MainWindow::restoreStore - undo/redo 共用换源：把历史态整体赋回并刷新界面

Parameter：
    target: 待恢复的 DataStore 历史态
    status: 恢复后写入状态栏的文案

Remark:
    必须原地赋值 m_store = target（保住 DataStore 地址，排课/控制器持指针不失效）；
    setDataStore 重置筛选为空（历史态可能缺当前筛选引用的对象），随后刷主题卡色与
    周选择器范围。Ctrl+S/导出/关闭自动保存只读当前 m_store，恢复后自然持久化新态。
*/
void MainWindow::restoreStore(const DataStore &target, const QString &status)
{
    m_store = target;
    m_model.setDataStore(&m_store);              // 重置筛选
    applyCardPalette();
    updateEmptyState();                          // 历史态可能无班 → 切空态页
    m_grid->syncWeekSelector();                  // 周数随历史态变化时钳制范围
    updateUndoActions();
    showStatusMsg(status);
}

/*
MainWindow::updateUndoActions - 按缓冲可用步数刷新撤销/重做按钮可用态

Remark:
    入环 push、undo/redo 换源、详情弹窗变更后调用；启动恢复后的初态两钮禁用。
*/
void MainWindow::updateUndoActions()
{
    m_undoBtn->setEnabled(m_undo.canUndo());
    m_redoBtn->setEnabled(m_undo.canRedo());
}

/*
MainWindow::onRunSchedulingClicked - 「自动排课」中转槽：捕获动作前状态入环后交给 ScheduleController

Remark:
    run 在无数据 / 无可动班时早退且不发 scheduleDone，若直接 pending=true 会悬挂；
    这里先用与 run 内早退相同的判据前置拦截（让其内部弹提示并返回，不入环）。
    通过守卫后 O(1) 捕获 before 并置 pending，成功换源在 onSchedulingDone 提交（push 一次）；
    取消/早退路径不产生空步。
*/
void MainWindow::onRunSchedulingClicked()
{
    if (!m_schedule || m_schedule->isRunning())
        return;
    if (m_store.teachingClasses().isEmpty()
        || m_schedule->unlockedMovableClasses().isEmpty()) {
        m_schedule->run();                  // 内部弹提示并早退，不入环
        return;
    }
    m_schedBefore = m_store;                // O(1)：worker 只排副本，期间 m_store 不动
    m_schedPending = true;
    m_schedule->run();
}
