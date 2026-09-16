/**
 * 文件职责：空态引导页实现。中央纵向排版：大标题 + 次要说明 + 两个 CTA
 * （「新建…」primary accent 实底 / 「导入快照…」普通按钮）。
 */

#include "emptystate.h"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

/*
EmptyState::EmptyState - 空态引导页构造：居中排版标题 / 引导 / 两个 CTA

Parameter：
    parent: 父控件指针，默认 nullptr
*/
EmptyState::EmptyState(QWidget *parent)
    : QWidget(parent)
{
    // 大标题：强调"还没有课表"；引导：次要文字随主题
    auto *title = new QLabel(QStringLiteral("还没有课表"), this);
    QFont tf = title->font();
    tf.setPointSize(22);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);

    auto *hint = new QLabel(
        QStringLiteral("导入课表数据，或新建工作区，让 ClassFlow 为你自动排好课"), this);
    hint->setProperty("secondary", true);        // 次要文字色（QSS QLabel[secondary=true]）
    hint->setAlignment(Qt::AlignCenter);

    // CTA：新建 = 主操作（accent 实底），导入快照 = 普通按钮
    auto *newBtn = new QPushButton(QStringLiteral("新建…"), this);
    newBtn->setProperty("primary", true);
    newBtn->setMinimumHeight(38);
    newBtn->setCursor(Qt::PointingHandCursor);
    connect(newBtn, &QPushButton::clicked, this, &EmptyState::newRequested);

    auto *importBtn = new QPushButton(QStringLiteral("导入快照…"), this);
    importBtn->setMinimumHeight(38);
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, &EmptyState::importRequested);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(newBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(importBtn);
    btnRow->addStretch();

    auto *center = new QVBoxLayout(this);
    center->addStretch();
    center->addWidget(title);
    center->addSpacing(8);
    center->addWidget(hint);
    center->addSpacing(24);
    center->addLayout(btnRow);
    center->addStretch();
}
