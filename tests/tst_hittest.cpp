/**
 * 文件职责：课表卡片点击命中回归测试（无头 GUI 测试）。
 * 用真实 TimetableView + TimetableModel + TimetableDelegate 载入样例数据并排课，
 * 以 QTest 向每个有课格、每张完整卡注入左键点击，断言 entryClicked 均以正确
 * (day, section, 卡序) 发射。
 * 背景：曾因 entryAt 用"格内局部坐标"比对 layoutCards 返回的"绝对坐标"卡矩形，
 * 导致只有视口首格(topLeft==(0,0))能命中；本测试固定覆盖全网格防回归。
 */

#include <QtTest>

#include <QSignalSpy>
#include <QTableView>

#include "core/schedule/scheduler.h"
#include "core/schedule/strategy.h"
#include "core/store/datastore.h"
#include "ui/timetable/timetabledelegate.h"
#include "ui/timetable/timetablemodel.h"
#include "ui/timetable/timetableview.h"

namespace {

/*
sampleStore - 载入示例数据并排课的数据仓库

Result:
    DataStore: 已排满课（scheduleEntries 非空）的仓库
*/
DataStore sampleStore()
{
    DataStore store;
    store.loadCsv(
        QStringLiteral(DATA_DIR) + "/teaching_classes.csv",
        QStringLiteral(DATA_DIR) + "/classrooms.csv",
        QStringLiteral(DATA_DIR) + "/sections.csv");
    Scheduler scheduler;
    GreedyStrategy greedy;
    scheduler.schedule(store, &greedy);
    return store;
}

} // namespace

class TestHitTest : public QObject
{
    Q_OBJECT

private slots:
    void clicksOpenCorrectCellCard();
};

/*
TestHitTest::clicksOpenCorrectCellCard - 每个有课格逐张卡点击都应发射 entryClicked(…)

Remark:
    覆盖全网格（含非首行首列）与多卡格（每张完整卡命中其自身卡序）。
*/
void TestHitTest::clicksOpenCorrectCellCard()
{
    DataStore store = sampleStore();
    QVERIFY(!store.scheduleEntries().isEmpty());

    TimetableModel model;
    model.setDataStore(&store);
    QVERIFY(model.rowCount() > 0);

    TimetableView view;
    view.setModel(&model);
    view.setItemDelegate(new TimetableDelegate(&view));
    view.resize(1400, 800);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // 统计有课格并逐个点首卡验证
    int clicked = 0;
    for (int r = 0; r < model.rowCount(); ++r) {
        for (int c = 0; c < model.columnCount(); ++c) {
            const int day = c + 1;
            const int section = r + 1;
            const auto entries = model.entriesAtCell(day, section);
            if (entries.isEmpty())
                continue;

            const QModelIndex idx = model.index(r, c);
            const QRect cell = view.visualRect(idx);
            if (!cell.isValid() || cell.isEmpty())
                continue;
            // 完整卡数 = visibleCourseCount(n)；逐张点它的中心验证命中卡序
            const int vis = TimetableDelegate::visibleCourseCount(entries.size());
            const QVector<QRect> cards = TimetableDelegate::layoutCards(cell, entries.size());
            for (int card = 0; card < vis; ++card) {
                QSignalSpy spy(&view, &TimetableView::entryClicked);
                const QPoint target = cards.at(card).center();
                QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);

                // 期望：entryClicked(day, section, card)
                const bool ok = spy.count() == 1;
                if (!ok) {
                    // 诊断：indexAt 落在哪格？直调 entryAt 结果？目标点坐标？
                    const auto *del = qobject_cast<const TimetableDelegate *>(
                        view.itemDelegateForIndex(idx));
                    const int direct = del ? del->entryAt(cell, idx,
                                                          target - cell.topLeft()) : -99;
                    qWarning("FAIL (%d,%d): entries=%d vis=%d cell=%d,%d %dx%d target=%d,%d "
                             "indexAt=%d,%d direct=entryAt=%d spy=%d",
                             day, section, int(entries.size()), vis,
                             cell.x(), cell.y(), cell.width(), cell.height(),
                             target.x(), target.y(),
                             view.indexAt(target).row(), view.indexAt(target).column(),
                             direct, spy.count());
                }
                QVERIFY2(ok, qPrintable(QStringLiteral("(%1,%2) 点第 %3 张卡未发射 entryClicked")
                                            .arg(day).arg(section).arg(card + 1)));
                const QList<QVariant> args = spy.takeFirst();
                QVERIFY2(args.at(0).toInt() == day && args.at(1).toInt() == section
                             && args.at(2).toInt() == card,
                         qPrintable(QStringLiteral("(%1,%2) 信号参数不符: day=%3 sec=%4 ord=%5")
                                        .arg(day).arg(section)
                                        .arg(args.at(0).toInt())
                                        .arg(args.at(1).toInt())
                                        .arg(args.at(2).toInt())));
            }
            ++clicked;
        }
    }
    QVERIFY2(clicked > 1, "样例数据应存在多于一个的有课格");
}

QTEST_MAIN(TestHitTest)
#include "tst_hittest.moc"
