#include "testuistyle.h"
#include "applicationthememanager.h"
#include "mainwindow.h"
#include "navigationpanecoordinator.h"
#include "tabmanager.h"
#include "uidialogs.h"
#include "webdocumentpage.h"
#include "webeditorcontainer.h"
#include "pdfdocumentpage.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"
#include <QScreen>
#include <QPdfWriter>
#include <QtTest>
#include <QtWidgets>
#include <QStringEncoder>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class WebDocumentIntegrationTest : public QObject {
    Q_OBJECT
    QString root;
    QMap<QString, QByteArray> clipboardFormats;
    QImage clipboardImage;
    QByteArray read(const QString& name) {
        QFile file(name); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    bool write(const QString& name, const QByteArray& bytes) {
        QFile file(name); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    QString file(const QString& name) const { return root + '/' + name; }
    bool ready(WebDocumentPage& page) {
        page.resize(1000, 760); page.show(); page.activateWindow();
        QElapsedTimer timer; timer.start();
        while (!page.ready() && timer.elapsed() < 30000) QTest::qWait(25);
        qInfo() << "Editor startup ms" << int(page.documentKind()) << timer.elapsed();
        if (!page.ready()) for (auto* label : page.findChildren<QLabel*>()) qWarning() << label->text();
        return page.ready();
    }
    QString notice(WebDocumentPage& page) const {
        QString text; for (auto* label : page.findChildren<QLabel*>()) text += label->text() + '\n'; return text;
    }
    bool setClipboard(const QString& text) {
        QElapsedTimer timer; timer.start();
        do {
            QApplication::clipboard()->setText(text);
            if (QApplication::clipboard()->ownsClipboard()) return true;
            QTest::qWait(25);
        } while (timer.elapsed() < 5000);
        return false;
    }
    bool setClipboard(const QImage& image) {
        QElapsedTimer timer; timer.start();
        do {
            QApplication::clipboard()->setImage(image);
            if (QApplication::clipboard()->ownsClipboard()) return true;
            QTest::qWait(25);
        } while (timer.elapsed() < 5000);
        return false;
    }
    void paste(WebDocumentPage& page, const QString& value, bool replace = false) {
        QVERIFY2(setClipboard(value), "Cannot prepare the system clipboard for the paste fixture.");
        page.executeCommand(replace ? "selectAll" : "focus");
        page.executeCommand("paste");
    }
    std::unique_ptr<QTimer> answer(const QString& buttonText) {
        auto timer = std::make_unique<QTimer>(); timer->setInterval(25);
        connect(timer.get(), &QTimer::timeout, this, [buttonText] {
            for (auto* top : QApplication::topLevelWidgets()) if (top->isVisible() && qobject_cast<QDialog*>(top)) {
                if (!top->property("documentTestLogged").toBool()) {
                    top->setProperty("documentTestLogged", true);
                    for (auto* button : top->findChildren<QAbstractButton*>()) qInfo() << "Dialog button" << button->text() << button->property("uiDialogStandardButton");
                }
                for (auto* button : top->findChildren<QAbstractButton*>())
                    if (button->isVisible() && (button->text().remove('&') == buttonText
                        || (buttonText == "OK" && button->property("uiDialogStandardButton").toInt() == int(QMessageBox::Ok)))) { button->click(); return; }
            }
        }); timer->start(); return timer;
    }
    void capture(QWidget& page, const QString& name) {
        page.window()->raise(); page.window()->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(page.window())); QTest::qWait(120);
        if (name.startsWith("layout-"))
            for (auto* web : page.findChildren<WebEditorContainer*>()) if (web->isVisible())
                QTRY_VERIFY_WITH_TIMEOUT(viewportMatches(web->parentWidget(), "layout-capture"), 5000);
        const auto origin = page.mapToGlobal(QPoint());
        const auto screen = page.screen()->availableGeometry();
        const auto frame = page.frameGeometry();
        QVERIFY(page.screen()->grabWindow(0, origin.x(), origin.y(), page.width(), page.height()).save(file(name + ".png")));
        QVERIFY(write(file(name + ".json"), QJsonDocument(QJsonObject{{"capture", "native screen rectangle"},
            {"width", page.width()}, {"height", page.height()}, {"dpr", page.devicePixelRatioF()},
            {"frame", QJsonArray{frame.x(), frame.y(), frame.width(), frame.height()}},
            {"availableScreen", QJsonArray{screen.x(), screen.y(), screen.width(), screen.height()}},
            {"withinScreen", screen.contains(frame)}}).toJson()));
        if (name.startsWith("layout-")) {
            if (!screen.contains(frame)) {
                qWarning() << "Screen" << screen << "window" << frame << "minimum" << page.minimumSizeHint();
                for (auto* child : page.findChildren<QWidget*>())
                    if (child->isVisible() && (child->minimumSizeHint().width() > 400 || child->minimumWidth() > 180))
                        qWarning() << child->metaObject()->className() << child->objectName() << child->size()
                                   << "minimum" << child->minimumSize() << child->minimumSizeHint();
            }
            QVERIFY(screen.contains(frame));
            if (auto* bar = page.findChild<QWidget*>("bottomToolDrawerButtonBar"))
                for (auto* button : bar->findChildren<QAbstractButton*>()) if (button->isVisible()) {
                    QVERIFY(bar->rect().contains(QRect(button->mapTo(bar, QPoint()), button->size())));
                    QVERIFY(button->width() >= button->sizeHint().width());
                }
        }
    }
    bool viewportMatches(QWidget* page, const char* phase) {
        auto* web = page->findChild<WebEditorContainer*>();
        if (!web || !web->isVisible()) return false;
        const HWND host = reinterpret_cast<HWND>(web->winId());
        const HWND child = GetWindow(host, GW_CHILD);
        RECT hostRect{}, childRect{}, hostScreen{};
        if (!child || !GetClientRect(host, &hostRect) || !GetWindowRect(child, &childRect)) return false;
        if (!GetWindowRect(host, &hostScreen)) return false;
        const auto offset = web->mapTo(web->window(), QPoint());
        POINT expectedOrigin{qRound(offset.x() * web->devicePixelRatioF()), qRound(offset.y() * web->devicePixelRatioF())};
        ClientToScreen(reinterpret_cast<HWND>(web->window()->winId()), &expectedOrigin);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&childRect), 2);
        const QSize hostSize(hostRect.right, hostRect.bottom);
        const QSize childSize(childRect.right - childRect.left, childRect.bottom - childRect.top);
        const QSize qtSize = (QSizeF(web->size()) * web->devicePixelRatioF()).toSize();
        for (auto* parent = web->parentWidget(); parent && !parent->isWindow(); parent = parent->parentWidget()) {
            if (!parent->internalWinId()) continue;
            RECT rect{}; if (!GetClientRect(reinterpret_cast<HWND>(parent->internalWinId()), &rect)) return false;
            const auto expected = (QSizeF(parent->size()) * parent->devicePixelRatioF()).toSize();
            if (qAbs(rect.right - expected.width()) > 1 || qAbs(rect.bottom - expected.height()) > 1) {
                qWarning() << "Clipped native ancestor" << parent->objectName() << expected << rect.right << rect.bottom;
                return false;
            }
        }
        qInfo() << "Viewport" << phase << page->property("pageKind") << "dpr" << web->devicePixelRatioF()
                << "Qt" << qtSize << "host" << hostSize << "WebView" << childSize
                << "nativeVisible" << bool(IsWindowVisible(host)) << bool(IsWindowVisible(child))
                << "origin" << QPoint(hostScreen.left, hostScreen.top) << QPoint(expectedOrigin.x, expectedOrigin.y);
        return hostSize == childSize && qAbs(hostSize.width() - qtSize.width()) <= 1
            && qAbs(hostScreen.left - expectedOrigin.x) <= 1 && qAbs(hostScreen.top - expectedOrigin.y) <= 1
            && qAbs(hostSize.height() - qtSize.height()) <= 1 && childRect.left == 0 && childRect.top == 0;
    }
private slots:
    void initTestCase() {
        QVERIFY2(QGuiApplication::platformName() == "windows", "These tests require real WebView2 on Windows, not an offscreen substitute.");
        root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR"); QVERIFY(!root.isEmpty()); QVERIFY(QDir().mkpath(root));
        QVERIFY(!qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT").isEmpty());
        QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlackDocumentIntegrationTest"));
        QVERIFY(QDir().mkpath(qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT")));
        QVERIFY(write(file("simdock-legacy.ini"), QByteArray()));
        qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", QFile::encodeName(file("simdock-legacy.ini")));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT"));
        auto& theme = ApplicationThemeManager::instance(); QVERIFY(initializeUiStyleForTest());
        theme.setMode(ThemeMode::Light); theme.applyToApplication();
        if (const auto* data = QApplication::clipboard()->mimeData()) {
            for (const auto& format : data->formats()) clipboardFormats[format] = data->data(format);
            if (data->hasImage()) clipboardImage = qvariant_cast<QImage>(data->imageData());
        }
    }
    void sharedDocumentWorkerSurvivesOriginalView_data() {
        QTest::addColumn<QString>("kind"); QTest::newRow("markdown") << QString("markdown"); QTest::newRow("blocks") << QString("blocks");
    }
    void sharedDocumentWorkerSurvivesOriginalView() {
        QFETCH(QString, kind);
        QWidget window; auto* layout = new QHBoxLayout(&window);
        const QString runtime = qEnvironmentVariable("ZEROSLACK_WEB_PROOF_RESOURCES"); QVERIFY(!runtime.isEmpty());
        const QString session = QUuid::createUuid().toString();
        QSet<WebEditorContainer*> initialized; QHash<WebEditorContainer*, quint64> revisions;
        QString failure; QHash<QString, QJsonObject> replies;
        auto create = [&] {
            auto* web = new WebEditorContainer(runtime, file("shared-profile-" + kind), kind, &window);
            layout->addWidget(web);
            connect(web, &WebEditorContainer::initializationFailed, this, [&](const QString& value) { failure = value; });
            connect(web, &WebEditorContainer::messageReceived, this, [&, web](const QJsonObject& value) {
                const QString type = value.value("type").toString();
                if (type == "hello") web->postMessage({{"type", "initialize"}, {"session", session}, {"document", "proof"}, {"content", "# Shared\n\nOriginal content\n"}, {"importMarkdown", kind == "blocks"}});
                else if (type == "ready") initialized.insert(web);
                else if (type == "changed") revisions[web] = value.value("revision").toInteger();
                else if (type == "error") { failure = value.value("message").toString(); qWarning() << "Shared editor error" << failure; }
                else if (type == "snapshot" || type == "snapshot-error") replies.insert(value.value("request").toString(), value);
            });
            return web;
        };
        auto* left = create(); auto* right = create(); window.resize(1200, 720); window.show(); window.activateWindow();
        QTRY_VERIFY2_WITH_TIMEOUT(initialized.size() == 2 || !failure.isEmpty(), qPrintable(failure), 30000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure)); QCOMPARE(initialized.size(), 2);
        auto command = [&](WebEditorContainer* view, const QString& name, const QString& text = QString()) {
            return view->postMessage({{"type", "command"}, {"session", session}, {"document", "proof"}, {"command", name},
                {"clipboard", QJsonObject{{"text", text}, {"html", ""}}}});
        };
        auto snapshot = [&](WebEditorContainer* view, bool native = false) {
            const QString request = QUuid::createUuid().toString();
            view->postMessage({{"type", "snapshot"}, {"session", session}, {"document", "proof"}, {"request", request}, {"mode", kind == "blocks" && !native ? "exportMarkdown" : ""}});
            QElapsedTimer timer; timer.start();
            while (!replies.contains(request) && timer.elapsed() < 30000) QTest::qWait(10);
            return replies.take(request);
        };
        left->setFocus(); QVERIFY(command(left, "focus"));
        QVERIFY(command(left, "selectAll")); QVERIFY(command(left, "paste", "Left transaction 中文 Ω"));
        QTRY_VERIFY2_WITH_TIMEOUT(revisions.value(right) > 0 || !failure.isEmpty(), qPrintable(failure), 5000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        auto value = snapshot(right); QVERIFY2(value.value("content").toString().contains("Left transaction 中文 Ω"), qPrintable(QJsonDocument(value).toJson()));
        const auto edited = value.value("content"); const auto editedRevision = value.value("revision").toInteger();
        QVERIFY(command(right, "undo")); QTRY_VERIFY_WITH_TIMEOUT(revisions.value(left) > quint64(editedRevision), 5000);
        value = snapshot(left); qInfo() << "After shared undo" << value; QVERIFY(value.value("content").toString().contains("Original content"));
        QVERIFY(command(left, "redo")); QTRY_VERIFY2_WITH_TIMEOUT(revisions.value(right) > quint64(value.value("revision").toInteger()) || !failure.isEmpty(), qPrintable(failure), 5000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(snapshot(right).value("content"), edited); capture(window, "shared-" + kind + "-redo");
        delete left;
        right->setFocus(); QVERIFY(command(right, "focus"));
        QVERIFY(command(right, "selectAll")); QVERIFY(command(right, "paste", "Saved after closing the original view"));
        value = snapshot(right); QVERIFY2(value.value("type") == "snapshot", qPrintable(QJsonDocument(value).toJson()));
        QVERIFY2(value.value("content").toString().contains("Saved after closing"), qPrintable(QJsonDocument(value).toJson()));
        value = snapshot(right, true);
        const QString survivor = "shared-worker-survivor." + (kind == "blocks" ? QString("zblock") : QString("md"));
        QSaveFile saved(file(survivor)); QVERIFY(saved.open(QIODevice::WriteOnly));
        const auto bytes = value.value("content").toString().toUtf8(); QCOMPARE(saved.write(bytes), bytes.size()); QVERIFY(saved.commit());
        QCOMPARE(read(file(survivor)), bytes); QVERIFY2(failure.isEmpty(), qPrintable(failure)); capture(window, "shared-" + kind + "-survivor");
    }
    void mainWindowSharedSessions_data() {
        QTest::addColumn<bool>("blocks"); QTest::newRow("markdown") << false; QTest::newRow("blocks") << true;
    }
    void mainWindowSharedSessions() {
        QFETCH(bool, blocks);
        MainWindow window; window.resize(1400, 850); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); QVERIFY(tabs);
        auto* first = tabs->createWebDocument(blocks ? WebDocumentPage::Kind::Blocks : WebDocumentPage::Kind::Markdown);
        QVERIFY(first); QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 30000); QVERIFY2(!first->dirty(), qPrintable(notice(*first)));
        paste(*first, "Original shared document", true);
        const QString path = file(blocks ? "shared-native.zblock" : "shared-native.md"); QVERIFY2(first->save(true, path), qPrintable(notice(*first)));
        QVERIFY(tabs->duplicateCurrentView()); auto* second = tabs->currentWebDocument(); QVERIFY(second && second != first);
        QTRY_VERIFY_WITH_TIMEOUT(second->ready(), 30000);
        QCOMPARE(first->documentSession(), second->documentSession()); QCOMPARE(first->documentId(), second->documentId());
        QVERIFY(first->property("documentViewId") != second->property("documentViewId")); QVERIFY(!second->dirty());
        auto* splits = tabs->editorSplitController(); QVERIFY(splits);
        QVERIFY(splits->movePage(first, splits->regionActiveGroup(QStringLiteral("left"))));
        QVERIFY(splits->activatePage(first)); QTRY_VERIFY(first->isVisible()); QTest::qWait(100);
        QSignalSpy saved(tabs, &TabManager::fileSaved);
        paste(*first, "Shared final paste before save 中文", true);
        QVERIFY2(second->save(), qPrintable(notice(*second))); QCOMPARE(saved.count(), 1); QVERIFY(!first->dirty()); QVERIFY(!second->dirty());
        auto copiedText = [&](WebDocumentPage* page) {
            splits->activatePage(page); page->executeCommand("focus"); QTest::qWait(60);
            if (!setClipboard(QStringLiteral("shared copy sentinel"))) return QStringLiteral("Clipboard fixture unavailable");
            page->executeCommand("selectAll"); QTest::qWait(60); page->executeCommand("copy");
            QElapsedTimer timer; timer.start(); while ((QApplication::clipboard()->text().isEmpty() || QApplication::clipboard()->text().startsWith("shared copy sentinel")) && timer.elapsed() < 5000) QTest::qWait(10);
            return QApplication::clipboard()->text();
        };
        QVERIFY(copiedText(second).contains("Shared final paste before save"));
        second->executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(first->dirty(), 5000);
        QVERIFY2(copiedText(first).contains("Original shared document"), qPrintable(QApplication::clipboard()->text()));
        first->executeCommand("redo"); QTest::qWait(100); QVERIFY(copiedText(second).contains("Shared final paste before save"));
        const QString relocated = file(blocks ? "shared-save-as.zblock" : "shared-save-as.md");
        QVERIFY(second->save(true, relocated)); QCOMPARE(first->fileName(), second->fileName()); QVERIFY(!first->dirty());
        paste(*second, "Unsaved before shared rename 中文", true); QTRY_VERIFY_WITH_TIMEOUT(second->dirty(), 5000);
        QString checkpoint; quint64 checkpointRevision = 0; QVERIFY(second->captureRecovery(&checkpoint, &checkpointRevision));
        const QString renamed = file(blocks ? "shared-renamed.zblock" : "shared-renamed.md");
        {
            WorkspacePathMutation mutation; QString issue;
            QVERIFY2(tabs->prepareWorkspacePathMutation(relocated, false, &mutation, &window, &issue, {}, renamed), qPrintable(issue));
            QCOMPARE(mutation.webDocuments.size(), 1); QVERIFY(QFile::rename(relocated, renamed));
            QVERIFY2(tabs->finalizeWorkspacePathMutation(mutation, &issue), qPrintable(issue));
        }
        QCOMPARE(first->fileName(), QDir::cleanPath(QDir::fromNativeSeparators(renamed))); QCOMPARE(second->fileName(), first->fileName()); QVERIFY(first->dirty()); QCOMPARE(second->documentSession()->views().size(), 2);
        QVERIFY(copiedText(first).contains("Unsaved before shared rename")); QVERIFY(second->save());
        paste(*second, "Survivor after original view close", true);
        auto* firstGroup = splits->groupForPage(first); QVERIFY(firstGroup); splits->setActiveGroup(firstGroup); firstGroup->setCurrentWidget(first);
        tabs->closeTab(firstGroup->indexOf(first)); QCOMPARE(second->documentSession()->views().size(), 1);
        QVERIFY(second->dirty()); QVERIFY2(second->save(), qPrintable(notice(*second))); QVERIFY(copiedText(second).contains("Survivor after original"));
        capture(window, blocks ? "mainwindow-shared-blocks" : "mainwindow-shared-markdown"); QVERIFY(tabs->closeAllTabs());
        QVERIFY(tabs->openFileInTab(renamed)); auto* reopened = tabs->currentWebDocument(); QVERIFY(reopened); QTRY_VERIFY_WITH_TIMEOUT(reopened->ready(), 30000);
        QVERIFY(copiedText(reopened).contains("Survivor after original")); QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void mainWindowSharedViewState_data() {
        QTest::addColumn<bool>("blocks"); QTest::newRow("markdown") << false; QTest::newRow("blocks") << true;
    }
    void mainWindowSharedViewState() {
        QFETCH(bool, blocks); MainWindow window; window.resize(1280, 820); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); auto* splits = tabs->editorSplitController();
        auto* first = tabs->createWebDocument(blocks ? WebDocumentPage::Kind::Blocks : WebDocumentPage::Kind::Markdown);
        QVERIFY(first); QTRY_VERIFY_WITH_TIMEOUT(first->ready(), 30000);
        paste(*first, QString::fromUtf8("A😀BC"), true); const QString path = file(blocks ? "positions.zblock" : "positions.md"); QVERIFY(first->save(true, path));
        first->executeCommand("focus"); QTest::qWait(100);
        const auto initial = first->viewState(); QVERIFY(write(file(blocks ? "blocks-selection.json" : "markdown-selection.json"), QJsonDocument(initial).toJson()));
        QJsonObject point;
        if (blocks) {
            for (const auto& entry : initial.value("selection").toArray()) if (entry.toObject().value("type") == QStringLiteral("text")) point = entry.toObject();
            QVERIFY2(!point.isEmpty(), qPrintable(QString::fromUtf8(QJsonDocument(initial).toJson())));
        }
        paste(*first, QStringLiteral("\n\nscroll paragraph\n\n").repeated(70)); QVERIFY(first->save());
        const auto stateAt = [&](int offset, int top) {
            if (blocks) {
                auto value = point; auto from = value.value("from").toObject(); from.insert("index", offset); from.insert("length", 0);
                value.insert("from", from); value.insert("to", QJsonValue::Null); value.insert("reverse", false);
                return QJsonObject{{"selection", QJsonArray{value}}, {"top", top}, {"left", 0}};
            }
            const QJsonObject edge{{"path", QJsonArray{0, "text"}}, {"offset", offset}};
            return QJsonObject{{"selection", QJsonObject{{"anchor", edge}, {"focus", edge}}}, {"top", top}, {"left", 0}};
        };
        const auto offsetOf = [&](WebDocumentPage* page) {
            const auto value = page->viewState().value("selection");
            if (blocks) { const auto entries = value.toArray(); return entries.isEmpty() ? -1 : entries.first().toObject().value("from").toObject().value("index").toInt(-1); }
            return value.toObject().value("anchor").toObject().value("offset").toInt(-1);
        };
        QVERIFY(tabs->duplicateCurrentView()); auto* second = tabs->currentWebDocument(); QVERIFY(second && second != first); QTRY_VERIFY_WITH_TIMEOUT(second->ready(), 30000);
        QVERIFY(splits->movePage(first, splits->regionActiveGroup(QStringLiteral("left"))));
        QVERIFY(splits->activatePage(first)); first->restoreViewState(stateAt(4, 400)); QTest::qWait(100);
        QVERIFY(splits->activatePage(second)); second->restoreViewState(stateAt(0, 0)); QTest::qWait(100);
        const int firstTop = first->viewState().value("top").toInt(); QVERIFY(firstTop > 0);
        paste(*second, QString::fromUtf8("😀"));
        QTRY_COMPARE_WITH_TIMEOUT(offsetOf(first), 6, 5000); QCOMPARE(first->viewState().value("top").toInt(), firstTop);
        QCOMPARE(second->viewState().value("top").toInt(), 0);
        first->restoreViewState(stateAt(6, firstTop)); second->restoreViewState(stateAt(0, 0));
        paste(*first, QStringLiteral("L")); paste(*second, QStringLiteral("R"));
        QVERIFY(second->save());
        const auto content = [&] {
            splits->activatePage(second); second->executeCommand("focus"); QTest::qWait(80); second->executeCommand("selectAll"); QTest::qWait(80);
            if (!setClipboard(QStringLiteral("position sentinel"))) return QStringLiteral("Clipboard fixture unavailable");
            second->executeCommand("copy");
            QElapsedTimer elapsed; elapsed.start(); while ((QApplication::clipboard()->text().isEmpty() || QApplication::clipboard()->text().startsWith("position sentinel")) && elapsed.elapsed() < 5000) QTest::qWait(10);
            return QApplication::clipboard()->text();
        };
        const auto combined = content(); QVERIFY(write(file(blocks ? "blocks-combined.txt" : "markdown-combined.txt"), combined.toUtf8()));
        QVERIFY2(combined.contains(QString::fromUtf8("R😀A😀BLC")), qPrintable(combined.left(100) + QStringLiteral("\n") + notice(*second)));
        auto revision = second->currentRevision(); first->executeCommand("undo"); first->executeCommand("undo");
        QTRY_VERIFY_WITH_TIMEOUT(second->currentRevision() >= revision + 2, 5000);
        QString undoSnapshot; quint64 undoRevision = 0; QVERIFY(second->captureRecovery(&undoSnapshot, &undoRevision));
        QVERIFY(write(file(blocks ? "blocks-after-undo.json" : "markdown-after-undo.json"), undoSnapshot.toUtf8()));
        const auto undone = content(); QVERIFY(write(file(blocks ? "blocks-undone.txt" : "markdown-undone.txt"), undone.toUtf8()));
        QVERIFY2(undone.startsWith(QString::fromUtf8("😀A😀BC")), qPrintable(undone.left(100) + QStringLiteral("\n") + notice(*second)));
        revision = second->currentRevision(); second->executeCommand("redo"); second->executeCommand("redo");
        QTRY_VERIFY_WITH_TIMEOUT(second->currentRevision() >= revision + 2, 5000); QCOMPARE(content(), combined);
        QVERIFY(second->save()); QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void markdownEncodingFinalPasteUndoAndUnchangedSave() {
        const QString path = file("encoding 中文.md");
        const QString original = "# Heading\r\n\r\nBody 中文\r\n";
        QStringEncoder encoder(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom);
        const QByteArray bytes = encoder.encode(original); QVERIFY(write(path, bytes));
        WebDocumentPage page(WebDocumentPage::Kind::Markdown); QString error;
        QVERIFY2(page.openFile(path, &error), qPrintable(error)); QVERIFY(ready(page)); QVERIFY2(!notice(page).contains("read-only"), qPrintable(notice(page))); QVERIFY(!page.dirty());
        const auto before = QFileInfo(path).lastModified(); QVERIFY(page.save()); QCOMPARE(read(path), bytes); QCOMPARE(QFileInfo(path).lastModified(), before);
        paste(page, "Replacement 中文 Ω\nsecond", true);
        QVERIFY2(page.save(), qPrintable(notice(page))); QVERIFY(!page.dirty());
        const auto saved = readDocumentFile(path); QVERIFY(saved.available); QVERIFY(saved.text.contains("Replacement 中文 Ω")); QVERIFY(saved.text.contains("second"));
        QCOMPARE(saved.format.encoding, QStringConverter::Utf16LE); QVERIFY(saved.format.byteOrderMark); QCOMPARE(saved.format.lineEnding, QString("\r\n"));
        QVERIFY(setClipboard(QStringLiteral("clipboard sentinel"))); page.executeCommand("selectAll"); page.executeCommand("copy");
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->text().contains("Replacement 中文 Ω"), 5000);
        page.executeCommand("cut"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(!readDocumentFile(path).text.contains("Replacement"));
        page.executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(readDocumentFile(path).text.contains("Replacement 中文 Ω"));
        page.executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save()); QVERIFY(readDocumentFile(path).text.contains("Heading"));
        page.executeCommand("redo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save()); QVERIFY(readDocumentFile(path).text.contains("Replacement 中文 Ω"));
        capture(page, "markdown-replace-redo"); QVERIFY(page.confirmClose());
    }
    void markdownProtectionAndDiskFailures() {
        const QString path = file("protected.md"); const QByteArray source("<custom-widget data-secret='keep'>Unknown</custom-widget>\n"); QVERIFY(write(path, source));
        {
            WebDocumentPage page(WebDocumentPage::Kind::Markdown); QVERIFY(page.openFile(path)); QVERIFY(ready(page));
            QVERIFY(notice(page).contains("read-only")); QVERIFY(page.save()); QVERIFY(!page.save(true, file("protected-copy.md"))); QCOMPARE(read(path), source);
        }
        const QString active = file("external.md"); QVERIFY(write(active, "Initial\n"));
        WebDocumentPage page(WebDocumentPage::Kind::Markdown); QVERIFY(page.openFile(active)); QVERIFY(ready(page));
        paste(page, "Local edit", true); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000);
        QVERIFY(write(active, "External edit\n")); page.checkExternalFile(); QVERIFY(page.title().contains("disk changed"));
        QVERIFY(!page.save()); QCOMPARE(read(active), QByteArray("External edit\n")); QVERIFY(page.dirty());
        QVERIFY(page.save(true, file("local-rescue.md"))); QVERIFY(read(file("local-rescue.md")).contains("Local edit"));
        paste(page, "Unsaved after rescue", true); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000);
        const auto preserved = read(file("local-rescue.md"));
        QVERIFY(!page.save(true, file("missing-directory/failure.md"))); QCOMPARE(read(file("local-rescue.md")), preserved); QVERIFY(page.dirty());
        QVERIFY(QFile::setPermissions(file("local-rescue.md"), QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        QVERIFY(!page.save()); QCOMPARE(read(file("local-rescue.md")), preserved); QVERIFY(page.dirty());
        QVERIFY(QFile::setPermissions(file("local-rescue.md"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser));
        QVERIFY(page.save());
        QVERIFY(write(file("local-rescue.md"), "Reloaded disk version\n")); page.checkExternalFile();
        QPushButton* reload = nullptr; for (auto* button : page.findChildren<QPushButton*>()) if (button->text() == "Reload from Disk") reload = button;
        QVERIFY(reload); reload->click(); QTRY_VERIFY_WITH_TIMEOUT(page.ready(), 30000);
        QVERIFY(page.save(true, file("reload-copy.md"))); QVERIFY(read(file("reload-copy.md")).contains("Reloaded disk version"));
        paste(page, "Edited after reload", true); QVERIFY(page.save()); QVERIFY(read(file("reload-copy.md")).contains("Edited after reload"));
    }
    void clipboardImagePortableSaveAndRecovery() {
        WebDocumentPage page(WebDocumentPage::Kind::Markdown); QVERIFY(page.setInitialContent("Image document\n\n")); QVERIFY(ready(page));
        QImage image(24, 16, QImage::Format_ARGB32); image.fill(QColor("#175cd3")); QVERIFY(setClipboard(image));
        page.executeCommand("focus"); page.executeCommand("paste");
        QVERIFY2(page.save(true, file("image.md")), qPrintable(notice(page)));
        const auto markdown = QString::fromUtf8(read(file("image.md"))); QVERIFY2(markdown.contains(".zeroslack-assets/"), qPrintable(markdown));
        const QRegularExpression pattern("\\.zeroslack-assets/[a-f0-9]+\\.png"); const auto match = pattern.match(markdown); QVERIFY(match.hasMatch());
        const QString imageName = match.captured(); QVERIFY(QFileInfo::exists(file(imageName)));
        const QString portable = file("迁移 目录"); QVERIFY(QDir().mkpath(portable)); QVERIFY(page.save(true, portable + "/image.md"));
        QCOMPARE(read(portable + '/' + imageName), read(file(imageName)));
        paste(page, "Recovery text", true); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000);
        QString record; quint64 revision = 0; QVERIFY(page.captureRecovery(&record, &revision)); QVERIFY(revision > 0);
        WebDocumentPage restored(WebDocumentPage::Kind::Markdown); QVERIFY(restored.restoreRecovery(record)); QVERIFY(ready(restored)); QVERIFY(restored.dirty());
        QVERIFY(restored.save(true, file("recovered.md"))); QVERIFY(read(file("recovered.md")).contains("Recovery text"));
        QVERIFY(read(portable + "/image.md").contains(".zeroslack-assets/"));
        capture(restored, "markdown-recovery");
    }
    void closeCancelDiscardAndSaveAfterFinalPaste() {
        WebDocumentPage page(WebDocumentPage::Kind::Markdown); QVERIFY(page.setInitialContent("Start")); QVERIFY(ready(page));
        QVERIFY(page.save(true, file("close.md"))); paste(page, "Final characters", true);
        { auto click = answer("Cancel"); QVERIFY(!page.confirmClose()); }
        QVERIFY(page.dirty()); QCOMPARE(read(file("close.md")), QByteArray("Start"));
        { auto click = answer("Save"); QVERIFY(page.confirmClose()); }
        QVERIFY(read(file("close.md")).contains("Final characters")); QVERIFY(!page.dirty());
        paste(page, "Discard only this edit", true);
        { auto click = answer("Discard"); QVERIFY(page.confirmClose()); }
        QVERIFY(read(file("close.md")).contains("Final characters"));
    }
    void blockNativeMultipleSavesImagesAndPortableReopen() {
        WebDocumentPage page(WebDocumentPage::Kind::Blocks); QVERIFY(page.setInitialContent("")); QVERIFY(ready(page));
        paste(page, "Native 中文 first"); QVERIFY2(page.save(true, file("native.zblock")), qPrintable(notice(page)));
        QTest::qWait(80); const auto insertionPoint = page.viewState();
        QVERIFY(!insertionPoint.value("selection").toArray().isEmpty());
        const auto first = QJsonDocument::fromJson(read(file("native.zblock"))).object(); QVERIFY(first.value("state").toString().size() > 50);
        QVERIFY(write(file("native-first.zblock"), read(file("native.zblock"))));
        page.executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(write(file("native-undo.zblock"), read(file("native.zblock"))));
        page.executeCommand("redo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(write(file("native-redo.zblock"), read(file("native.zblock"))));
        QVERIFY(setClipboard(QStringLiteral("block clipboard sentinel"))); page.executeCommand("selectAll"); QTest::qWait(100); page.executeCommand("copy");
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->text().contains("Native 中文 first"), 5000);
        QVERIFY(QApplication::clipboard()->mimeData()->html().contains("data-blocksuite-snapshot"));
        page.restoreViewState(insertionPoint); QTest::qWait(80);
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(QColor("#12b76a")); QVERIFY(setClipboard(image));
        page.executeCommand("focus"); page.executeCommand("paste");
        QVERIFY2(page.save(), qPrintable(notice(page)));
        const auto second = QJsonDocument::fromJson(read(file("native.zblock"))).object(); QCOMPARE(second.value("documentId"), first.value("documentId")); QCOMPARE(second.value("workspaceId"), first.value("workspaceId"));
        QVERIFY(second.value("state") != first.value("state")); QCOMPARE(second.value("attachments").toArray().size(), 1);
        QString error; QVERIFY2(WebDocumentPage::validateBlockPackage(second, &error), qPrintable(error));
        const auto bytes = read(file("native.zblock")); const auto modified = QFileInfo(file("native.zblock")).lastModified(); QVERIFY(page.save()); QCOMPARE(read(file("native.zblock")), bytes); QCOMPARE(QFileInfo(file("native.zblock")).lastModified(), modified);
        page.executeCommand("selectAll"); QTest::qWait(100); QVERIFY(setClipboard(QStringLiteral("copy all sentinel"))); page.executeCommand("copy");
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->text().contains("Native 中文 first"), 5000);
        WebDocumentPage pasted(WebDocumentPage::Kind::Blocks); QVERIFY(pasted.setInitialContent("")); QVERIFY(ready(pasted));
        pasted.executeCommand("focus"); pasted.executeCommand("paste"); QVERIFY2(pasted.save(true, file("block-copied.zblock")), qPrintable(notice(pasted)));
        QCOMPARE(QJsonDocument::fromJson(read(file("block-copied.zblock"))).object().value("attachments").toArray().size(), 1);
        QVERIFY(QDir().mkpath(file("Blocks 迁移"))); QVERIFY(write(file("Blocks 迁移/native.zblock"), bytes));
        WebDocumentPage copied(WebDocumentPage::Kind::Blocks, nullptr, {{}, file("fresh block profile")});
        QVERIFY(copied.openFile(file("Blocks 迁移/native.zblock"))); QVERIFY(ready(copied)); QVERIFY(!copied.dirty());
        paste(copied, "Second environment"); QVERIFY(copied.save());
        const auto third = QJsonDocument::fromJson(read(file("Blocks 迁移/native.zblock"))).object(); QCOMPARE(third.value("documentId"), first.value("documentId")); QCOMPARE(third.value("attachments").toArray().size(), 1); QCOMPARE(read(file("native.zblock")), bytes);
        capture(copied, "block-portable-image");
    }
    void blockImportExportUnknownFormatAndMissingAttachment() {
        const QString markdown = "# Heading\n\nParagraph **bold** and *italic*.\n\n- parent\n  - child\n\n- [x] checked\n\n> Quote\n\n```custom-language\nkeep this language\n```\n\n| Left | Right |\n| --- | --- |\n| A | B |\n";
        QVERIFY(write(file("conversion.md"), markdown.toUtf8()));
        WebDocumentPage page(WebDocumentPage::Kind::Blocks); QString error; QVERIFY(page.importMarkdown(file("conversion.md"), &error)); QVERIFY2(ready(page), qPrintable(notice(page)));
        QVERIFY2(page.save(true, file("converted.zblock")), qPrintable(notice(page))); QCOMPARE(read(file("conversion.md")), markdown.toUtf8());
        {
            auto click = answer("OK"); QTimer picker; picker.setInterval(30);
            connect(&picker, &QTimer::timeout, this, [&] { for (auto* top : QApplication::topLevelWidgets()) if (auto* dialog = qobject_cast<QFileDialog*>(top); dialog && dialog->isVisible() && !dialog->property("documentTestSelected").toBool()) {
                dialog->setProperty("documentTestSelected", true); dialog->setDirectory(root); dialog->selectFile("exported.md");
                QTimer::singleShot(200, dialog, [dialog] { if (auto* name = dialog->findChild<QLineEdit*>("fileNameEdit")) name->setText("exported.md"); qInfo() << "Export selected files" << dialog->selectedFiles(); QMetaObject::invokeMethod(dialog, "accept"); });
            } }); picker.start();
            QTimer watchdog; watchdog.setSingleShot(true); watchdog.setInterval(15000);
            connect(&watchdog, &QTimer::timeout, this, [&] { for (auto* top : QApplication::topLevelWidgets()) if (auto* dialog = qobject_cast<QDialog*>(top); dialog && dialog->isVisible()) { dialog->grab().save(file("export-dialog-timeout.png")); dialog->reject(); } }); watchdog.start();
            QVERIFY2(page.exportMarkdown(), qPrintable(notice(page)));
        }
        QVERIFY(read(file("exported.md")).contains("custom-language")); QVERIFY(read(file("exported.md")).contains("Heading")); QCOMPARE(read(file("conversion.md")), markdown.toUtf8());
        WebDocumentPage exported(WebDocumentPage::Kind::Markdown); QVERIFY(exported.openFile(file("exported.md"))); QVERIFY(ready(exported)); QVERIFY(exported.save());
        auto package = QJsonDocument::fromJson(read(file("native.zblock"))).object(); QVERIFY(!package.isEmpty());
        package["version"] = 999; WebDocumentPage unknown(WebDocumentPage::Kind::Blocks); QVERIFY(!unknown.setInitialContent(QString::fromUtf8(QJsonDocument(package).toJson()), &error)); QVERIFY(error.contains("unknown format"));
        package["version"] = 1; package["attachments"] = QJsonArray();
        WebDocumentPage missing(WebDocumentPage::Kind::Blocks); QVERIFY(missing.setInitialContent(QString::fromUtf8(QJsonDocument(package).toJson()))); missing.show();
        QTRY_VERIFY_WITH_TIMEOUT(notice(missing).contains("attachment", Qt::CaseInsensitive), 30000); QVERIFY(!missing.ready()); QVERIFY(!missing.save(true, file("missing-attachment.zblock"))); QVERIFY(!QFileInfo::exists(file("missing-attachment.zblock")));
        capture(page, "blocks-converted-table-nesting");
    }
    void failedRuntimeStaleMessagesAndClosedCallbacks() {
        WebDocumentPage absent(WebDocumentPage::Kind::Markdown, nullptr, {file("missing-runtime"), file("missing-runtime-profile")});
        QVERIFY(absent.setInitialContent("Keep me")); absent.show(); QTRY_VERIFY_WITH_TIMEOUT(notice(absent).contains("resource", Qt::CaseInsensitive), 5000); QVERIFY(!absent.save(true, file("absent.md"))); QVERIFY(absent.dirty());
        auto page = std::make_unique<WebDocumentPage>(WebDocumentPage::Kind::Markdown); QVERIFY(page->setInitialContent("Guard")); QVERIFY(ready(*page));
        auto* web = page->findChild<WebEditorContainer*>(); QVERIFY(web); const auto revision = page->currentRevision();
        emit web->messageReceived({{"type", "changed"}, {"session", "stale"}, {"document", page->documentId()}, {"revision", 999}}); QCOMPARE(page->currentRevision(), revision);
        emit web->messageReceived({{"type", "snapshot"}, {"session", "stale"}, {"document", page->documentId()}, {"request", "obsolete"}, {"revision", 999}, {"content", "overwrite"}});
        QVERIFY(page->save(true, file("guard.md"))); QCOMPARE(read(file("guard.md")), QByteArray("Guard"));
        QPointer<WebEditorContainer> guarded(web); page.reset(); QVERIFY(guarded.isNull()); QTest::qWait(100);
        auto loading = std::make_unique<WebDocumentPage>(WebDocumentPage::Kind::Blocks); QVERIFY(loading->setInitialContent("")); loading->show(); QTest::qWait(1); loading.reset(); QTest::qWait(250);
    }
    void unavailableInstalledRuntimePreservesDocument() {
        const QByteArray previous = qgetenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER");
        const auto restore = qScopeGuard([previous] { if (previous.isNull()) qunsetenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"); else qputenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", previous); });
        const QString runtime = file("empty runtime"); QVERIFY(QDir().mkpath(runtime));
        qputenv("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", QDir::toNativeSeparators(runtime).toUtf8());
        const QString path = file("runtime-unavailable.md"); const QByteArray source("Preserve on unavailable runtime\n"); QVERIFY(write(path, source));
        WebDocumentPage page(WebDocumentPage::Kind::Markdown, nullptr, {{}, file("unavailable-runtime-profile")});
        QVERIFY(page.openFile(path)); page.show();
        QTRY_VERIFY_WITH_TIMEOUT(notice(page).contains("Runtime"), 30000);
        QVERIFY(!page.ready()); QVERIFY(!page.save()); QCOMPARE(read(path), source);
        qInfo() << "Unavailable runtime diagnostic" << notice(page);
    }
    void mainWindowRoutingSplitPathLockAndTargetCollision() {
        MainWindow window; window.resize(1400, 900); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); QVERIFY(tabs); QVERIFY(tabs->webDocuments().isEmpty());
        QVERIFY(window.findChildren<WebEditorContainer*>().isEmpty());
        QVERIFY(write(file("routing.sv"), "module routing; endmodule\n")); QVERIFY(tabs->openFileInTab(file("routing.sv"))); auto* code = tabs->getCurrentEditor(); QVERIFY(code);
        QVERIFY(tabs->openFileInTab(file("close.md"))); auto* markdown = tabs->currentWebDocument(); QVERIFY(markdown); QTRY_VERIFY_WITH_TIMEOUT(markdown->ready(), 30000); QVERIFY(!tabs->getCurrentEditor());
        QVERIFY(tabs->openFileInTab(file("native.zblock"))); auto* blocks = tabs->currentWebDocument(); QVERIFY(blocks); QTRY_VERIFY_WITH_TIMEOUT(blocks->ready(), 30000);
        QElapsedTimer switches; switches.start();
        for (int i = 0; i < 20; ++i) { QVERIFY(tabs->openFileInTab(markdown->fileName())); QVERIFY(tabs->openFileInTab(blocks->fileName())); QCoreApplication::processEvents(); }
        qInfo() << "Forty warm document switches ms" << switches.elapsed();
        QVERIFY(tabs->splitCurrentView(EditorSplitDirection::Right)); QCOMPARE(tabs->webDocuments().size(), 3);
        auto* splitBlocks = tabs->currentWebDocument(); QVERIFY(splitBlocks != blocks); QCOMPARE(splitBlocks->documentSession(), blocks->documentSession());
        QTRY_VERIFY_WITH_TIMEOUT(splitBlocks->ready(), 30000); QCOMPARE(tabs->splitCount(), 2);
        QVERIFY(tabs->mergeCurrentSplit()); QCOMPARE(tabs->splitCount(), 1);
        QVERIFY(!tabs->saveEditorView(code, true, markdown->fileName()));
        const auto markdownBytes = read(markdown->fileName());
        WorkspacePathMutation prepared; QString reason; QVERIFY2(tabs->prepareWorkspacePathMutation(markdown->fileName(), false, &prepared, &window, &reason), qPrintable(reason));
        QVERIFY(markdown->busy()); QVERIFY(tabs->validateWorkspacePathMutation(prepared, &reason));
        paste(*markdown, "Must not enter while locked", true); QTest::qWait(100); QCOMPARE(read(markdown->fileName()), markdownBytes);
        prepared = {}; QVERIFY(!markdown->busy()); QVERIFY(markdown->save()); QCOMPARE(read(markdown->fileName()), markdownBytes);
        capture(window, "mainwindow-routing-split"); QVERIFY(tabs->closeAllTabs()); QVERIFY(tabs->webDocuments().isEmpty()); QVERIFY(tabs->reopenClosedTab());
        if (auto* reopened = tabs->currentWebDocument()) QTRY_VERIFY_WITH_TIMEOUT(reopened->ready(), 30000);
        QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void hiddenWebDocumentsKeepViewportAndState() {
        QVERIFY(write(file("layout.sv"), "module layout; endmodule\n"));
        QVERIFY(write(file("layout-guide.md"), "# Heading\n\nA long first paragraph must wrap within the visible viewport, including its final Coordinator text.\n\n```cpp\nconst int value = 42;\n```\n"));
        QVERIFY(write(file("layout-image.md"), "Second background document\n"));
        {
            WebDocumentPage seed(WebDocumentPage::Kind::Blocks);
            QVERIFY(seed.setInitialContent("")); QTRY_VERIFY_WITH_TIMEOUT(seed.ready(), 30000);
            QVERIFY(seed.save(true, file("layout-photo.zblock")));
            QVERIFY(write(file("layout-blocks.zblock"), read(file("layout-photo.zblock"))));
        }
        MainWindow window; window.resize(1048, 600); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); QVERIFY(tabs);
        for (const auto& name : {"layout.sv", "layout-guide.md", "layout-image.md", "layout-photo.zblock", "layout-blocks.zblock"})
            QVERIFY(tabs->openFileInTab(file(name)));
        const auto pages = tabs->webDocuments(); QCOMPARE(pages.size(), 4);
        for (auto* page : pages) QTRY_VERIFY_WITH_TIMEOUT(page->ready(), 30000);
        for (const auto& name : {"layout-guide.md", "layout-photo.zblock", "layout-image.md", "layout-blocks.zblock"}) {
            QVERIFY(tabs->openFileInTab(file(name))); QTest::qWait(100);
            QVERIFY2(viewportMatches(tabs->currentWebDocument(), "first-show"), name);
        }
        auto* navigation = window.findChild<NavigationPaneCoordinator*>(); QVERIFY(navigation);
        for (const auto& name : {"layout-guide.md", "layout-blocks.zblock"}) {
            QVERIFY(tabs->openFileInTab(file(name))); QTest::qWait(100);
            auto* page = tabs->currentWebDocument(); QVERIFY(page);
            auto* web = page->findChild<WebEditorContainer*>(); QVERIFY(web);
            const auto nativeView = GetWindow(reinterpret_cast<HWND>(web->winId()), GW_CHILD);
            const auto identity = page->documentId();
            QSignalSpy navigations(web, &WebEditorContainer::navigationReady);
            const QString marker = "Unsaved layout state 中文";
            paste(*page, marker, page->documentKind() == WebDocumentPage::Kind::Markdown);
            QTRY_VERIFY_WITH_TIMEOUT(page->dirty(), 5000);
            QString before, after; quint64 beforeRevision = 0, afterRevision = 0;
            QVERIFY(page->captureRecovery(&before, &beforeRevision));
            QVERIFY(write(file(QString(name) + ".before-layout.json"), before.toUtf8()));
            QVERIFY(tabs->openFileInTab(file("layout.sv")));
            window.resize(1108, 640); QTest::qWait(100);
            QVERIFY(tabs->openFileInTab(file(name))); QTest::qWait(100);
            QVERIFY(viewportMatches(page, "hidden-resize-show"));
            window.resize(1048, 600); QTest::qWait(100);
            QVERIFY(viewportMatches(page, "visible-resize"));
            for (const auto* actionId : {ActionIds::ViewEditorSplitRight, ActionIds::ViewEditorSplitBelow}) {
                QString reason;
                QVERIFY(tabs->moveCurrentViewToSplit(actionId == ActionIds::ViewEditorSplitRight ? EditorSplitDirection::Right : EditorSplitDirection::Below));
                QTest::qWait(100); QCOMPARE(tabs->splitCount(), 2);
                for (auto* splitGroup : tabs->editorSplitController()->groups(QStringLiteral("main"))) QVERIFY(splitGroup->isVisible());
                QCOMPARE(tabs->currentWebDocument(), page);
                QVERIFY(viewportMatches(page, "split-move"));
                QAction* maximize = nullptr;
                for (auto* action : window.findChildren<QAction*>())
                    if (action->property("actionId").toString() == QString::fromLatin1(ActionIds::ViewEditorSplitMaximize)) maximize = action;
                QVERIFY(maximize); QVERIFY(maximize->isEnabled()); const auto splitSize = web->size();
                maximize->trigger(); QTest::qWait(100);
                QVERIFY(viewportMatches(page, "split-maximize")); QVERIFY(web->size() != splitSize);
                maximize->trigger(); QTest::qWait(100); QVERIFY(viewportMatches(page, "split-restore"));
                auto* splitter = window.findChild<QSplitter*>("editorSplit"); QVERIFY(splitter);
                splitter->setSizes({200, 500}); QTest::qWait(100);
                QVERIFY(viewportMatches(page, "split-resize"));
                QVERIFY2(tabs->requestTabAction(QString::fromLatin1(ActionIds::ViewEditorSplitMerge), nullptr, -1, &reason), qPrintable(reason));
                QTest::qWait(100); QCOMPARE(tabs->splitCount(), 1);
                QVERIFY(viewportMatches(page, "split-merge"));
            }
            const bool expanded = navigation->isExpanded(); const int oldWidth = web->width();
            navigation->setExpanded(!expanded); QTRY_VERIFY_WITH_TIMEOUT(!navigation->isAnimating(), 3000);
            QTest::qWait(100); QVERIFY(web->width() != oldWidth);
            QVERIFY(viewportMatches(page, "sidebar-toggle"));
            navigation->setExpanded(expanded); QTRY_VERIFY_WITH_TIMEOUT(!navigation->isAnimating(), 3000);
            QTest::qWait(100); QVERIFY(viewportMatches(page, "sidebar-restore"));
            QCOMPARE(page->documentId(), identity);
            QCOMPARE(GetWindow(reinterpret_cast<HWND>(web->winId()), GW_CHILD), nativeView);
            QCOMPARE(navigations.count(), 0);
            QVERIFY(page->captureRecovery(&after, &afterRevision));
            QCOMPARE(afterRevision, beforeRevision); QCOMPARE(after, before);
            page->executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(page->currentRevision() > afterRevision, 5000);
            QVERIFY(page->captureRecovery(&after, &afterRevision)); QVERIFY(after != before);
            QVERIFY(write(file(QString(name) + ".undo.json"), after.toUtf8()));
            page->executeCommand("redo"); QTRY_VERIFY_WITH_TIMEOUT(page->currentRevision() > afterRevision, 5000);
            QVERIFY(page->captureRecovery(&after, &afterRevision));
            QVERIFY(write(file(QString(name) + ".redo.json"), after.toUtf8()));
            page->executeCommand("focus"); QTest::qWait(100);
            QVERIFY(setClipboard(QStringLiteral("layout copy sentinel")));
            page->executeCommand("selectAll"); QTest::qWait(100); page->executeCommand("copy");
            QTRY_VERIFY2_WITH_TIMEOUT(QApplication::clipboard()->text().contains(marker), qPrintable(QApplication::clipboard()->text()), 5000);
            QVERIFY(page->save());
        }
        QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void mainWindowLayoutRestoresMixedViews_data() {
        QTest::addColumn<bool>("blocks"); QTest::newRow("markdown") << false; QTest::newRow("blocks") << true;
    }
    void mainWindowLayoutRestoresMixedViews() {
        QFETCH(bool, blocks);
        const QString workspace = file(blocks ? "layout-blocks" : "layout-markdown");
        QVERIFY(QDir().mkpath(workspace));
        const QString codePath = workspace + "/top.sv", pdfPath = workspace + "/read.pdf";
        QVERIFY(write(codePath, "module top;\n  logic signal;\nendmodule\n"));
        { QPdfWriter pdf(pdfPath); pdf.setResolution(96); QPainter paint(&pdf); paint.setFont(QFont("Arial", 24));
            paint.drawText(70, 100, "First"); QVERIFY(pdf.newPage()); paint.drawText(70, 100, "Second"); }
        auto windowOwner = std::make_unique<MainWindow>(); auto& window = *windowOwner;
        const auto screen = window.screen()->availableGeometry(); const auto available = screen.size();
        window.resize(available.width() * 9 / 10, available.height() * 9 / 10);
        window.move(screen.topLeft() + QPoint(12, 12)); window.show(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        auto* tabs = window.tabManager.get(); auto* splits = tabs->editorSplitController();
        auto* coordinator = window.findChild<WorkspaceSessionCoordinator*>(); QVERIFY(coordinator);
        QVERIFY(tabs->openFileInRegion(codePath, "right")); auto* code = tabs->getCurrentEditor(); QVERIFY(code);
        auto cursor = code->textCursor(); cursor.setPosition(3); cursor.setPosition(10, QTextCursor::KeepAnchor); code->setTextCursor(cursor);
        QString issue; const QString webPath = workspace + (blocks ? "/notes.zblock" : "/notes.md");
        auto* first = tabs->createWebDocumentFile(blocks ? WebDocumentPage::Kind::Blocks : WebDocumentPage::Kind::Markdown, webPath, "left", &issue);
        QVERIFY2(first, qPrintable(issue));
        QString content; for (int i = 0; i < 50; ++i) content += QStringLiteral("Saved paragraph %1 中文\n\n").arg(i);
        paste(*first, content, true); QTRY_VERIFY_WITH_TIMEOUT(first->dirty(), 5000); QVERIFY(first->save());
        QVERIFY(write(file(blocks ? "layout-blocks-initial.zblock" : "layout-markdown-initial.md"), read(webPath)));
        QVERIFY(tabs->duplicateCurrentView()); auto* second = tabs->currentWebDocument(); QVERIFY(second && second != first);
        QTRY_VERIFY_WITH_TIMEOUT(second->ready(), 30000); QVERIFY(splits->movePage(second, splits->regionGroup("main")));
        auto firstState = first->viewState(); firstState.insert("top", 120); first->restoreViewState(firstState);
        auto secondState = second->viewState(); secondState.insert("top", 0); second->restoreViewState(secondState);
        QVERIFY(tabs->openFileInRegion(pdfPath, "bottom")); auto* pdf = tabs->currentPdfDocument(); QVERIFY(pdf);
        QTRY_VERIFY_WITH_TIMEOUT(pdf->ready(), 30000); pdf->setPage(2); pdf->setScale("1.25");
        QTRY_COMPARE_WITH_TIMEOUT(pdf->viewState().value("page").toInt(), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(pdf->viewState().value("scale").toDouble(), 125.0, 10000);
        if (auto* tool = splits->pageById("tool:simulationLog")) QVERIFY(splits->movePage(tool, splits->regionGroup("right"), 0));
        QVERIFY(splits->activatePage(second)); QTest::qWait(150);
        capture(window, blocks ? "layout-blocks-before-save" : "layout-markdown-before-save");
        QVERIFY(coordinator->saveSession(false));
        WorkspaceSessionStateService service;
        const auto saved = service.load(workspace); QVERIFY(saved.loaded); QCOMPARE(saved.state.tabs.size(), 4);
        QVERIFY(!saved.state.ui.pageLayout.isEmpty());
        QVERIFY(write(file(blocks ? "layout-blocks.json" : "layout-markdown.json"), QJsonDocument(saved.state.ui.pageLayout).toJson()));
        QVERIFY(tabs->closeAllTabs());
        QVERIFY(coordinator->restoreSession());
        QCOMPARE(tabs->workspaceSessionTabs(workspace).size(), saved.state.tabs.size());
        QList<WebDocumentPage*> restoredWeb;
        for (const auto& state : saved.state.tabs) {
            auto* page = splits->pageById(state.pageId); QVERIFY(page);
            auto* group = splits->groupForPage(page); QVERIFY(group);
            QCOMPARE(splits->groupId(group), state.groupId); QCOMPARE(splits->regionForGroup(group), state.regionId);
            QCOMPARE(group->indexOf(page), state.tabIndex); QCOMPARE(page->property("editorViewId").toString(), state.viewId);
            if (auto* web = qobject_cast<WebDocumentPage*>(page)) {
                QTRY_VERIFY_WITH_TIMEOUT(web->ready(), 30000); QCOMPARE(web->documentId(), state.documentId); restoredWeb.append(web);
                QTRY_VERIFY_WITH_TIMEOUT(qAbs(web->viewState().value("top").toDouble() - state.viewState.value("top").toDouble()) < 2, 5000);
                QTRY_VERIFY_WITH_TIMEOUT(viewportMatches(web, "restored-layout"), 5000);
            } else if (auto* restoredPdf = qobject_cast<PdfDocumentPage*>(page)) {
                QTRY_VERIFY_WITH_TIMEOUT(restoredPdf->ready(), 30000);
                QTRY_COMPARE_WITH_TIMEOUT(restoredPdf->viewState().value("page").toInt(), 2, 10000);
                QTRY_COMPARE_WITH_TIMEOUT(restoredPdf->viewState().value("scale").toDouble(), 125.0, 10000);
                QTRY_VERIFY_WITH_TIMEOUT(viewportMatches(restoredPdf, "restored-pdf"), 5000);
                QTRY_VERIFY_WITH_TIMEOUT(restoredPdf->hasRenderedPage(2), 15000);
                capture(window, blocks ? "layout-blocks-pdf-render" : "layout-markdown-pdf-render");
            } else if (auto* restoredCode = qobject_cast<MyCodeEditor*>(page)) {
                QCOMPARE(restoredCode->textCursor().position(), 10); QCOMPARE(restoredCode->textCursor().anchor(), 3);
            }
        }
        QCOMPARE(restoredWeb.size(), 2); QCOMPARE(restoredWeb[0]->documentSession(), restoredWeb[1]->documentSession());
        const QString active = saved.state.ui.pageLayout.value("activePage").toString();
        QCOMPARE(splits->pageId(splits->activeGroup()->currentWidget()), active);
        capture(window, blocks ? "layout-blocks-wide" : "layout-markdown-wide");
        const int wideWidth = window.width(); window.resize(available.width() * 2 / 3, available.height() * 3 / 4); QTest::qWait(150);
        QVERIFY(window.width() < wideWidth); capture(window, blocks ? "layout-blocks-narrow" : "layout-markdown-narrow");
        if (auto* more = window.findChild<QToolButton*>("bottomPanelMore"); more && more->isVisible()) {
            auto* menu = more->menu(); QVERIFY(menu);
            menu->popup(more->mapToGlobal(more->rect().bottomLeft())); QTRY_VERIFY(menu->isVisible());
            auto* events = window.findChild<QToolButton*>("bottomPanelButton_simulationEvents"); QVERIFY(events);
            QTRY_VERIFY(events->isVisible()); QVERIFY(menu->isAncestorOf(events));
            if (!screen.contains(menu->frameGeometry())) qWarning() << "Overflow menu" << menu->frameGeometry() << screen;
            QTRY_VERIFY_WITH_TIMEOUT(screen.contains(menu->frameGeometry()), 5000);
            events->click(); menu->close();
            QCOMPARE(splits->regionActiveGroup("bottom")->currentWidget(), splits->pageById("tool:simulationEvents"));
            for (const auto& state : saved.state.tabs) if (state.kind == "pdf")
                QVERIFY(splits->activatePage(splits->pageById(state.pageId)));
        }
        QVERIFY(splits->activatePage(restoredWeb[0])); paste(*restoredWeb[0], "Restored shared history", true);
        QTRY_VERIFY_WITH_TIMEOUT(restoredWeb[1]->dirty(), 5000); restoredWeb[1]->executeCommand("undo");
        QVERIFY(restoredWeb[1]->save());
        const QByteArray beforeSwitch = read(webPath);
        QString beforeSwitchText;
        if (blocks) {
            QVERIFY(splits->activatePage(restoredWeb[0])); QVERIFY(setClipboard("before switch sentinel"));
            restoredWeb[0]->executeCommand("selectAll"); QTest::qWait(100); restoredWeb[0]->executeCommand("copy");
            QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->text().contains("Saved paragraph 0"), 5000);
            beforeSwitchText = QApplication::clipboard()->text();
        }
        paste(*restoredWeb[0], "Keep unsaved across workspace switch", true);
        QTRY_VERIFY_WITH_TIMEOUT(restoredWeb[1]->dirty(), 5000);
        const QString stableGroup = splits->groupId(splits->groupForPage(restoredWeb[0]));
        const QString otherWorkspace = workspace + "-other"; QVERIFY(QDir().mkpath(otherWorkspace));
        QVERIFY(write(otherWorkspace + "/other.sv", "module other; endmodule\n"));
        QVERIFY(coordinator->openWorkspace(otherWorkspace));
        QVERIFY(tabs->openFileInTab(otherWorkspace + "/other.sv"));
        QVERIFY(coordinator->switchWorkspace(0));
        QCOMPARE(splits->groupId(splits->groupForPage(restoredWeb[0])), stableGroup);
        QVERIFY(restoredWeb[0]->dirty()); QVERIFY(restoredWeb[1]->dirty());
        QCOMPARE(tabs->workspaceSessionTabs(workspace).size(), 4);
        restoredWeb[1]->executeCommand("undo"); QVERIFY(restoredWeb[1]->save());
        if (blocks) {
            QVERIFY(splits->activatePage(restoredWeb[1])); QVERIFY(setClipboard("after switch sentinel"));
            restoredWeb[1]->executeCommand("selectAll"); QTest::qWait(100); restoredWeb[1]->executeCommand("copy");
            QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->text(), beforeSwitchText, 5000);
            QVERIFY(WebDocumentPage::validateBlockPackage(QJsonDocument::fromJson(read(webPath)).object(), &issue));
        } else QCOMPARE(read(webPath), beforeSwitch);
        QVERIFY(coordinator->saveSession(false));
        const auto beforeReopen = service.load(workspace); QVERIFY(beforeReopen.loaded);
        QVERIFY(window.close()); windowOwner.reset();

        MainWindow reopened; reopened.show(); reopened.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&reopened));
        QVERIFY(reopened.workspaceManager->openWorkspace(workspace));
        auto* reopenedTabs = reopened.tabManager.get(); auto* reopenedSplits = reopenedTabs->editorSplitController();
        QTRY_COMPARE_WITH_TIMEOUT(reopenedTabs->workspaceSessionTabs(workspace).size(), 4, 10000);
        for (const auto& state : beforeReopen.state.tabs) {
            auto* page = reopenedSplits->pageById(state.pageId); QVERIFY(page);
            auto* group = reopenedSplits->groupForPage(page); QVERIFY(group);
            QCOMPARE(reopenedSplits->groupId(group), state.groupId); QCOMPARE(reopenedSplits->regionForGroup(group), state.regionId);
            QCOMPARE(group->indexOf(page), state.tabIndex); QCOMPARE(page->property("editorViewId").toString(), state.viewId);
            if (auto* web = qobject_cast<WebDocumentPage*>(page)) {
                QTRY_VERIFY_WITH_TIMEOUT(web->ready(), 30000); QCOMPARE(web->documentId(), state.documentId);
                QVERIFY(!web->dirty());
            } else if (auto* reading = qobject_cast<PdfDocumentPage*>(page)) {
                QTRY_VERIFY_WITH_TIMEOUT(reading->ready(), 30000);
                QTRY_COMPARE_WITH_TIMEOUT(reading->viewState().value("page").toInt(), 2, 10000);
                QTRY_COMPARE_WITH_TIMEOUT(reading->viewState().value("scale").toDouble(), 125.0, 10000);
                QTRY_VERIFY_WITH_TIMEOUT(reading->hasRenderedPage(2), 15000);
            }
        }
        capture(reopened, blocks ? "layout-blocks-reopened" : "layout-markdown-reopened");
        QVERIFY(reopenedTabs->closeAllTabs()); reopenedTabs->clearCrashRecoveryAfterNormalClose();
    }
    void mainWindowPdfReadingRangesAndLifecycle() {
        const auto phase = [&](const char* value) { QVERIFY(write(file("pdf-phase.txt"), QByteArray(value))); };
        phase("create");
        const QString path = file("reading.pdf");
        {
            QPdfWriter pdf(path); pdf.setResolution(96); QPainter paint(&pdf); paint.setFont(QFont(QStringLiteral("Arial"), 24));
            for (int page = 1; page <= 3; ++page) {
                if (page > 1) QVERIFY(pdf.newPage());
                paint.drawText(QPoint(70, 100), QStringLiteral("PDF range reading %1").arg(page));
            }
            QVERIFY(pdf.newPage()); QImage scan(700, 600, QImage::Format_RGB32); scan.fill(Qt::white);
            QPainter imagePaint(&scan); imagePaint.fillRect(QRect(50, 50, 600, 300), QColor(30, 100, 180)); imagePaint.end();
            paint.drawImage(QPoint(30, 30), scan); paint.end();
        }
        const auto original = read(path); const auto marker = original.lastIndexOf("startxref"); QVERIFY(marker >= 0);
        const auto xref = original.mid(marker + 9).trimmed().split('\n').first().trimmed(); bool numeric = false; xref.toLongLong(&numeric); QVERIFY(numeric);
        {
            QFile sparse(path); QVERIFY(sparse.open(QIODevice::ReadWrite)); QVERIFY(sparse.resize(96 * 1024 * 1024)); QVERIFY(sparse.seek(sparse.size()));
            const auto tail = QByteArray("\nstartxref\n") + xref + "\n%%EOF\n"; QCOMPARE(sparse.write(tail), tail.size());
        }
        MainWindow window; window.resize(1280, 820); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); auto* splits = tabs->editorSplitController();
        QVERIFY(tabs->openFileInRegion(path, QStringLiteral("left"))); auto* page = tabs->currentPdfDocument(); QVERIFY(page);
        QTRY_VERIFY2_WITH_TIMEOUT(page->ready(), qPrintable(page->lastError()), 30000); QCOMPARE(page->pageCount(), 4);
        QTRY_VERIFY2_WITH_TIMEOUT(page->lastRenderedPage() > 0, qPrintable(page->lastError()), 15000);
        QVERIFY(page->bytesRead() < 2 * 1024 * 1024); QVERIFY(!tabs->currentWebDocument()); QVERIFY(!tabs->getCurrentEditor());
        page->findText(QStringLiteral("range reading")); QTRY_COMPARE_WITH_TIMEOUT(page->matchCount(), 3, 10000);
        page->setPage(4); QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(), 4, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(page->lastRenderedPage(), 4, 10000); capture(window, "pdf-scanned-left");
        page->setPage(2); page->setScale(QStringLiteral("1.5"));
        QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("scale").toDouble(), 150.0, 10000);
        QCOMPARE(page->findChild<QComboBox*>(QStringLiteral("pdfZoom"))->currentText(), QStringLiteral("150%"));
        const auto view = page->viewState(); const auto viewId = page->viewIdentity(); const auto count = page->bytesRead();
        for (const auto& region : {QStringLiteral("main"), QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left")}) {
            QVERIFY(splits->movePage(page, splits->regionActiveGroup(region))); QVERIFY(splits->activatePage(page)); QTest::qWait(150);
            QCOMPARE(page->viewIdentity(), viewId); QCOMPARE(page->viewState().value("page").toInt(), 2); QVERIFY(page->ready());
            QVERIFY(page->findChild<WebEditorContainer*>()->isVisible());
        }
        QVERIFY(page->bytesRead() <= count + 65536); capture(window, "pdf-text-after-four-regions");
        page->setScale(QStringLiteral("page-width"));
        QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("scale").toString(), QStringLiteral("page-width"), 10000);
        for (const int readingPage : {2, 4}) {
            QVERIFY(splits->movePage(page,splits->regionActiveGroup(QStringLiteral("main"))));
            QVERIFY(splits->activatePage(page)); QTest::qWait(200);
            page->setPage(readingPage); QTest::qWait(150);
            QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(),readingPage,10000);
            const auto fitAnchor=page->viewState();
            for(const auto& region:{QStringLiteral("left"),QStringLiteral("main"),QStringLiteral("bottom"),QStringLiteral("main")}){
                QVERIFY(splits->movePage(page,splits->regionActiveGroup(region)));QVERIFY(splits->activatePage(page));QTest::qWait(200);
                QCOMPARE(page->viewState().value("page").toInt(),readingPage);
                QVERIFY2(qAbs(page->viewState().value("top").toDouble()-fitAnchor.value("top").toDouble())<3,
                    qPrintable(QString::fromUtf8(QJsonDocument(page->viewState()).toJson(QJsonDocument::Compact))));
                QCOMPARE(page->viewIdentity(),viewId);
                if (readingPage == 4) capture(window, "pdf-final-page-" + region);
            }
            page->setPage(1); QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(),1,10000);
        }
        page->restoreViewState(view);QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("scale").toDouble(),150.0,10000);
        QVERIFY(tabs->duplicateCurrentView()); auto* second = tabs->currentPdfDocument(); QVERIFY(second && second != page);
        QTRY_VERIFY2_WITH_TIMEOUT(second->ready(), qPrintable(second->lastError()), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(second->viewState().value("page").toInt(), 2, 10000);
        second->setPage(3); QTRY_COMPARE_WITH_TIMEOUT(second->viewState().value("page").toInt(), 3, 10000); QCOMPARE(page->viewState().value("page").toInt(), 2);
        QVERIFY(tabs->closeAllTabs()); QVERIFY(tabs->pdfDocuments().isEmpty());
        QVERIFY(tabs->reopenClosedTab()); page = tabs->currentPdfDocument(); QVERIFY(page); QTRY_VERIFY_WITH_TIMEOUT(page->ready(), 30000);
        connect(page, &PdfDocumentPage::statusChanged, this, [&, page] {
            QFile log(file("pdf-status.jsonl")); if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return;
            log.write(QJsonDocument(QJsonObject{{"ready", page->ready()}, {"error", page->lastError()}, {"state", page->viewState()}}).toJson(QJsonDocument::Compact) + '\n');
        });
        phase("overwrite");
        QVERIFY(write(path, original)); QTRY_VERIFY_WITH_TIMEOUT(!page->ready(), 5000); QVERIFY(!page->lastError().isEmpty());
        phase("reload-overwrite");
        QString issue; QVERIFY(page->openFile(path, &issue)); QTRY_VERIFY2_WITH_TIMEOUT(page->ready(), qPrintable(page->lastError()), 30000);
        page->restoreViewState(view); QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(), 2, 10000);
        phase("atomic-replace");
        {
            QSaveFile replaced(path); QVERIFY(replaced.open(QIODevice::WriteOnly)); QCOMPARE(replaced.write(original), original.size());
            QVERIFY2(replaced.commit(), qPrintable(replaced.errorString()));
        }
        phase("await-replacement-notice");
        QTRY_VERIFY_WITH_TIMEOUT(!page->ready(), 5000); QVERIFY(!page->lastError().isEmpty());
        phase("reload-replacement");
        QVERIFY(page->openFile(path, &issue)); QTRY_VERIFY2_WITH_TIMEOUT(page->ready(), qPrintable(page->lastError()), 30000);
        const QString renamed = file("renamed-reading.pdf"); const auto samePage = page;
        {
            phase("prepare-rename");
            WorkspacePathMutation mutation;
            QVERIFY2(tabs->prepareWorkspacePathMutation(path, false, &mutation, &window, &issue, {}, renamed), qPrintable(issue));
            phase("rename"); QVERIFY(QFile::rename(path, renamed));
            phase("finalize-rename"); QVERIFY2(tabs->finalizeWorkspacePathMutation(mutation, &issue), qPrintable(issue));
        }
        QCOMPARE(tabs->currentPdfDocument(), samePage); QCOMPARE(page->fileName(), QFileInfo(renamed).absoluteFilePath());
        QTRY_VERIFY2_WITH_TIMEOUT(page->ready(), qPrintable(page->lastError()), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(page->viewState().value("page").toInt(), 2, 10000);
        phase("close-renamed");
        QVERIFY(tabs->closeAllTabs());
        const QString broken = file("broken.pdf"); QVERIFY(write(broken, "invalid PDF")); QVERIFY(tabs->openFileInTab(broken));
        page = tabs->currentPdfDocument(); QVERIFY(page); QTRY_VERIFY_WITH_TIMEOUT(!page->lastError().isEmpty(), 10000); QVERIFY(!page->ready()); QVERIFY(tabs->closeAllTabs());
        QVERIFY(tabs->openFileInTab(renamed)); QVERIFY(tabs->closeAllTabs()); QTest::qWait(100); QVERIFY(tabs->pdfDocuments().isEmpty());
        phase("complete");
    }
    void mainWindowUntitledActions_data() {
        QTest::addColumn<bool>("blocks"); QTest::newRow("markdown") << false; QTest::newRow("blocks") << true;
    }
    void mainWindowUntitledActions() {
        QFETCH(bool,blocks);
        MainWindow window; window.resize(1200,800); window.show(); window.activateWindow();
        auto* tabs=window.tabManager.get(); auto* splits=tabs->editorSplitController();
        auto* action=window.findChild<QAction*>(blocks ? "leftDocument.blocks" : "leftDocument.markdown"); QVERIFY(action);
        const auto before=QDir(root).entryList(QDir::Files);
        action->trigger(); auto* first=tabs->currentWebDocument(); QVERIFY(first);
        QVERIFY(first->fileName().isEmpty()); QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(splits->groupForPage(first),splits->regionGroup("left"));
        QTRY_VERIFY_WITH_TIMEOUT(first->ready(),30000);
        action->trigger(); auto* second=tabs->currentWebDocument(); QVERIFY(second && second!=first);
        QVERIFY(second->fileName().isEmpty()); QVERIFY(first->documentId()!=second->documentId());
        QTRY_VERIFY_WITH_TIMEOUT(second->ready(),30000);
        QCOMPARE(QDir(root).entryList(QDir::Files),before);
        QVERIFY(splits->activatePage(first)); paste(*first,"Untitled content before first save",true);
        QTRY_VERIFY_WITH_TIMEOUT(first->dirty(),5000);
        QTimer picker; picker.setInterval(25); bool cancelled=false;
        connect(&picker,&QTimer::timeout,&window,[&] {
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                cancelled=true; picker.stop(); dialog->reject();
            }
        });
        picker.start(); QVERIFY(!first->save()); QVERIFY(cancelled); QVERIFY(first->fileName().isEmpty()); QVERIFY(first->dirty());
        { auto cancel=answer("Cancel"); QVERIFY(!first->confirmClose()); }
        const auto path=file(blocks ? "untitled-first.zblock" : "untitled-first.md");
        disconnect(&picker,nullptr,&window,nullptr); bool selected=false;
        connect(&picker,&QTimer::timeout,&window,[&] {
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                selected=true; picker.stop(); dialog->setDirectory(QFileInfo(path).absolutePath());
                dialog->selectFile(QFileInfo(path).fileName());
                QTimer::singleShot(100,dialog,[dialog,path]{
                    if(auto* name=dialog->findChild<QLineEdit*>("fileNameEdit")) name->setText(QFileInfo(path).fileName());
                    QMetaObject::invokeMethod(dialog,"accept");
                });
            }
        });
        picker.start(); QVERIFY2(first->save(),qPrintable(notice(*first))); QVERIFY(selected);
        QCOMPARE(first->fileName(),QFileInfo(path).absoluteFilePath()); QVERIFY(QFileInfo::exists(path)); QVERIFY(!first->dirty());
        QCOMPARE(splits->groupForPage(first)->tabToolTip(splits->groupForPage(first)->indexOf(first)),first->fileName());
        QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void mainWindowCreateDocumentsWithoutOverwrite() {
        MainWindow window; window.resize(1200, 800); window.show(); window.activateWindow();
        auto* tabs = window.tabManager.get(); auto* splits = tabs->editorSplitController(); QString issue;
        for (const bool blocks : {false, true}) {
            const auto kind = blocks ? WebDocumentPage::Kind::Blocks : WebDocumentPage::Kind::Markdown;
            const QString path = file(blocks ? "new-left.zblock" : "new-left.md");
            auto* page = tabs->createWebDocumentFile(kind, path, QStringLiteral("left"), &issue); QVERIFY2(page, qPrintable(issue)); QVERIFY(page->ready()); QVERIFY(!page->dirty());
            QCOMPARE(splits->groupForPage(page), splits->regionGroup(QStringLiteral("left"))); QVERIFY(QFileInfo::exists(path));
            const auto bytes = read(path); if (blocks) QVERIFY(WebDocumentPage::validateBlockPackage(QJsonDocument::fromJson(bytes).object(), &issue));
            const auto count = tabs->webDocuments().size(); QVERIFY(!tabs->createWebDocumentFile(kind, path, QStringLiteral("left"), &issue)); QCOMPARE(tabs->webDocuments().size(), count); QCOMPARE(read(path), bytes);
            const QString missingParent = file(blocks ? "absent/new.zblock" : "absent/new.md");
            QVERIFY(!tabs->createWebDocumentFile(kind, missingParent, QStringLiteral("left"), &issue)); QVERIFY(!QFileInfo::exists(missingParent)); QCOMPARE(tabs->webDocuments().size(), count);
            QVERIFY(splits->movePage(page, splits->regionGroup(QStringLiteral("main"))));
            QVERIFY(tabs->openFileInRegion(path, QStringLiteral("left"))); QCOMPARE(tabs->currentWebDocument(), page); QCOMPARE(tabs->webDocuments().size(), count);
        }
        QVERIFY(tabs->closeAllTabs()); tabs->clearCrashRecoveryAfterNormalClose();
    }
    void cleanupTestCase() {
        auto* restored = new QMimeData;
        for (auto i = clipboardFormats.cbegin(); i != clipboardFormats.cend(); ++i) restored->setData(i.key(), i.value());
        if (!clipboardImage.isNull()) restored->setImageData(clipboardImage);
        QApplication::clipboard()->setMimeData(restored);
    }
};
QTEST_MAIN(WebDocumentIntegrationTest)
#include "web_document_integration_test.moc"
