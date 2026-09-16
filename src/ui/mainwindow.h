#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QMainWindow>

#include "core/store/datastore.h"
#include "core/store/undobuffer.h"
#include "ui/dialog/editinfodialog.h"
#include "ui/timetable/timetablemodel.h"

QT_BEGIN_NAMESPACE
class QEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QCloseEvent;
class QTimer;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class TimetableView;
class EmptyState;
class ScheduleController;
class TimetableController;

// 主窗口：窗口拼装 + 文件级动作（新建/导入快照/保存/导出快照）的编排。
// 保存分两层：L1 自动恢复（改动防抖写 .classflow/workspace.dat）与
// L2 手动保存到教务选定的项目文件（首次=选路径即导出语义，路径跨会话记住）。
// 侧栏「导出快照」是独立副本：另存一份现场，不设为当前项目文件。
// 排课的生命周期交给 ScheduleController，读课表/点卡/筛选/错误查看交给
// TimetableController；本类只负责把两者接到按钮与视图信号上，并做状态栏文案。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;   // 关闭时自动保存工作区
    bool eventFilter(QObject *watched, QEvent *event) override;  // 空白处点击 → 取消残留高亮

private slots:
    void onNewClicked();       // 从三份源文件新建，新建后立即自动排课
    void onImportClicked();    // 从快照恢复工作区（导入成功即设为当前项目文件）
    void onExportClicked();    // 导出快照：选路径另存一份副本（不改当前项目文件）
    void onSaveClicked();      // 手动保存到项目文件（Ctrl+S；无目标首次走另存为）
    void onAddCourseClicked(); // 新增课程/教学班：双标签表单→按激活页建空课 或 增班后最小排
    void onDeleteClicked();    // 删除课程/教学班：双标签勾选→确认摘要→落库删除并刷新课表
    void onEditClicked();      // 编辑课程/教学班信息：浏览弹窗→editui 落库→必要时对该班局部重排
    void onEditClassRequested(const QString &classId);  // 排课错误列表「编辑」→钉选该班编辑并重试
    void onRescheduleNeeded(const QString &classId);  // 详情编辑确认扩容换大教室→对该班局部重排
    void onLockClicked();   // 锁定管理：弹窗 OK 后仅写回锁定集并刷新锁标（不排课）
    void onUndoClicked();   // 撤销一步（Ctrl+Z / 顶栏撤销按钮）
    void onRedoClicked();   // 重做一步（Ctrl+Shift+Z / 顶栏重做按钮）
    void onRunSchedulingClicked();  // 自动排课中转：捕获动作前状态入环后交给 ScheduleController::run
    void onSchedulingDone(bool ok, int scheduledCount, int failedCount,
                          bool aborted);   // 排课结束：刷主题卡色 + 状态栏/错误提示

private:
    void applyCardPalette();             // 按当前主题给课表模型注入卡片配色盘
    void applyTopButtonIcons();          // 按当前主题刷新顶栏 保存/撤销/重做 图标（含禁用态）
    void updateEmptyState();             // 无教学班 → 显示空态引导页，否则显示课表网格
    void showStatusMsg(const QString &text);        // 状态栏纯文本（自动 HTML 转义）
    void showStatusColored(const QString &lead, const QColor &leadColor,
                           const QString &tail);    // 状态栏：首词着色（成功 accent / 失败 danger）
    void showStatusHtml(const QString &html);       // 状态栏富文本（调用方负责转义/自构标记）
    void refreshFilterVisual();          // 「筛选」按钮：有生效筛选 → accent 点亮 + 尾缀条件数
    void updateErrorBadge(int failedCount);  // 「排课错误」：失败>0 → danger 态 + 计数
    void clearTransientHighlights();     // 取消课表选中格与当前按钮的焦点高亮（点空白时）
    QString memoryPath() const;          // 自动恢复文件（workspace.dat）完整路径
    void writeWorkspace();               // 写自动恢复文件 L1（目录不存在自动创建；不弹状态栏）
    void persistProjectFile();         // 手动保存到项目文件（无目标时弹路径选择）
    void clearProjectIdentity();         // 新建后：清当前项目文件与记忆 →「未命名」新文档
    void updateWindowTitle();            // 标题 = 项目文件名（基名）+ 脏标记 ●
    void restoreStore(const DataStore &target,
                      const QString &status);  // undo/redo 共用：整体换源并刷新
    void updateUndoActions();            // 按 canUndo/canRedo 刷新撤销/重做按钮可用态

    // 一次编辑弹窗 OK 后的落库摘要（供浏览/错误列表两入口按各自策略决定是否局部重排）
    struct EditApplyResult {
        bool changed = false;          // 确有落库改动（整步入环的前提）
        bool classApplied = false;     // 教学班字段已实际应用（教师/人数/容量改到）
        bool roomMoveNeeded = false;   // 扩容超当前教室容量：需对该班局部重排换大教室
        QString classId;               // classApplied 时的教学班 id
    };
    EditApplyResult applyAcceptedEdit(const EditInfoDialog::Result &res);  // 编辑弹窗 OK 后公共落库/入环/提示

    Ui::MainWindow *ui;
    DataStore      m_store;        // 内存数据仓库（单一数据源）
    TimetableModel m_model;        // 课表网格模型
    TimetableView *m_view = nullptr;
    QStackedWidget *m_stack = nullptr;   // 右栏切换容器：空态页 / 课表网格
    EmptyState    *m_empty = nullptr;    // 空态引导页（无教学班时显示）
    QLabel        *m_status = nullptr;   // 状态栏富文本标签（纯文本 / 首词着色都走这里）
    QSpinBox      *m_weekSpin = nullptr;  // 周选择器（1..semesterWeeks）
    QPushButton   *m_errBtn = nullptr;   // 「排课错误」入口（失败>0 时切 danger 态）
    QPushButton   *m_filterBtn = nullptr; // 「筛选」入口（有生效筛选时 accent 点亮）
    QPushButton   *m_lockBtn = nullptr;  // 「锁定」入口：仅维护锁定集（排课期间禁用）
    QPushButton   *m_editBtn = nullptr;  // 「编辑」入口：改课程/教学班基本信息（排课期间禁用）
    QPushButton   *m_saveBtn = nullptr;  // 「保存」入口（图标钮；无目标项目文件时首次走另存为）
    QPushButton   *m_undoBtn = nullptr;  // 「撤销」入口（图标钮，排课期间禁用）
    QPushButton   *m_redoBtn = nullptr;  // 「重做」入口（图标钮，排课期间禁用）
    UndoBuffer    m_undo;                // 会话级撤销/重做缓冲（动作前状态快照环）
    DataStore     m_schedBefore;         // 自动排课入环用的动作前状态（pending 提交）
    bool          m_schedPending = false;  // 本次自动排课是否已捕获待提交的 before
    bool          m_errRetryPending = false;  // 错误列表「编辑」→局部重排已发出，待 onSchedulingDone 收尾
    QSet<QString> m_errRetryMovable;     // 该重排的可动集（被编辑且排课相关的失败班）
    QVector<ScheduleFailure> m_errRetryPrior;  // 重排前失败明细：收尾时把未参与重排的班补回
    QTimer       *m_autoTimer = nullptr; // 自动恢复防抖定时器（1.5s；内容变才写 workspace.dat）
    QString       m_projectFile;         // 当前项目文件路径（空 = 未命名）
    QString       m_projectText;         // 最近手动保存/导入的项目文件快照文本（脏标记 ● 基准）
    QString       m_autoText;            // 最近写入 workspace.dat 的快照文本（避免空写）
    ScheduleController *m_schedule = nullptr;   // 排课生命周期（后台排课 + 换源）
    TimetableController *m_grid = nullptr;      // 课表交互（周/筛选/点卡/错误查看）
};

#endif // MAINWINDOW_H
