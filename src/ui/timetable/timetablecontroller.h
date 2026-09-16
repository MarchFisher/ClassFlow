#ifndef TIMETABLECONTROLLER_H
#define TIMETABLECONTROLLER_H

#include <functional>
#include <utility>

#include <QObject>

class QMainWindow;
class QSpinBox;
class DataStore;
class TimetableModel;
class UndoBuffer;
struct ScheduleEntry;

// 课表交互控制器：把「读当前课表、点卡片/点更多」的查看类动作从主窗口抽出来。
// 负责：周次切换 / 周选择器范围对齐 / 筛选应用 / 点卡直达详情 / 点「更多」列被折叠课 /
// 查看排课错误。常态只读主数据源与课表模型（引用）；仅课程详情弹窗内的"锁定/解锁本班"
// 会经本类改数据源锁定集（非只读入口），改后原地刷新课表。
class TimetableController : public QObject
{
    Q_OBJECT

public:
    // store: 主数据源（只读）；model: 课表模型；weekSpin: 顶栏周选择器；
    // undo: 会话级撤销缓冲（详情弹窗整段一步入环用，MainWindow 注入；可空）
    // host: 对话框 / 菜单父级（应为 MainWindow）
    TimetableController(DataStore &store, TimetableModel &model, QSpinBox *weekSpin,
                        UndoBuffer *undo, QMainWindow *host, QObject *parent = nullptr);

    // 状态栏文字出口（MainWindow 注入；用于把文案接到富文本 QLabel）。
    // 未注入时退化为旧的宿主 statusBar 直写，保持可独立测试。
    void setStatusSink(std::function<void(const QString &text)> sink);

signals:
    void undoStackChanged();           // 详情弹窗内发生撤销级变更 → MainWindow 刷新按钮
    // 详情内「编辑信息」确认扩容换大教室 → MainWindow 对该教学班做一次局部重排
    void rescheduleNeeded(const QString &classId);
    // 排课错误列表里点了某行「编辑」→ MainWindow 钉选该教学班编辑并局部重排重试
    void editClassRequested(const QString &classId);

public slots:
    void setWeek(int week);            // 周次切换 → 刷新课表模型
    void openFilter();                 // 打开筛选弹窗并应用
    void showCourseDetailAt(int day, int section, int ordinal); // 点卡片 → 直达该课详情
    void showMoreCourses(int day, int section); // 点「更多」→ 菜单列被折叠课
    void showScheduleErrors();         // 查看排课错误列表（无错误时提示）

public:
    void syncWeekSelector();           // 周选择器范围与当前学期周数对齐

private:
    void showCourseDetail(const ScheduleEntry &entry); // 弹课程详情窗口

    void reportStatus(const QString &text);   // 状态栏写入：有 sink 走 sink，否则直写宿主 statusBar

    DataStore &m_store;
    TimetableModel &m_model;
    QSpinBox *m_weekSpin;
    UndoBuffer *m_undo;
    QMainWindow *m_host;
    std::function<void(const QString &)> m_statusSink;   // 状态栏出口（可空）
};

#endif // TIMETABLECONTROLLER_H
