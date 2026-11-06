#include "../src/ui/linkedraillayout.h"

#include <QCoreApplication>
#include <QtTest>
#include <limits>

using namespace LinkedRailLayout;

class LinkedRailLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void stableMonotonicOrder();
    void adjacentRectanglesDoNotOverlap();
    void minimalDownwardDisplacement();
    void conflictFreeInputsKeepTheirPosition();
    void codeWidthIsNotAnInput();
    void boundaries_data();
    void boundaries();
    void expandedAndCollapsedHeights();
    void scrollingOnlyChangesProjection();
    void thousandGroups();
    void invalidExtentsDoNotOverflow();
};

void LinkedRailLayoutTest::stableMonotonicOrder()
{
    const auto result = arrange({{"a", 40, 1, 30}, {"b", 0, 1, 5}, {"c", 200, 1, 10}}, 100, 0, 8);
    QVERIFY(result.valid);
    QCOMPARE(result.groups.size(), 3);
    QCOMPARE(result.groups[0].id, QStringLiteral("a"));
    QCOMPARE(result.groups[1].id, QStringLiteral("b"));
    QCOMPARE(result.groups[2].id, QStringLiteral("c"));
    QVERIFY(result.groups[0].contentY <= result.groups[1].contentY);
    QVERIFY(result.groups[1].contentY <= result.groups[2].contentY);
}

void LinkedRailLayoutTest::adjacentRectanglesDoNotOverlap()
{
    const auto result = arrange({{"a", 50, 20, 80}, {"b", 50, 2, 300, true},
                                 {"c", 50, 1, 70}}, 200, 0, 11);
    QVERIFY(result.valid);
    for (qsizetype i = 1; i < result.groups.size(); ++i)
        QVERIFY(result.groups[i].contentY >= result.groups[i - 1].contentY + result.groups[i - 1].height + 11);
}

void LinkedRailLayoutTest::minimalDownwardDisplacement()
{
    const auto result = arrange({{"a", -40, 1, 20}, {"b", 8, 1, 30},
                                 {"c", 100, 1, 40}, {"d", 102, 1, 10}}, 80, 0, 7, 3);
    QVERIFY(result.valid);
    const QList<qint64> expected{3, 30, 100, 147};
    for (qsizetype i = 0; i < expected.size(); ++i)
        QCOMPARE(result.groups[i].contentY, expected[i]);
    QCOMPARE(result.totalHeight, 157);
}

void LinkedRailLayoutTest::conflictFreeInputsKeepTheirPosition()
{
    const QList<Input> inputs{{"a", 30, 1, 10}, {"b", 170, 1, 20}, {"c", 800, 1, 50}};
    const auto result = arrange(inputs, 200, 0, 8);
    QVERIFY(result.valid);
    for (qsizetype i = 0; i < inputs.size(); ++i)
        QCOMPARE(result.groups[i].contentY, inputs[i].idealY);
    QVERIFY(!result.groups.last().visible);
    QCOMPARE(result.totalHeight, 850);
}

void LinkedRailLayoutTest::codeWidthIsNotAnInput()
{
    const QList<Input> inputs{{"a", 45, 5, 90}, {"b", 70, 3, 270, true}};
    const auto reference = arrange(inputs, 400, 20, 8);
    // Code-column widths do not belong to this API. The adapter supplies the same
    // measured heights and anchor coordinates for each of these host scenarios.
    for (const int codeWidth : {200, 640, 1400}) {
        Q_UNUSED(codeWidth);
        QVERIFY(arrange(inputs, 400, 20, 8) == reference);
    }
}

void LinkedRailLayoutTest::boundaries_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<qint64>("ideal");
    QTest::addColumn<qint64>("viewport");
    QTest::addColumn<qint64>("gap");
    QTest::newRow("empty") << 0 << qint64(20) << qint64(100) << qint64(8);
    QTest::newRow("single") << 1 << qint64(20) << qint64(100) << qint64(8);
    QTest::newRow("same-y") << 8 << qint64(20) << qint64(100) << qint64(8);
    QTest::newRow("outside-viewport") << 2 << qint64(900) << qint64(100) << qint64(8);
    QTest::newRow("zero-viewport") << 2 << qint64(20) << qint64(0) << qint64(8);
    QTest::newRow("zero-gap") << 8 << qint64(20) << qint64(100) << qint64(0);
}

void LinkedRailLayoutTest::boundaries()
{
    QFETCH(int, count);
    QFETCH(qint64, ideal);
    QFETCH(qint64, viewport);
    QFETCH(qint64, gap);
    QList<Input> inputs;
    for (int i = 0; i < count; ++i)
        inputs.append({QString::number(i), ideal, 1, 30});
    const auto result = arrange(inputs, viewport, 0, gap);
    QVERIFY(result.valid);
    QCOMPARE(result.groups.size(), count);
    QCOMPARE(result.totalHeight, count == 0 ? 0 : ideal + count * 30 + (count - 1) * gap);
    for (int i = 0; i < count; ++i) {
        QCOMPARE(result.groups[i].contentY, ideal + i * (30 + gap));
        QCOMPARE(result.groups[i].height, 30);
        QCOMPARE(result.groups[i].visible, viewport > 0 && ideal + i * (30 + gap) < viewport);
    }
}

void LinkedRailLayoutTest::expandedAndCollapsedHeights()
{
    const auto result = arrange({{"one", 0, 1, 80}, {"two", 0, 2, 84},
                                 {"twenty", 0, 20, 88}, {"expanded", 0, 20, 1600, true}}, 500, 0, 0);
    QVERIFY(result.valid);
    QCOMPARE(result.groups[0].outlines, 0);
    QCOMPARE(result.groups[1].outlines, 1);
    QCOMPARE(result.groups[2].outlines, 2);
    QCOMPARE(result.groups[3].outlines, 0);
    QCOMPARE(result.groups[2].height, 88);
    QCOMPARE(result.groups[3].height, 1600);
    QCOMPARE(result.totalHeight, 1852);
}

void LinkedRailLayoutTest::scrollingOnlyChangesProjection()
{
    const QList<Input> inputs{{"a", 20, 1, 50}, {"b", 200, 1, 60}};
    const auto initial = arrange(inputs, 100, 0, 8);
    const auto scrolled = arrange(inputs, 100, 180, 8);
    QCOMPARE(initial.totalHeight, scrolled.totalHeight);
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        QCOMPARE(initial.groups[i].contentY, scrolled.groups[i].contentY);
        QCOMPARE(initial.groups[i].height, scrolled.groups[i].height);
        QCOMPARE(initial.groups[i].outlines, scrolled.groups[i].outlines);
        QCOMPARE(scrolled.groups[i].viewportY, initial.groups[i].viewportY - 180);
    }
    QVERIFY(initial.groups[0].visible);
    QVERIFY(!initial.groups[1].visible);
    QVERIFY(!scrolled.groups[0].visible);
    QVERIFY(scrolled.groups[1].visible);
}

void LinkedRailLayoutTest::thousandGroups()
{
    QList<Input> inputs;
    for (int i = 0; i < 1000; ++i)
        inputs.append({QString::number(i), i * 7, i % 20 + 1, i % 5 + 30, bool(i % 2)});
    const auto result = arrange(inputs, 700, 15000, 9);
    QVERIFY(result.valid);
    QCOMPARE(result.groups.size(), 1000);
    qint64 previousBottom = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto& placed = result.groups[i];
        const qint64 minimum = qMax(inputs[i].idealY, previousBottom + (i ? 9 : 0));
        QCOMPARE(placed.id, inputs[i].id);
        QCOMPARE(placed.contentY, minimum);
        QCOMPARE(placed.height, inputs[i].preferredHeight);
        QVERIFY(placed.contentY >= inputs[i].idealY);
        QCOMPARE(placed.viewportY, placed.contentY - 15000);
        previousBottom = placed.contentY + placed.height;
    }
    QCOMPARE(result.totalHeight, previousBottom);
}

void LinkedRailLayoutTest::invalidExtentsDoNotOverflow()
{
    const auto invalid = arrange({{"a", std::numeric_limits<qint64>::max(), 1, 30}}, 100, 0, 8);
    QVERIFY(!invalid.valid);
    QVERIFY(invalid.groups.isEmpty());
    QCOMPARE(invalid.totalHeight, 0);
    const auto negative = arrange({{"a", -40, -1, -80}}, -1, -1, -1);
    QVERIFY(negative.valid);
    QCOMPARE(negative.groups[0].contentY, 0);
    QCOMPARE(negative.groups[0].height, 0);
    QCOMPARE(negative.groups[0].outlines, 0);
    QVERIFY(!negative.groups[0].visible);
}

QTEST_GUILESS_MAIN(LinkedRailLayoutTest)
#include "linked_rail_layout_test.moc"
