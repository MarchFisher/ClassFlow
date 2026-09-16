/**
 * 文件职责：快照导入对话框实现。单行文件选择 + 内容预览 + 确定/取消；
 * 确定时仅校验路径非空，实际恢复由调用方执行 DataStore::loadSnapshot。
 */

#include "snapshotimportdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

/*
SnapshotImportDialog - 构造函数，搭建文件选择行 + 预览 + 按钮

Parameter：
    parent: 父窗口指针，默认 nullptr
*/
SnapshotImportDialog::SnapshotImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("恢复工作区快照"));
    resize(480, 360);

    // 文件选择行：输入框 + 浏览按钮
    m_pathEdit = new QLineEdit(this);
    auto *btn = new QPushButton(QStringLiteral("浏览…"), this);
    auto *row = new QHBoxLayout;
    row->addWidget(m_pathEdit, 1);
    row->addWidget(btn);

    // 预览区（只读）
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);

    // 确认 / 取消
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(row);
    layout->addWidget(m_preview, 1);
    layout->addWidget(buttons);

    connect(btn, &QPushButton::clicked, this, &SnapshotImportDialog::browse);
    connect(buttons, &QDialogButtonBox::accepted, this, &SnapshotImportDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

/*
SnapshotImportDialog::path - 取选中的快照文件路径

Result:
    QString: 路径文本（去除首尾空白）
*/
QString SnapshotImportDialog::path() const
{
    return m_pathEdit->text().trimmed();
}

/*
SnapshotImportDialog::browse - 弹出文件选择对话框，并把快照内容预览到下方
*/
void SnapshotImportDialog::browse()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择快照文件"), QString(),
        QStringLiteral("快照文件 (*.csv *.dat);;所有文件 (*)"));
    if (p.isEmpty())
        return;
    m_pathEdit->setText(p);

    // 预览该文件前 100 行
    m_preview->clear();
    QFile f(p);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        for (int i = 0; i < 100 && !in.atEnd(); ++i)
            m_preview->appendPlainText(in.readLine());
    }
}

/*
SnapshotImportDialog::onAccepted - 点击确定：校验路径非空
*/
void SnapshotImportDialog::onAccepted()
{
    if (path().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入"),
                             QStringLiteral("请先选择快照文件。"));
        return;
    }
    accept();
}
