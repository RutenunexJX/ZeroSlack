#include "editorcoordinator.h"
#include "editorhoverpopup.h"
#include "mycodeeditor.h"
#include "mainwindow.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "pinloompreviewimage.h"
#include "testuistyle.h"
#include "tsdocument.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

namespace {
void moveMouse(QWidget* widget, QPoint position)
{
    const QPoint global = widget->mapToGlobal(position);
    QCursor::setPos(global);
    auto* window = widget->window()->windowHandle();
    QTest::mouseMove(window, window->mapFromGlobal(global));
}
void clickMouse(QWidget* widget, Qt::MouseButton button,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier, QPoint position = {})
{
    if (position.isNull()) position = widget->rect().center();
    const QPoint global = widget->mapToGlobal(position);
    QCursor::setPos(global);
    auto* window = widget->window()->windowHandle();
    QTest::mouseClick(window, button, modifiers, window->mapFromGlobal(global));
}
QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

struct Harness {
    struct Pending { QJsonObject identity; PinloomHostClient::RawReplyHandler reply; };
    QTemporaryDir temporary;
    PinloomCodeLinkStore store;
    QList<Pending> pending;
    QList<QJsonObject> opened;
    int resolves = 0;
    bool delay = false;
    bool confirmLocation = true;
    QJsonObject overrideDocument;
    QString text = QStringLiteral("module hover_fixture;\nlogic a, b, c;\nassign a = b;\nassign c = b;\nendmodule\n");
    QList<PinloomSourceSelection> sources;
    QList<PinloomHostIdentity> identities;
    QStringList images;
    PinloomHostClient client;
    MyCodeEditor editor;
    EditorCoordinator coordinator{nullptr};

    Harness() : client([this](const QJsonObject& request, PinloomHostClient::RawReplyHandler reply) {
        const auto id = request.value(QStringLiteral("params")).toObject().value(QStringLiteral("identity")).toObject();
        if (request.value(QStringLiteral("method")) == QStringLiteral("resolve")) {
            ++resolves;
            if (delay) pending.append({id, std::move(reply)});
            else reply(overrideDocument.isEmpty() ? document(id) : overrideDocument, {});
        } else if (request.value(QStringLiteral("method")) == QStringLiteral("open")) {
            opened.append(id);
            reply({{QStringLiteral("message"), QStringLiteral("Open request accepted")},
                   {QStringLiteral("state"), confirmLocation ? QStringLiteral("located") : QStringLiteral("open_requested")},
                   {QStringLiteral("positionVerified"), confirmLocation}}, {});
        } else reply({}, QStringLiteral("Unexpected test request"));
    }) {}

    bool init(bool multiple = false)
    {
        if (!temporary.isValid()) return false;
        const auto path = temporary.filePath(QStringLiteral("fixture.sv"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0) return false;
        file.close();
        store.setWorkspaceRoot(temporary.path());
        TSDocument syntax;
        syntax.setText(text);
        for (int n = 0; n < 2; ++n) {
            PinloomHostIdentity identity;
            identity.anchorId = QStringLiteral("crop-%1").arg(n);
            identity.resourceId = QStringLiteral("pdf-fixture");
            identity.entryId = QStringLiteral("anchor:") + identity.anchorId;
            identities.append(identity);
            const auto source = PinloomSourceSelection::fromSyntaxAnchor(temporary.path(), path, text,
                syntax.bindableCodeAnchorAt(text.indexOf(n == 0 ? QStringLiteral("assign a") : QStringLiteral("assign c"))));
            if (!source.isValid()) return false;
            sources.append(source);
            if (!store.addLink(source, identity.toUri(), QStringLiteral("PDF region %1").arg(n), identity.toVariantMap())) return false;
            images.append(temporary.filePath(QStringLiteral("crop-%1.png").arg(n)));
            QImage image(640, 320, QImage::Format_RGB32);
            image.fill(n == 0 ? Qt::red : Qt::blue);
            if (!image.save(images.last())) return false;
        }
        if (multiple && !store.addLink(sources[0], identities[1].toUri(), QStringLiteral("Second region"), identities[1].toVariantMap())) return false;
        editor.resize(900, 600);
        editor.setDocumentFileName(path);
        editor.setPlainText(text);
        coordinator.setPinloomCodeLinkStore(&store);
        coordinator.setPinloomHostClient(&client);
        coordinator.attachEditor(&editor);
        editor.show();
        editor.raise();
        editor.activateWindow();
        editor.setFocus();
        if (!QTest::qWaitForWindowExposed(&editor) || !QTest::qWaitForWindowActive(&editor)) return false;
        QTest::qWait(200);
        return true;
    }

    QJsonObject document(const QJsonObject& identity) const
    {
        const int n = identity.value(QStringLiteral("anchorId")).toString() == identities[1].anchorId ? 1 : 0;
        return {{QStringLiteral("entry"), QJsonObject{
                    {QStringLiteral("identity"), identity},
                    {QStringLiteral("uri"), identities[n].toUri().toString()},
                    {QStringLiteral("type"), QStringLiteral("anchor")},
                    {QStringLiteral("title"), QStringLiteral("Region %1").arg(n)}}},
                {QStringLiteral("details"), QJsonObject{
                    {QStringLiteral("locatorType"), QStringLiteral("sumatrapdf.rect")},
                    {QStringLiteral("fileName"), QStringLiteral("fixture.pdf")},
                    {QStringLiteral("page"), n + 2}}},
                {QStringLiteral("preview"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("image")},
                    {QStringLiteral("state"), QStringLiteral("ready")},
                    {QStringLiteral("filePath"), images[n]},
                    {QStringLiteral("uri"), QUrl::fromLocalFile(images[n]).toString()},
                    {QStringLiteral("mimeType"), QStringLiteral("image/png")},
                    {QStringLiteral("byteSize"), qMax<qint64>(0, QFileInfo(images[n]).size())},
                    {QStringLiteral("pixelWidth"), 640}, {QStringLiteral("pixelHeight"), 320},
                    {QStringLiteral("cropped"), true}, {QStringLiteral("page"), n + 2}}}};
    }
    QWidget* gutter() { return editor.findChild<QWidget*>(QStringLiteral("editorLineNumberGutter")); }
    QPoint marker(int n)
    {
        const auto geometry = editor.blockGeometry(n + 2);
        return QPoint(7, int(geometry.top + geometry.height / 2));
    }
    void hover(int n) {
        moveMouse(gutter(), QPoint(30, marker(n).y()));
        moveMouse(gutter(), marker(n));
    }
    QLabel* label(const char* name) { return editor.findChild<QLabel*>(QString::fromLatin1(name)); }
    QPushButton* button(const char* name) { return editor.findChild<QPushButton*>(QString::fromLatin1(name)); }
    EditorHoverPopup* popup() { return editor.findChild<EditorHoverPopup*>(); }
    bool ready() {
        const auto* view = label("pinloomHoverImage");
        return view && view->isVisible() && !view->pixmap(Qt::ReturnByValue).isNull();
    }
    void diagnose() {
        qInfo() << "Hover diagnostic" << "resolves" << resolves << "popup" << (popup() && popup()->isVisible())
                << "cursor" << QCursor::pos() << "marker" << gutter()->mapToGlobal(marker(0));
        for (const auto& a : store.linksForDocument(editor.documentFileName(), editor.cachedDocumentText(), editor.syntaxDocument()))
            qInfo() << "Binding" << a.firstLine << a.anchor.id << a.anchor.links.size() << a.available();
        for (auto* label : editor.findChildren<QLabel*>())
            if (label->objectName().startsWith(QStringLiteral("pinloomHover")))
                qInfo() << label->objectName() << label->text() << label->isVisible() << label->size() << label->pixmap(Qt::ReturnByValue).isNull();
        const QString artifacts = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) editor.grab().save(artifacts + QStringLiteral("/multi-diagnostic.png"));
    }
};
}

class PinloomHoverTest : public QObject {
    Q_OBJECT
private slots:
    void gutterToCardAndOriginal();
    void delayedSwitchAndLifetime();
    void failureRetryAndFreshness();
    void multipleTargetsAndPopupPriority();
    void pendingScrollAndDocumentChanges();
    void loaderValidationAndCancellation();
    void mainWindowBindingPreview();
};

void PinloomHoverTest::gutterToCardAndOriginal()
{
    Harness h;
    QVERIFY(h.init());
    const auto persisted = readFile(h.store.storagePath());
    QElapsedTimer visibleLatency;
    visibleLatency.start();
    h.hover(0);
    QTest::qWait(90);
    QCOMPARE(h.resolves, 0);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    qInfo() << "Fixture hover to image ms (300 ms dwell included)" << visibleLatency.elapsed()
            << "DPR" << h.editor.devicePixelRatioF();
    QCOMPARE(h.resolves, 1);
    QCOMPARE(QApplication::focusWidget(), &h.editor);
    QVERIFY(h.label("pinloomHoverDetails")->text().contains(QStringLiteral("fixture.pdf")));
    QVERIFY(h.label("pinloomHoverDetails")->text().contains(QStringLiteral("Page 2")));
    for (int i = 0; i < 8; ++i) moveMouse(h.gutter(), h.marker(0) + QPoint(i % 2, 0));
    QTest::qWait(350);
    QCOMPARE(h.resolves, 1);
    const QPoint gap = h.popup()->mapToGlobal(QPoint(-4, 20));
    QCursor::setPos(gap);
    QTest::qWait(50);
    moveMouse(h.label("pinloomHoverImage"), QPoint(20, 20));
    QTest::qWait(280);
    QVERIFY(h.ready());
    h.button("pinloomHoverOpen")->click();
    QCOMPARE(h.opened.size(), 1);
    QCOMPARE(h.opened.first(), h.identities[0].toJson());
    h.confirmLocation = false;
    h.button("pinloomHoverOpen")->click();
    QVERIFY(h.label("pinloomHoverStatus")->text().contains(QStringLiteral("did not confirm")));
    const auto codeCursor = h.editor.textCursor().position();
    clickMouse(h.label("pinloomHoverImage"), Qt::LeftButton);
    QVERIFY(QTest::qWaitFor([&] { return h.editor.findChild<QDialog*>(QStringLiteral("pinloomContextImageDialog")); }));
    auto* dialog = h.editor.findChild<QDialog*>(QStringLiteral("pinloomContextImageDialog"));
    QVERIFY(dialog->isVisible());
    QCOMPARE(h.editor.textCursor().position(), codeCursor);
    QCOMPARE(dialog->findChild<QLabel*>(QStringLiteral("pinloomContextLargeImage"))->pixmap(Qt::ReturnByValue).size(), QSize(640, 320));
    QVERIFY(!h.popup()->isVisible());
    dialog->close();
    h.editor.activateWindow();
    h.hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    moveMouse(h.editor.viewport(), QPoint(h.editor.viewport()->width() - 20, h.editor.viewport()->height() - 20));
    QVERIFY(QTest::qWaitFor([&] { return !h.popup()->isVisible(); }));
    QCOMPARE(readFile(h.store.storagePath()), persisted);
}

void PinloomHoverTest::delayedSwitchAndLifetime()
{
    auto h = std::make_unique<Harness>();
    QVERIFY(h->init());
    h->delay = true;
    h->hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h->pending.size() == 1; }));
    auto old = h->pending[0];
    h->hover(1);
    QTest::qWait(380);
    QCOMPARE(h->pending.size(), 1);
    QVERIFY(!h->ready());
    old.reply(h->document(old.identity), {});
    QVERIFY(QTest::qWaitFor([&] { return h->pending.size() == 2; }));
    auto latest = h->pending[1];
    latest.reply(h->document(latest.identity), {});
    QVERIFY(QTest::qWaitFor([&] { return h->ready(); }));
    QCOMPARE(h->label("pinloomHoverTitle")->text(), QStringLiteral("Region 1"));
    QVERIFY(h->label("pinloomHoverImage")->pixmap(Qt::ReturnByValue).toImage().pixelColor(2, 2).blue() > 200);
    old.reply(h->document(old.identity), {});
    QTest::qWait(30);
    QCOMPARE(h->label("pinloomHoverTitle")->text(), QStringLiteral("Region 1"));
    h->button("pinloomHoverRetry")->click();
    QCOMPARE(h->pending.size(), 3);
    const auto afterClose = h->pending[2];
    const auto result = h->document(afterClose.identity);
    h.reset();
    afterClose.reply(result, {});
    QTest::qWait(30);
}

void PinloomHoverTest::failureRetryAndFreshness()
{
    Harness h;
    QVERIFY(h.init());
    QVERIFY(QFile::remove(h.images[0]));
    h.hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h.label("pinloomHoverStatus") && h.label("pinloomHoverStatus")->text().contains(QStringLiteral("does not exist")); }));
    QVERIFY(!h.ready());
    QVERIFY(h.button("pinloomHoverOpen")->isEnabled());
    QImage replacement(640, 320, QImage::Format_RGB32);
    replacement.fill(Qt::green);
    QVERIFY(replacement.save(h.images[0]));
    h.button("pinloomHoverRetry")->click();
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    QVERIFY(h.label("pinloomHoverImage")->pixmap(Qt::ReturnByValue).toImage().pixelColor(2, 2).green() > 200);
    replacement.fill(Qt::blue);
    QVERIFY(replacement.save(h.images[0]));
    moveMouse(h.editor.viewport(), QPoint(820, 500));
    QVERIFY(QTest::qWaitFor([&] { return !h.popup()->isVisible(); }));
    h.hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    QCOMPARE(h.resolves, 3);
    QVERIFY(h.label("pinloomHoverImage")->pixmap(Qt::ReturnByValue).toImage().pixelColor(2, 2).blue() > 200);
    h.overrideDocument = h.document(h.identities[1].toJson());
    h.button("pinloomHoverRetry")->click();
    QVERIFY(!h.ready());
    QVERIFY(h.label("pinloomHoverStatus")->text().contains(QStringLiteral("different target")));
    QVERIFY(!h.button("pinloomHoverOpen")->isEnabled());
}

void PinloomHoverTest::multipleTargetsAndPopupPriority()
{
    Harness h;
    QVERIFY(h.init(true));
    h.hover(0);
    const bool loaded = QTest::qWaitFor([&] { return h.ready(); });
    if (!loaded) h.diagnose();
    QVERIFY(loaded);
    QPointer<QComboBox> selector = h.editor.findChild<QComboBox*>(QStringLiteral("pinloomHoverTargets"));
    QVERIFY(selector);
    QCOMPARE(selector->count(), 2);
    clickMouse(selector, Qt::LeftButton);
    QVERIFY(QTest::qWaitFor([&] { return selector && selector->view()->isVisible(); }));
    QTest::qWait(350);
    clickMouse(selector->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      selector->view()->visualRect(selector->model()->index(1, 0)).center());
    QTRY_VERIFY_WITH_TIMEOUT(selector && selector->currentIndex() == 1, 1500);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    QCOMPARE(h.label("pinloomHoverTitle")->text(), QStringLiteral("Region 1"));
    h.button("pinloomHoverOpen")->click();
    QCOMPARE(h.opened.last(), h.identities[1].toJson());
    PeekContentModel explicitContent;
    explicitContent.kind = PeekContentKind::ActionPlanPreview;
    explicitContent.title = QStringLiteral("Explicit editor action");
    h.popup()->showContent(explicitContent, QRect(h.editor.mapToGlobal(QPoint(20, 20)), QSize(10, 10)), h.editor.font());
    h.hover(1);
    QTest::qWait(400);
    QCOMPARE(h.popup()->contentModel().kind, PeekContentKind::ActionPlanPreview);
    QCOMPARE(h.resolves, 2);
    h.popup()->closePopup();
    h.overrideDocument = h.document(h.identities[0].toJson());
    h.overrideDocument.remove(QStringLiteral("preview"));
    h.overrideDocument.insert(QStringLiteral("details"), QJsonObject{});
    h.hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h.label("pinloomHoverStatus"); }));
    QVERIFY(h.label("pinloomHoverStatus")->text().contains(QStringLiteral("no PDF region")));
    QVERIFY(!h.button("pinloomHoverOpen")->isEnabled());
}

void PinloomHoverTest::pendingScrollAndDocumentChanges()
{
    Harness h;
    for (int i = 0; i < 80; ++i) h.text += QStringLiteral("// scroll fixture\n");
    QVERIFY(h.init());
    h.hover(0);
    h.editor.verticalScrollBar()->setValue(10);
    QTest::qWait(380);
    QCOMPARE(h.resolves, 0);
    h.editor.verticalScrollBar()->setValue(0);
    h.hover(1);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    QVERIFY(h.editor.rect().contains(h.popup()->geometry()));
    h.editor.resize(360, 250);
    QVERIFY(!h.popup()->isVisible());
    h.hover(0);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    QVERIFY(h.editor.rect().contains(h.popup()->geometry()));
    h.editor.setDocumentFileName(h.temporary.filePath(QStringLiteral("different.sv")));
    QVERIFY(!h.popup()->isVisible());
    h.editor.setDocumentFileName(h.temporary.filePath(QStringLiteral("fixture.sv")));
    h.hover(1);
    QVERIFY(QTest::qWaitFor([&] { return h.ready(); }));
    h.coordinator.setPinloomCodeLinkStore(nullptr);
    QVERIFY(!h.popup()->isVisible());
}

void PinloomHoverTest::loaderValidationAndCancellation()
{
    Harness h;
    QVERIFY(h.init());
    PinloomPreviewImageLoader loader;
    const auto valid = PinloomHostPreview::fromJson(h.document(h.identities[0].toJson()).value(QStringLiteral("preview")).toObject());
    int replies = 0;
    PinloomPreviewImageResult result;
    auto accept = [&](const PinloomPreviewImageResult& value) { ++replies; result = value; };
    for (int i = 0; i < 50; ++i) loader.load(valid, accept);
    QVERIFY(QTest::qWaitFor([&] { return replies == 1; }));
    QVERIFY(!result.image.isNull());
    auto invalid = valid;
    invalid.byteSize += 1;
    loader.load(invalid, accept);
    QVERIFY(QTest::qWaitFor([&] { return replies == 2; }));
    QVERIFY(result.image.isNull());
    QVERIFY(result.error.contains(QStringLiteral("changed")));
    invalid = valid;
    invalid.mimeType = QStringLiteral("image/jpeg");
    loader.load(invalid, accept);
    QVERIFY(QTest::qWaitFor([&] { return replies == 3; }));
    QVERIFY(result.image.isNull());
    QVERIFY(result.error.contains(QStringLiteral("MIME")));
    invalid = valid;
    invalid.pixelWidth = 16385;
    loader.load(invalid, accept);
    QVERIFY(QTest::qWaitFor([&] { return replies == 4; }));
    QVERIFY(result.image.isNull());
    invalid = valid;
    invalid.uri = QUrl(QStringLiteral("https://example.invalid/image.png"));
    loader.load(invalid, accept);
    QVERIFY(QTest::qWaitFor([&] { return replies == 5; }));
    QVERIFY(result.image.isNull());
    loader.load(valid, accept);
    QVERIFY(loader.cancel());
    QTest::qWait(80);
    QCOMPARE(replies, 5);
}

void PinloomHoverTest::mainWindowBindingPreview()
{
    Harness h;
    QVERIFY(h.init());
    h.editor.hide();
    MainWindow window;
    window.resize(1300, 850);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.workspaceManager->openWorkspace(h.temporary.path()));
    QVERIFY(window.tabManager->openFileInTab(h.temporary.filePath(QStringLiteral("fixture.sv"))));
    auto* coordinator = window.findChild<EditorCoordinator*>();
    QVERIFY(coordinator);
    coordinator->setPinloomHostClient(&h.client);
    auto* code = window.tabManager->getCurrentEditor();
    QVERIFY(code);
    code->setFocus();
    QTest::qWait(200);
    auto* gutter = code->findChild<QWidget*>(QStringLiteral("editorLineNumberGutter"));
    QVERIFY(gutter);
    const auto geometry = code->blockGeometry(2);
    moveMouse(gutter, QPoint(30, int(geometry.top + geometry.height / 2)));
    moveMouse(gutter, QPoint(7, int(geometry.top + geometry.height / 2)));
    QVERIFY(QTest::qWaitFor([&] {
        auto* image = code->findChild<QLabel*>(QStringLiteral("pinloomHoverImage"));
        return image && image->isVisible() && !image->pixmap(Qt::ReturnByValue).isNull();
    }));
    QCOMPARE(QApplication::focusWidget(), code);
    auto* peek = code->findChild<EditorHoverPopup*>();
    QVERIFY(peek && code->rect().contains(peek->geometry()));
    QTest::qWait(60);
    const QString artifacts = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) QVERIFY(window.grab().save(artifacts + QStringLiteral("/mainwindow-hover.png")));
    auto* viewport = peek->findChild<QScrollArea*>()->viewport();
    for (auto* button : peek->findChildren<QPushButton*>()) {
        qInfo() << "Card action bounds" << button->text() << QRect(button->mapTo(viewport, QPoint()), button->size()) << viewport->rect();
        QVERIFY(viewport->rect().contains(QRect(button->mapTo(viewport, QPoint()), button->size())));
    }
    auto* image = code->findChild<QLabel*>(QStringLiteral("pinloomHoverImage"));
    moveMouse(image, QPoint(20, 20));
    QTest::qWait(280);
    QVERIFY(peek->isVisible());
    code->findChild<QPushButton*>(QStringLiteral("pinloomHoverOpen"))->click();
    QCOMPARE(h.opened.last(), h.identities[0].toJson());
    clickMouse(image, Qt::LeftButton);
    QVERIFY(QTest::qWaitFor([&] { return code->findChild<QDialog*>(QStringLiteral("pinloomContextImageDialog")); }));
    auto* dialog = code->findChild<QDialog*>(QStringLiteral("pinloomContextImageDialog"));
    if (!artifacts.isEmpty()) QVERIFY(dialog->grab().save(artifacts + QStringLiteral("/mainwindow-enlargement.png")));
    dialog->close();
    window.close();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;
    PinloomHoverTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "pinloom_hover_test.moc"
