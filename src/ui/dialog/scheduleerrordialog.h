#ifndef SCHEDULEERRORDIALOG_H
#define SCHEDULEERRORDIALOG_H

#include <QDialog>

#include "core/models/models.h"

// 排课错误列表弹窗：展示未排入的教学班及其失败原因，
// 供用户照着提示修改课程数据后重新排课。由主窗口在排课失败时创建，
// 也用于顶栏「排课错误」按钮重看上次持久化的失败明细。
// 每行附带「编辑」钮：点击后 emit editRequested(classId)，由主窗口钉选该班编辑并重试。
class ScheduleErrorDialog : public QDialog
{
    Q_OBJECT

public:
    // failures: 排课失败明细（教学班 + 课程名 + 失败原因）
    ScheduleErrorDialog(const QVector<ScheduleFailure> &failures, QWidget *parent = nullptr);

signals:
    // 某行「编辑」被点击：携带该失败教学班 id（TimetableController 转发给主窗口编辑）
    void editRequested(const QString &classId);
};

#endif // SCHEDULEERRORDIALOG_H
