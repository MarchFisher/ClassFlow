/**
 * 文件职责：排课错误列表弹窗实现。主窗口在排课失败后创建，
 * 以只读表格列出各失败教学班的 ID、课程与失败原因，
 * 方便用户照着提示修改课程数据后重新排课。
 * 每行「操作」列放一个「编辑」钮，点击发射 editRequested(classId)，
 * 由主窗口钉选该教学班打开编辑弹窗（改完自动重试排该班）。
 */

#include "scheduleerrordialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

/*
ScheduleErrorDialog - 构造函数：以只读表格展示失败明细，每行附「编辑」钮

Parameter：
    failures: 排课失败明细列表
    parent: 父窗口指针

Remark:
    表格四列：教学班 / 课程 / 失败原因 / 操作；原因列撑满剩余宽度。
    「操作」列每行一个「编辑」钮，点击发射 editRequested(该行教学班 id)。
*/
ScheduleErrorDialog::ScheduleErrorDialog(const QVector<ScheduleFailure> &failures,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("排课错误"));

    auto *tip = new QLabel(
        QStringLiteral("以下教学班未能排入课表，可按「编辑」直接修改该班，改完自动重试。"), this);
    QFont f = tip->font();
    f.setBold(true);
    tip->setFont(f);

    auto *table = new QTableWidget(int(failures.size()), 4, this);
    table->setHorizontalHeaderLabels({
        QStringLiteral("教学班"),
        QStringLiteral("课程"),
        QStringLiteral("失败原因"),
        QStringLiteral("操作")
    });
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setColumnWidth(3, 72);                     // 操作列定宽（按钮不随内容重算）
    table->verticalHeader()->setDefaultSectionSize(34);  // 行高足够容纳「编辑」钮
    for (int i = 0; i < failures.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(failures.at(i).classId));
        table->setItem(i, 1, new QTableWidgetItem(failures.at(i).courseName));
        table->setItem(i, 2, new QTableWidgetItem(failures.at(i).reason));
        auto *editBtn = new QPushButton(QStringLiteral("编辑"), table);
        editBtn->setAutoDefault(false);
        editBtn->setToolTip(
            QStringLiteral("修改该教学班基本信息，确认后自动对该班局部重排一次"));
        const QString cid = failures.at(i).classId;
        connect(editBtn, &QPushButton::clicked, this,
                [this, cid] { emit editRequested(cid); });
        table->setCellWidget(i, 3, editBtn);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tip);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);
    resize(600, 320);
}
