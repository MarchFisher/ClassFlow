/**
 * 文件职责：本格全部课程长卡列表弹窗实现。
 * 用 QScrollArea + 纵向布局把"某一格的每一门课"排成一张张可点长条形卡
 * （课程色底、圆角、两行文本），供用户滚动浏览该格全部课程；
 * 点任意一张卡 → 经回调/信号让外层打开对应课程详情。
 * 行数据（标题/副标题/配色）由调用方在 showMoreCourses 预拼好，本窗口只负责展示。
 */

#include "courselistdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace {

const int kCardHeight = 64;   // 每张长卡高度
const int kRadius     = 6;    // 卡片圆角
const int kPadH       = 12;   // 卡内文字左右内边距
const int kTextV      = 6;    // 卡内文字上下留白
const int kSpacing    = 8;    // 卡间距 / 容器留白

const int kLockW = 8;        // 锁标：锁身宽
const int kLockH = 7;        // 锁标：锁身高
const int kLockGap = 8;      // 锁标距卡右缘留白（标题行为此让位）

/*
drawLockAt - 在长卡标题行右端画一枚小锁标（表示该教学班已锁定）

Parameter：
    p: 绘制器
    center: 锁标中心点（纵向对齐标题行中心）
    color: 锁标颜色（用卡内文字色，保证与课程色底对比可读）

Remark:
    锁体 = 圆角小矩形 + 上方半圆锁环（与课表 delegate 锁标同构，非字体字形）；
    纯装饰不参与点击命中。
*/
void drawLockAt(QPainter *p, const QPoint &center, const QColor &color)
{
    const QRect body(center.x() - kLockW, center.y() - kLockH / 2, kLockW, kLockH);
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

/*
CourseCard - 单张可点长条课程卡（自绘，非 QObject：点击经 std::function 回调）

Parameter：
    row: 该卡显示数据（标题 / 副标题 / 课程色）
    clicked: 点击回调（外层包成"发 cardClicked(entry)"）
    parent: 父控件
*/
class CourseCard : public QWidget
{
public:
    CourseCard(const CourseListRow &row, std::function<void()> clicked,
               QWidget *parent = nullptr)
        : QWidget(parent), m_row(row), m_clicked(std::move(clicked))
    {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return QSize(240, kCardHeight); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 课程色圆角底（同课同色，与课表网格一致）；悬停描细边提示可点
        p.setPen(Qt::NoPen);
        p.setBrush(m_row.background);
        p.drawRoundedRect(rect(), kRadius, kRadius);
        if (m_hover) {
            p.setPen(QPen(m_row.foreground, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), kRadius, kRadius);
        }

        // 两行文本：上课程名（加粗），下 教师 · 教室[ · 周范围]；过长横向 elide
        const QFont fTitle = QWidget::font();
        QFont fSub = QWidget::font();
        QFont title = fTitle;
        title.setBold(true);
        const int titleH = QFontMetrics(title).height();
        const int subH   = QFontMetrics(fSub).height();
        const QRect inner = rect().adjusted(kPadH, 0, -kPadH, 0);

        // 锁定条目：标题行右端让位画锁标（与课表网格同款）；textW = 预留后的可绘宽
        const int reserve = m_row.locked ? (kLockW + 2 * kLockGap) : 0;
        const int textW = qMax(0, inner.width() - reserve);

        p.setFont(title);
        p.setPen(m_row.foreground);
        const QRect line1(inner.left(), rect().top() + kTextV, textW, titleH);
        p.drawText(line1, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(title).elidedText(m_row.title, Qt::ElideRight,
                                                  line1.width()));
        if (m_row.locked) {
            const QPoint lockCenter(inner.right() - kLockGap - kLockW / 2,
                                    line1.top() + titleH / 2);
            drawLockAt(&p, lockCenter, m_row.foreground);
        }
        p.setFont(fSub);
        const QRect line2(inner.left(), rect().bottom() - kTextV - subH,
                          inner.width(), subH);
        p.drawText(line2, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(fSub).elidedText(m_row.subtitle, Qt::ElideRight,
                                                 line2.width()));
    }

    void enterEvent(QEnterEvent *) override
    {
        m_hover = true;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hover = false;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            m_clicked();
        QWidget::mouseReleaseEvent(event);
    }

private:
    CourseListRow m_row;
    bool m_hover = false;
    std::function<void()> m_clicked;
};

} // namespace

/*
CourseListDialog - 构造：滚动区长卡列表 + 顶部说明 + 底部关闭

Parameter：
    rows: 该格全部课程行（按格内顺序）
    caption: 弹窗顶部说明（含格位置 / 门数）
    parent: 父窗口
*/
CourseListDialog::CourseListDialog(const QVector<CourseListRow> &rows,
                                   const QString &caption, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("本格课程列表"));
    setMinimumSize(420, 320);
    resize(480, 560);

    auto *layout = new QVBoxLayout(this);

    auto *cap = new QLabel(caption, this);
    cap->setWordWrap(true);
    QFont capFont = cap->font();
    capFont.setBold(true);
    capFont.setPointSize(capFont.pointSize() + 1);
    cap->setFont(capFont);
    layout->addWidget(cap);

    // 滚动区：内容容器里依次塞长卡，末尾加弹性占位使卡片靠上
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget;
    m_col = new QVBoxLayout(container);
    m_col->setContentsMargins(0, 0, kSpacing, 0);
    m_col->setSpacing(kSpacing);
    for (const CourseListRow &row : rows)
        addRow(row);
    m_col->addStretch();
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    auto *hint = new QLabel(QStringLiteral("滚动查看全部 · 点击长卡查看课程详情"), this);
    hint->setProperty("secondary", true);   // 次要文字随主题（弹窗统一）
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

/*
CourseListDialog::addRow - 追加一张长卡并接线到 cardClicked 信号

Parameter：
    row: 该行课程数据
*/
void CourseListDialog::addRow(const CourseListRow &row)
{
    m_entries.append(row.entry);
    const int index = m_entries.size() - 1;
    auto *card = new CourseCard(row,
                                [this, index]() { emit cardClicked(m_entries.at(index)); },
                                this);
    m_col->insertWidget(index, card);   // 插到当前末尾（其后是弹性占位）
}
