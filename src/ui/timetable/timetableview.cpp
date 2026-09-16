/**
 * 文件职责：课表网格视图子类实现（QTableView 子类）。
 * 重写 mouseReleaseEvent 做"像素 → 具体条目卡片"命中：局部坐标经
 * TimetableDelegate::entryAt 与 layoutCards 同一几何换算成 (星期, 节次, 卡序)，
 * 命中完整课程卡发 entryClicked、命中「更多」折叠卡位发 moreClicked
 * （后者由外层 TimetableController 弹窗列该格全部课程）。
 */

#include "timetableview.h"

#include <QMouseEvent>

#include "timetabledelegate.h"

/*
TimetableView - 构造函数

Parameter：
    parent: 父对象指针，默认 nullptr
*/
TimetableView::TimetableView(QWidget *parent)
    : QTableView(parent)
{
}

/*
TimetableView::mouseReleaseEvent - 鼠标左键释放：命中卡片则发对应信号

Parameter：
    event: 鼠标事件（pos 相对 viewport）

Remark:
    先取 indexAt(pos) 得单元格，再由视图 visualRect 换算出单元格局部坐标，
    交 delegate entryAt 判定命中哪张卡：完整卡发 entryClicked(卡序)、
    「更多」卡位发 moreClicked；空格 / 空白处不发。最后交回基类维持原有
    选中行为（点击空白格仍可选中/取消）。
*/
void TimetableView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->pos());
        if (index.isValid()) {
            if (const auto *delegate = qobject_cast<const TimetableDelegate *>(
                    itemDelegateForIndex(index))) {
                const QRect cell = visualRect(index);
                const QPoint local = event->pos() - cell.topLeft();
                const int hit = delegate->entryAt(cell, index, local);
                const int day = index.column() + 1;
                const int section = index.row() + 1;
                if (hit >= 0)
                    emit entryClicked(day, section, hit);
                else if (hit == TimetableDelegate::MoreHit)
                    emit moreClicked(day, section);
            }
        }
    }
    QTableView::mouseReleaseEvent(event);
}
