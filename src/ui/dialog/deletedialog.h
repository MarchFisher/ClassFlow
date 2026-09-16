#ifndef DELETEDIALOG_H
#define DELETEDIALOG_H

#include <QDialog>
#include <QSet>
#include <QString>

class QTabWidget;
class QTreeWidget;
class QWidget;
class DataStore;

// 删除课程 / 删除教学班双标签弹窗。课程与教学班相互独立：
// Tab「删除课程」列出全部课程（含空课程），勾选整门删除（连带其教学班 / 排课 / 锁定）；
// Tab「删除教学班」沿用课程分组三态树：勾课程头只级联其下属班，勾单班 = 只删该班，
// 删到最后一班也只留空课程，不自动删课。两页只读数据源、各自独立维护选择，
// OK 时合并成 DeletionRequest 返回，由调用方确认后落库删除。
class DeleteDialog : public QDialog
{
    Q_OBJECT

public:
    // 一次确定的删除请求：整门删的课程号 + 单独删的教学班号（两者可同时非空）
    struct DeletionRequest {
        QSet<QString> courseIds;
        QSet<QString> classIds;
    };

    // store: 数据仓库（只读；取课程/教学班/教师与排课状态）
    DeleteDialog(const DataStore &store, QWidget *parent = nullptr);

    // 合并两个页签各自的选择（课程页勾的整门课 + 班页勾的单班）
    DeletionRequest request() const;

private:
    QWidget *buildCourseTab();      // 「删除课程」页（含空课，勾整门删）
    QWidget *buildClassTab();       // 「删除教学班」页（沿用分组三态树）

    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_courseTree = nullptr;  // 删除课程页树（行 = 课程，可勾选）
    QTreeWidget *m_classTree = nullptr;   // 删除教学班页树（课程头三态级联到班）
    const DataStore &m_store;             // 只读数据源（建两页树时取课程/班/教师）
};

#endif // DELETEDIALOG_H
