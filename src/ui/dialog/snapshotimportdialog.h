#ifndef SNAPSHOTIMPORTDIALOG_H
#define SNAPSHOTIMPORTDIALOG_H

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

// 快照导入对话框：选择工作区快照文件并预览，确定后由外部调用 DataStore::loadSnapshot。
class SnapshotImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SnapshotImportDialog(QWidget *parent = nullptr);

    QString path() const;   // 选中的快照文件路径（accept 后有效）

private slots:
    void browse();          // 选择快照文件并预览
    void onAccepted();      // 校验路径非空后 accept

private:
    QLineEdit      *m_pathEdit = nullptr;
    QPlainTextEdit *m_preview  = nullptr;
};

#endif // SNAPSHOTIMPORTDIALOG_H
