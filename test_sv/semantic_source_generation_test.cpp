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

    void unchangedDirectoryNotifications_data()
    {
        QTest::addColumn<bool>("dirtyOverlay");
        QTest::newRow("disk-publication") << false;
        QTest::newRow("dirty-document-publication") << true;
    }
    void unchangedDirectoryNotifications()
    {
        QFETCH(bool, dirtyOverlay);
        std::atomic_bool release{false};
        std::atomic_int workers{0};
        Fixture f;
        const QString file = f.directory.filePath("stable.sv");
        const QString disk = "module fixture; logic stable_value; endmodule\n";
        QString visible = disk;
        QVERIFY(writeBytes(file, disk.toUtf8()));
        if (dirtyOverlay) {
            QVERIFY(f.tabs.openFileInTab(file));
            QTextCursor cursor(f.tabs.getCurrentEditor()->document()); cursor.insertText("// local overlay\n");
            visible = f.tabs.getCurrentEditor()->toPlainText();
        }
        f.start({file});
        if (dirtyOverlay) {
            QTRY_COMPARE(f.tabs.getDocumentModel()->cachedDocumentForFile(file).text, visible);
            f.scheduler.requestDocumentSemanticRefresh({file});
        }
        QTRY_VERIFY_WITH_TIMEOUT(f.finished >= 1 && f.published(file, visible), 10000);
        const auto publication = SemanticIndex::getInstance()->snapshotToken();
        const int started = f.started, finished = f.finished;
        QSignalSpy checked(&f.analyzer, &SymbolAnalyzer::semanticInputsChecked);
        f.analyzer.setWorkspaceWorkerStartGateForTesting([&](const auto& cancelled) {
            ++workers;
            while (!release.load() && !cancelled()) QThread::msleep(2);
        });
        const auto releaseOnExit = qScopeGuard([&] { release = true; });
        QVERIFY(QDir().mkpath(f.directory.filePath(".simdock")));
        QVERIFY(writeBytes(f.directory.filePath(".simdock/design.json"), "{\"expanded\":[\"fixture\"]}"));
        f.scheduler.handleSemanticInputsChanged(f.directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(workers.load(), 1, 3000);
        for (int i = 0; i < 20; ++i) f.scheduler.handleSemanticInputsChanged(f.directory.path());
        QCOMPARE(f.started, started); QVERIFY(!f.scheduler.isSemanticAnalysisActive());
        release = true;
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 2, 10000);
        QCOMPARE(workers.load(), 2);
        for (const auto& result : checked) QVERIFY(!result.at(1).toBool());
        QCOMPARE(f.started, started); QCOMPARE(f.finished, finished); QCOMPARE(f.failed, 0);
        QCOMPARE(SemanticIndex::getInstance()->snapshotToken().revision, publication.revision);
        QCOMPARE(SemanticIndex::getInstance()->snapshotToken().snapshot, publication.snapshot);
        if (dirtyOverlay) {
            QCOMPARE(f.tabs.getCurrentEditor()->toPlainText(), visible);
            QVERIFY(f.tabs.getCurrentEditor()->document()->isModified());
        }
    }

    void changedDirectoryInputs_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char* kind : {"source", "include", "missing-include", "higher-include-created", "higher-include-deleted"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void changedDirectoryInputs()
    {
        QFETCH(QString, kind);
        Fixture f;
        const QString file = f.directory.filePath("top.sv");
        const QString first = f.directory.filePath("first"), second = f.directory.filePath("second");
        QVERIFY(QDir().mkpath(first)); QVERIFY(QDir().mkpath(second));
        const QString initial = "`include \"pick.svh\"\nmodule fixture; logic source_before; endmodule\n";
        QVERIFY(writeBytes(file, initial.toUtf8()));
        if (kind != "missing-include")
            QVERIFY(writeBytes(second + "/pick.svh", "module picked_low; endmodule\n"));
        if (kind == "higher-include-deleted")
            QVERIFY(writeBytes(first + "/pick.svh", "module picked_high; endmodule\n"));
        ProjectSnapshot project;
        project.workspaceRoot = f.directory.path(); project.systemVerilogFiles = {file}; project.allFiles = {file};
        project.includeDirs = {first, second};
        f.project.setWorkspaceState(project); f.scheduler.setProjectModel(&f.project);
        QTRY_VERIFY_WITH_TIMEOUT(f.finished == 1 && f.published(file, initial), 10000);
        QSignalSpy checked(&f.analyzer, &SymbolAnalyzer::semanticInputsChecked);
        QString expected = "picked_after", absent = "picked_low";
        if (kind == "source") {
            QVERIFY(writeBytes(file, QString(initial).replace("source_before", "source_after").toUtf8()));
            expected = "source_after"; absent = "source_before";
        } else if (kind == "higher-include-deleted") {
            QVERIFY(QFile::remove(first + "/pick.svh")); expected = "picked_low"; absent = "picked_high";
        } else {
            const QString folder = kind == "higher-include-created" ? first : second;
            QVERIFY(writeBytes(folder + "/pick.svh", "module picked_after; endmodule\n"));
        }
        f.scheduler.handleSemanticInputsChanged(f.directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(!checked.isEmpty(), 10000);
        QVERIFY(checked.first().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(f.finished == 2 && !f.scheduler.isSemanticAnalysisActive(), 10000);
        QVERIFY(!SemanticIndex::getInstance()->getSymbolRecordsByName(expected).isEmpty());
        QVERIFY(SemanticIndex::getInstance()->getSymbolRecordsByName(absent).isEmpty());
        QCOMPARE(f.failed, 0);
        const int started = f.started;
        checked.clear(); f.scheduler.handleSemanticInputsChanged(f.directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
        QVERIFY(!checked.first().at(1).toBool()); QCOMPARE(f.started, started);
    }

    void directoryProbeCancellation_data()
    {
        QTest::addColumn<QString>("transition");
        for (const char* transition : {"workspace-switch", "disable", "shutdown"})
            QTest::newRow(transition) << QString::fromLatin1(transition);
    }
    void directoryProbeCancellation()
    {
        QFETCH(QString, transition);
        std::atomic_bool release{false}, entered{false}, exited{false};
        Fixture f;
        const QString file = f.directory.filePath("old.sv");
        const QString text = "module old_root; endmodule\n";
        QVERIFY(writeBytes(file, text.toUtf8())); f.start({file});
        QTRY_VERIFY_WITH_TIMEOUT(f.finished == 1 && f.published(file, text), 10000);
        QSignalSpy checked(&f.analyzer, &SymbolAnalyzer::semanticInputsChecked);
        f.analyzer.setWorkspaceWorkerStartGateForTesting([&](const auto& cancelled) {
            entered = true;
            while (!release.load() && !cancelled()) QThread::msleep(2);
            exited = true;
        });
        const auto releaseOnExit = qScopeGuard([&] { release = true; });
        f.scheduler.handleSemanticInputsChanged(f.directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(entered.load(), 3000);
        f.analyzer.setWorkspaceWorkerStartGateForTesting({});
        QTemporaryDir next;
        const QString nextFile = next.filePath("next.sv"), nextText = "module next_root; endmodule\n";
        if (transition == "workspace-switch") {
            QVERIFY(next.isValid()); QVERIFY(writeBytes(nextFile, nextText.toUtf8()));
            f.project.setWorkspaceState(next.path(), {nextFile});
        } else if (transition == "disable") {
            auto policy = f.scheduler.semanticAnalysisRuntimePolicy(); policy.enabled = false;
            f.scheduler.setSemanticAnalysisRuntimePolicy(policy);
        } else {
            QElapsedTimer timer; timer.start(); f.scheduler.shutdown(); QVERIFY(timer.elapsed() < 2000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(exited.load(), 3000);
        QCoreApplication::processEvents(); QVERIFY(checked.isEmpty());
        if (transition == "workspace-switch") {
            QTRY_VERIFY_WITH_TIMEOUT(f.published(nextFile, nextText), 10000);
            const int started = f.started;
            f.scheduler.handleSemanticInputsChanged(f.directory.path());
            QTest::qWait(40); QCOMPARE(f.started, started); QVERIFY(checked.isEmpty());
            f.scheduler.handleSemanticInputsChanged(next.path());
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
            QCOMPARE(qvariant_cast<ProjectSnapshot>(checked.first().at(0)).workspaceRoot, next.path());
            QVERIFY(!checked.first().at(1).toBool()); QCOMPARE(f.started, started);
        } else if (transition == "disable") {
            const int started = f.started;
            f.scheduler.handleSemanticInputsChanged(f.directory.path());
            QTest::qWait(40); QVERIFY(checked.isEmpty()); QCOMPARE(f.started, started);
            auto policy = f.scheduler.semanticAnalysisRuntimePolicy(); policy.enabled = true;
            f.scheduler.setSemanticAnalysisRuntimePolicy(policy);
            f.scheduler.handleSemanticInputsChanged(f.directory.path());
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
            QVERIFY(!checked.first().at(1).toBool()); QCOMPARE(f.started, started);
        }
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
