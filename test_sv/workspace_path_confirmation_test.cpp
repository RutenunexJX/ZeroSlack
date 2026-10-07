#include "actionregistry.h"
#include "analysisscheduler.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "navigationcommandcoordinator.h"
#include "tabmanager.h"
#include "testuistyle.h"
#include "workspacemanager.h"
#include "workspacefileoperationservice.h"
#include "workspacesessioncoordinator.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLockFile>
#include <QProcess>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <aclapi.h>
#endif

namespace {
bool put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray get(const QString& path)
{
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
#ifdef Q_OS_WIN
class TemporaryDirectoryAcl {
public:
    HANDLE directory = INVALID_HANDLE_VALUE;
    PSECURITY_DESCRIPTOR original = nullptr;
    PACL oldAcl = nullptr;
    bool changed = false;
    DWORD error = ERROR_SUCCESS;
    QString directoryPath;

    void releaseHandle()
    {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        directory = INVALID_HANDLE_VALUE;
    }

    bool denyListing(const QString& path, const QString& fixture)
    {
        // Only a newly created directory strictly inside this isolated fixture.
        if (!QFileInfo(path).absoluteFilePath().startsWith(QDir(fixture).absolutePath() + '/')
            || QFileInfo(path).isSymLink() || !QFileInfo(path).isDir()) return false;
        directoryPath = path;
        directory = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), READ_CONTROL | WRITE_DAC,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
        error = GetSecurityInfo(directory, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &oldAcl, nullptr, &original);
        if (error != ERROR_SUCCESS) return false;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { error = GetLastError(); return false; }
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        QByteArray user(int(size), 0);
        const bool read = GetTokenInformation(token, TokenUser, user.data(), size, &size);
        error = read ? ERROR_SUCCESS : GetLastError();
        CloseHandle(token);
        if (!read) return false;
        EXPLICIT_ACCESSW deny{};
        deny.grfAccessPermissions = FILE_LIST_DIRECTORY;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = NO_INHERITANCE;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.TrusteeType = TRUSTEE_IS_USER;
        deny.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
        PACL next = nullptr;
        error = SetEntriesInAclW(1, &deny, oldAcl, &next);
        if (error != ERROR_SUCCESS) return false;
        error = SetSecurityInfo(directory, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, next, nullptr);
        LocalFree(next);
        changed = error == ERROR_SUCCESS;
        return changed;
    }
    bool restore()
    {
        if (!changed) return true;
        if (directory == INVALID_HANDLE_VALUE) {
            directory = CreateFileW(reinterpret_cast<LPCWSTR>(directoryPath.utf16()), READ_CONTROL | WRITE_DAC,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
        }
        error = SetSecurityInfo(directory, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, oldAcl, nullptr);
        if (error != ERROR_SUCCESS) return false;
        changed = false;
        PACL actual = nullptr;
        PSECURITY_DESCRIPTOR restored = nullptr;
        error = GetSecurityInfo(directory, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &actual, nullptr, &restored);
        const bool identical = error == ERROR_SUCCESS && oldAcl && actual
            && oldAcl->AclSize == actual->AclSize && memcmp(oldAcl, actual, oldAcl->AclSize) == 0;
        if (restored) LocalFree(restored);
        releaseHandle();
        return identical;
    }
    ~TemporaryDirectoryAcl()
    {
        if (!restore()) qCritical() << "Temporary ACL restore failed:" << error;
        if (original) LocalFree(original);
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    }
};
#endif
struct Fixture {
    QTemporaryDir storage;
    QString root = storage.filePath("workspace");
    QString source = root + "/source.sv", target = root + "/renamed.sv";
    std::unique_ptr<MainWindow> window;
    Fixture()
    {
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", storage.filePath("sessions.ini").toUtf8());
        QSettings().clear();
        window = std::make_unique<MainWindow>();
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window->tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(storage.filePath("recovery")));
        window->tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        window->resize(1000, 700); window->show();
    }
    ~Fixture()
    {
        window->tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        window->close();
    }
    ActionInvocation preview(const char* action, const QString& path, const QString& name = "renamed.sv")
    {
        ActionInvocation invocation;
        invocation.workspaceId = root;
        invocation.parameters = {{"path", path}, {"parentDirectory", path}, {"name", name}};
        invocation.mode = ActionExecutionMode::DryRun;
        const auto result = executeAction(*findActionById(QString::fromLatin1(action)), *window->navigationManager, invocation);
        invocation.parameters.insert("workspacePlanRevision", result.output.value("revisionToken"));
        invocation.mode = ActionExecutionMode::Execute;
        return invocation;
    }
    ActionExecutionResult run(const char* action, const ActionInvocation& invocation)
    {
        return executeAction(*findActionById(QString::fromLatin1(action)), *window->navigationManager, invocation);
    }
};
}

class WorkspacePathConfirmationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(initializeUiStyleForTest());
#ifdef Q_OS_WIN
        for (const wchar_t* name : {L"libzeroslack_core.dll", L"libzeroslack_documents.dll", L"libzeroslack_semantic.dll", L"ElaWidgetTools.dll"}) {
            wchar_t path[32768]{};
            const auto size = GetModuleFileNameW(GetModuleHandleW(name), path, 32768);
            QVERIFY(size > 0);
            const auto file = QString::fromWCharArray(path, int(size));
            qInfo().noquote() << "Loaded:" << file << QCryptographicHash::hash(get(file), QCryptographicHash::Sha256).toHex();
        }
#endif
    }
    void decisionGeneration_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char* value : {"clean", "discard", "save", "cancel", "external_discard", "external_cancel",
                                  "external_save", "target_conflict", "deleted", "real_modal", "save_as",
                                  "same_bytes_replaced", "buffer_edit", "new_view", "workspace_switch"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void decisionGeneration()
    {
        QFETCH(QString, scenario);
        Fixture f;
        const QByteArray original = "module original; endmodule\n", external = "module external; endmodule\n";
        QVERIFY(put(f.source, original));
        QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        auto* tabs = f.window->tabManager.get();
        QVERIFY(tabs->openFileInTab(f.source));
        auto* editor = tabs->getCurrentEditor();
        QPointer<SharedDocument> document = tabs->sharedDocumentForEditor(editor);
        QVERIFY(document);
        if (scenario != "clean") editor->insertPlainText("// dirty\n");
        const auto dirtyText = document->textDocument()->toPlainText();
        QWidget auxiliaryHost;
        auto* auxiliary = tabs->createAuxiliaryView(document->documentId(), f.source, &auxiliaryHost);
        QVERIFY(auxiliary); QCOMPARE(document->viewCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive()
            && !f.window->analysisScheduler->isSemanticAnalysisActive(), 10000);
        const auto invocation = f.preview(ActionIds::WorkspacePathRename, f.source);
        QVERIFY(!invocation.parameters.value("workspacePlanRevision").toString().isEmpty());
        bool observed = false, changed = true;
        QString expectedText = dirtyText;
        const QString savedAs = f.storage.filePath("saved-as.sv");
        auto decision = [&] {
            observed = true;
            if (scenario.startsWith("external") || scenario == "real_modal") changed = put(f.source, external);
            if (scenario == "target_conflict") changed = put(f.target, "another owner\n");
            if (scenario == "deleted") changed = QFile::remove(f.source);
            if (scenario == "same_bytes_replaced") {
                const auto modified = QFileInfo(f.source).lastModified();
                changed = put(f.source, original);
                QFile file(f.source);
                changed &= file.open(QIODevice::ReadWrite) && file.setFileTime(modified, QFileDevice::FileModificationTime);
            }
            if (scenario == "buffer_edit") { editor->insertPlainText("// later edit\n"); expectedText = editor->toPlainText(); }
            if (scenario == "new_view") changed = tabs->createAuxiliaryView(document->documentId(), f.source, &auxiliaryHost);
            if (scenario == "workspace_switch") {
                const auto other = f.storage.filePath("other");
                auto* coordinator = f.window->findChild<WorkspaceSessionCoordinator*>();
                changed = coordinator && QDir().mkpath(other) && coordinator->openWorkspace(other);
            }
            if (scenario == "save_as") changed = tabs->saveEditorView(editor, true, savedAs);
            if (scenario.endsWith("cancel")) return UnsavedDocumentBatchDecision::Cancel;
            return scenario.endsWith("save") ? UnsavedDocumentBatchDecision::SaveAll : UnsavedDocumentBatchDecision::DiscardAll;
        };
        QTimer driver, timeout;
        if (scenario == "real_modal") {
            tabs->unsavedDocumentManagerForTesting()->setDecisionProvider({});
            connect(&driver, &QTimer::timeout, &driver, [&] {
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (!widget->isVisible() || widget->windowTitle() != "Pending Documents") continue;
                    for (auto* button : widget->findChildren<QAbstractButton*>()) {
                        if (button->text() == "Discard and Close All") {
                            driver.stop(); decision(); button->click(); return;
                        }
                    }
                }
            });
            connect(&timeout, &QTimer::timeout, &timeout, [] {
                if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            });
            driver.start(10); timeout.setSingleShot(true); timeout.start(5000);
        } else {
            tabs->unsavedDocumentManagerForTesting()->setDecisionProvider([&](const auto&, QWidget*) { return decision(); });
        }
        const auto result = f.run(ActionIds::WorkspacePathRename, invocation);
        driver.stop(); timeout.stop();
        QVERIFY(changed); QCOMPARE(observed, scenario != "clean");
        const bool success = scenario == "clean" || scenario == "discard" || scenario == "save" || scenario == "save_as";
        QCOMPARE(result.succeeded, success);
        if (success) {
            QVERIFY(!QFile::exists(f.source));
            QCOMPARE(get(f.target), scenario == "save" ? dirtyText.toUtf8() : original);
            if (scenario == "save_as") {
                QVERIFY(document); QCOMPARE(document->viewCount(), 2); QVERIFY(!document->dirty());
                QCOMPARE(get(savedAs), dirtyText.toUtf8());
            } else { QVERIFY(!document); QCOMPARE(tabs->auxiliaryViews().size(), 0); }
        } else {
            QVERIFY(document); QCOMPARE(document->viewCount(), scenario == "new_view" ? 3 : 2); QVERIFY(document->dirty());
            QCOMPARE(document->textDocument()->toPlainText(), expectedText);
            QVERIFY(!result.failureReason.isEmpty());
            if (scenario == "deleted") QVERIFY(!QFile::exists(f.source));
            else QCOMPARE(get(f.source), scenario.startsWith("external") || scenario == "real_modal" ? external : original);
            if (scenario == "target_conflict") QCOMPARE(get(f.target), QByteArray("another owner\n"));
            else QVERIFY(!QFile::exists(f.target));
            if (scenario == "external_discard") {
                tabs->unsavedDocumentManagerForTesting()->setDecisionProvider(
                    [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
                QVERIFY(f.run(ActionIds::WorkspacePathRename, f.preview(ActionIds::WorkspacePathRename, f.source)).succeeded);
                QCOMPARE(get(f.target), external); QVERIFY(!document);
            }
        }
    }
    void directoryGeneration_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char* value : {"discard", "save", "unopened_external", "save_then_external", "partial_failure",
                                  "partial_cancel", "save_as_during_save", "save_then_same_file_external", "added_file"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void directoryGeneration()
    {
        QFETCH(QString, scenario);
        Fixture f;
        const auto source = f.root + "/rtl", target = f.root + "/renamed", a = source + "/a.sv", b = source + "/b.sv";
        QVERIFY(put(a, "module a; endmodule\n")); QVERIFY(put(b, "module b; endmodule\n"));
        QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        auto* tabs = f.window->tabManager.get();
        QVERIFY(tabs->openFileInTab(a));
        QPointer<SharedDocument> first = tabs->sharedDocumentForEditor(tabs->getCurrentEditor());
        tabs->getCurrentEditor()->insertPlainText("// dirty a\n");
        QPointer<SharedDocument> second;
        if (scenario == "save" || scenario == "partial_failure" || scenario == "partial_cancel" || scenario == "save_as_during_save") {
            QVERIFY(tabs->openFileInTab(b)); second = tabs->sharedDocumentForEditor(tabs->getCurrentEditor());
            tabs->getCurrentEditor()->insertPlainText("// dirty b\n");
            if (scenario == "partial_cancel" || scenario == "save_as_during_save") second->setReadOnly(true);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive()
            && !f.window->analysisScheduler->isSemanticAnalysisActive(), 10000);
        const auto invocation = f.preview(ActionIds::WorkspacePathRename, source, "renamed");
        bool changed = true;
        if (scenario == "save_then_external" || scenario == "partial_failure" || scenario == "save_then_same_file_external") {
            connect(tabs, &TabManager::fileSaved, f.window.get(), [&](const QString& path) {
                if (path == a) changed = put(scenario == "save_then_same_file_external" ? a : b, "module external; endmodule\n");
            });
        }
        tabs->unsavedDocumentManagerForTesting()->setDecisionProvider([&](const auto&, QWidget*) {
            if (scenario == "unopened_external") changed = put(b, "module external; endmodule\n");
            if (scenario == "added_file") changed = put(source + "/new.sv", "module new_file; endmodule\n");
            return scenario == "discard" || scenario == "unopened_external" || scenario == "added_file"
                ? UnsavedDocumentBatchDecision::DiscardAll : UnsavedDocumentBatchDecision::SaveAll;
        });
        QTimer dialogDriver;
        bool fileDialogObserved = false;
        const auto savedAs = f.storage.filePath("saved-b.sv");
        if (scenario == "partial_cancel" || scenario == "save_as_during_save") {
            connect(&dialogDriver, &QTimer::timeout, &dialogDriver, [&] {
                if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    fileDialogObserved = true; dialogDriver.stop();
                    if (scenario == "partial_cancel") dialog->reject();
                    else { dialog->selectFile(savedAs); QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); }
                }
            });
            dialogDriver.start(10);
        }
        const auto result = f.run(ActionIds::WorkspacePathRename, invocation);
        dialogDriver.stop();
        QVERIFY(changed);
        const bool success = scenario == "discard" || scenario == "save" || scenario == "save_as_during_save";
        if (scenario == "partial_cancel" || scenario == "save_as_during_save") QVERIFY(fileDialogObserved);
        QCOMPARE(result.succeeded, success);
        if (success) {
            QVERIFY(!first); QVERIFY(!QFile::exists(source)); QVERIFY(QFile::exists(target + "/a.sv"));
            if (scenario == "save_as_during_save") { QVERIFY(second); QVERIFY(!second->dirty()); QVERIFY(get(savedAs).startsWith("// dirty b")); }
        }
        else {
            QVERIFY(first); QVERIFY(!QFile::exists(target)); QVERIFY(QFile::exists(a));
            const bool bExternal = scenario == "unopened_external" || scenario == "save_then_external" || scenario == "partial_failure";
            QCOMPARE(get(b), bExternal ? QByteArray("module external; endmodule\n") : QByteArray("module b; endmodule\n"));
            if (scenario != "unopened_external" && scenario != "added_file") {
                QVERIFY(!first->dirty());
                if (scenario == "save_then_same_file_external") QCOMPARE(get(a), QByteArray("module external; endmodule\n"));
                else QVERIFY(get(a).startsWith("// dirty a"));
            }
            if (second) {
                QVERIFY(second->dirty()); tabs->flushCrashRecovery();
                const auto recovery = tabs->listCrashRecoveryCandidates(f.root);
                QVERIFY(!recovery.candidates.isEmpty());
            }
        }
    }

    void recoverableDelete_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char* value : {"normal", "trash_failure", "finalize_failure", "auxiliary_only"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void recoverableDelete()
    {
        QFETCH(QString, scenario);
        Fixture f; QVERIFY(put(f.source, "module source; endmodule\n"));
        QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        auto* tabs = f.window->tabManager.get();
        QWidget auxiliaryHost;
        MyCodeEditor* editor = nullptr;
        if (scenario == "auxiliary_only") editor = tabs->createAuxiliaryView({}, f.source, &auxiliaryHost);
        else { QVERIFY(tabs->openFileInTab(f.source)); editor = tabs->getCurrentEditor(); }
        QVERIFY(editor); editor->insertPlainText("// keep on failure\n");
        QPointer<SharedDocument> doc = tabs->sharedDocumentForEditor(editor);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive()
            && !f.window->analysisScheduler->isSemanticAnalysisActive(), 10000);
        const auto trash = f.storage.filePath("recoverable-source.sv");
        int attempts = 0;
        f.window->navigationManager->fileOperationServiceForTesting()->setTrashMoverForTesting(
            [&](const QString& source, QString* recovered, QString* failure) {
                ++attempts;
                if (scenario == "trash_failure" && attempts == 1) { *failure = "controlled recoverable Trash failure"; return false; }
                if (!QFile::rename(source, trash)) return false;
                *recovered = trash;
                if (scenario == "finalize_failure") tabs->setTabLocked(editor, true);
                return true;
            });
        const auto invocation = f.preview(ActionIds::WorkspacePathDelete, f.source);
        auto result = f.run(ActionIds::WorkspacePathDelete, invocation);
        if (scenario == "trash_failure") {
            QVERIFY(!result.succeeded); QVERIFY(QFile::exists(f.source)); QVERIFY(!QFile::exists(trash));
            QVERIFY(doc && doc->dirty());
            result = f.run(ActionIds::WorkspacePathDelete, invocation); QCOMPARE(attempts, 2);
        }
        QVERIFY(!QFile::exists(f.source)); QCOMPARE(get(trash), QByteArray("module source; endmodule\n"));
        if (scenario == "finalize_failure") {
            QVERIFY(!result.succeeded); QVERIFY(doc && doc->dirty());
            QVERIFY(result.output.value("diskOperationCompleted").toBool());
            QCOMPARE(result.output.value("recoveredPath").toString(), trash);
            QVERIFY(result.failureReason.contains("completed"));
            tabs->setTabLocked(editor, false);
        } else { QVERIFY(result.succeeded); QVERIFY(!doc); }
        const int priorAttempts = attempts;
        QVERIFY(!f.run(ActionIds::WorkspacePathDelete, invocation).succeeded); QCOMPARE(attempts, priorAttempts);
        // Recoverable substitute proves bytes remain available after disk commit.
        QVERIFY(QFile::rename(trash, f.source)); QCOMPARE(get(f.source), QByteArray("module source; endmodule\n"));
    }

    void createConflictAndRetry()
    {
        Fixture f; QVERIFY(QDir().mkpath(f.root)); QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        const auto invocation = f.preview(ActionIds::WorkspaceFileCreate, f.root, "new.sv");
        const auto target = f.root + "/new.sv";
        QVERIFY(put(target, "external owner\n"));
        QVERIFY(!f.run(ActionIds::WorkspaceFileCreate, invocation).succeeded);
        QCOMPARE(get(target), QByteArray("external owner\n"));
        QVERIFY(QFile::remove(target));
        QVERIFY(f.run(ActionIds::WorkspaceFileCreate, f.preview(ActionIds::WorkspaceFileCreate, f.root, "new.sv")).succeeded);
        QVERIFY(f.window->tabManager->getDocumentModel()->editorForFile(target));
        QVERIFY(!f.run(ActionIds::WorkspaceFileCreate, invocation).succeeded); QCOMPARE(get(target), QByteArray());
    }

    void aliasRetargetDuringDecision()
    {
        Fixture f;
        // Keep this isolated fixture; automatic recursive cleanup must never
        // traverse a Windows directory junction.
        f.storage.setAutoRemove(false);
        qInfo().noquote() << "Retained junction fixture:" << f.storage.path();
        const auto original = f.root + "/original", replacement = f.root + "/replacement";
        const auto alias = f.root + "/alias", retired = f.root + "/retired-alias";
        const QByteArray bytes = "module same; endmodule\n";
        QVERIFY(put(original + "/unit.sv", bytes)); QVERIFY(put(replacement + "/unit.sv", bytes));
        const auto link = [&](const QString& target) {
#ifdef Q_OS_WIN
            QProcess process;
            process.start("cmd.exe", {"/d", "/s", "/c", "mklink", "/J",
                QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
            return process.waitForFinished(10000) && process.exitCode() == 0;
#else
            return QFile::link(target, alias);
#endif
        };
        QVERIFY(link(original)); QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        auto* tabs = f.window->tabManager.get(); QVERIFY(tabs->openFileInTab(alias + "/unit.sv"));
        auto* editor = tabs->getCurrentEditor(); editor->insertPlainText("// local\n");
        QPointer<SharedDocument> doc = tabs->sharedDocumentForEditor(editor);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive()
            && !f.window->analysisScheduler->isSemanticAnalysisActive(), 10000);
        const auto invocation = f.preview(ActionIds::WorkspacePathRename, alias + "/unit.sv");
        bool rebound = false;
        tabs->unsavedDocumentManagerForTesting()->setDecisionProvider([&](const auto&, QWidget*) {
            rebound = QFileInfo(alias).absolutePath() == f.root && QFileInfo(retired).absolutePath() == f.root
                && QDir(f.root).rename("alias", "retired-alias") && link(replacement);
            return UnsavedDocumentBatchDecision::DiscardAll;
        });
        const auto result = f.run(ActionIds::WorkspacePathRename, invocation);
        QVERIFY(rebound); QVERIFY(!result.succeeded); QVERIFY(doc && doc->dirty());
        QCOMPARE(get(original + "/unit.sv"), bytes); QCOMPARE(get(replacement + "/unit.sv"), bytes);
        QVERIFY(!QFile::exists(replacement + "/renamed.sv"));
    }

    void sessionFailureTransitionRestoreAndReset()
    {
        Fixture f;
        const auto second = f.root + "/second.sv", other = f.storage.filePath("other"), store = f.storage.filePath("sessions.ini");
        QVERIFY(put(f.source, "module source; endmodule\n")); QVERIFY(put(second, "module second; endmodule\n"));
        QVERIFY(put(other + "/other.sv", "module other; endmodule\n"));
        auto* coordinator = f.window->findChild<WorkspaceSessionCoordinator*>(); auto* tabs = f.window->tabManager.get();
        QVERIFY(coordinator);
        QVERIFY(coordinator->openWorkspace(f.root)); QVERIFY(tabs->openFileInTab(f.source));
        QVERIFY(coordinator->saveSession());
        WorkspaceSessionStateService service(store);
        QCOMPARE(service.load(f.root).state.tabs.size(), 1);
        QVERIFY(tabs->openFileInTab(second)); tabs->getCurrentEditor()->insertPlainText("// pending source text\n");
        QPointer<SharedDocument> dirty = tabs->sharedDocumentForEditor(tabs->getCurrentEditor());
        tabs->flushCrashRecovery(); QVERIFY(!tabs->listCrashRecoveryCandidates(f.root).candidates.isEmpty());
        QLockFile lock(store + ".write.lock"); QVERIFY(lock.tryLock(0));
        QVERIFY(!coordinator->saveSession()); QCOMPARE(coordinator->pendingSaveCount(), 1);
        QVERIFY(coordinator->openWorkspace(other)); QVERIFY(dirty && dirty->dirty());
        QVERIFY(coordinator->switchWorkspace(0)); QVERIFY(dirty && dirty->dirty());
        tabs->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::SaveAll; });
        QVERIFY(coordinator->closeWorkspace(0)); QVERIFY(!dirty);
        QVERIFY(get(second).startsWith("// pending source text"));
        QVERIFY(coordinator->openWorkspace(f.root));
        QCOMPARE(tabs->workspaceSessionTabs(f.root).size(), 2);
        QVERIFY(coordinator->restoreSession()); QCOMPARE(tabs->workspaceSessionTabs(f.root).size(), 2);
        QCOMPARE(service.load(f.root).state.tabs.size(), 1);
        QSignalSpy cleared(coordinator, &WorkspaceSessionCoordinator::sessionClearFinished);
        coordinator->clearSession(); QVERIFY(!cleared.isEmpty()); QVERIFY(!cleared.last().at(1).toBool());
        // Reset intentionally discards owned pending data even if disk clearing
        // fails; the existing policy suppresses restore/save for this process.
        QVERIFY(!coordinator->restoreSession());
        lock.unlock(); coordinator->flushPendingSaves();
        QCOMPARE(service.load(f.root).state.tabs.size(), 1);
        coordinator->clearSession(); QVERIFY(cleared.last().at(1).toBool());
        QCOMPARE(service.load(f.root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(coordinator->saveBeforeWorkspaceTransition());
        QCOMPARE(service.load(f.root).status, WorkspaceSessionReadStatus::Missing);
        QVERIFY(coordinator->saveSession()); QCOMPARE(service.load(f.root).state.tabs.size(), 2);
    }

    void directoryEnumerationPermission_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::newRow("selected_directory") << false;
        QTest::newRow("nested_directory") << true;
    }
    void directoryEnumerationPermission()
    {
#ifdef Q_OS_WIN
        QFETCH(bool, nested);
        QTemporaryDir fixture(QDir::tempPath() + "/ZeroSlack-R6-review1-acl-XXXXXX");
        QVERIFY(fixture.isValid());
        fixture.setAutoRemove(false);
        const auto source = fixture.filePath("selected"), target = fixture.filePath("renamed");
        const auto restricted = nested ? source + "/restricted" : source;
        const auto known = restricted + "/known.sv";
        QVERIFY(put(known, "module original; endmodule\n"));
        TemporaryDirectoryAcl permissions;
        QVERIFY2(permissions.denyListing(restricted, fixture.path()), qPrintable(QString::number(permissions.error)));
        WIN32_FIND_DATAW found{};
        const QString glob = QDir::toNativeSeparators(restricted + "/*");
        HANDLE enumeration = FindFirstFileW(reinterpret_cast<LPCWSTR>(glob.utf16()), &found);
        const DWORD enumerationError = enumeration == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (enumeration != INVALID_HANDLE_VALUE) FindClose(enumeration);
        QCOMPARE(enumerationError, DWORD(ERROR_ACCESS_DENIED));
        WorkspaceFileOperationService service;
        const auto before = WorkspaceFileOperationService::snapshot(source, false);
        QVERIFY(before.exists && before.directory && !before.objectId.isEmpty());
        const auto plan = service.planRename(fixture.path(), source, "renamed");
        const QByteArray replacement = "module external; endmodule\n";
        QFile file(known);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(replacement), replacement.size()); file.close();
        QCOMPARE(get(known), replacement);
        const auto after = WorkspaceFileOperationService::snapshot(source, false);
        QCOMPARE(after, before); // In-place child write does not change the directory generation.
        const auto afterPlan = service.planRename(fixture.path(), source, "renamed");
        // An open handle on a child directory can itself block parent rename
        // on Windows. Keep its original DACL, reopen only for restoration.
        permissions.releaseHandle();
        const auto result = service.apply(plan);
        if (result.succeeded) permissions.directoryPath = target + (nested ? "/restricted" : "");
        qInfo() << "ACL fixture:" << fixture.path() << "nested:" << nested
                << "native enumeration error:" << enumerationError
                << "plan valid:" << plan.valid << "entries:" << plan.sourceEntries.size()
                << "old token unchanged:" << (plan.revisionToken == afterPlan.revisionToken)
                << "stale action succeeded:" << result.succeeded << "reason:" << result.failureReason;
        // Restore the saved DACL on this exact fixture, including after a
        // faulty baseline rename. No cleanup occurs while its ACL is modified.
        QVERIFY2(permissions.restore(), qPrintable(QString::number(permissions.error)));
        qInfo() << "Original temporary DACL restored byte-for-byte";
        if (result.succeeded) {
            QCOMPARE(QFileInfo(source).absolutePath(), fixture.path());
            QCOMPARE(QFileInfo(target).absolutePath(), fixture.path());
            QVERIFY(QDir(fixture.path()).rename("renamed", "selected"));
        }
        fixture.setAutoRemove(true);
        const auto retry = service.planRename(fixture.path(), source, "renamed");
        QVERIFY(retry.valid); QVERIFY(retry.sourceEntries.size() >= 2);
        QVERIFY(service.apply(retry).succeeded);
        QCOMPARE(get(target + (nested ? "/restricted/known.sv" : "/known.sv")), replacement);
        QVERIFY(!plan.valid); QVERIFY(!result.succeeded);
#else
        QSKIP("Windows ACL scenario");
#endif
    }
    void emptyDirectoryStillRenames()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto source = fixture.filePath("empty");
        QVERIFY(QDir().mkdir(source));
        WorkspaceFileOperationService service;
        const auto plan = service.planRename(fixture.path(), source, "renamed");
        QVERIFY(plan.valid); QCOMPARE(plan.sourceEntries.size(), 1);
        QVERIFY(service.apply(plan).succeeded);
        QVERIFY(QDir(fixture.filePath("renamed")).isEmpty());
    }

    void createCommittedOpenFailureAndRetry()
    {
#ifdef Q_OS_WIN
        Fixture f;
        const auto b = f.root + "/previous.sv", created = f.root + "/new.sv";
        QVERIFY(put(f.source, "module source;\nendmodule\n")); QVERIFY(put(b, "module previous; endmodule\n"));
        QVERIFY(f.window->workspaceManager->openWorkspace(f.root));
        auto* tabs = f.window->tabManager.get();
        auto* navigation = f.window->navigationManager.get();
        auto* coordinator = f.window->findChild<NavigationCommandCoordinator*>(); QVERIFY(coordinator);
        QVERIFY(tabs->openFileInTab(f.source));
        auto* original = tabs->getCurrentEditor(); original->insertPlainText("// local\n");
        const auto originalText = original->toPlainText();
        const auto originalCursor = original->textCursor();
        navigation->navigateToFile(b, 1);
        coordinator->navigateBack(); QCOMPARE(tabs->getCurrentEditor(), original);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window->workspaceManager->isWorkspaceScanActive()
            && !f.window->analysisScheduler->isSemanticAnalysisActive(), 10000);
        struct ReadBlock {
            HANDLE handle = INVALID_HANDLE_VALUE;
            void release() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
            ~ReadBlock() { release(); }
        } blocked;
        bool committedObserved = false, warningObserved = false;
        QString warningText;
        const auto injection = connect(f.window->workspaceManager.get(), &WorkspaceManager::workspaceScanStarted,
            f.window.get(), [&](const QString& root) {
                if (root != f.root || !QFileInfo::exists(created) || committedObserved) return;
                committedObserved = true;
                // refreshAfterFileOperation runs after exclusive creation and
                // before navigateToFile. Keep the real signal consumer intact.
                blocked.handle = CreateFileW(reinterpret_cast<LPCWSTR>(created.utf16()), GENERIC_WRITE,
                    FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            });
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &dismiss, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "warning") return;
            for (auto* label : dialog->findChildren<QLabel*>()) warningText += label->text();
            warningObserved = warningText.contains("can not open file:");
            dialog->reject();
        });
        const auto invocation = f.preview(ActionIds::WorkspaceFileCreate, f.root, "new.sv");
        dismiss.start(10);
        const auto result = f.run(ActionIds::WorkspaceFileCreate, invocation);
        dismiss.stop(); disconnect(injection);
        QVERIFY(committedObserved); QVERIFY(blocked.handle != INVALID_HANDLE_VALUE);
        QVERIFY(warningObserved);
        QVERIFY(result.succeeded); QCOMPARE(result.output.value("path").toString(), created);
        QVERIFY(QFileInfo(created).isFile()); QCOMPARE(QFileInfo(created).size(), 0);
        QVERIFY(!tabs->getDocumentModel()->editorForFile(created));
        QCOMPARE(tabs->getCurrentEditor(), original); QCOMPARE(original->toPlainText(), originalText);
        QCOMPARE(original->textCursor().position(), originalCursor.position());
        QVERIFY(tabs->sharedDocumentForEditor(original)->dirty());
        coordinator->navigateForward(); QCOMPARE(tabs->getCurrentDocument().fileName, b);
        coordinator->navigateBack(); QCOMPARE(tabs->getCurrentEditor(), original);
        blocked.release();
        const QByteArray retained = "// retained after open failure\n";
        QVERIFY(put(created, retained));
        QVERIFY(!f.run(ActionIds::WorkspaceFileCreate, invocation).succeeded);
        QCOMPARE(get(created), retained); // Retry creation never overwrites or truncates.
        navigation->navigateToFile(created, 1);
        QVERIFY(tabs->getDocumentModel()->editorForFile(created));
        QCOMPARE(tabs->getCurrentDocument().fileName, created);
        QCOMPARE(tabs->getCurrentEditor()->toPlainText().toUtf8(), retained);
        coordinator->navigateBack(); QCOMPARE(tabs->getCurrentEditor(), original);
        coordinator->navigateForward(); QCOMPARE(tabs->getCurrentDocument().fileName, created);
        QCOMPARE(original->toPlainText(), originalText);
        qInfo() << "Creation succeeded; real MainWindow open warning:" << warningText
                << "document/history and retained bytes preserved; navigation retry succeeded";
#else
        QSKIP("Windows sharing violation scenario");
#endif
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings; if (!settings.isValid()) return 2;
    QCoreApplication::setOrganizationName("ZeroSlack"); QCoreApplication::setApplicationName("ZeroSlack");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    WorkspacePathConfirmationTest test; return QTest::qExec(&test, argc, argv);
}
#include "workspace_path_confirmation_test.moc"
