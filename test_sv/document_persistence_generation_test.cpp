#include "mainwindow.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacesessionstateservice.h"
#include <zeroslack/documents/documentfileread.h>
#include <zeroslack/ui/applicationwindow.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QtTest>

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QByteArray bytesAt(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString stateKey(const QString& root)
{
    return "workspaceSessions/v2/workspaces/" + WorkspaceSessionStateService::workspaceIdentity(root) + "/state";
}

bool writeLocal(const QString& store, const QString& root, const QByteArray& payload)
{
    QSettings settings(store, QSettings::IniFormat);
    settings.setValue(stateKey(root), payload);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QByteArray localPayload(int version)
{
    return QJsonDocument(QJsonObject{{"schema", "ZeroSlack.LocalWorkspaceSession"},
        {"version", version}, {"unrecognized", "retain-me"}}).toJson(QJsonDocument::Compact);
}

bool writeLegacy(const QString& root)
{
    return writeBytes(root + "/.zs", QJsonDocument(QJsonObject{
        {"schema", "ZeroSlack.WorkspaceSessionState"}, {"version", 1},
        {"originalRoot", root}, {"ui", QJsonObject{{"navigationFilesQuery", "legacy"}}}
    }).toJson(QJsonDocument::Compact));
}

bool sameFormat(const DocumentFileFormat& a, const DocumentFileFormat& b)
{
    return a.encoding == b.encoding && a.byteOrderMark == b.byteOrderMark && a.lineEnding == b.lineEnding;
}
}

class DocumentPersistenceGenerationTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }

    void sessionBarrierPreservesProductState_data()
    {
        QTest::addColumn<int>("kind"); QTest::addColumn<bool>("automatic");
        for (int kind = 0; kind < 4; ++kind) {
            QTest::newRow(qPrintable(QString("invalid-%1-explicit").arg(kind))) << kind << false;
            QTest::newRow(qPrintable(QString("invalid-%1-activation").arg(kind))) << kind << true;
        }
    }

    void sessionBarrierPreservesProductState()
    {
        QFETCH(int, kind); QFETCH(bool, automatic);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("workspace"), store = fixture.filePath("session.ini");
        const QString file = root + "/current.sv";
        QVERIFY(writeBytes(file, "module current; endmodule\n")); QVERIFY(writeLegacy(root));
        if (kind == 3) QVERIFY(writeBytes(store, "[broken section\nkey=value\n"));
        else QVERIFY(writeLocal(store, root, kind == 0 ? localPayload(999)
                               : kind == 1 ? QByteArray("{broken") : QByteArray()));
        const QByteArray original = bytesAt(store), legacy = bytesAt(root + "/.zs");
        const QByteArray previous = qgetenv("ZEROSLACK_SESSION_STORAGE_PATH");
        const auto restoreEnvironment = qScopeGuard([&] { qputenv("ZEROSLACK_SESSION_STORAGE_PATH", previous); });
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", store.toUtf8());
        auto host = ZeroSlack::createApplicationWindow();
        auto* window = qobject_cast<MainWindow*>(host.get()); QVERIFY(window);
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        auto* coordinator = window->findChild<WorkspaceSessionCoordinator*>(); QVERIFY(coordinator);
        coordinator->setRestoreOnActivation(automatic);
        QVERIFY(ZeroSlack::openApplicationDocument(*host, file));
        auto* editor = window->tabManager->getCurrentEditor(); QVERIFY(editor);
        QTextCursor cursor(editor->document()); cursor.insertText("// local\n");
        editor->setTextCursor(cursor);
        const auto before = window->tabManager->getDocumentForEditor(editor);
        const int undo = editor->document()->availableUndoSteps();
        QSignalSpy restored(coordinator, &WorkspaceSessionCoordinator::sessionRestoreFinished);
        QVERIFY(window->workspaceManager->openWorkspace(root));
        if (!automatic) QVERIFY(!coordinator->restoreSession());
        QVERIFY(!restored.isEmpty()); QVERIFY(!restored.last().at(1).toBool());
        QCOMPARE(bytesAt(store), original);
        QVERIFY(!coordinator->saveSession());
        coordinator->scheduleSessionSave();
        QVERIFY(QMetaObject::invokeMethod(coordinator, "flushScheduledSave", Qt::DirectConnection));
        coordinator->flushPendingSaves();
        QVERIFY(!coordinator->saveBeforeWorkspaceTransition());
        QVERIFY(!coordinator->restoreSession());
        QCOMPARE(bytesAt(store), original); QCOMPARE(bytesAt(root + "/.zs"), legacy);
        QCOMPARE(window->tabManager->getCurrentEditor(), editor);
        const auto after = window->tabManager->getDocumentForEditor(editor);
        QCOMPARE(after.text, before.text); QCOMPARE(after.textVersion, before.textVersion);
        QCOMPARE(after.dirty, before.dirty); QCOMPARE(editor->document()->availableUndoSteps(), undo);
        QCOMPARE(editor->textCursor().position(), cursor.position());
        window->tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        QVERIFY(window->close());
        QCOMPARE(bytesAt(store), original);
    }

    void sessionSupportedAndMissing_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("missing-legacy-migration") << 0;
        QTest::newRow("supported-v2") << 2;
        QTest::newRow("supported-v3") << 3;
        QTest::newRow("supported-v4") << 4;
    }

    void sessionSupportedAndMissing()
    {
        QFETCH(int, version);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("workspace"), store = fixture.filePath("session.ini");
        QVERIFY(writeLegacy(root)); const auto legacy = bytesAt(root + "/.zs");
        if (version) QVERIFY(writeLocal(store, root, localPayload(version)));
        WorkspaceSessionStateService service(store);
        QCOMPARE(service.load(root).status, version ? WorkspaceSessionReadStatus::Loaded : WorkspaceSessionReadStatus::Missing);
        WorkspaceManager manager; manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QTabWidget widget; TabManager tabs(&widget);
        WorkspaceSessionUiState ui; ui.navigationFilesQuery = "current";
        WorkspaceSessionUiBridge bridge;
        bridge.captureUiState = [&](bool) { return ui; };
        bridge.restoreUiState = [&](const auto& restored, bool) { ui = restored; return WorkspaceSessionUiRestoreResult{}; };
        WorkspaceSessionCoordinator coordinator(&manager, &tabs, nullptr, bridge, store);
        coordinator.setRestoreOnActivation(false);
        QVERIFY(manager.openWorkspace(root));
        QVERIFY(coordinator.restoreSession());
        QCOMPARE(ui.navigationFilesQuery, version ? QString() : QString("legacy"));
        QVERIFY(coordinator.saveSession());
        QVERIFY(service.load(root).loaded);
        QSettings saved(store, QSettings::IniFormat);
        QCOMPARE(QJsonDocument::fromJson(saved.value(stateKey(root)).toByteArray()).object().value("version").toInt(), 4);
        coordinator.clearSession();
        QCOMPARE(service.load(root).status, WorkspaceSessionReadStatus::Missing);
        coordinator.scheduleSessionSave(); coordinator.flushPendingSaves();
        QVERIFY(coordinator.saveBeforeWorkspaceTransition());
        QCOMPARE(service.load(root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(coordinator.saveSession());
        QVERIFY(service.load(root).loaded); QCOMPARE(bytesAt(root + "/.zs"), legacy);
    }

    void sessionFailureRecoveryAndPendingBarrier()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("workspace"), store = fixture.filePath("session.ini");
        QVERIFY(writeLegacy(root));
        const QString otherRoot = fixture.filePath("other-workspace");
        QVERIFY(QDir().mkpath(otherRoot));
        const QByteArray opaqueOther = "{unknown-format-other-workspace";
        QVERIFY(writeLocal(store, otherRoot, opaqueOther));
        WorkspaceSessionStateService service(store);
        WorkspaceManager manager; manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QTabWidget widget; TabManager tabs(&widget);
        WorkspaceSessionUiState ui; ui.navigationFilesQuery = "current";
        int restores = 0;
        WorkspaceSessionUiBridge bridge;
        bridge.captureUiState = [&](bool) { return ui; };
        bridge.restoreUiState = [&](const auto& restored, bool) { ui = restored; ++restores; return WorkspaceSessionUiRestoreResult{}; };
        WorkspaceSessionCoordinator coordinator(&manager, &tabs, nullptr, bridge, store);
        coordinator.setRestoreOnActivation(false);
        QVERIFY(manager.openWorkspace(root));
        QLockFile writer(store + ".write.lock"); QVERIFY(writer.tryLock(0));
        QVERIFY(!coordinator.restoreSession()); // Migration must commit before changing UI.
        QCOMPARE(restores, 0); QCOMPARE(ui.navigationFilesQuery, QString("current"));
        QCOMPARE(service.load(root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(!coordinator.saveSession()); QCOMPARE(coordinator.pendingSaveCount(), 1);
        writer.unlock();
        QVERIFY(writeLocal(store, root, localPayload(999)));
        const auto future = bytesAt(store);
        QVERIFY(!coordinator.restoreSession()); QCOMPARE(restores, 0);
        coordinator.flushPendingSaves(); QCOMPARE(bytesAt(store), future);
        QCOMPARE(coordinator.pendingSaveCount(), 1);
        QVERIFY(writeLocal(store, root, localPayload(4))); // Explicit repair retains the owned pending state.
        QVERIFY(coordinator.restoreSession()); QCOMPARE(ui.navigationFilesQuery, QString("current"));
        coordinator.flushPendingSaves(); QCOMPARE(coordinator.pendingSaveCount(), 0);
        QCOMPARE(service.load(root).state.ui.navigationFilesQuery, QString("current"));
        QVERIFY(writeLocal(store, root, localPayload(999)));
        coordinator.clearSession();
        QCOMPARE(service.load(root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(coordinator.saveBeforeWorkspaceTransition());
        QCOMPARE(service.load(root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(coordinator.saveSession());

        QSettings preserved(store, QSettings::IniFormat);
        QCOMPARE(preserved.value(stateKey(otherRoot)).toByteArray(), opaqueOther);
        const QString unreadable = fixture.filePath("unreadable.ini");
        QVERIFY(QDir().mkpath(unreadable));
        WorkspaceSessionState state; state.workspaceRoot = root;
        WorkspaceSessionStateService blocked(unreadable);
        QCOMPARE(blocked.load(root).status, WorkspaceSessionReadStatus::ReadError);
        QVERIFY(!blocked.save(state).saved); QVERIFY(!blocked.clear(root));
        QVERIFY(QDir().rmdir(unreadable)); QVERIFY(blocked.save(state).saved);
        QVERIFY(blocked.load(root).loaded);
        const auto beforeLock = bytesAt(unreadable);
        QLockFile lock(unreadable + ".write.lock"); QVERIFY(lock.tryLock(0));
        state.ui.navigationFilesQuery = "must-not-leak-through-QSettings";
        QVERIFY(!blocked.save(state).saved);
        QCOMPARE(blocked.load(root).state.ui.navigationFilesQuery, QString());
        QCOMPARE(bytesAt(unreadable), beforeLock); lock.unlock();
        QVERIFY(blocked.save(state).saved);
        QCOMPARE(blocked.load(root).state.ui.navigationFilesQuery, state.ui.navigationFilesQuery);

        QVERIFY(writeBytes(unreadable, "[invalid section\nvalue=original\n"));
        const auto invalidIni = bytesAt(unreadable);
        QVERIFY(!blocked.save(state).saved); QVERIFY(!blocked.clear(root));
        QCOMPARE(bytesAt(unreadable), invalidIni);
    }

    void cleanExternalFormatGeneration_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("utf16-bom-crlf") << 0;
        QTest::newRow("crlf-only") << 1;
        QTest::newRow("bom-only") << 2;
        QTest::newRow("text-and-format-control") << 3;
        QTest::newRow("unchanged-control") << 4;
    }

    void cleanExternalFormatGeneration()
    {
        QFETCH(int, kind);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("format.sv");
        const QString initial = "module initial;\nendmodule\n";
        QVERIFY(writeBytes(file, initial.toUtf8()));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(file));
        auto* editor = tabs.getCurrentEditor(); auto* document = tabs.sharedDocumentForEditor(editor);
        auto* sync = tabs.externalDocumentSyncController();
        QTextCursor cursor(editor->document()); cursor.insertText("x"); editor->undo();
        QVERIFY(!document->dirty());
        const auto revision = document->textRevision();
        const int redo = editor->document()->availableRedoSteps();
        DocumentFileFormat format;
        format.encoding = kind == 0 || kind == 3 ? QStringConverter::Utf16LE : QStringConverter::Utf8;
        format.byteOrderMark = kind == 0 || kind == 2 || kind == 3;
        format.lineEnding = kind == 0 || kind == 1 || kind == 3 ? "\r\n" : "\n";
        const QString external = kind == 3 ? "module changed;\nendmodule\n" : initial;
        QVERIFY(writeBytes(file, encodeDocumentText(external, format)));
        const auto disk = readDocumentFile(file); QVERIFY(disk.available);
        resetDocumentFileReadMetricsForTest();
        QCOMPARE(sync->processFileChange(file).outcome, kind == 4
                 ? ExternalDocumentSyncOutcome::Unchanged : ExternalDocumentSyncOutcome::Reloaded);
        QVERIFY(sameFormat(document->fileFormat(), disk.format));
        QCOMPARE(document->savedBaselineSha256(), disk.rawSha256);
        QCOMPARE(editor->toPlainText(), external); QVERIFY(!document->dirty());
        if (kind != 3) {
            QCOMPARE(document->textRevision(), revision);
            QCOMPARE(editor->document()->availableRedoSteps(), redo);
        }
        QCOMPARE(documentFileReadMetricsForTest().reads, std::uint64_t(1));
        QTextCursor edit(editor->document()); edit.movePosition(QTextCursor::End); edit.insertText("// edit\n");
        QVERIFY(tabs.saveCurrentTab());
        const auto saved = readDocumentFile(file);
        QVERIFY(sameFormat(saved.format, disk.format));
        QCOMPARE(saved.text, external + "// edit\n");
        QCOMPARE(document->savedBaselineSha256(), saved.rawSha256);
        QVERIFY(tabs.closeAllTabs());
    }

    void dirtyExternalFormatGeneration_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("keep-local-recovery-save") << 0;
        QTest::newRow("reload-undo-redo-save") << 1;
        QTest::newRow("save-local-as") << 2;
    }

    void dirtyExternalFormatGeneration()
    {
        QFETCH(int, action);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString file = fixture.filePath("dirty.sv"), initial = "module original;\nendmodule\n";
        QVERIFY(writeBytes(file, initial.toUtf8()));
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        tabs.unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        QVERIFY(tabs.openFileInTab(file));
        auto* editor = tabs.getCurrentEditor(); auto* document = tabs.sharedDocumentForEditor(editor);
        auto* sync = tabs.externalDocumentSyncController();
        const auto originalFormat = document->fileFormat(); const auto originalDigest = document->savedBaselineSha256();
        QTextCursor cursor(editor->document()); cursor.movePosition(QTextCursor::End); cursor.insertText("// local\n");
        const auto local = editor->toPlainText(); const auto revision = document->textRevision();
        const int undo = editor->document()->availableUndoSteps();
        DocumentFileFormat format; format.encoding = QStringConverter::Utf16LE;
        format.byteOrderMark = true; format.lineEnding = "\r\n";
        QVERIFY(writeBytes(file, encodeDocumentText(initial, format)));
        const auto external = readDocumentFile(file);
        QVERIFY(!tabs.saveCurrentTab()); // Save preflight must detect an undelivered format-only event.
        QCOMPARE(bytesAt(file), external.rawBytes);
        QCOMPARE(document->externalState(), SharedDocumentExternalState::Conflict);
        QCOMPARE(document->savedBaselineSha256(), originalDigest);
        QVERIFY(sameFormat(document->fileFormat(), originalFormat));
        QCOMPARE(editor->toPlainText(), local); QCOMPARE(document->textRevision(), revision);
        QCOMPARE(editor->document()->availableUndoSteps(), undo);
        const auto oldReview = tabs.externalConflictReview(file); QVERIFY(oldReview.valid);
        format.lineEnding = "\n"; QVERIFY(writeBytes(file, encodeDocumentText(initial, format)));
        QCOMPARE(sync->keepLocal(oldReview).status, ExternalDocumentConflictActionStatus::ExternalChanged);
        QCOMPARE(sync->reloadExternal(oldReview).status, ExternalDocumentConflictActionStatus::ExternalChanged);
        QString reason;
        QVERIFY(!tabs.saveExternalConflictLocalAs(oldReview, fixture.filePath("stale.sv"), &reason));
        QVERIFY(!QFileInfo::exists(fixture.filePath("stale.sv")));
        auto review = tabs.externalConflictReview(file); QVERIFY(review.valid);
        const auto accepted = readDocumentFile(file);
        if (action == 0) {
            QVERIFY(tabs.keepLocalExternalConflict(review).applied());
            QVERIFY(document->dirty()); QCOMPARE(document->textRevision(), revision);
            QCOMPARE(editor->document()->availableUndoSteps(), undo);
            QVERIFY(sameFormat(document->fileFormat(), accepted.format));
            QCOMPARE(document->savedBaselineSha256(), accepted.rawSha256);
            tabs.checkpointCrashRecovery(); tabs.flushCrashRecovery();
            const auto candidates = tabs.listCrashRecoveryCandidates().candidates;
            QCOMPARE(candidates.size(), 1);
            QCOMPARE(candidates.first().savedBaselineSha256, accepted.rawSha256);
            QVERIFY(!candidates.first().sourceChangedSinceBaseline);
            format.lineEnding = "\r\n"; QVERIFY(writeBytes(file, encodeDocumentText(initial, format)));
            QVERIFY(!tabs.saveCurrentTab()); // A kept-local decision cannot authorize another raw generation.
            QCOMPARE(document->externalState(), SharedDocumentExternalState::Conflict);
            review = tabs.externalConflictReview(file);
            QVERIFY(tabs.keepLocalExternalConflict(review).applied()); QVERIFY(tabs.saveCurrentTab());
        } else if (action == 1) {
            resetDocumentFileReadMetricsForTest();
            QVERIFY(tabs.reloadExternalConflict(review).applied());
            QCOMPARE(documentFileReadMetricsForTest().reads, std::uint64_t(1));
            QCOMPARE(editor->toPlainText(), initial); QVERIFY(!document->dirty());
            QVERIFY(sameFormat(document->fileFormat(), accepted.format));
            QCOMPARE(document->savedBaselineSha256(), accepted.rawSha256);
            editor->undo(); QCOMPARE(editor->toPlainText(), local); QVERIFY(document->dirty());
            editor->redo(); QCOMPARE(editor->toPlainText(), initial); QVERIFY(!document->dirty());
            editor->undo(); QVERIFY(tabs.saveCurrentTab());
        } else {
            const auto originalBytes = bytesAt(file);
            const QString copy = fixture.filePath("saved-as.sv");
            QVERIFY(tabs.saveExternalConflictLocalAs(review, copy, &reason));
            QCOMPARE(bytesAt(file), originalBytes);
            const auto copied = readDocumentFile(copy);
            QCOMPARE(copied.text, local); QVERIFY(sameFormat(copied.format, originalFormat));
            QCOMPARE(document->savedBaselineSha256(), copied.rawSha256);
            QVERIFY(tabs.closeAllTabs()); return;
        }
        const auto saved = readDocumentFile(file);
        QCOMPARE(saved.text, local); QVERIFY(sameFormat(saved.format, format));
        QCOMPARE(document->savedBaselineSha256(), saved.rawSha256);
        QCOMPARE(sync->processFileChange(file).outcome, ExternalDocumentSyncOutcome::Unchanged);
        QVERIFY(tabs.closeAllTabs());
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    application.setOrganizationName("ZeroSlack"); application.setApplicationName("ZeroSlack");
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", settings.filePath("session.ini").toUtf8());
    DocumentPersistenceGenerationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "document_persistence_generation_test.moc"
