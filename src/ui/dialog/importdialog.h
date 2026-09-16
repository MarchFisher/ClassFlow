#ifndef IMPORTDIALOG_H
#define IMPORTDIALOG_H

#include <QDialog>

#include "core/store/datastore.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ImportDialog;
}
QT_END_NAMESPACE

class QLineEdit;
class QPlainTextEdit;

// 新建工作区对话框：选择教学班 / 教室 / 作息表三个源文件，预览并触发导入。
// 确定后调用 DataStore::loadCsv，可通过 dataStore() 取解析结果。
class ImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportDialog(QWidget *parent = nullptr);
    ~ImportDialog();

    const DataStore &dataStore() const;   // 解析结果（accept 后有效）

private slots:
    void browseTeachingClass();
    void browseClassroom();
    void browseSection();
    void browseTeacher();
    void onAccepted();

private:
    void browseFor(QLineEdit *edit);      // 弹出文件选择并刷新预览

    Ui::ImportDialog *ui;
    DataStore  m_store;
    QLineEdit *m_tcEdit      = nullptr;   // 教学班路径
    QLineEdit *m_roomEdit    = nullptr;   // 教室路径
    QLineEdit *m_secEdit     = nullptr;   // 作息表路径
    QLineEdit *m_teacherEdit = nullptr;   // 教师路径（可选，留空则自动推导）
    QPlainTextEdit *m_preview = nullptr;
};

#endif // IMPORTDIALOG_H
