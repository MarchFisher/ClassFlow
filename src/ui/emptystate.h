#ifndef EMPTYSTATE_H
#define EMPTYSTATE_H

/**
 * 文件职责：空态引导页组件。
 * 无数据（未新建/导入教学班）时，在课表区域中央展示一句引导 + 两个 accent CTA，
 * 替代只刷状态栏；有数据后由 MainWindow 用 QStackedWidget 整体切走。
 */

#include <QWidget>

class EmptyState : public QWidget
{
    Q_OBJECT

public:
    explicit EmptyState(QWidget *parent = nullptr);

signals:
    void newRequested();      // 「新建…」被点：交给 MainWindow 走新建流程
    void importRequested();   // 「导入快照…」被点：交给 MainWindow 走导入流程
};

#endif // EMPTYSTATE_H
