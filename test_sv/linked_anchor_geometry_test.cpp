#include "linkedanchorgeometry.h"
#include "mycodeeditor.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollBar>
#include <QTextBlock>
#include <QtTest>
#include <climits>

namespace Geometry = LinkedAnchorGeometry;
namespace {
QRect rectangle(const QJsonValue& value)
{
    const auto a = value.toArray();
    return {a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt()};
}
QPoint point(const QJsonValue& value)
{
    const auto a = value.toArray();
    return {a.at(0).toInt(), a.at(1).toInt()};
}
QJsonObject operation(const QJsonArray& calls, const QString& name)
{
    for (const auto& value : calls)
        if (value.toObject()["operation"].toString() == name) return value.toObject();
    return {};
}
Geometry::Anchor anchor(const QJsonObject& value)
{
    return {value["id"].toString(), value["available"].toBool(), value["start"].toInt(), value["end"].toInt()};
}
Geometry::Selection selection(const QJsonObject& value)
{
    return {value["selectionStart"].toInt(), value["selectionEnd"].toInt(), value["selected"].toString()};
}
QByteArray readFixture(const char* name)
{
    QFile file(QString::fromUtf8(LINKED_ANCHOR_FIXTURE_DIR) + '/' + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
void prepareEditor(MyCodeEditor& editor, const QString& text)
{
    editor.setFont(QFont(QStringLiteral("Maple Mono"), 15));
    editor.setLineWrapMode(QPlainTextEdit::NoWrap);
    editor.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    editor.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    editor.resize(800, 480);
    editor.setPlainText(text);
    editor.acceptLoadedTextAsSemanticBaseline();
    editor.show();
    QCoreApplication::processEvents();
}
void materializeLines(MyCodeEditor& editor)
{
    for (auto block = editor.document()->begin(); block.isValid(); block = block.next())
        editor.document()->documentLayout()->blockBoundingRect(block);
}
}

class LinkedAnchorGeometryTest : public QObject {
    Q_OBJECT
private:
    QJsonArray phases;
    QString source;
    QList<Geometry::Anchor> anchors;

private slots:
    void initTestCase()
    {
        QJsonParseError error;
        const auto baseline = QJsonDocument::fromJson(readFixture("baseline.json"), &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(baseline["head"].toString(), QStringLiteral("6f26b974a505b1a3f1c455bf918f2cf740633b56"));
        const auto bytes = readFixture("anchor_geometry_fixture.sv");
        QVERIFY(!bytes.isEmpty());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()), baseline["fixtureSha256"].toString());
        source = QString::fromUtf8(bytes);
        phases = baseline["samples"].toArray();
        QCOMPARE(phases.size(), 4);
        const auto samples = phases.first().toObject()["geometry"].toObject()["editor"].toObject()["samples"].toArray();
        for (const auto& value : samples)
            anchors.append(anchor(value.toObject()));
        QCOMPARE(anchors.size(), 10);
    }

    void frozenPaths_data()
    {
        QTest::addColumn<int>("phaseIndex");
        QTest::newRow("opened") << 0;
        QTest::newRow("expanded") << 1;
        QTest::newRow("collapsed") << 2;
        QTest::newRow("reexpanded") << 3;
    }
    void frozenPaths()
    {
        QFETCH(int, phaseIndex);
        const auto phase = phases[phaseIndex].toObject();
        const auto web = phase["geometry"].toObject();
        const auto editor = web["editor"].toObject();
        const auto original = web["geometry"].toObject();
        const auto expected = original["anchors"].toArray();
        const auto frozenSamples = editor["samples"].toArray();
        const auto selected = selection(editor);
        const auto origin = point(editor["origin"]);
        const auto size = rectangle(editor["viewport"]).size();
        const auto oldDraws = phase["overlay"].toObject()["draws"].toArray();
        QJsonArray actual;
        int drawIndex = 0;
        QCOMPARE(frozenSamples.size(), 10);
        for (qsizetype i = 0; i < frozenSamples.size(); ++i) {
            const auto value = frozenSamples[i].toObject();
            const Geometry::Sample sample{anchor(value), rectangle(value["firstRect"]), rectangle(value["lastRect"])};
            const auto item = Geometry::webItem(sample, selected, origin, size.height());
            QCOMPARE(item, expected[i].toObject());
            QCOMPARE(item.keys(), QStringList({"active", "id", "visible", "y"}));
            QVERIFY(item["id"].isString());
            QVERIFY(item["y"].isDouble());
            QVERIFY(item["visible"].isBool());
            QVERIFY(item["active"].isBool());
            actual.append(item);
            const auto overlay = Geometry::overlay(sample, selected, size);
            if (!overlay.draw)
                continue;
            QVERIFY(drawIndex < oldDraws.size());
            const auto draw = oldDraws[drawIndex++].toObject();
            QCOMPARE(sample.anchor.id, draw["id"].toString());
            const auto calls = draw["calls"].toArray();
            QCOMPARE(calls.size(), 3);
            QStringList names;
            for (const auto& call : calls) names.append(call.toObject()["operation"].toString());
            QCOMPARE(names, QStringList({"fillRect", "setPen", "drawRoundedRect"}));
            QVERIFY(operation(calls, QStringLiteral("drawLine")).isEmpty());
            const auto band = operation(calls, QStringLiteral("fillRect"));
            QCOMPARE(band.keys(), QStringList({"color", "operation", "rect"}));
            QCOMPARE(overlay.band, rectangle(band["rect"]));
            QCOMPARE(band["color"].toArray(), QJsonArray({52, 120, 212, 18}));
            const auto pen = operation(calls, QStringLiteral("setPen"));
            QCOMPARE(pen, QJsonObject({{"operation", "setPen"}, {"pen", QJsonObject{
                {"brushStyle", 1}, {"cap", 16}, {"color", QJsonArray{52, 120, 212, 180}},
                {"cosmetic", false}, {"join", 64}, {"style", 1}, {"width", 1.5}}}}));
            QCOMPARE(overlay.leading, QLine());
            QCOMPARE(overlay.trailing, QLine());
            const auto outline = operation(calls, QStringLiteral("drawRoundedRect"));
            QCOMPARE(outline.keys(), QStringList({"operation", "radius", "rect", "sizeMode"}));
            QCOMPARE(overlay.outline, rectangle(outline["rect"]));
            QCOMPARE(outline["radius"].toArray(), QJsonArray({2, 2}));
            QCOMPARE(outline["sizeMode"].toInt(), int(Qt::AbsoluteSize));
        }
        QCOMPARE(drawIndex, oldDraws.size());
        auto result = original;
        result.insert("anchors", actual);
        QCOMPARE(result, original);
        QCOMPARE(QJsonDocument(result).toJson(QJsonDocument::Compact), web["compact"].toString().toUtf8());
    }

    void activeBoundaries_data()
    {
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("end");
        QTest::addColumn<QString>("selected");
        QTest::addColumn<bool>("available");
        QTest::addColumn<bool>("expected");
        QTest::newRow("id-only") << 30 << 40 << QString("a") << true << true;
        QTest::newRow("range-overlap") << 5 << 15 << QString() << true << true;
        QTest::newRow("empty-inside") << 15 << 15 << QString() << true << true;
        QTest::newRow("empty-at-start") << 10 << 10 << QString() << true << true;
        QTest::newRow("empty-at-end") << 20 << 20 << QString() << true << true;
        QTest::newRow("touch-start") << 5 << 10 << QString() << true << true;
        QTest::newRow("touch-end") << 20 << 25 << QString() << true << true;
        QTest::newRow("before") << 0 << 9 << QString() << true << false;
        QTest::newRow("after") << 21 << 30 << QString() << true << false;
        QTest::newRow("missing-selected") << 15 << 15 << QString("a") << false << true;
        QTest::newRow("missing-overlap") << 15 << 15 << QString() << false << false;
    }
    void activeBoundaries()
    {
        QFETCH(int, start); QFETCH(int, end); QFETCH(QString, selected);
        QFETCH(bool, available); QFETCH(bool, expected);
        const Geometry::Sample sample{{"a", available, 10, 20}, QRect(4, 6, 1, 20), QRect(9, 6, 1, 20)};
        const Geometry::Selection selection{start, end, selected};
        QCOMPARE(Geometry::active(sample.anchor, selection), expected);
        const auto web = Geometry::webItem(sample, selection, QPoint(7, 32), 40);
        QCOMPARE(web["active"].toBool(), expected);
        QCOMPARE(web["y"].toInt(), available ? 47 : 32);
        QCOMPARE(web["visible"].toBool(), available);
        QCOMPARE(Geometry::overlay(sample, selection, QSize(200, 40)).draw, available && expected);
    }

    void visibilityAndClipping_data()
    {
        QTest::addColumn<QRect>("first"); QTest::addColumn<QRect>("last");
        QTest::addColumn<int>("y"); QTest::addColumn<bool>("visible"); QTest::addColumn<bool>("draw");
        QTest::newRow("above") << QRect(4,-10,1,10) << QRect(4,-10,1,10) << 27 << false << false;
        QTest::newRow("touch-upper") << QRect(4,-9,1,10) << QRect(4,-9,1,10) << 28 << true << true;
        QTest::newRow("straddle-lower") << QRect(4,39,1,10) << QRect(4,39,1,10) << 75 << true << true;
        QTest::newRow("exact-lower") << QRect(4,40,1,10) << QRect(4,40,1,10) << 76 << false << true;
        QTest::newRow("below") << QRect(4,41,1,10) << QRect(4,41,1,10) << 77 << false << false;
        QTest::newRow("spanning") << QRect(4,-20,1,10) << QRect(12,20,1,10) << 17 << false << true;
        QTest::newRow("hidden-empty") << QRect() << QRect() << 32 << false << false;
        QTest::newRow("visible-start-hidden-end") << QRect(4,8,1,10) << QRect() << 44 << true << false;
    }
    void visibilityAndClipping()
    {
        QFETCH(QRect, first); QFETCH(QRect, last); QFETCH(int, y); QFETCH(bool, visible); QFETCH(bool, draw);
        const Geometry::Sample sample{{"a",true,10,20},first,last};
        const Geometry::Selection selected{0,0,"a"};
        const auto web = Geometry::webItem(sample, selected, QPoint(7,32),40);
        QCOMPARE(web["y"].toInt(), y);
        QCOMPARE(web["visible"].toBool(), visible);
        const auto overlay = Geometry::overlay(sample, selected, QSize(200,40));
        QCOMPARE(overlay.draw, draw);
        if (draw) {
            QCOMPARE(overlay.leading, QLine());
            QCOMPARE(overlay.trailing, QLine());
            QVERIFY(overlay.leading.isNull() && overlay.trailing.isNull());
        }
    }

    void actualSamplingAndClamping()
    {
        MyCodeEditor editor;
        prepareEditor(editor, source);
        QTextCursor selected(editor.document()); selected.setPosition(194); selected.setPosition(209,QTextCursor::KeepAnchor);
        editor.setTextCursor(selected);
        const auto selection = Geometry::sampleSelection(editor,"inside-reset");
        QCOMPARE(selection.start,194); QCOMPARE(selection.end,209); QCOMPARE(selection.selected,QString("inside-reset"));
        for (const auto& a : anchors) {
            const auto sampled = Geometry::sampleAnchor(editor,a,true);
            QTextCursor cursor(editor.document());cursor.setPosition(a.start);
            QCOMPARE(sampled.first,editor.cursorRect(cursor));
            cursor.setPosition(a.end);QCOMPARE(sampled.last,editor.cursorRect(cursor));
            QCOMPARE(Geometry::sampleAnchor(editor,a).last,QRect());
        }
        const Geometry::Anchor outside{"clamped",true,-100,INT_MAX};
        auto sampled=Geometry::sampleAnchor(editor,outside,true);
        QTextCursor cursor(editor.document());cursor.setPosition(0);QCOMPARE(sampled.first,editor.cursorRect(cursor));
        cursor.setPosition(editor.document()->characterCount()-1);QCOMPARE(sampled.last,editor.cursorRect(cursor));
        const auto missing=Geometry::sampleAnchor(editor,{"missing",false,-100,INT_MAX},true);
        QCOMPARE(missing.first,QRect());QCOMPARE(missing.last,QRect());
        editor.setPlainText(QString());editor.acceptLoadedTextAsSemanticBaseline();
        sampled=Geometry::sampleAnchor(editor,outside,true);
        cursor=QTextCursor(editor.document());
        QCOMPARE(sampled.first,editor.cursorRect(cursor));QCOMPARE(sampled.last,editor.cursorRect(cursor));
        QCOMPARE(editor.visibleRowForSourceLine(0),0);
        QCOMPARE(editor.visibleRowForSourceLine(1),-1);
    }

    void scrolling()
    {
        MyCodeEditor editor;prepareEditor(editor,source);materializeLines(editor);
        const QPoint origin(62,32);const Geometry::Selection selection{};
        editor.verticalScrollBar()->setValue(0);
        const auto initial=Geometry::sampleAnchor(editor,anchors.first(),true);
        QVERIFY(Geometry::webItem(initial,selection,origin,editor.viewport()->height())["visible"].toBool());
        QVERIFY(!Geometry::webItem(Geometry::sampleAnchor(editor,anchors.last()),selection,origin,editor.viewport()->height())["visible"].toBool());
        QVERIFY(editor.verticalScrollBar()->maximum()>8);
        editor.verticalScrollBar()->setValue(8);QCoreApplication::processEvents();
        const auto scrolled=Geometry::sampleAnchor(editor,anchors.first(),true);
        const auto web=Geometry::webItem(scrolled,selection,origin,editor.viewport()->height());
        QVERIFY(!web["visible"].toBool());QVERIFY(scrolled.first.center().y()<initial.first.center().y());
        QTextCursor cursor(editor.document());cursor.setPosition(anchors.first().start);
        QCOMPARE(web["y"].toInt(),32+editor.cursorRect(cursor).center().y());
        editor.verticalScrollBar()->setValue(editor.verticalScrollBar()->maximum());QCoreApplication::processEvents();
        const auto last=Geometry::sampleAnchor(editor,anchors.last(),true);
        QVERIFY(Geometry::webItem(last,selection,origin,editor.viewport()->height())["visible"].toBool());
    }

    void foldingProjectionAndReadOnlyAccessor()
    {
        MyCodeEditor editor;prepareEditor(editor,source);
        const auto a=anchors.at(5);QCOMPARE(a.id,QString("inside-reset"));
        const auto before=Geometry::sampleAnchor(editor,a,true);
        QCOMPARE(editor.visibleRowForSourceLine(-1),-1);
        QCOMPARE(editor.visibleRowForSourceLine(editor.document()->blockCount()),-1);
        for(int row=0;row<editor.document()->blockCount();++row)QCOMPARE(editor.visibleRowForSourceLine(row),row);
        QVERIFY(editor.toggleFoldAtLineForTest(7));
        QVERIFY(editor.foldCollapsedAtLineForTest(7));
        const auto text=editor.toPlainText();const auto cursor=editor.textCursor();const int scroll=editor.verticalScrollBar()->value();
        QCOMPARE(editor.visibleRowForSourceLine(7),7);
        for(int row=8;row<=15;++row){QVERIFY(!editor.sourceLineVisible(row));QCOMPARE(editor.visibleRowForSourceLine(row),-1);}
        QCOMPARE(editor.visibleRowForSourceLine(16),8);
        QCOMPARE(editor.toPlainText(),text);QCOMPARE(editor.textCursor().position(),cursor.position());
        QCOMPARE(editor.textCursor().anchor(),cursor.anchor());QCOMPARE(editor.verticalScrollBar()->value(),scroll);
        const auto hidden=Geometry::sampleAnchor(editor,a,true);
        QCOMPARE(hidden.first,QRect());QCOMPARE(hidden.last,QRect());
        const Geometry::Selection selected{0,0,a.id};
        QCOMPARE(Geometry::webItem(hidden,selected,QPoint(62,32),editor.viewport()->height()),
                 QJsonObject({{"id",a.id},{"y",32},{"visible",false},{"active",true}}));
        QVERIFY(!Geometry::overlay(hidden,selected,editor.viewport()->size()).draw);
        const auto spanning=Geometry::sampleAnchor(editor,anchors.at(4),true);
        QVERIFY(!spanning.first.isEmpty());QCOMPARE(spanning.last,QRect());
        QVERIFY(!Geometry::overlay(spanning,{0,0,anchors.at(4).id},editor.viewport()->size()).draw);
        QVERIFY(editor.toggleFoldAtLineForTest(7));QVERIFY(!editor.viewProjectionActive());
        QCOMPARE(editor.visibleRowForSourceLine(9),9);
        QCOMPARE(Geometry::sampleAnchor(editor,a,true).first,before.first);
        QCOMPARE(Geometry::sampleAnchor(editor,a,true).last,before.last);
    }

    void noWrapWidthInvariant()
    {
        MyCodeEditor editor;prepareEditor(editor,source);materializeLines(editor);
        editor.verticalScrollBar()->setValue(20);QCoreApplication::processEvents();
        const auto font=editor.font();const auto dpr=editor.devicePixelRatioF();
        const auto height=editor.viewport()->height();const auto scroll=editor.verticalScrollBar()->value();
        const bool horizontal=editor.horizontalScrollBar()->isVisible();
        const int horizontalValue=editor.horizontalScrollBar()->value();const int width=editor.viewport()->width();
        QVERIFY(scroll>0);QVERIFY(scroll<editor.verticalScrollBar()->maximum());
        QVERIFY(horizontal);QCOMPARE(editor.lineWrapMode(),QPlainTextEdit::NoWrap);
        QList<int> relativeY,webY;const QPoint origin(62,32);const auto selected=Geometry::sampleSelection(editor,{});
        for(const auto& a:anchors){const auto s=Geometry::sampleAnchor(editor,a);relativeY.append(s.first.center().y());webY.append(Geometry::webItem(s,selected,origin,height)["y"].toInt());}
        editor.resize(520,editor.height());QCoreApplication::processEvents();materializeLines(editor);
        QVERIFY(editor.viewport()->width()<width);QCOMPARE(editor.viewport()->height(),height);
        QCOMPARE(editor.font(),font);QCOMPARE(editor.devicePixelRatioF(),dpr);
        QCOMPARE(editor.verticalScrollBar()->value(),scroll);QCOMPARE(editor.horizontalScrollBar()->isVisible(),horizontal);
        QCOMPARE(editor.horizontalScrollBar()->value(),horizontalValue);QVERIFY(!editor.viewProjectionActive());
        qInfo().noquote()<<"NoWrap preconditions: font="<<font.toString()<<"dpr="<<dpr<<"viewportHeight="<<height
            <<"verticalBefore/After="<<scroll<<editor.verticalScrollBar()->value()<<"horizontalBefore/After="<<horizontal<<editor.horizontalScrollBar()->isVisible()
            <<"origin="<<origin<<"widthBefore/After="<<width<<editor.viewport()->width();
        for(qsizetype i=0;i<anchors.size();++i){const auto s=Geometry::sampleAnchor(editor,anchors[i]);QCOMPARE(s.first.center().y(),relativeY[i]);QCOMPARE(Geometry::webItem(s,selected,origin,height)["y"].toInt(),webY[i]);}
    }
};

QTEST_MAIN(LinkedAnchorGeometryTest)
#include "linked_anchor_geometry_test.moc"
