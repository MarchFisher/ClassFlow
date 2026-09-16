#ifndef TIMETABLEVIEW_H
#define TIMETABLEVIEW_H

#include <QTableView>

class QMouseEvent;

// 课表网格视图子类：为"点哪张开哪张"补充像素级命中。
// 鼠标左键释放时把坐标经 delegate 的 entryAt 解析为 (星期, 节次, 卡序)，
// 命中完整课程卡发 entryClicked、命中「更多」折叠卡位发 moreClicked；
// 空格 / 空白处不发射，选中态由基类照常维护。
class TimetableView : public QTableView
{
    Q_OBJECT

public:
    explicit TimetableView(QWidget *parent = nullptr);

signals:
    void entryClicked(int day, int section, int ordinal);   // day 1..7，section 从 1 起，ordinal 0 起
    void moreClicked(int day, int section);                 // 命中「更多」折叠卡位（由外层弹全课程列表）

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
};

#endif // TIMETABLEVIEW_H
