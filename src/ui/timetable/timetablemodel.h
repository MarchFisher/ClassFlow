#ifndef TIMETABLEMODEL_H
#define TIMETABLEMODEL_H

#include <QAbstractTableModel>
#include <QColor>
#include <QHash>
#include <QString>
#include <QVector>

#include "core/filter/schedulefilter.h"
#include "core/models/models.h"

class DataStore;

// 课表网格模型：行 = 节次（由作息表决定），列 = 星期（周一..周日）。
// 横看周几、竖看节次，符合习惯；只显示周范围覆盖「当前周」的条目。
// 单元格按 (星期, 节次) 查 ScheduleEntry，显示 "课程名\n教室"，Tooltip 带周范围。
// 卡片视觉：同一课程的所有格子取同一底色（同课同色），多班重合时取第一条颜色。
class TimetableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TimetableModel(QObject *parent = nullptr);

    // 绑定数据仓库并刷新网格尺寸与单元格内容（会重置筛选为空）
    void setDataStore(const DataStore *store);

    // 同数据源原地刷新（数据已变、指针未变时用：排课换源/锁定后），保留当前筛选与周次
    void refresh();

    // 教学班 id 是否被锁定（供卡片锁标 / 详情弹窗按钮态）
    bool isClassLocked(const QString &classId) const;

    // 设置当前显示周次，切换周次会重建单元格内容
    void setCurrentWeek(int week);

    // 设置筛选条件（空 = 不限）；换数据源时筛选会被重置
    void setFilter(const ScheduleFilter &filter);
    ScheduleFilter filter() const;

    // 注入当前主题的卡片配色盘（底色盘 + 文字色盘）+「更多」折叠卡位的中性底色/文字色；
    // 明暗切换时由 MainWindow 调用刷新
    void setCardPalette(const QVector<QColor> &backgrounds, const QVector<QColor> &foregrounds,
                        const QColor &moreBackground, const QColor &moreForeground);

    // 取某 (星期, 节次) 在当前周内的条目（空则无课）
    QVector<ScheduleEntry> entriesAtCell(int day, int section) const;

    // 教学班 id → 课程名（供点击弹窗 / delegate 卡上显示）
    QString courseNameOfClass(const QString &classId) const;

    // 教学班 id → 卡片底色 / 文字色（供 delegate 逐卡取色；同课同色，空卡片盘回退默认）
    QColor cardBackground(const QString &classId) const;
    QColor cardForeground(const QString &classId) const;

    // 「更多 +N」折叠卡位的中性底色 / 文字色（供 delegate 画折叠卡位）
    QColor moreBackground() const;
    QColor moreForeground() const;

    // 教师 id → 姓名（供 delegate 卡上第二行显示；未指定返回"未安排"，缺名但 id 非空回退 id）
    QString teacherNameOf(const QString &teacherId) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    void rebuildCells();                             // 按当前周重建 (星期,节次) → 条目
    void assignCourseColors();                       // 按卡片盘重建 教学班→底色/文字色 映射

    const DataStore *m_store = nullptr;
    int m_currentWeek = 1;                           // 当前显示周次
    int m_maxSection = 0;                            // 列数 = 节数
    ScheduleFilter m_filter;                         // 当前筛选（空 = 不限）
    QHash<QString, QVector<ScheduleEntry>> m_byCell; // "星期|节次" → 条目
    QHash<QString, QString> m_courseOfClass;         // 教学班 id → 课程名
    QHash<QString, QString> m_courseOfClassId;       // 教学班 id → 课程 id（供筛选）
    QHash<QString, QString> m_teacherName;           // 教师 id → 姓名（课格子第二行显示）
    QHash<QString, QColor> m_courseColor;            // 教学班 id → 同课同色底色
    QHash<QString, QColor> m_courseFgColor;          // 教学班 id → 卡片文字色
    QVector<QColor> m_cardBg;                        // 当前主题卡片底色盘（轮换取用）
    QVector<QColor> m_cardFg;                        // 当前主题卡片文字色盘
    QColor m_moreBg = QColor(0xD0D0D0);              // 「更多」卡位中性底色（默认亮色浅灰）
    QColor m_moreFg = QColor(0x1A1A1A);              // 「更多」卡位文字色（默认亮色前景）
};

#endif // TIMETABLEMODEL_H
