#include "analysisscheduler.h"
#include "editorcoordinator.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include <zeroslack/documents/documentfileread.h>
#include <zeroslack/semantic/pinloomcodelinkstore.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QAction* actionFor(QMenu& menu, const QString& id)
{
    for (auto* action : menu.actions()) {
        if (action->property("actionId").toString() == id) return action;
        if (action->menu()) if (auto* found = actionFor(*action->menu(), id)) return found;
    }
    return nullptr;
}

struct Fixture {
    QTemporaryDir directory;
    QTabWidget widget;
    TabManager tabs{&widget};
    ProjectModel project;
    SymbolAnalyzer analyzer;
    AnalysisScheduler scheduler;
    int finished = 0;
    int failed = 0;
    int started = 0;

    Fixture()
    {
        tabs.setWorkspaceScope({directory.path()}, directory.path());
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory.filePath("recovery")));
        tabs.unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(tabs.getDocumentModel());
        QObject::connect(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisStarted, &scheduler, [&] { ++started; });
        QObject::connect(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished, &scheduler, [&] { ++finished; });
        QObject::connect(&analyzer, &SymbolAnalyzer::semanticAnalysisFailed, &scheduler, [&] { ++failed; });
    }
    ~Fixture() { scheduler.shutdown(); tabs.closeAllTabs(); }
    void start(const QStringList& files)
    {
        project.setWorkspaceState(directory.path(), files);
        scheduler.setProjectModel(&project);
    }
    bool published(const QString& path, const QString& text) const
    {
        return !scheduler.isSemanticAnalysisActive()
            && SemanticIndex::getInstance()->getCachedFileContent(path) == text;
    }
};
}

class SemanticSourceGenerationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }

    void externalNormalization_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char* kind : {"lf", "crlf", "utf16-crlf", "cr-only", "line-separator", "nbsp"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void externalNormalization()
    {
        QFETCH(QString, kind);
        Fixture f; QVERIFY(f.directory.isValid());
        const QString file = f.directory.filePath("external.sv");
        const QString initial = "module fixture;\n// marker\nlogic old_value;\nendmodule\n";
        const QString expected = "module fixture;\n// marker\nlogic new_value;\nendmodule\n";
        QVERIFY(writeBytes(file, initial.toUtf8())); f.start({file});
        QTRY_VERIFY_WITH_TIMEOUT(f.finished == 1 && f.published(file, initial), 10000);
        QString serialized = expected;
        if (kind == "cr-only") serialized.replace('\n', '\r');
        if (kind == "line-separator") serialized.replace('\n', QChar::LineSeparator);
        if (kind == "nbsp") serialized.replace("logic new_value", QString("logic") + QChar::Nbsp + "new_value");
        if (kind == "crlf" || kind == "utf16-crlf") serialized.replace("\n", "\r\n");
        const QByteArray raw = kind == "utf16-crlf"
            ? QByteArray(QStringEncoder(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom)(serialized))
            : serialized.toUtf8();
        QVERIFY(writeBytes(file, raw));
        QCOMPARE(readDocumentFile(file).text, expected);
        f.scheduler.handleExternalFileChanged(file, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.finished >= 2 && f.published(file, expected), 10000);
        QVERIFY(f.failed == 0);
        bool symbolFound = false;
        for (const auto& record : SemanticIndex::getInstance()->getSymbolRecords(file)) symbolFound |= record.name == "new_value";
        QVERIFY(symbolFound); QCOMPARE(readDocumentFile(file).rawBytes, raw);
    }

    void externalRequestKeepsDiskProvenance_data()
    {
        QTest::addColumn<bool>("removeBeforeWorker");
        QTest::newRow("latest-change-before-worker") << false;
        QTest::newRow("deleted-before-worker-and-recover") << true;
    }
    void externalRequestKeepsDiskProvenance()
    {
        QFETCH(bool, removeBeforeWorker);
        std::atomic_bool release{false};
        Fixture f;
        const QString file = f.directory.filePath("generation.sv");
        const QString initial = "module fixture; logic initial_value; endmodule\n";
        const QString first = "module fixture; logic first_value; endmodule\n";
        const QString latest = "module fixture;\rlogic latest_value;\rendmodule\r";
        QVERIFY(writeBytes(file, initial.toUtf8())); f.start({file});
        QTRY_VERIFY_WITH_TIMEOUT(f.finished == 1 && f.published(file, initial), 10000);
        const auto published = SemanticIndex::getInstance()->snapshot();
        f.analyzer.setWorkspaceWorkerStartGateForTesting([&](const auto& cancelled) {
            while (!release.load() && !cancelled()) QThread::msleep(2);
        });
        const auto releaseOnExit = qScopeGuard([&] { release = true; });
        QVERIFY(writeBytes(file, first.toUtf8())); f.scheduler.handleExternalFileChanged(file, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.started >= 2, 3000);
        if (removeBeforeWorker) QVERIFY(QFile::remove(file));
        else QVERIFY(writeBytes(file, latest.toUtf8()));
        release = true;
        if (removeBeforeWorker) {
            QTRY_VERIFY_WITH_TIMEOUT(f.failed > 0 && !f.scheduler.isSemanticAnalysisActive(), 10000);
            QCOMPARE(SemanticIndex::getInstance()->snapshot(), published);
            QVERIFY(writeBytes(file, latest.toUtf8())); f.scheduler.handleExternalFileChanged(file, 0);
        }
        QTRY_VERIFY_WITH_TIMEOUT(f.published(file, QString(latest).replace('\r', '\n')), 10000);
        QCOMPARE(readDocumentFile(file).rawBytes, latest.toUtf8());
    }

    void dirtyCloseAndUnreadableRecovery()
    {
        Fixture f;
        const QString file = f.directory.filePath("dirty.sv");
        const QString initial = "module fixture; logic initial_value; endmodule\n";
        const QString external = "module fixture;\rlogic external_value;\rendmodule\r";
        QVERIFY(writeBytes(file, initial.toUtf8())); QVERIFY(f.tabs.openFileInTab(file)); f.start({file});
        QTRY_VERIFY_WITH_TIMEOUT(f.finished >= 1 && f.published(file, initial), 10000);
        auto* editor = f.tabs.getCurrentEditor();
        QTextCursor edit(editor->document()); edit.insertText("// local\n");
        const QString local = editor->toPlainText();
        const int starts = f.started;
        QVERIFY(writeBytes(file, external.toUtf8())); f.scheduler.handleExternalFileChanged(file, 0);
        QTest::qWait(40); QCOMPARE(f.started, starts);
        QCOMPARE(editor->toPlainText(), local); QVERIFY(editor->document()->isModified());
        QVERIFY(f.tabs.closeAllTabs()); f.scheduler.handleExternalFileChanged(file, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.published(file, QString(external).replace('\r', '\n')), 10000);
        const auto valid = SemanticIndex::getInstance()->snapshot();
        QVERIFY(QFile::remove(file)); QVERIFY(QDir().mkdir(file));
        f.scheduler.handleExternalFileChanged(file, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.failed > 0 && !f.scheduler.isSemanticAnalysisActive(), 10000);
        QCOMPARE(SemanticIndex::getInstance()->snapshot(), valid);
        QVERIFY(QDir().rmdir(file));
        QVERIFY(writeBytes(file, initial.toUtf8())); f.scheduler.handleExternalFileChanged(file, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.published(file, initial), 10000);
    }

    void menuSourceGeneration_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char* kind : {"current", "prefix-shift", "cross-file", "cross-file-dirty",
             "origin-edit-after-menu", "target-edit-after-menu", "target-reopen-after-menu",
             "closed-target-change-after-menu", "origin-close-after-menu"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void menuSourceGeneration()
    {
        QFETCH(QString, kind);
        Fixture f;
        const bool cross = kind.startsWith("cross-") || kind.startsWith("target-") || kind.startsWith("closed-target-");
        const QString origin = f.directory.filePath("origin.sv"), target = f.directory.filePath("defs.sv");
        const QString originText = cross ? "module fixture; import defs::*; logic out; assign out = payload; endmodule\n"
            : "module fixture;\n  logic payload;\n  assign payload = 1'b0;\nendmodule\n";
        const QString targetText = "package defs;\n  parameter payload = 1;\nendpackage\n";
        QVERIFY(writeBytes(origin, originText.toUtf8()));
        if (cross) QVERIFY(writeBytes(target, targetText.toUtf8()));
        MyCodeEditor* targetEditor = nullptr;
        if (cross && !kind.startsWith("closed-target")) {
            QVERIFY(f.tabs.openFileInTab(target)); targetEditor = f.tabs.getCurrentEditor();
        }
        QVERIFY(f.tabs.openFileInTab(origin)); auto* editor = f.tabs.getCurrentEditor();
        f.start(cross ? QStringList{target, origin} : QStringList{origin});
        QTRY_VERIFY_WITH_TIMEOUT(f.finished >= 1 && f.published(origin, originText), 10000);
        f.scheduler.shutdown();
        EditorActionContextService actionContext; actionContext.updateWorkspaceContext(f.project.snapshot());
        EditorCoordinator coordinator(&f.tabs); coordinator.setActionContextService(&actionContext); coordinator.attachEditor(editor);
        QVariantMap emitted; QString reason;
        coordinator.setRegisteredActionRequestHandler([&](const QString&, const QVariantMap& parameters) { emitted = parameters.value("linkSource").toMap(); });
        coordinator.setStatusMessageHandler([&](const QString& message, int) { reason = message; });
        if (kind == "prefix-shift") { QTextCursor edit(editor->document()); edit.insertText("// added prefix\n"); }
        if (kind == "cross-file-dirty") { QTextCursor edit(targetEditor->document()); edit.insertText("// target prefix\n"); }
        const int position = editor->cachedDocumentText().indexOf("payload");
        QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor);
        const auto context = editor->editorSemanticContextForPosition(position, true);
        QMenu menu; coordinator.populateSourceSymbolContextMenuForTest(&menu, context);
        auto* action = actionFor(menu, "pinloom.linkSelection"); QVERIFY(action);
        if (kind == "prefix-shift" || kind == "cross-file-dirty") {
            QVERIFY(!action->isEnabled()); QVERIFY(!action->toolTip().isEmpty()); QVERIFY(emitted.isEmpty()); return;
        }
        QVERIFY2(action->isEnabled(), qPrintable(action->toolTip()));
        if (kind == "origin-edit-after-menu") { QTextCursor edit(editor->document()); edit.insertText("// changed\n"); }
        if (kind == "target-edit-after-menu") { QTextCursor edit(targetEditor->document()); edit.insertText("// changed\n"); }
        if (kind == "target-reopen-after-menu") {
            QVERIFY(f.tabs.activateOpenFile(target)); f.tabs.closeTab(f.widget.currentIndex());
            QVERIFY(f.tabs.openFileInTab(target)); QVERIFY(f.tabs.activateOpenFile(origin));
        }
        if (kind == "closed-target-change-after-menu") QVERIFY(writeBytes(target, ("// changed\n" + targetText).toUtf8()));
        if (kind == "origin-close-after-menu") { QVERIFY(f.tabs.closeAllTabs()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
        action->trigger();
        if (kind.endsWith("after-menu")) {
            QVERIFY(emitted.isEmpty());
            if (kind != "origin-close-after-menu") QVERIFY(!reason.isEmpty());
        } else {
            const auto source = PinloomSourceSelection::fromVariantMap(emitted);
            QVERIFY(source.isValid()); QCOMPARE(source.symbolName, QString("payload"));
            QCOMPARE(source.selectedText, QString("payload"));
            QCOMPARE(source.absoluteFilePath, cross ? target : origin);
            QCOMPARE(source.startPosition, cross ? targetText.indexOf("payload") : originText.indexOf("payload"));
        }
        QCOMPARE(readDocumentFile(origin).rawBytes, originText.toUtf8());
    }

    void syntaxOnlyLink_data()
    {
        QTest::addColumn<QString>("block");
        QTest::newRow("always") << QString("always_comb begin\n  payload = 1'b0;\nend");
        QTest::newRow("assign") << QString("assign payload = 1'b0;");
    }
    void syntaxOnlyLink()
    {
        QFETCH(QString, block);
        Fixture f; const QString file = f.directory.filePath("syntax.sv");
        const QString text = "module fixture; logic payload;\n" + block + "\nendmodule\n";
        QVERIFY(writeBytes(file, text.toUtf8())); QVERIFY(f.tabs.openFileInTab(file));
        SemanticIndex::getInstance()->clearSemanticState();
        f.project.setWorkspaceState(f.directory.path(), {file});
        EditorActionContextService actionContext; actionContext.updateWorkspaceContext(f.project.snapshot());
        EditorCoordinator coordinator(&f.tabs); coordinator.setActionContextService(&actionContext);
        auto* editor = f.tabs.getCurrentEditor(); coordinator.attachEditor(editor);
        QVariantMap emitted;
        coordinator.setRegisteredActionRequestHandler([&](const QString&, const QVariantMap& parameters) { emitted = parameters.value("linkSource").toMap(); });
        const int start = text.indexOf(block);
        QTextCursor selection(editor->document()); selection.setPosition(start); selection.setPosition(start + block.size(), QTextCursor::KeepAnchor);
        editor->setTextCursor(selection);
        QMenu menu; coordinator.populateSourceSymbolContextMenuForTest(&menu, editor->editorSemanticContextForPosition(start, true));
        auto* action = actionFor(menu, "pinloom.linkSelection"); QVERIFY(action); QVERIFY2(action->isEnabled(), qPrintable(action->toolTip()));
        action->trigger(); const auto source = PinloomSourceSelection::fromVariantMap(emitted);
        QVERIFY(source.isValid()); QCOMPARE(source.selectedText, block); QCOMPARE(source.startPosition, start);
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv); application.setQuitOnLastWindowClosed(false);
    application.setOrganizationName("ZeroSlack"); application.setApplicationName("SemanticSourceGenerationTest");
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", profile.filePath("session.ini").toUtf8());
    SemanticSourceGenerationTest test; return QTest::qExec(&test, argc, argv);
}
#include "semantic_source_generation_test.moc"
