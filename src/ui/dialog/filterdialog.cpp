/**
 * 文件职责：排课筛选弹窗实现。由侧边栏「筛选」按钮创建，
 * 三组「搜索框 + 可多选列表」（教师 / 教室 / 课程）。
 * 勾选栏（列表）是唯一真源：勾选状态由用户手动维护。
 * 搜索框仅做子串过滤（大小写不敏感），与勾选状态完全独立、不同步：
 *   输入即过滤列表显示，空词显示全部；勾选 / 取消勾选不回写搜索框。
 * 顶部「清空」按钮一键取消全部勾选并清空三组搜索框（回到「不限」）。
 * 维度间 AND、维度内 OR，空 = 不限；点「应用」后返回 ScheduleFilter。
 */

#include "filterdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/store/datastore.h"

namespace {
// 选项显示文本：name 与 id 相同时只显示 id，否则 "名称（ID）"
QString optionLabel(const QString &name, const QString &id)
{
    if (name.isEmpty() || name == id)
        return id;
    return name + QStringLiteral("（") + id + QStringLiteral("）");
}
}

/*
FilterDialog::FilterDialog - 构造筛选弹窗

Parameter：
    store: 数据仓库（提供教师 / 教室 / 课程选项）
    current: 当前筛选条件（用于初始化勾选状态）
    parent: 父窗口指针
*/
FilterDialog::FilterDialog(const DataStore &store, const ScheduleFilter &current,
                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("筛选"));

    // 三个维度的候选：教师显示 "姓名（ID）"，教室显示教室号，课程显示 "课程名（ID）"
    QVector<QPair<QString, QString>> teachers;   // (teacherId, 显示文本)
    for (const TeacherInfo &t : store.teachers())
        teachers.append(qMakePair(t.teacherId, optionLabel(t.name, t.teacherId)));

    QVector<QPair<QString, QString>> rooms;
    for (const Classroom &c : store.classrooms())
        rooms.append(qMakePair(c.roomNumber, c.roomNumber));

    QVector<QPair<QString, QString>> courses;
    for (const Course &c : store.courses())
        courses.append(qMakePair(c.id, optionLabel(c.name, c.id)));

    // 三组并排：左教师、中教室、右课程
    auto *groups = new QHBoxLayout;
    groups->addLayout(addGroup(QStringLiteral("教师"), teachers, current.teacherIds,
                               m_teacherSearch, m_teacherList), 1);
    groups->addLayout(addGroup(QStringLiteral("教室"), rooms, current.classroomIds,
                               m_roomSearch, m_roomList), 1);
    groups->addLayout(addGroup(QStringLiteral("课程"), courses, current.courseIds,
                               m_courseSearch, m_courseList), 1);

    // 顶部「清空」按钮：一键取消全部勾选并清空三组搜索框
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        clearGroup(m_teacherList, m_teacherSearch);
        clearGroup(m_roomList, m_roomSearch);
        clearGroup(m_courseList, m_courseSearch);
    });
    auto *clearRow = new QHBoxLayout;
    clearRow->addWidget(clearBtn);
    clearRow->addStretch(1);

    // 应用 / 取消
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(clearRow);
    layout->addLayout(groups, 1);
    layout->addWidget(buttons);
    resize(780, 440);
}

/*
FilterDialog::result - 收集各维度勾选项

Result:
    ScheduleFilter: 各维度选中的 key 集合；某维度空 = 不限
*/
ScheduleFilter FilterDialog::result() const
{
    ScheduleFilter f;
    f.teacherIds   = checkedKeys(m_teacherList);
    f.classroomIds = checkedKeys(m_roomList);
    f.courseIds    = checkedKeys(m_courseList);
    return f;
}

/*
FilterDialog::checkedKeys - 取列表中勾选项的 key 集合

Parameter：
    list: 可多选列表（key 存于 Qt::UserRole）

Result:
    QSet<QString>: 已勾选条目对应的 key；无勾选返回空集
*/
QSet<QString> FilterDialog::checkedKeys(const QListWidget *list)
{
    QSet<QString> keys;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem *it = list->item(i);
        if (it->checkState() == Qt::Checked)
            keys.insert(it->data(Qt::UserRole).toString());
    }
    return keys;
}

/*
FilterDialog::applySearch - 按搜索文本过滤列表显示

Parameter：
    list: 目标列表
    text: 搜索词；空则全部显示；否则隐藏不含该子串的条目

Remark:
    按条目显示文本做不区分大小写的子串匹配；只改可见性，
    不动勾选状态。
*/
void FilterDialog::applySearch(QListWidget *list, const QString &text)
{
    const QString t = text.trimmed();
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *it = list->item(i);
        it->setHidden(!t.isEmpty() && !it->text().contains(t, Qt::CaseInsensitive));
    }
}

/*
FilterDialog::clearGroup - 取消某组全部勾选并清空搜索框

Parameter：
    list: 该组的可多选列表
    search: 该组的搜索框

Remark:
    搜索框 clear() 触发 textChanged → applySearch("")，列表恢复显示全部。
*/
void FilterDialog::clearGroup(QListWidget *list, QLineEdit *search)
{
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(Qt::Unchecked);
    search->clear();
}

/*
FilterDialog::addGroup - 创建一组「标题 + 搜索框 + 可多选列表」

Parameter：
    title: 组标题（如 教师）
    options: 候选 (key, 显示文本)
    selected: 当前已选中的 key 集合（用于初始化勾选）
    search: 输出，搜索框指针
    list: 输出，列表指针

Result:
    QVBoxLayout*: 该组的纵向布局（供外部拼入三列布局）

Remark:
    信号联动：
      textChanged → applySearch 子串过滤，不自动勾选、不回写；
      勾选状态完全由用户手动维护，搜索框与列表互不联动。
*/
QVBoxLayout *FilterDialog::addGroup(const QString &title,
                                    const QVector<QPair<QString, QString>> &options,
                                    const QSet<QString> &selected,
                                    QLineEdit *&search, QListWidget *&list)
{
    auto *v = new QVBoxLayout;

    auto *titleLabel = new QLabel(title, this);
    search = new QLineEdit(this);
    search->setClearButtonEnabled(true);   // × 一键清空，便于重新搜索
    search->setPlaceholderText(QStringLiteral("输入以筛选"));
    list = new QListWidget(this);
    list->setToolTip(QStringLiteral("勾选即选中；空 = 不限"));

    for (const auto &opt : options) {
        auto *item = new QListWidgetItem(opt.second, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(opt.first) ? Qt::Checked
                                                         : Qt::Unchecked);
        item->setData(Qt::UserRole, opt.first);
    }

    // 输入即过滤：子串匹配，不自动勾选、不回写搜索框
    connect(search, &QLineEdit::textChanged, this,
            [this, list](const QString &text) {
        applySearch(list, text);
    });

    v->addWidget(titleLabel);
    v->addWidget(search);
    v->addWidget(list, 1);
    return v;
}
