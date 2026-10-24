#include "applicationthememanager.h"
#include "mainwindow.h"
#include "navigationpanecoordinator.h"
#include "tabmanager.h"
#include "uidialogs.h"
#include "webdocumentpage.h"
#include "webeditorcontainer.h"
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
    void paste(WebDocumentPage& page, const QString& value, bool replace = false) {
        QApplication::clipboard()->setText(value);
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
    void capture(QWidget& page, const QString& name) { QVERIFY(page.grab().save(file(name + ".png"))); }
    bool viewportMatches(WebDocumentPage* page, const char* phase) {
        auto* web = page->findChild<WebEditorContainer*>();
        if (!web || !web->isVisible()) return false;
        const HWND host = reinterpret_cast<HWND>(web->winId());
        const HWND child = GetWindow(host, GW_CHILD);
        RECT hostRect{}, childRect{};
        if (!child || !GetClientRect(host, &hostRect) || !GetWindowRect(child, &childRect)) return false;
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&childRect), 2);
        const QSize hostSize(hostRect.right, hostRect.bottom);
        const QSize childSize(childRect.right - childRect.left, childRect.bottom - childRect.top);
        const QSize qtSize = (QSizeF(web->size()) * web->devicePixelRatioF()).toSize();
        qInfo() << "Viewport" << phase << int(page->documentKind()) << "dpr" << web->devicePixelRatioF()
                << "Qt" << qtSize << "host" << hostSize << "WebView" << childSize;
        return hostSize == childSize && qAbs(hostSize.width() - qtSize.width()) <= 1
            && qAbs(hostSize.height() - qtSize.height()) <= 1 && childRect.left == 0 && childRect.top == 0;
    }
private slots:
    void initTestCase() {
        QVERIFY2(QGuiApplication::platformName() == "windows", "These tests require real WebView2 on Windows, not an offscreen substitute.");
        root = qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR"); QVERIFY(!root.isEmpty()); QVERIFY(QDir().mkpath(root));
        QVERIFY(!qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT").isEmpty());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT"));
        auto& theme = ApplicationThemeManager::instance(); QVERIFY(theme.selectBackend(UiStyleBackend::Ela));
        theme.setMode(ThemeMode::Light); theme.applyToApplication();
        if (const auto* data = QApplication::clipboard()->mimeData()) {
            for (const auto& format : data->formats()) clipboardFormats[format] = data->data(format);
            if (data->hasImage()) clipboardImage = qvariant_cast<QImage>(data->imageData());
        }
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
        QApplication::clipboard()->setText("clipboard sentinel"); page.executeCommand("selectAll"); page.executeCommand("copy");
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
        QImage image(24, 16, QImage::Format_ARGB32); image.fill(QColor("#175cd3")); QApplication::clipboard()->setImage(image);
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
        const auto first = QJsonDocument::fromJson(read(file("native.zblock"))).object(); QVERIFY(first.value("state").toString().size() > 50);
        QVERIFY(write(file("native-first.zblock"), read(file("native.zblock"))));
        page.executeCommand("undo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(write(file("native-undo.zblock"), read(file("native.zblock"))));
        page.executeCommand("redo"); QTRY_VERIFY_WITH_TIMEOUT(page.dirty(), 5000); QVERIFY(page.save());
        QVERIFY(write(file("native-redo.zblock"), read(file("native.zblock"))));
        QApplication::clipboard()->setText("block clipboard sentinel"); page.executeCommand("selectAll"); QTest::qWait(100); page.executeCommand("copy");
        QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->text().contains("Native 中文 first"), 5000);
        QVERIFY(QApplication::clipboard()->mimeData()->html().contains("data-blocksuite-snapshot"));
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(QColor("#12b76a")); QApplication::clipboard()->setImage(image);
        page.executeCommand("focus"); page.executeCommand("paste");
        QVERIFY2(page.save(), qPrintable(notice(page)));
        const auto second = QJsonDocument::fromJson(read(file("native.zblock"))).object(); QCOMPARE(second.value("documentId"), first.value("documentId")); QCOMPARE(second.value("workspaceId"), first.value("workspaceId"));
        QVERIFY(second.value("state") != first.value("state")); QCOMPARE(second.value("attachments").toArray().size(), 1);
        QString error; QVERIFY2(WebDocumentPage::validateBlockPackage(second, &error), qPrintable(error));
        const auto bytes = read(file("native.zblock")); const auto modified = QFileInfo(file("native.zblock")).lastModified(); QVERIFY(page.save()); QCOMPARE(read(file("native.zblock")), bytes); QCOMPARE(QFileInfo(file("native.zblock")).lastModified(), modified);
        page.executeCommand("selectAll"); QTest::qWait(100); QApplication::clipboard()->setText("copy all sentinel"); page.executeCommand("copy");
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
        QVERIFY(tabs->splitCurrentView(EditorSplitDirection::Right)); QCOMPARE(tabs->webDocuments().size(), 2); QCOMPARE(tabs->currentWebDocument(), blocks); QCOMPARE(tabs->splitCount(), 2);
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
                QVERIFY2(tabs->requestTabAction(QString::fromLatin1(actionId), nullptr, -1, &reason), qPrintable(reason));
                QTest::qWait(100); QCOMPARE(tabs->splitCount(), 2);
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
            QApplication::clipboard()->setText("layout copy sentinel");
            page->executeCommand("selectAll"); QTest::qWait(100); page->executeCommand("copy");
            QTRY_VERIFY2_WITH_TIMEOUT(QApplication::clipboard()->text().contains(marker), qPrintable(QApplication::clipboard()->text()), 5000);
            QVERIFY(page->save());
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
