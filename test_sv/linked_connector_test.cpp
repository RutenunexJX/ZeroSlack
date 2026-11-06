#include "../src/ui/linkedconnectorpath.h"
#include "linkedconnectoroverlay.h"
#include "linkedcontextcontroller.h"
#include "linkednativerails.h"
#include "mycodeeditor.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "tabmanager.h"
#include "testuistyle.h"

#include <QApplication>
#include <QAbstractAnimation>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <QtTest>
#include <QtTest/qtestwheel.h>
#include <cmath>
#include <limits>
#include <memory>

using namespace LinkedConnectorPath;
Q_DECLARE_METATYPE(LinkedConnectorPath::Input)
Q_DECLARE_METATYPE(LinkedConnectorPath::Reason)

namespace {
Input sample()
{
    return {QStringLiteral("anchor"), Side::Left, 45, QPointF(80, 130),
            QRectF(200, 20, 300, 240), QRectF(0, 0, 800, 300), true, true, true, false};
}

bool segmentsAvoidInterior(const QPolygonF& points, const QRectF& rect)
{
    for (qsizetype i = 1; i < points.size(); ++i) {
        const auto a = points[i - 1], z = points[i];
        if (a.x() == z.x()) {
            if (a.x() > rect.left() && a.x() < rect.right()
                && qMax(a.y(), z.y()) > rect.top() && qMin(a.y(), z.y()) < rect.bottom()) return false;
        } else if (a.y() == z.y()) {
            if (a.y() > rect.top() && a.y() < rect.bottom()
                && qMax(a.x(), z.x()) > rect.left() && qMin(a.x(), z.x()) < rect.right()) return false;
        } else return false;
    }
    return true;
}

struct NativeEnvironment {
    const char* name = "ZEROSLACK_LINKED_NATIVE_RAILS";
    bool existed = qEnvironmentVariableIsSet(name);
    QByteArray previous = qgetenv(name);
    NativeEnvironment() { qputenv(name, "1"); }
    ~NativeEnvironment() { if (existed) qputenv(name, previous);else qunsetenv(name); }
};

bool writeFixture(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

struct ConnectorHarness {
    QTemporaryDir temp{QDir::tempPath() + "/connector-XXXXXX"};
    QMainWindow window;
    QStackedWidget* stack = new QStackedWidget(&window);
    QWidget* page = new QWidget(stack);
    QTabWidget* tabWidget = new QTabWidget(page);
    PinloomCodeLinkStore links;
    std::unique_ptr<TabManager> tabs;
    std::unique_ptr<PinloomHostClient> client;
    std::unique_ptr<LinkedContextController> controller;
    QString file, text = QStringLiteral("module connector_fixture;\nlogic [31:0] before_word, anchor_word, after_word;\nlogic another_word;\nendmodule\n");
    bool ready = false;
    bool defer = false;
    QList<std::function<void()>> pending;

    explicit ConnectorHarness(const QString& content = {})
    {
        if (!temp.isValid()) return;
        if (!content.isEmpty()) text = content;
        file = temp.filePath("fixture.sv");if (!writeFixture(file, text.toUtf8())) return;
        window.setCentralWidget(stack);page->setObjectName("connectorFixtureEditorPage");
        auto* layout = new QVBoxLayout(page);layout->setContentsMargins(0, 0, 0, 0);layout->addWidget(tabWidget);
        stack->addWidget(page);tabs = std::make_unique<TabManager>(tabWidget);
        tabs->setWorkspaceScope({temp.path()}, temp.path());links.setWorkspaceRoot(temp.path());
        client = std::make_unique<PinloomHostClient>([this](const QJsonObject& request, PinloomHostClient::RawReplyHandler reply) {
            const auto identity = request["params"].toObject()["identity"].toObject();
            const QJsonObject result{{"entry", QJsonObject{{"identity", identity}, {"title", "Connector fixture"}, {"type", "text"}, {"location", "Task fixture"}}},
                                     {"content", "Controlled reference"}, {"contentType", "text/plain"}};
            std::function<void()> complete = [reply = std::move(reply), result] { reply(result, {}); };
            if (defer) pending.append(std::move(complete));
            else QTimer::singleShot(0, client.get(), std::move(complete));
        });
        controller = std::make_unique<LinkedContextController>(&window, stack, page, tabs.get(), &links, client.get(), QList<QWidget*>{});
        if (!tabs->openFileInTab(file)) return;
        editor()->setFont(QFont(QStringLiteral("Consolas"), 10));
        editor()->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        editor()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        window.resize(1440, 720);window.show();QCoreApplication::processEvents();ready = true;
    }
    ~ConnectorHarness() { controller.reset();tabs.reset();client.reset();window.close(); }
    MyCodeEditor* editor() const { return tabs->getCurrentEditor(); }
    LinkedNativeRails* rails() const { return window.findChild<LinkedNativeRails*>(); }
    LinkedConnectorOverlay* connector() const { return window.findChild<LinkedConnectorOverlay*>(); }
    bool resourcesComplete() const {
        const auto values = controller->viewSnapshot()["resources"].toArray();
        if (values.isEmpty()) return false;
        for (const auto& value : values) if (!value.toObject()["complete"].toBool()) return false;
        return true;
    }
    void releasePending() { auto replies = std::move(pending);pending.clear();for (const auto& reply : replies) reply(); }
    QJsonObject anchor(const QString& id, int first, int last, int count = 1) const
    {
        auto source = PinloomSourceSelection::fromDocumentSelection(temp.path(), file, "connector_fixture", text, first, last);
        source.anchorId = id;QJsonArray references;
        for (int i = 0; i < count; ++i) {
            PinloomHostIdentity identity;identity.entryId = "connector-" + id + QString::number(i);
            references.append(QJsonObject{{"id", "reference" + QString::number(i)}, {"title", id},
                {"uri", identity.toUri().toString(QUrl::FullyEncoded)}, {"identity", QJsonObject{{"entryId", identity.entryId}}}});
        }
        return {{"id", id}, {"source", QJsonObject::fromVariantMap(source.toVariantMap())},
                {"links", references}};
    }
    bool store(const QJsonArray& rows)
    {
        return writeFixture(links.storagePath(), QJsonDocument(QJsonObject{{"schema", "ZeroSlack.PinloomCodeLinks"}, {"version", 2}, {"anchors", rows}}).toJson()) && links.reload();
    }
};

QImage transparentImage(const QSize& logical, qreal dpr)
{
    QImage result(QSize(qCeil(logical.width() * dpr), qCeil(logical.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(dpr);result.fill(Qt::transparent);return result;
}

QImage renderProduction(QWidget* widget, qreal dpr)
{
    auto result = transparentImage(widget->size(), dpr);QPainter painter(&result);
    widget->render(&painter, QPoint(), QRegion(widget->rect()), QWidget::RenderFlags());
    return result;
}

qsizetype pixelDifferences(const QImage& a, const QImage& z)
{
    if (a.size() != z.size() || a.format() != z.format()) return -1;
    qsizetype count = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) count += a.pixel(x, y) != z.pixel(x, y);
    return count;
}

QJsonObject savePixels(const QImage& image, const QString& suffix)
{
    QString root = qEnvironmentVariable("ZEROSLACK_MANAGED_TEST_LOG_ROOT");
    if (root.isEmpty()) root = QDir::tempPath() + "/connector-pixels-" + QString::number(QCoreApplication::applicationPid());
    const QString directory = QDir(root).filePath("connector-pixels");
    if (!QDir().mkpath(directory)) return {};
    const QString name = QString::fromLatin1(QTest::currentTestFunction()) + '-' + QString::fromLatin1(QTest::currentDataTag()) + '-' + suffix + ".png";
    const QString path = QDir(directory).filePath(name);
    if (!image.save(path, "PNG")) return {};
    QFile file(path);if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.readAll();if (!bytes.startsWith("\x89PNG\r\n\x1a\n")) return {};
    return {{"path", path}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"width", image.width()}, {"height", image.height()}, {"dpr", image.devicePixelRatio()}, {"format", "PNG"}};
}

qsizetype inkInside(const QImage& image, const QRectF& rect)
{
    qsizetype count = 0;
    const qreal dpr = image.devicePixelRatio();
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const QPointF center((x + .5) / dpr, (y + .5) / dpr);
        if (center.x() > rect.left() && center.x() < rect.right()
            && center.y() > rect.top() && center.y() < rect.bottom()) count += qAlpha(image.pixel(x, y)) != 0;
    }
    return count;
}

bool paintedCurrent(const LinkedConnectorOverlay* overlay)
{
    return overlay && overlay->completion().paintSerial > 0
        && overlay->completion().geometrySerial == overlay->geometrySerial()
        && overlay->completion().identity.requestSerial == overlay->scene().identity.requestSerial;
}

QString manyLines()
{
    QString text = "module connector_fixture;\n";
    for (int i = 0; i < 120; ++i) text += QString("logic signal_%1;\n").arg(i);
    return text + "endmodule\n";
}

bool hitIsWithin(QWidget* target, const QPoint& global)
{
    auto* hit = QApplication::widgetAt(global);
    return hit && (hit == target || target->isAncestorOf(hit));
}

QJsonObject editorMotion(MyCodeEditor* editor)
{
    return {{"position", editor->textCursor().position()}, {"anchor", editor->textCursor().anchor()},
        {"vertical", editor->verticalScrollBar()->value()}, {"horizontal", editor->horizontalScrollBar()->value()}};
}

struct MotionWatch {
    MyCodeEditor* editor;
    QJsonObject before;
    QSignalSpy cursor, selection, vertical, horizontal;
    explicit MotionWatch(MyCodeEditor* value) : editor(value), before(editorMotion(value)),
        cursor(value, &QPlainTextEdit::cursorPositionChanged), selection(value, &QPlainTextEdit::selectionChanged),
        vertical(value->verticalScrollBar(), &QScrollBar::valueChanged), horizontal(value->horizontalScrollBar(), &QScrollBar::valueChanged) {}
    bool unchanged() const { return before == editorMotion(editor) && cursor.isEmpty() && selection.isEmpty() && vertical.isEmpty() && horizontal.isEmpty(); }
};

QJsonArray activeIds(const ConnectorHarness& h) { return h.rails()->snapshot()["activeAnchorIds"].toArray(); }
QJsonArray displayedIds(const ConnectorHarness& h) { return h.rails()->snapshot()["displayHighlightedMembers"].toArray(); }
QJsonObject groupFor(const ConnectorHarness& h, const QString& member)
{
    for (const auto& value : h.rails()->snapshot()["presentation"].toArray())
        if (value.toObject()["members"].toArray().contains(member)) return value.toObject();
    return {};
}

QFrame* cardFor(const ConnectorHarness& h, const QString& id)
{
    for (auto* card : h.rails()->findChildren<QFrame*>("linkedNativeCard"))
        if (card->property("referenceId").toString() == id) return card;
    return nullptr;
}

QJsonObject presentationGeometry(const ConnectorHarness& h)
{
    QJsonObject result;
    for (const auto& value : h.rails()->snapshot()["presentation"].toArray()) {
        auto group = value.toObject();group.remove("highlighted");result[group["id"].toString()] = group;
    }
    return result;
}
}

class LinkedConnectorTest final : public QObject {
    Q_OBJECT
private slots:
    void exactEndpointsAndThreeSegments();
    void completeSegmentsAvoidCentralInterior();
    void crossingDetectorRejectsLegalEndpoints();
    void mirror_data();
    void mirror();
    void widthChangesKeepEndpointY();
    void invalidInput_data();
    void invalidInput();
    void duplicateIdsAndEmptyCollections();
    void highlightOnlyChangesPaintState();
    void anchorOverlayPixels_data();
    void anchorOverlayPixels();
    void connectorPixels_data();
    void connectorPixels();
    void actualEndpointsAndScrollNotification();
    void controllerSceneAndRevision();
    void overlayPassesActualHitTesting();
    void selectionHighlightsAndPreviewArrival();
    void aggregateHighlightsAndSessionClear();
    void detachedSelectionAndTypingNeverFollow();
};

void LinkedConnectorTest::exactEndpointsAndThreeSegments()
{
    const auto input = sample();
    const auto result = build(input);
    QVERIFY(result.draw);
    QCOMPARE(result.reason, Reason::None);
    QCOMPARE(result.id, input.id);
    QCOMPARE(result.points, QPolygonF({{200, 45}, {140, 45}, {140, 130}, {80, 130}}));
    QCOMPARE(result.points.first().y(), input.codeY);
    QCOMPARE(result.points.last(), input.card);
}

void LinkedConnectorTest::completeSegmentsAvoidCentralInterior()
{
    for (const auto side : {Side::Left, Side::Right}) {
        auto input = sample();input.side = side;input.card.setX(side == Side::Left ? 0 : 800);
        for (const auto y : {20., 45., 260.}) {
            input.codeY = y;
            const auto result = build(input);
            QVERIFY(result.draw);
            QVERIFY(segmentsAvoidInterior(result.points, input.central));
            QVERIFY(staysOutside(result.points, input.central));
        }
    }
}

void LinkedConnectorTest::crossingDetectorRejectsLegalEndpoints()
{
    const QRectF central(200, 20, 300, 240);
    const QPolygonF horizontal{{190, 100}, {510, 100}};
    QVERIFY(!central.contains(horizontal.first()));
    QVERIFY(!central.contains(horizontal.last()));
    QVERIFY(!staysOutside(horizontal, central));
    QVERIFY(!staysOutside({{190, 10}, {510, 270}}, central));
    QVERIFY(!staysOutside({{350, 10}, {350, 270}}, central));
    QVERIFY(staysOutside({{200, 20}, {200, 260}}, central));
    QVERIFY(staysOutside({{190, 20}, {510, 20}}, central));
    QVERIFY(!staysOutside({{300, 100}, {300, 100}}, central));
    QVERIFY(!staysOutside({}, central));
}

void LinkedConnectorTest::mirror_data()
{
    QTest::addColumn<double>("gap");QTest::addColumn<double>("cardY");
    QTest::newRow("ordinary") << 120. << 130.;
    QTest::newRow("same-y") << 120. << 45.;
    QTest::newRow("subpixel-gap") << .001 << 260.;
    QTest::newRow("edge-of-overlay") << 200. << 0.;
}

void LinkedConnectorTest::mirror()
{
    QFETCH(double, gap);QFETCH(double, cardY);
    auto left = sample();left.card = {200 - gap, cardY};
    auto right = left;right.side = Side::Right;right.card.setX(800 - left.card.x());
    right.central = QRectF(800 - left.central.right(), left.central.top(), left.central.width(), left.central.height());
    const auto a = build(left), z = build(right);
    QVERIFY(a.draw);QVERIFY(z.draw);QCOMPARE(a.points.size(), 4);QCOMPARE(z.points.size(), 4);
    for (qsizetype i = 0; i < a.points.size(); ++i) {
        QCOMPARE(z.points[i].x(), 800 - a.points[i].x());
        QCOMPARE(z.points[i].y(), a.points[i].y());
    }
    QVERIFY(segmentsAvoidInterior(a.points, left.central));
    QVERIFY(segmentsAvoidInterior(z.points, right.central));
}

void LinkedConnectorTest::widthChangesKeepEndpointY()
{
    for (const auto side : {Side::Left, Side::Right}) {
        auto input = sample();input.side = side;
        for (const int width : {30, 300, 500}) {
            input.central = QRectF(100, 20, width, 240);
            input.card.setX(side == Side::Left ? 50 : width + 150);
            const auto result = build(input);QVERIFY(result.draw);
            QCOMPARE(result.points.first().y(), input.codeY);
            QCOMPARE(result.points.last().y(), input.card.y());
            QCOMPARE(result.points.last().x(), input.card.x());
        }
    }
}

void LinkedConnectorTest::invalidInput_data()
{
    QTest::addColumn<Input>("input");QTest::addColumn<Reason>("reason");
    const auto row = [](const char* name, Input value, Reason reason) { QTest::newRow(name) << value << reason; };
    auto v = sample();v.id.clear();row("empty-id", v, Reason::EmptyId);
    v = sample();v.positioned = false;row("unpositioned", v, Reason::NotPositioned);
    v = sample();v.codeVisible = false;row("code-offscreen", v, Reason::EndpointInvisible);
    v = sample();v.cardVisible = false;row("card-offscreen", v, Reason::EndpointInvisible);
    v = sample();v.central.setWidth(0);row("zero-central-width", v, Reason::InvalidGeometry);
    v = sample();v.central.setHeight(0);row("zero-central-height", v, Reason::InvalidGeometry);
    v = sample();v.central.setWidth(-2);row("negative-central-width", v, Reason::InvalidGeometry);
    v = sample();v.overlay.setWidth(0);row("zero-overlay-width", v, Reason::InvalidGeometry);
    v = sample();v.overlay.setHeight(0);row("zero-overlay-height", v, Reason::InvalidGeometry);
    v = sample();v.codeY = std::numeric_limits<double>::quiet_NaN();row("nan-code-y", v, Reason::InvalidGeometry);
    v = sample();v.codeY = std::numeric_limits<double>::infinity();row("infinite-code-y", v, Reason::InvalidGeometry);
    v = sample();v.card.setX(std::numeric_limits<double>::infinity());row("infinite-card-x", v, Reason::InvalidGeometry);
    v = sample();v.card.setY(std::numeric_limits<double>::quiet_NaN());row("nan-card-y", v, Reason::InvalidGeometry);
    v = sample();v.central.setX(std::numeric_limits<double>::quiet_NaN());row("nan-central-x", v, Reason::InvalidGeometry);
    v = sample();v.overlay.setWidth(std::numeric_limits<double>::infinity());row("infinite-overlay", v, Reason::InvalidGeometry);
    v = sample();v.side = static_cast<Side>(8);row("invalid-side", v, Reason::InvalidGeometry);
    v = sample();v.card.setX(230);row("left-wrong-half-plane", v, Reason::WrongSide);
    v = sample();v.side = Side::Right;v.card.setX(490);row("right-wrong-half-plane", v, Reason::WrongSide);
    v = sample();v.card.setX(200);row("zero-left-gap", v, Reason::ZeroGap);
    v = sample();v.side = Side::Right;v.card.setX(500);row("zero-right-gap", v, Reason::ZeroGap);
    v = sample();v.card.setY(-1);row("card-before-overlay", v, Reason::OutsideOverlay);
    v = sample();v.card.setY(301);row("card-after-overlay", v, Reason::OutsideOverlay);
    v = sample();v.card.setX(-1);row("card-outside-overlay-x", v, Reason::OutsideOverlay);
    v = sample();v.codeY = 19;row("code-before-central", v, Reason::OutsideOverlay);
    v = sample();v.codeY = 261;row("code-after-central", v, Reason::OutsideOverlay);
    v = sample();v.central.moveLeft(-1);row("central-outside-overlay", v, Reason::OutsideOverlay);
}

void LinkedConnectorTest::invalidInput()
{
    QFETCH(Input, input);QFETCH(Reason, reason);
    const auto result = build(input);
    QVERIFY(!result.draw);QVERIFY(result.points.isEmpty());QCOMPARE(result.reason, reason);
}

void LinkedConnectorTest::duplicateIdsAndEmptyCollections()
{
    QVERIFY(buildAll({}).isEmpty());
    auto a = sample(), z = sample();z.card.setY(220);
    const auto duplicates = buildAll({a, z});QCOMPARE(duplicates.size(), 2);
    QVERIFY(duplicates[0].draw);QVERIFY(!duplicates[1].draw);
    QCOMPARE(duplicates[1].reason, Reason::DuplicateId);QVERIFY(duplicates[1].points.isEmpty());
    z.id = QStringLiteral("other");const auto distinct = buildAll({a, z});
    QCOMPARE(distinct.size(), 2);QVERIFY(distinct[0].draw);QVERIFY(distinct[1].draw);
    a.codeVisible = false;z.cardVisible = false;
    for (const auto& route : buildAll({a, z})) { QVERIFY(!route.draw);QVERIFY(route.points.isEmpty()); }
}

void LinkedConnectorTest::highlightOnlyChangesPaintState()
{
    auto input = sample();const auto normal = build(input);input.highlighted = true;const auto active = build(input);
    QVERIFY(normal.draw);QVERIFY(active.draw);QVERIFY(!normal.highlighted);QVERIFY(active.highlighted);
    QCOMPARE(normal.id, active.id);QCOMPARE(normal.points, active.points);QCOMPARE(normal.reason, active.reason);
}

void LinkedConnectorTest::anchorOverlayPixels_data()
{
    QTest::addColumn<double>("dpr");QTest::addColumn<int>("scenario");
    for (const double dpr : {1., 1.5, 2.})
        for (int scenario = 0; scenario < 3; ++scenario)
            QTest::newRow(qPrintable(QString("dpr%1-case%2").arg(dpr).arg(scenario))) << dpr << scenario;
}

void LinkedConnectorTest::anchorOverlayPixels()
{
    QFETCH(double, dpr);QFETCH(int, scenario);
    NativeEnvironment environment;ConnectorHarness h;QVERIFY(h.ready);
    const int first = scenario == 2 ? 0 : h.text.indexOf("anchor_word");
    const int last = first + (scenario == 2 ? 6 : 11);
    const int repetitions = scenario == 1 ? 2 : 1;
    QJsonArray rows;
    for (int i = 0; i < repetitions; ++i) rows.append(h.anchor(QString("a%1").arg(i), first, last));
    QVERIFY(h.store(rows));
    const auto originalChildren = h.editor()->viewport()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(h.controller->setActive(true));QVERIFY(h.rails());
    QTRY_COMPARE(h.controller->viewSnapshot()["references"].toArray().size(), repetitions);
    QWidget* overlay = nullptr;
    for (auto* child : h.editor()->viewport()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (!originalChildren.contains(child) && child->testAttribute(Qt::WA_TransparentForMouseEvents)
            && child->testAttribute(Qt::WA_NoSystemBackground)) { QVERIFY(!overlay);overlay = child; }
    QVERIFY(overlay);QTRY_COMPARE(overlay->size(), h.editor()->viewport()->size());
    QTextCursor selection(h.editor()->document());selection.setPosition(first);selection.setPosition(last, QTextCursor::KeepAnchor);
    h.editor()->setTextCursor(selection);
    QTextCursor start(h.editor()->document());start.setPosition(first);
    const QRect character = h.editor()->cursorRect(start);
    QVERIFY(character.top() > 0);QVERIFY(character.bottom() < overlay->height());
    QVERIFY(character.left() > 2);QVERIFY(character.right() + 8 < overlay->width());
    auto actual = renderProduction(overlay, dpr);
    auto expected = transparentImage(overlay->size(), dpr);
    {
        QPainter painter(&expected);painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipRect(QRect(QPoint(), overlay->size()));
        for (int i = 0; i < repetitions; ++i) {
            painter.fillRect(QRect(0, character.top(), overlay->width(), character.height()), QColor(52, 120, 212, 18));
            painter.setPen(QPen(QColor(52, 120, 212, 180), 1.5));
            painter.drawRoundedRect(character.adjusted(-2, -1, 2, 1), 2, 2);
        }
    }
    QJsonArray artifacts;
    for (const auto& item : {savePixels(actual, "actual"), savePixels(expected, "expected")}) { QVERIFY(!item.isEmpty());artifacts.append(item); }
    QCOMPARE(actual.size(), QSize(qCeil(overlay->width() * dpr), qCeil(overlay->height() * dpr)));
    QCOMPARE(pixelDifferences(actual, expected), 0);
    QVERIFY(pixelDifferences(expected, transparentImage(overlay->size(), dpr)) > 0);
    QCOMPARE(qAlpha(actual.pixel(0, 0)), 0);
    for (int side = 0; side < 3; ++side) {
        auto wrong = actual.copy();
        {
            QPainter painter(&wrong);painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(QColor(52, 120, 212, 180), 1.5));
            const int y = character.center().y();
            if (side == 0) painter.drawLine(QPointF(0, y), QPointF(character.x() - 3, y));
            else if (side == 1) painter.drawLine(QPointF(character.x() + 6, y), QPointF(overlay->width(), y));
            else painter.drawLine(QLineF(0, 0, 0, 0));
        }
        const auto evidence = savePixels(wrong, QString("wrong-%1").arg(side));QVERIFY(!evidence.isEmpty());artifacts.append(evidence);
        QVERIFY2(pixelDifferences(wrong, expected) > 0, "Pixel discriminator missed an injected guide or origin point");
    }
    const auto path = artifacts.first().toObject()["path"].toString();
    const QJsonObject report{{"case", QString::fromLatin1(QTest::currentDataTag())}, {"dpr", dpr}, {"bands", repetitions},
        {"logicalSize", QJsonArray{overlay->width(), overlay->height()}}, {"character", QJsonArray{character.x(), character.y(), character.width(), character.height()}},
        {"pixelDifferences", 0}, {"legalDecorationPresent", true}, {"leftGuideRejected", true}, {"rightGuideRejected", true}, {"originPointRejected", true}, {"artifacts", artifacts}};
    QVERIFY(writeFixture(path + ".json", QJsonDocument(report).toJson()));
    QVERIFY(h.controller->setActive(false));
}

void LinkedConnectorTest::connectorPixels_data()
{
    QTest::addColumn<double>("dpr");QTest::addColumn<double>("gap");
    for (const double dpr : {1., 1.5, 2.}) for (const double gap : {120., 1.25})
        QTest::newRow(qPrintable(QString("dpr%1-gap%2").arg(dpr).arg(gap))) << dpr << gap;
}

void LinkedConnectorTest::connectorPixels()
{
    QFETCH(double, dpr);QFETCH(double, gap);
    QWidget host;host.resize(800, 300);QTextDocument document;
    LinkedConnectorOverlay overlay(&host);host.show();overlay.show();
    LinkedConnectorOverlay::Scene scene;
    scene.valid = true;scene.identity.document = &document;scene.identity.documentRevision = document.revision();
    scene.identity.session = "raster";scene.identity.requestSerial = 18;
    scene.central = QRectF(201, 21, 300, 240);
    for (const auto side : {Side::Left, Side::Right}) {
        auto input = sample();input.id = side == Side::Left ? "left" : "right";input.side = side;
        input.central = scene.central;
        input.card.setX(side == Side::Left ? scene.central.left() - gap : scene.central.right() + gap);
        const qreal left = side == Side::Left ? input.card.x() : scene.central.right();
        scene.strokes.append({input, QRectF(left, 21, gap, 240)});
    }
    overlay.setScene(scene);QTRY_VERIFY(paintedCurrent(&overlay));
    QCOMPARE(overlay.completion().paths, 2);QCOMPARE(overlay.routes().size(), 2);
    const auto actual = renderProduction(&overlay, dpr);
    QCOMPARE(actual.size(), QSize(qCeil(800 * dpr), qCeil(300 * dpr)));
    QCOMPARE(inkInside(actual, scene.central), 0);
    for (qsizetype i = 0; i < scene.strokes.size(); ++i) {
        const auto& input = scene.strokes[i].input;
        const auto& route = overlay.routes()[i];QVERIFY(route.draw);
        QCOMPARE(route.points.first().y(), input.codeY);QCOMPARE(route.points.last(), input.card);
        QVERIFY(inkInside(actual, scene.strokes[i].clip) > 0);
        QVERIFY(inkInside(actual, QRectF(route.points.first() - QPointF(2, 2), QSizeF(4, 4))) > 0);
        QVERIFY(inkInside(actual, QRectF(input.card - QPointF(2, 2), QSizeF(4, 4))) > 0);
    }
    QJsonArray artifacts;const auto saved = savePixels(actual, "actual");QVERIFY(!saved.isEmpty());artifacts.append(saved);
    for (int side = 0; side < 2; ++side) {
        auto wrong = actual.copy();
        { QPainter painter(&wrong);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor(52, 120, 212, 145), 1.5));
          painter.drawLine(side == 0 ? QPointF(100, 45) : QPointF(700, 45), QPointF(350, 45)); }
        QVERIFY(inkInside(wrong, scene.central) > 0);
        const auto item = savePixels(wrong, QString("wrong-%1").arg(side));QVERIFY(!item.isEmpty());artifacts.append(item);
    }
    const QJsonObject report{{"centralAlphaPixels", 0}, {"leftVisible", true}, {"rightVisible", true},
        {"leftCrossingRejected", true}, {"rightCrossingRejected", true}, {"dpr", dpr}, {"gap", gap}, {"artifacts", artifacts}};
    QVERIFY(writeFixture(saved["path"].toString() + ".json", QJsonDocument(report).toJson()));
    const auto serial = overlay.geometrySerial();overlay.setScene({});
    QTRY_VERIFY(paintedCurrent(&overlay));QVERIFY(overlay.geometrySerial() > serial);
    QCOMPARE(overlay.completion().paths, 0);QVERIFY(!overlay.completion().valid);
    QCOMPARE(inkInside(renderProduction(&overlay, dpr), QRectF(overlay.rect())), 0);
    overlay.setScene(scene);document.setPlainText("changed before paint");
    QCOMPARE(inkInside(renderProduction(&overlay, dpr), QRectF(overlay.rect())), 0);
    QVERIFY(!overlay.completion().valid);
    QCOMPARE(overlay.completion().identity.requestSerial, quint64(18));
}

void LinkedConnectorTest::actualEndpointsAndScrollNotification()
{
    QWidget host;auto* layout = new QVBoxLayout(&host);layout->setContentsMargins(0, 0, 0, 0);
    auto* center = new QWidget;auto* rails = new LinkedNativeRails(center, &host);layout->addWidget(rails);
    rails->setInitialSizes({}, .5);host.resize(1200, 600);host.show();QTRY_VERIFY(rails->isVisible());
    QList<LinkedNativeRails::Group> groups;
    for (int i = 0; i < 8; ++i) {
        LinkedNativeRails::Group group;group.anchor = QString("g%1").arg(i);
        group.contentY = 80 + i * 4;group.viewportY = int(group.contentY);group.sourceLine = i;
        for (int j = 0; j < 2; ++j) group.references.append({group.anchor + QString::number(j), QString("source%1").arg(i), "Reference"});
        groups.append(group);
    }
    rails->setGroups(groups, 7);
    auto* left = rails->findChild<QScrollArea*>("linkedNativeLeftRail");
    auto* right = rails->findChild<QScrollArea*>("linkedNativeRightRail");QVERIFY(left);QVERIFY(right);
    QTRY_VERIFY(left->verticalScrollBar()->maximum() > 10);
    QTRY_VERIFY(!rails->snapshot()["visible"].toArray().isEmpty());
    const auto snapshot = rails->connectorSnapshot(&host);QCOMPARE(snapshot.endpoints.size(), 8);
    for (const auto& endpoint : snapshot.endpoints) {
        QWidget* group = nullptr;
        for (auto* item : rails->findChildren<QWidget*>("linkedNativeGroup"))
            if (item->property("anchorId").toString() == endpoint.id) group = item;
        QVERIFY(group);QLabel* title = nullptr;
        for (auto* item : group->findChildren<QLabel*>()) if (item->text() == "2 references") title = item;
        QVERIFY(title);
        const QRectF frame(group->mapTo(&host, QPoint()), group->size());
        const QRectF header(title->mapTo(&host, QPoint()), title->size());
        QCOMPARE(endpoint.header, QPointF(endpoint.side == 0 ? frame.right() : frame.left(), header.center().y()));
        QCOMPARE(endpoint.members, QStringList{endpoint.id});QVERIFY(endpoint.positioned);
    }
    const QJsonValue visible = rails->snapshot()["visible"];
    QSignalSpy applied(rails, &LinkedNativeRails::layoutApplied), presentation(rails, &LinkedNativeRails::presentationChanged);
    const int before = left->verticalScrollBar()->value();left->verticalScrollBar()->setValue(before + 1);
    const auto scrolled = rails->connectorSnapshot(&host);
    QCOMPARE(scrolled.layoutSerial, snapshot.layoutSerial);QVERIFY(scrolled.presentationSerial > snapshot.presentationSerial);
    QCOMPARE(applied.size(), 0);QCOMPARE(presentation.size(), 1);
    for (qsizetype i = 0; i < snapshot.endpoints.size(); ++i) {
        QCOMPARE(scrolled.endpoints[i].header.y(), snapshot.endpoints[i].header.y() - (snapshot.endpoints[i].side == 0 ? 1 : 0));
        QCOMPARE(scrolled.endpoints[i].visible, snapshot.endpoints[i].visible);
    }
    QCOMPARE(rails->snapshot()["visible"], visible);
    const auto sizes = rails->sizes();const auto* editorIdentity = rails->widget(1);
    rails->setGroupExpanded("g0", true);
    const auto expanded = rails->connectorSnapshot(&host);
    const auto find = [](const auto& value) { for (const auto& e : value.endpoints) if (e.id == "g0") return e;return LinkedNativeRails::ConnectorEndpoint{}; };
    const auto group = find(expanded);QVERIFY(!group.id.isEmpty());
    QWidget* expandedFrame = nullptr;
    for (auto* frame : rails->findChildren<QWidget*>("linkedNativeGroup"))
        if (frame->property("anchorId").toString() == "g0") expandedFrame = frame;
    QVERIFY(expandedFrame);QLabel* expandedTitle = nullptr;
    for (auto* title : expandedFrame->findChildren<QLabel*>()) if (title->text() == "2 references") expandedTitle = title;
    QVERIFY(expandedTitle);
    QCOMPARE(group.header.y(), QRectF(expandedTitle->mapTo(&host, QPoint()), expandedTitle->size()).center().y());
    QCOMPARE(rails->sizes(), sizes);QCOMPARE(rails->widget(1), editorIdentity);QCOMPARE(rails->count(), 3);
    // Each side maps its own actual viewport; deliberately make their top offsets different.
    right->parentWidget()->layout()->setContentsMargins(0, 13, 0, 0);
    QTRY_VERIFY(rails->connectorSnapshot(&host).viewports[0].top() != rails->connectorSnapshot(&host).viewports[1].top());
    const auto asymmetric = rails->connectorSnapshot(&host);
    QCOMPARE(asymmetric.viewports[0], QRectF(left->viewport()->mapTo(&host, QPoint()), left->viewport()->size()));
    QCOMPARE(asymmetric.viewports[1], QRectF(right->viewport()->mapTo(&host, QPoint()), right->viewport()->size()));
    rails->resetSession();QVERIFY(rails->connectorSnapshot(&host).endpoints.isEmpty());
}

void LinkedConnectorTest::controllerSceneAndRevision()
{
    NativeEnvironment environment;ConnectorHarness h(manyLines());QVERIFY(h.ready);
    QJsonArray rows;
    for (int i = 0; i < 8; ++i) { const int first = h.text.indexOf(QString("signal_%1;").arg(i));rows.append(h.anchor(QString("g%1").arg(i), first, first + 8)); }
    QVERIFY(h.store(rows));auto* editor = h.editor();auto* document = editor->document();
    QVERIFY(h.controller->setActive(true));auto* overlay = h.connector();QVERIFY(overlay);
    QTRY_VERIFY(h.resourcesComplete());
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paths >= 2);
    QCOMPARE(h.rails()->count(), 3);QCOMPARE(h.rails()->widget(1), h.page);
    QCOMPARE(overlay->parentWidget(), h.rails()->parentWidget());QVERIFY(overlay->parentWidget() != h.rails());
    const auto scene = overlay->scene();const auto endpoints = h.rails()->connectorSnapshot(overlay);
    QCOMPARE(scene.central, QRectF(h.page->mapTo(overlay->parentWidget(), QPoint()) - overlay->pos(), h.page->size()));
    QCOMPARE(scene.codeViewport, QRectF(editor->viewport()->mapTo(overlay->parentWidget(), QPoint()) - overlay->pos(), editor->viewport()->size()));
    QVERIFY(scene.central.contains(scene.codeViewport));QCOMPARE(scene.identity.contentRevision, qint64(editor->semanticDocumentRevision()));
    QCOMPARE(scene.identity.layoutSerial, endpoints.layoutSerial);QCOMPARE(scene.identity.document.data(), document);
    for (qsizetype i = 0; i < scene.strokes.size(); ++i) {
        QCOMPARE(scene.strokes[i].input.card, endpoints.endpoints[i].header);
        const auto block = document->findBlockByNumber(endpoints.endpoints[i].sourceLine);
        QCOMPARE(scene.strokes[i].input.codeY, scene.codeViewport.top() + editor->cursorRect(QTextCursor(block)).center().y());
    }
    QVERIFY(!scene.occlusions.isEmpty());
    for (const double dpr : {1., 1.5, 2.}) {
        const auto image = renderProduction(overlay, dpr);
        QCOMPARE(inkInside(image, scene.central), 0);
        QVERIFY(inkInside(image, QRectF(overlay->rect())) > 0);
        for (const auto& rect : scene.occlusions) QCOMPARE(inkInside(image, rect), 0);
        for (const auto& endpoint : endpoints.endpoints) QCOMPARE(inkInside(image, endpoint.groupRect), 0);
        const auto saved = savePixels(image, QString("real-dpr%1").arg(dpr));QVERIFY(!saved.isEmpty());
        QVERIFY(writeFixture(saved["path"].toString() + ".json", QJsonDocument(QJsonObject{
            {"actualController", true}, {"dpr", dpr}, {"centralAlphaPixels", 0},
            {"scrollbarAlphaPixels", 0}, {"groupInteriorAlphaPixels", 0}, {"legalPathsPresent", true}, {"artifact", saved}}).toJson()));
    }
    const auto sizes = h.rails()->sizes();const auto revision = scene.identity.contentRevision;
    const auto painted = overlay->completion().paintSerial;
    h.window.resize(1300, 680);QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paintSerial > painted);
    QCOMPARE(overlay->size(), overlay->parentWidget()->size());QCOMPARE(h.editor(), editor);QCOMPARE(editor->document(), document);
    const auto resized = h.rails()->sizes();QVERIFY(resized != sizes);
    const auto repaint = overlay->completion().paintSerial;overlay->update();
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paintSerial > repaint);QCOMPARE(h.rails()->sizes(), resized);
    h.window.hide();QVERIFY(!overlay->scene().valid);
    h.window.show();QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paths >= 2);
    QCOMPARE(h.rails()->sizes(), resized);
    QTextCursor cursor(document);cursor.movePosition(QTextCursor::End);cursor.insertText("// revision\n");
    QVERIFY(qint64(editor->semanticDocumentRevision()) > revision);QVERIFY(!overlay->scene().valid);
    QCOMPARE(inkInside(renderProduction(overlay, 1), QRectF(overlay->rect())), 0);
    QVERIFY(!overlay->completion().valid);
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().valid
        && overlay->completion().identity.contentRevision == qint64(editor->semanticDocumentRevision()));
    const auto oldSession = overlay->scene().identity.session;
    QVERIFY(h.store({}));h.controller->reloadBoard();
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paths == 0);
    QVERIFY(overlay->scene().identity.session != oldSession);QVERIFY(overlay->routes().isEmpty());
    QCOMPARE(inkInside(renderProduction(overlay, 1), QRectF(overlay->rect())), 0);
    const auto next = h.temp.filePath("other.sv");QVERIFY(writeFixture(next, "module other; endmodule\n"));
    QVERIFY(h.tabs->openFileInTab(next));
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().identity.document == h.editor()->document());
    QVERIFY(h.editor()->document() != document);QVERIFY(overlay->routes().isEmpty());
    QPointer<LinkedConnectorOverlay> tracked = overlay;QVERIFY(h.controller->setActive(false));QVERIFY(!tracked);
    QCOMPARE(h.stack->currentWidget(), h.page);QVERIFY(h.controller->setActive(true));
    QTRY_VERIFY(paintedCurrent(h.connector()));QVERIFY(h.connector()->routes().isEmpty());
    QVERIFY(h.controller->setActive(false));
}

void LinkedConnectorTest::overlayPassesActualHitTesting()
{
    NativeEnvironment environment;ConnectorHarness h(manyLines());QVERIFY(h.ready);
    QJsonArray rows;
    for (int i = 0; i < 8; ++i) { const int first = h.text.indexOf(QString("signal_%1;").arg(i));rows.append(h.anchor(QString("g%1").arg(i), first, first + 8, 3)); }
    QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));
    QTRY_VERIFY(paintedCurrent(h.connector()) && h.connector()->completion().paths > 0);
    auto* overlay = h.connector();QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(overlay->focusPolicy(), Qt::NoFocus);QVERIFY(overlay->findChildren<QWidget*>().isEmpty());
    auto* editor = h.editor();auto* document = editor->document();
    QTextCursor cursor(document);cursor.setPosition(h.text.indexOf("signal_2;"));
    const QPoint codeGlobal = editor->viewport()->mapToGlobal(editor->cursorRect(cursor).center());
    QVERIFY(hitIsWithin(editor->viewport(), codeGlobal));
    const int beforePosition = editor->textCursor().position();
    QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, h.window.mapFromGlobal(codeGlobal));
    QTRY_VERIFY(editor->textCursor().position() != beforePosition);
    auto* expand = static_cast<QPushButton*>(nullptr);
    for (auto* button : h.rails()->findChildren<QPushButton*>("linkedNativeExpand"))
        if (button->property("anchorId").toString() == "g2") expand = button;
    QVERIFY(expand);
    // The preceding real code click follows g2; wait for its actual hit target.
    QTRY_VERIFY(hitIsWithin(expand, expand->mapToGlobal(expand->rect().center())));
    const QPoint expandGlobal = expand->mapToGlobal(expand->rect().center());
    QVERIFY(hitIsWithin(expand, expandGlobal));
    QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, h.window.mapFromGlobal(expandGlobal));
    QTRY_COMPARE(expand->text(), QString("Collapse references"));
    QTRY_VERIFY(hitIsWithin(expand, expand->mapToGlobal(expand->rect().center())));
    const QPoint collapseGlobal = expand->mapToGlobal(expand->rect().center());QVERIFY(hitIsWithin(expand, collapseGlobal));
    QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, h.window.mapFromGlobal(collapseGlobal));
    QTRY_COMPARE(expand->text(), QString("Expand references"));
    auto* area = h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(area);
    QTRY_VERIFY(area->verticalScrollBar()->maximum() > 0);
    QTRY_VERIFY(paintedCurrent(overlay));
    const QPoint wheelGlobal = area->viewport()->mapToGlobal(QPoint(2, area->viewport()->height() / 2));
    QVERIFY(hitIsWithin(area->viewport(), wheelGlobal));
    const int scroll = area->verticalScrollBar()->value();
    const auto beforeScroll = overlay->scene();
    QTest::mouseMove(h.window.windowHandle(), h.window.mapFromGlobal(wheelGlobal));
    QTest::wheelEvent(h.window.windowHandle(), h.window.mapFromGlobal(wheelGlobal), QPoint(0, -120));
    QTRY_VERIFY(area->verticalScrollBar()->value() > scroll);
    const auto animationsStopped = [area] {
        for (const auto* animation : area->verticalScrollBar()->findChildren<QAbstractAnimation*>())
            if (animation->state() == QAbstractAnimation::Running) return false;
        return true;
    };
    QTRY_VERIFY(animationsStopped());
    QTRY_COMPARE(h.rails()->snapshot()["tracks"].toArray()[0].toObject()["state"].toString(), QString("Detached"));
    QTRY_VERIFY(paintedCurrent(overlay));
    const int scrollDelta = area->verticalScrollBar()->value() - scroll;QVERIFY(scrollDelta > 0);
    const auto afterScroll = overlay->scene();
    QCOMPARE(afterScroll.strokes.size(), beforeScroll.strokes.size());
    for (qsizetype i = 0; i < beforeScroll.strokes.size(); ++i) {
        QCOMPARE(afterScroll.strokes[i].input.id, beforeScroll.strokes[i].input.id);
        QCOMPARE(afterScroll.strokes[i].input.card.y(), beforeScroll.strokes[i].input.card.y()
            - (beforeScroll.strokes[i].input.side == Side::Left ? scrollDelta : 0));
        QCOMPARE(afterScroll.strokes[i].input.codeY, beforeScroll.strokes[i].input.codeY);
    }
    auto* handle = h.rails()->handle(1);const QPoint handleGlobal = handle->mapToGlobal(handle->rect().center());
    QVERIFY(hitIsWithin(handle, handleGlobal));const auto sizes = h.rails()->sizes();
    QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, h.window.mapFromGlobal(handleGlobal));
    QTest::mouseMove(h.window.windowHandle(), h.window.mapFromGlobal(handleGlobal + QPoint(40, 0)));
    QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, h.window.mapFromGlobal(handleGlobal + QPoint(40, 0)));
    QTRY_VERIFY(h.rails()->sizes() != sizes);QTRY_VERIFY(paintedCurrent(overlay));
    const auto dragged = h.rails()->sizes();const auto paint = overlay->completion().paintSerial;overlay->update();
    QTRY_VERIFY(paintedCurrent(overlay) && overlay->completion().paintSerial > paint);
    QCOMPARE(h.rails()->sizes(), dragged);QCOMPARE(h.rails()->count(), 3);
    QCOMPARE(h.editor(), editor);QCOMPARE(editor->document(), document);
    QVERIFY(h.controller->setActive(false));
}

void LinkedConnectorTest::selectionHighlightsAndPreviewArrival()
{
    NativeEnvironment environment;ConnectorHarness h(manyLines());QVERIFY(h.ready);h.defer = true;
    const int a = h.text.indexOf("signal_1;"), b = h.text.indexOf("signal_2;"), c = h.text.indexOf("signal_6;");
    auto first = h.anchor("a", a, a + 8, 2);auto links = first["links"].toArray();
    auto sameSource = links[0].toObject();sameSource["id"] = "reference1";links[1] = sameSource;first["links"] = links;
    QVERIFY(h.store({first, h.anchor("b", b, b + 8), h.anchor("c", c, c + 8)}));
    QVERIFY(h.controller->setActive(true));h.rails()->setGroupExpanded("a", true);
    QTRY_VERIFY(!h.pending.isEmpty());QTRY_VERIFY(paintedCurrent(h.connector()));
    QCOMPARE(activeIds(h), QJsonArray());QCOMPARE(displayedIds(h), QJsonArray());
    auto* overlay = h.connector();auto* editor = h.editor();
    QPointer<QFrame> a0 = cardFor(h, "a/reference0"), a1 = cardFor(h, "a/reference1"), b0 = cardFor(h, "b/reference0"), c0 = cardFor(h, "c/reference0");
    QVERIFY(a0);QVERIFY(a1);QVERIFY(b0);QVERIFY(c0);QVERIFY(a0 != a1);
    const auto normalA = renderProduction(a0, 1), normalB = renderProduction(b0, 1), normalC = renderProduction(c0, 1);
    const auto normalLines = renderProduction(overlay, 1);
    const auto geometry = presentationGeometry(h);const auto sizes = h.rails()->sizes();
    const auto tracks = h.rails()->snapshot()["tracks"].toArray();
    const auto revision = editor->semanticDocumentRevision();const bool modified = editor->document()->isModified();
    QTextCursor selection(editor->document());selection.setPosition(a);selection.setPosition(b + 8, QTextCursor::KeepAnchor);editor->setTextCursor(selection);
    MotionWatch selectedMotion(editor);
    QTRY_COMPARE(activeIds(h), QJsonArray({"a", "b"}));QTRY_VERIFY(paintedCurrent(overlay));
    QCOMPARE(displayedIds(h), QJsonArray({"a", "b"}));
    for (const auto& value : h.rails()->snapshot()["cards"].toArray()) {
        const auto card = value.toObject();QCOMPARE(card["highlighted"].toBool(), !card["id"].toString().startsWith("c/"));
    }
    for (const auto& route : overlay->routes()) QCOMPARE(route.highlighted, route.id != "c");
    const auto activeA = renderProduction(a0, 1), activeB = renderProduction(b0, 1), activeLines = renderProduction(overlay, 1);
    QVERIFY(pixelDifferences(normalA, activeA) > 0);QVERIFY(pixelDifferences(normalB, activeB) > 0);
    QCOMPARE(pixelDifferences(normalC, renderProduction(c0, 1)), 0);
    QVERIFY(pixelDifferences(normalLines, activeLines) > 0);QCOMPARE(inkInside(activeLines, overlay->scene().central), 0);
    QJsonArray artifacts;
    for (const auto& item : {savePixels(normalA, "a-normal"), savePixels(activeA, "a-active"),
        savePixels(normalLines, "lines-normal"), savePixels(activeLines, "lines-active")}) { QVERIFY(!item.isEmpty());artifacts.append(item); }
    QVERIFY(writeFixture(artifacts[0].toObject()["path"].toString() + ".json", QJsonDocument(QJsonObject{
        {"active", QJsonArray{"a", "b"}}, {"unrelatedPixelsUnchanged", true}, {"centralAlphaPixels", 0}, {"artifacts", artifacts}}).toJson()));
    QCOMPARE(presentationGeometry(h), geometry);QCOMPARE(h.rails()->sizes(), sizes);QCOMPARE(h.rails()->snapshot()["tracks"].toArray(), tracks);
    QVERIFY(selectedMotion.unchanged());QCOMPARE(editor->semanticDocumentRevision(), revision);QCOMPARE(editor->document()->isModified(), modified);
    QSignalSpy layout(h.rails(), &LinkedNativeRails::layoutApplied), demand(h.rails(), &LinkedNativeRails::visibleReferencesChanged);
    const auto layoutSerial = h.rails()->connectorSnapshot(overlay).layoutSerial;
    h.rails()->setActiveAnchors({"a", "b"});h.rails()->setActiveAnchors({"a", "b"});
    QCOMPARE(layout.size(), 0);QCOMPARE(demand.size(), 0);QCOMPARE(h.rails()->connectorSnapshot(overlay).layoutSerial, layoutSerial);
    h.defer = false;h.releasePending();QTRY_VERIFY(h.resourcesComplete());QTRY_VERIFY(paintedCurrent(overlay));
    QCOMPARE(activeIds(h), QJsonArray({"a", "b"}));QCOMPARE(displayedIds(h), QJsonArray({"a", "b"}));
    QCOMPARE(cardFor(h, "a/reference0"), a0.data());QCOMPARE(cardFor(h, "a/reference1"), a1.data());
    QCOMPARE(cardFor(h, "b/reference0"), b0.data());QCOMPARE(cardFor(h, "c/reference0"), c0.data());
    QVERIFY(selectedMotion.unchanged());
    selection.setPosition(a + 2);editor->setTextCursor(selection);MotionWatch caretMotion(editor);
    QTRY_COMPARE(activeIds(h), QJsonArray({"a"}));QTRY_VERIFY(paintedCurrent(overlay));QVERIFY(caretMotion.unchanged());
    selection.setPosition(0);editor->setTextCursor(selection);MotionWatch clearedMotion(editor);
    QTRY_COMPARE(activeIds(h), QJsonArray());QTRY_VERIFY(paintedCurrent(overlay));QCOMPARE(displayedIds(h), QJsonArray());
    QVERIFY(clearedMotion.unchanged());QCOMPARE(pixelDifferences(normalA, renderProduction(a0, 1)), 0);
    auto* locate = b0->findChild<QPushButton*>("linkedNativeLocate");QVERIFY(locate);locate->click();
    QCOMPARE(editor->textCursor().selectionStart(), b);QCOMPARE(editor->textCursor().selectionEnd(), b + 8);
    MotionWatch locatedMotion(editor);QTRY_COMPARE(activeIds(h), QJsonArray({"b"}));QTRY_VERIFY(paintedCurrent(overlay));
    QCOMPARE(h.rails()->snapshot()["tracks"].toArray()[1].toObject()["reason"].toString(), QString("go-to-code"));
    QVERIFY(locatedMotion.unchanged());QCOMPARE(editor->semanticDocumentRevision(), revision);
    QVERIFY(h.controller->setActive(false));
}

void LinkedConnectorTest::aggregateHighlightsAndSessionClear()
{
    const QString text = "module connector_fixture;\nlogic clk, rst_n;\nlogic [31:0] signal_a, signal_b;\n"
        "always_ff @(posedge clk) begin\n    if (!rst_n) begin\n        signal_a <= 0;\n"
        "    end else begin\n        signal_b <= signal_a+1;\n    end\nend\nendmodule\n";
    NativeEnvironment environment;ConnectorHarness h(text);QVERIFY(h.ready);
    const auto row = [&h](const QString& id, const QString& needle) { const int first = h.text.indexOf(needle);return h.anchor(id, first, first + needle.size(), 2); };
    const auto a = row("a", "signal_a <="), b = row("b", "signal_b <="), head = row("head", "always_ff"), plain = row("plain", "logic clk");
    QVERIFY(h.store({a, b, head, plain}));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.resourcesComplete());
    QTextCursor caret(h.editor()->document());caret.setPosition(h.text.indexOf("always_ff") + 2);h.editor()->setTextCursor(caret);
    QTRY_COMPARE(activeIds(h), QJsonArray({"head"}));
    QVERIFY(h.editor()->toggleFoldAtLineForTest(3));
    QTRY_VERIFY(groupFor(h, "a")["folded"].toBool());QTRY_VERIFY(paintedCurrent(h.connector()));
    const auto aggregate = groupFor(h, "a");const auto aggregateId = aggregate["id"].toString();
    QCOMPARE(aggregate["members"].toArray(), QJsonArray({"a", "b", "head"}));
    QCOMPARE(activeIds(h), QJsonArray({"head"}));QCOMPARE(displayedIds(h), QJsonArray({"a", "b", "head"}));
    QVERIFY(aggregate["highlighted"].toBool());QVERIFY(!groupFor(h, "plain")["highlighted"].toBool());
    auto* card = cardFor(h, "a/reference0");QVERIFY(card);
    const auto cards = h.rails()->snapshot()["cards"].toArray();
    for (const auto& value : cards) { const auto c = value.toObject();QCOMPARE(c["highlighted"].toBool(), !c["id"].toString().startsWith("plain/")); }
    bool found = false;
    for (const auto& route : h.connector()->routes()) if (route.id == aggregateId) {
        found = true;QVERIFY(route.draw);QVERIFY(route.highlighted);
        const QRect line = h.editor()->cursorRect(QTextCursor(h.editor()->document()->findBlockByNumber(3)));
        QCOMPARE(route.points.first().y(), h.connector()->scene().codeViewport.top() + line.center().y());
    }
    QVERIFY(found);
    auto* toggle = static_cast<QPushButton*>(nullptr);
    for (auto* button : h.rails()->findChildren<QPushButton*>("linkedNativeExpand")) if (button->property("anchorId").toString() == aggregateId) toggle = button;
    QVERIFY(toggle);MotionWatch unfoldedMotion(h.editor());toggle->click();
    QTRY_VERIFY(!groupFor(h, "a")["folded"].toBool());QTRY_VERIFY(paintedCurrent(h.connector()));
    QCOMPARE(activeIds(h), QJsonArray({"head"}));QCOMPARE(displayedIds(h), QJsonArray({"head"}));
    QVERIFY(groupFor(h, "head")["highlighted"].toBool());QVERIFY(!groupFor(h, "a")["highlighted"].toBool());QVERIFY(!groupFor(h, "b")["highlighted"].toBool());
    QVERIFY(unfoldedMotion.unchanged());
    QPointer<QFrame> retainedA = cardFor(h, "a/reference0");QVERIFY(retainedA);
    QVERIFY(h.store({a, b, plain}));h.controller->refreshAnchors();
    QTRY_VERIFY(groupFor(h, "head").isEmpty());QTRY_VERIFY(paintedCurrent(h.connector()));
    QCOMPARE(activeIds(h), QJsonArray());QCOMPARE(displayedIds(h), QJsonArray());QCOMPARE(cardFor(h, "a/reference0"), retainedA.data());
    caret.setPosition(h.text.indexOf("signal_a <=") + 2);h.editor()->setTextCursor(caret);
    QTRY_COMPARE(activeIds(h), QJsonArray({"a"}));const auto oldSession = h.connector()->scene().identity.session;
    const auto second = h.temp.filePath("unlinked.sv");QVERIFY(writeFixture(second, "module unlinked; endmodule\n"));QVERIFY(h.tabs->openFileInTab(second));
    QTRY_VERIFY(paintedCurrent(h.connector()) && h.connector()->scene().identity.session != oldSession);
    QCOMPARE(activeIds(h), QJsonArray());QCOMPARE(displayedIds(h), QJsonArray());QVERIFY(h.connector()->routes().isEmpty());
    QVERIFY(h.tabs->openFileInTab(h.file));QTRY_COMPARE(activeIds(h), QJsonArray({"a"}));QTRY_VERIFY(paintedCurrent(h.connector()));
    const auto beforeReload = h.connector()->scene().identity.session;h.controller->reloadBoard();
    QTRY_VERIFY(paintedCurrent(h.connector()) && h.connector()->scene().identity.session != beforeReload);
    QCOMPARE(activeIds(h), QJsonArray({"a"}));QCOMPARE(displayedIds(h), QJsonArray({"a"}));
    QVERIFY(h.controller->setActive(false));
}

void LinkedConnectorTest::detachedSelectionAndTypingNeverFollow()
{
    NativeEnvironment environment;ConnectorHarness h(manyLines());QVERIFY(h.ready);QJsonArray rows;
    for (int i = 0; i < 12; ++i) { const int first = h.text.indexOf(QString("signal_%1;").arg(i));rows.append(h.anchor(QString("g%1").arg(i), first, first + 8)); }
    QVERIFY(h.store(rows));QVERIFY(h.controller->setActive(true));QTRY_VERIFY(h.resourcesComplete());
    auto* area = h.rails()->findChild<QScrollArea*>("linkedNativeLeftRail");QVERIFY(area);
    QTRY_VERIFY(area->verticalScrollBar()->maximum() > 0);QTest::keyClick(area->verticalScrollBar(), Qt::Key_End);
    QCOMPARE(h.rails()->snapshot()["tracks"].toArray()[0].toObject()["state"].toString(), QString("Detached"));
    const int scroll = area->verticalScrollBar()->value();
    QTextCursor caret(h.editor()->document());caret.setPosition(h.text.indexOf("signal_0;") + 2);h.editor()->setTextCursor(caret);
    MotionWatch watch(h.editor());QTRY_COMPARE(activeIds(h), QJsonArray({"g0"}));QTRY_VERIFY(paintedCurrent(h.connector()));
    QCOMPARE(area->verticalScrollBar()->value(), scroll);QVERIFY(watch.unchanged());
    QTextCursor edit(h.editor()->document());edit.movePosition(QTextCursor::End);edit.insertText("// another revision\n");
    QVERIFY(!h.connector()->scene().valid);QCOMPARE(activeIds(h), QJsonArray());
    QTRY_VERIFY(paintedCurrent(h.connector()) && h.connector()->completion().valid);
    QCOMPARE(activeIds(h), QJsonArray({"g0"}));QCOMPARE(area->verticalScrollBar()->value(), scroll);
    QCOMPARE(h.rails()->snapshot()["tracks"].toArray()[0].toObject()["state"].toString(), QString("Detached"));
    QVERIFY(watch.unchanged());
    const auto paint = h.connector()->completion().paintSerial;h.connector()->update();
    QTRY_VERIFY(h.connector()->completion().paintSerial > paint);QVERIFY(watch.unchanged());
    QVERIFY(h.controller->setActive(false));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings(QDir::tempPath() + "/connector-settings-XXXXXX");if (!settings.isValid()) return 2;
    QCoreApplication::setOrganizationName("ZeroSlackConnectorTest");QCoreApplication::setApplicationName("Stage3");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    if (!initializeUiStyleForTest()) return 3;
    LinkedConnectorTest test;return QTest::qExec(&test, argc, argv);
}
#include "linked_connector_test.moc"
