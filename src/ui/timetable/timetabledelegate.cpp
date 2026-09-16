/**
 * 文件职责：课表卡片自绘 Delegate 实现（QStyledItemDelegate 子类）。
 * 取代默认 delegate 的"整格拼文本平铺"：有课格按条目数纵向均分画卡片，
 * 每张卡片一门课（课程色、课程名、教师·教室），选中格描强调边；
 * 空白格直通基类画表底。
 * 同格课程过多时折叠（防卡过矮难点/难读）：≤3 门全显示为完整卡；
 * >3 门只画前 2 张完整卡 + 1 张「更多 +N」卡位。可见卡数/几何的纯函数
 * visibleCourseCount/hasOverflow/slotCount 与命中 entryAt 共用同一套。
 */

#include "timetabledelegate.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyleOptionViewItem>

#include "timetablemodel.h"
#include "ui/theme/theme.h"

namespace {
const int kMarginH = 2;     // 卡片左右留白
const int kGap     = 2;     // 卡片纵向间距
const int kMarginV = 2;     // 卡片上下留白
const int kPadH    = 4;     // 卡内文字横向内边距
const int kPadV    = 2;     // 卡内文字纵向内边距
const int kRadius  = 4;     // 卡片圆角半径

const int kShowAllUpTo = 3; // 一格 ≤3 门课：全部画成完整课程卡
const int kShowOnOverflow = 2; // 一格 >3 门课：只画前 2 张完整卡，第 3 张起折叠

const int kLockW = 7;      // 锁定小锁标：锁身宽
const int kLockH = 6;      // 锁定小锁标：锁身高
const int kLockMargin = 3; // 锁标距卡片右上角的边距

/*
drawLockBadge - 在卡片右上角内侧画一枚小锁标（表示该教学班已锁定）

Parameter：
    p: 绘制器
    card: 卡片矩形（锁标定位其右上角）
    color: 锁标颜色（用卡片文字色，保证与底色对比可读）

Remark:
    锁体 = 圆角小矩形 + 上方半圆锁环，比 emoji 字形稳定（不依赖字体回退）；
    锁标不参与命中（纯装饰，标注"此班在局部重排时原样保留"）。
*/
void drawLockBadge(QPainter *p, const QRect &card, const QColor &color)
{
    const QRect body(card.right() - kLockMargin - kLockW,
                     card.top() + kLockMargin, kLockW, kLockH);
    QColor c = color;
    c.setAlpha(235);

    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawRoundedRect(body, 1, 1);                 // 锁身

    QPen pen(c, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawArc(body.adjusted(1, -3, -1, 1), 0, 180 * 16);   // 锁环（半圆）
    p->restore();
}

} // namespace

/*
TimetableDelegate::visibleCourseCount - 一格 n 门课画几张完整课程卡

Parameter：
    total: 该格排课条目总数（≥0）

Result:
    int: 完整课程卡张数。total ≤ 3 返回 total；否则返回 kShowOnOverflow（2）

Remark:
    >3 门时"第 3 张起折叠"，即只露出前 2 张完整卡。
*/
int TimetableDelegate::visibleCourseCount(int total)
{
    if (total <= 0)
        return 0;
    return total <= kShowAllUpTo ? total : kShowOnOverflow;
}

/*
TimetableDelegate::hasOverflow - 是否有被折叠进「更多」的课

Parameter：
    total: 该格排课条目总数（≥0）

Result:
    bool: 存在未画成完整卡的课（即 total > visibleCourseCount(total)）
*/
bool TimetableDelegate::hasOverflow(int total)
{
    return total > visibleCourseCount(total);
}

/*
TimetableDelegate::slotCount - 一格实际绘制几个视觉卡位

Parameter：
    total: 该格排课条目总数（≥0）

Result:
    int: 视觉卡位数 = 完整课程卡数 +（有折叠时 1 张「更多」卡位）
*/
int TimetableDelegate::slotCount(int total)
{
    return visibleCourseCount(total) + (hasOverflow(total) ? 1 : 0);
}

/*
TimetableDelegate - 构造函数

Parameter：
    parent: 父对象指针，默认 nullptr
*/
TimetableDelegate::TimetableDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

/*
TimetableDelegate::layoutCards - 单元格内纵向均分 slotCount(n) 个视觉卡位的几何

Parameter：
    cellRect: 单元格矩形（paint 传 option.rect，命中传 visualRect）
    n: 该格排课条目总数（≥1）

Result:
    QVector<QRect>: 绘制视觉卡位的矩形序列（含四周留白与卡间距）。
    前 visibleCourseCount(n) 个为完整课程卡，其后若有 1 个即为「更多」卡位；
    paint 与命中共用，是"画在哪 = 点在哪"的单源几何。

Remark:
    卡位高度按实际绘制数均分，保证一格最多 slotCount(n) ≤ 3 个视觉块。
*/
QVector<QRect> TimetableDelegate::layoutCards(const QRect &cellRect, int n)
{
    QVector<QRect> cards;
    if (n <= 0)
        return cards;
    const int count = slotCount(n);
    const int usable = qMax(1, cellRect.height() - 2 * kMarginV - (count - 1) * kGap);
    const int cardH = qMax(1, usable / count);
    cards.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int y = cellRect.top() + kMarginV + i * (cardH + kGap);
        cards.append(QRect(cellRect.left() + kMarginH, y,
                           cellRect.width() - 2 * kMarginH, cardH));
    }
    return cards;
}

/*
TimetableDelegate::paint - 绘制单元格

Parameter：
    painter: 绘制器（进入时已 clip 到单元格）
    option: 视图传入的绘制选项（含 rect / palette / state）
    index: 单元格索引（行 = 节次，列 = 星期）

Remark:
    有课格：前 visibleCourseCount(n) 张画完整课程卡（各自课程底色/文字色），
    余下收进一张「更多 +N」卡位（需折叠时）；两者共用 slotCount 均分几何。
    选中态以强调色描整格边框（默认 delegate 的选中底被替换后自行表达）；
    空白格交回基类（表底/行列表头现状由 QSS + 默认绘制负责）。
*/
void TimetableDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    const auto *model = qobject_cast<const TimetableModel *>(index.model());
    const QVector<ScheduleEntry> entries =
        model ? model->entriesAtCell(index.column() + 1, index.row() + 1)
              : QVector<ScheduleEntry>();
    if (!model || entries.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const int n = entries.size();
    const int visible = visibleCourseCount(n);
    const QVector<QRect> cards = layoutCards(option.rect, n);
    for (int i = 0; i < visible; ++i)
        paintCard(painter, cards.at(i), option, model, entries.at(i));
    if (visible < cards.size())
        paintMoreCard(painter, cards.last(), option, model, n - visible);

    // 选中态：以 accent 实色描整格边框。这里显式取主题色而非 QPalette.highlight——
    // 后者恒为默认蓝，切换 accent 时不会跟随。
    if (option.state & QStyle::State_Selected) {
        const QColor accent = ThemeManager::instance().accentColor();
        painter->setPen(QPen(accent, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
    }
    painter->restore();
}

/*
TimetableDelegate::paintCard - 绘制单张课程卡片

Parameter：
    painter: 绘制器
    card: 卡片矩形
    option: 绘制选项（取字体 / 调色板）
    model: 课表模型（查课程名/教师名/配色）
    entry: 该卡片对应的排课条目

Remark:
    底色圆角 + 课程名在上、教师·教室在下，过长横向 elide；
    卡高不足以排两行时只画课程名（纵向小格仍可辨认）。
*/
void TimetableDelegate::paintCard(QPainter *painter, const QRect &card,
                                  const QStyleOptionViewItem &option,
                                  const TimetableModel *model,
                                  const ScheduleEntry &entry) const
{
    // 底色圆角矩形（同课同色，由模型访问器实时取）
    painter->setPen(Qt::NoPen);
    painter->setBrush(model->cardBackground(entry.teachingClassId));
    painter->drawRoundedRect(card, kRadius, kRadius);

    // 两行文本：课程名 / 教师·教室
    const QString name = model->courseNameOfClass(entry.teachingClassId);
    const QString sub  = model->teacherNameOf(entry.teacherId)
                         + QStringLiteral(" · ") + entry.classroomId;

    const QFontMetrics fm(option.font);
    const int lineH = fm.height();
    const QRect inner = card.adjusted(kPadH, 0, -kPadH, 0);
    painter->setFont(option.font);
    painter->setPen(model->cardForeground(entry.teachingClassId));

    if (card.height() >= 2 * lineH + 2 * kPadV + 1) {
        const QRect line1(inner.left(), card.top() + kPadV, inner.width(), lineH);
        const QRect line2(inner.left(), card.top() + kPadV + lineH, inner.width(), lineH);
        painter->drawText(line1, Qt::AlignCenter,
                          fm.elidedText(name, Qt::ElideRight, line1.width()));
        painter->drawText(line2, Qt::AlignCenter,
                          fm.elidedText(sub, Qt::ElideRight, line2.width()));
    } else {
        painter->drawText(inner, Qt::AlignCenter,
                          fm.elidedText(name, Qt::ElideRight, inner.width()));
    }

    // 已锁定教学班：右上角画锁标（窄卡空间不足时省略，避免遮挡文字）
    if (card.width() >= 60
        && model->isClassLocked(entry.teachingClassId))
        drawLockBadge(painter, card, model->cardForeground(entry.teachingClassId));
}

/*
TimetableDelegate::paintMoreCard - 绘制「更多 +N」卡位（折叠入口）

Parameter：
    painter: 绘制器
    card: 该卡位矩形
    option: 绘制选项（取字体）
    model: 课表模型（取「更多」中性底色/文字色）
    moreCount: 被折叠的课程数（>0）

Remark:
    中性底色（区别于彩色课程卡）+ 短文本「更多 +N」，居中、过长 elide。
    底色/文字色取模型注入的主题色，不查 palette 角色（QSS 会把 Midlight/Text
    合成近同色，曾致两色模式下文字≈底色不可读）。
*/
void TimetableDelegate::paintMoreCard(QPainter *painter, const QRect &card,
                                      const QStyleOptionViewItem &option,
                                      const TimetableModel *model,
                                      int moreCount) const
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(model ? model->moreBackground() : QColor(Qt::lightGray));
    painter->drawRoundedRect(card, kRadius, kRadius);

    const QString text = QStringLiteral("更多 +%1").arg(moreCount);
    const QFontMetrics fm(option.font);
    const QRect inner = card.adjusted(kPadH, 0, -kPadH, 0);
    painter->setFont(option.font);
    painter->setPen(model ? model->moreForeground() : QColor(Qt::black));
    painter->drawText(inner, Qt::AlignCenter,
                      fm.elidedText(text, Qt::ElideRight, inner.width()));
}

/*
TimetableDelegate::entryAt - 命中测试：局部坐标落在哪个视觉卡位

Parameter：
    cellRect: 单元格矩形（与 paint 的 option.rect 同一几何来源）
    index: 单元格索引
    cellLocalPos: 相对单元格左上角的局部坐标（视图经 visualRect 换算）

Result:
    int: 命中完整卡返回其条目序（0 起）；命中「更多」卡位返回 MoreHit；
    空格 / 未点中任何卡位返回 NoHit（-1）

Remark:
    layoutCards 返回的是绝对坐标卡矩形（相对 cellRect 所在坐标系，paint 直接按它
    落笔）。命中时须把格内局部坐标加回 cellRect.topLeft() 换成同一坐标系再比，
    否则只有 topLeft 为 (0,0) 的首格碰巧成立（历史"只有首行首列能开详情"即此因）。
    单门课（全格即一张卡）时整格命中都算点中，消除格边 2px 留白的漏点。
*/
int TimetableDelegate::entryAt(const QRect &cellRect, const QModelIndex &index,
                               const QPoint &cellLocalPos) const
{
    const auto *model = qobject_cast<const TimetableModel *>(index.model());
    if (!model)
        return NoHit;
    const QVector<ScheduleEntry> entries =
        model->entriesAtCell(index.column() + 1, index.row() + 1);
    if (entries.isEmpty())
        return NoHit;
    const int n = entries.size();
    if (n == 1)
        return QRect(QPoint(0, 0), cellRect.size()).contains(cellLocalPos) ? 0 : NoHit;

    const int visible = visibleCourseCount(n);
    const QVector<QRect> cards = layoutCards(cellRect, n);
    const QPoint absPos = cellLocalPos + cellRect.topLeft();   // 局部 → 绝对，与 cards 同系
    for (int i = 0; i < cards.size(); ++i) {
        if (!cards.at(i).contains(absPos))
            continue;
        if (i < visible)
            return i;        // 完整课程卡 → 返回条目序
        if (i == cards.size() - 1)
            return MoreHit;  // 最末位且未画为完整卡 → 即「更多」卡位
    }
    return NoHit;
}
