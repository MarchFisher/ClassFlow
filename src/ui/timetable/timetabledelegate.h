#ifndef TIMETABLEDELEGATE_H
#define TIMETABLEDELEGATE_H

#include <QStyledItemDelegate>

class TimetableModel;
struct ScheduleEntry;

// 课表卡片自绘 Delegate：把"整格拼文本 + 整格单色"改为逐格画课程卡片。
// 一格一门课一张卡片（各自课程色/圆角/两行文本）；空白格交给基类画表底。
// 同格课程过多时折叠：≥4 门只画前 2 张完整卡，余下收进一张「更多 +N」卡位。
// layoutCards 与 entryAt 共用同一套几何，保证"画在哪 = 点在哪"。
class TimetableDelegate : public QStyledItemDelegate
{
    Q_OBJECT   // qobject_cast（视图命中时判型）需要 Q_OBJECT

public:
    // 命中结果：NoHit = 未点中任何卡；MoreHit = 点中「更多」卡位（由视图转发处理）
    enum HitResult { NoHit = -1, MoreHit = -2 };

    explicit TimetableDelegate(QObject *parent = nullptr);

    // n 门课一格里显示几张完整课程卡：≤3 全显示；>3 只显示前 2 张（第 3 张起折叠）
    static int visibleCourseCount(int total);

    // 是否有被折叠的课（决定是否画「更多」卡位）
    static bool hasOverflow(int total);

    // 一格里实际绘制几个视觉卡位 = 完整卡数 +（有折叠时 1 张「更多」）
    static int slotCount(int total);

    // 单元格内纵向均分 slotCount(n) 个视觉卡位的几何（单源布局）
    static QVector<QRect> layoutCards(const QRect &cellRect, int n);

    // 绘制单元格：空白格走基类；有课格画完整卡 + 可选「更多」卡位/选中描边
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    // 命中：命中完整卡返回其条目序（0 起）；命中「更多」返回 MoreHit；未点中返回 NoHit
    int entryAt(const QRect &cellRect, const QModelIndex &index,
                const QPoint &cellLocalPos) const;

private:
    void paintCard(QPainter *painter, const QRect &card, const QStyleOptionViewItem &option,
                   const TimetableModel *model, const ScheduleEntry &entry) const;
    void paintMoreCard(QPainter *painter, const QRect &card,
                       const QStyleOptionViewItem &option, const TimetableModel *model,
                       int moreCount) const;
};

#endif // TIMETABLEDELEGATE_H
