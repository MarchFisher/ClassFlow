/**
 * 文件职责：新建工作区对话框实现。三行源文件选择 + 预览 + 确定/取消；
 * 确定时调用 DataStore::loadCsv，成功后 accept()。
 */

#include "importdialog.h"
#include "ui_importdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include "core/store/datastore.h"

/*
ImportDialog - 构造函数，搭建三行文件选择 + 预览 + 按钮

Parameter：
    parent: 父窗口指针，默认 nullptr
*/
ImportDialog::ImportDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ImportDialog)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("新建工作区"));

    // 三行文件选择：标签 + 输入框 + 浏览按钮
    auto *form = new QFormLayout;

    m_tcEdit = new QLineEdit(this);
    auto *tcBtn = new QPushButton(QStringLiteral("浏览…"), this);
    auto *tcRow = new QHBoxLayout;
    tcRow->addWidget(m_tcEdit, 1);
    tcRow->addWidget(tcBtn);
    form->addRow(QStringLiteral("教学班 CSV："), tcRow);

    m_roomEdit = new QLineEdit(this);
    auto *roomBtn = new QPushButton(QStringLiteral("浏览…"), this);
    auto *roomRow = new QHBoxLayout;
    roomRow->addWidget(m_roomEdit, 1);
    roomRow->addWidget(roomBtn);
    form->addRow(QStringLiteral("教室 CSV："), roomRow);

    m_secEdit = new QLineEdit(this);
    auto *secBtn = new QPushButton(QStringLiteral("浏览…"), this);
    auto *secRow = new QHBoxLayout;
    secRow->addWidget(m_secEdit, 1);
    secRow->addWidget(secBtn);
    form->addRow(QStringLiteral("作息表 CSV："), secRow);

    // 教师 CSV 可选：留空时由教学班中的教师 ID 自动推导
    m_teacherEdit = new QLineEdit(this);
    auto *teacherBtn = new QPushButton(QStringLiteral("浏览…"), this);
    auto *teacherRow = new QHBoxLayout;
    teacherRow->addWidget(m_teacherEdit, 1);
    teacherRow->addWidget(teacherBtn);
    form->addRow(QStringLiteral("教师 CSV（可选）："), teacherRow);

    // 预览区（只读）
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);

    // 确认 / 取消
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_preview, 1);
    layout->addWidget(buttons);

    connect(tcBtn,   &QPushButton::clicked, this, &ImportDialog::browseTeachingClass);
    connect(roomBtn, &QPushButton::clicked, this, &ImportDialog::browseClassroom);
    connect(secBtn,  &QPushButton::clicked, this, &ImportDialog::browseSection);
    connect(teacherBtn, &QPushButton::clicked, this, &ImportDialog::browseTeacher);
    connect(buttons, &QDialogButtonBox::accepted, this, &ImportDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

/*
~ImportDialog - 析构函数，释放 ui 对象

*/
ImportDialog::~ImportDialog()
{
    delete ui;
}

/*
ImportDialog::browseTeachingClass - 选择教学班 CSV
*/
void ImportDialog::browseTeachingClass()
{
    browseFor(m_tcEdit);
}

/*
ImportDialog::browseClassroom - 选择教室 CSV
*/
void ImportDialog::browseClassroom()
{
    browseFor(m_roomEdit);
}

/*
ImportDialog::browseSection - 选择作息表 CSV
*/
void ImportDialog::browseSection()
{
    browseFor(m_secEdit);
}

/*
ImportDialog::browseTeacher - 选择教师 CSV（可选）
*/
void ImportDialog::browseTeacher()
{
    browseFor(m_teacherEdit);
}

/*
ImportDialog::browseFor - 弹出文件选择对话框，并把文件内容预览到下方

Parameter：
    edit: 接收所选路径的输入框
*/
void ImportDialog::browseFor(QLineEdit *edit)
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 CSV 文件"), QString(),
        QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    edit->setText(path);

    // 预览该文件前 100 行
    m_preview->clear();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        for (int i = 0; i < 100 && !in.atEnd(); ++i)
            m_preview->appendPlainText(in.readLine());
    }
}

/*
ImportDialog::onAccepted - 点击确定：校验并解析三个 CSV

Remark:
    任一路径为空或 loadCsv 失败时弹窗提示，不关闭对话框。
*/
void ImportDialog::onAccepted()
{
    const QString tc  = m_tcEdit->text().trimmed();
    const QString room = m_roomEdit->text().trimmed();
    const QString sec  = m_secEdit->text().trimmed();
    const QString teacher = m_teacherEdit->text().trimmed();   // 可选

    if (tc.isEmpty() || room.isEmpty() || sec.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新建"),
                             QStringLiteral("请先选择教学班 / 教室 / 作息表三份源文件。"));
        return;
    }
    if (!m_store.loadCsv(tc, room, sec, teacher)) {
        QMessageBox::warning(this, QStringLiteral("新建"),
                             QStringLiteral("解析失败：请检查文件路径与格式。"));
        return;
    }
    accept();
}

/*
ImportDialog::dataStore - 取解析后的数据仓库

Result:
    const DataStore&: 解析结果（accept 后有效）

Remark:
    返回的是内部引用，外部请拷贝后再长期持有。
*/
const DataStore &ImportDialog::dataStore() const
{
    return m_store;
}
