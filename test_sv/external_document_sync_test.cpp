#include "externaldocumentsynccontroller.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStringList>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n",
                value ? "PASS" : "FAIL",
                name);
}

bool replaceFile(const QString& fileName,
                 const QString& text)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    if (stream.status() != QTextStream::Ok)
        return false;
    return file.commit();
}

QString readFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream stream(&file);
    return stream.readAll();
}

QString longFixture(const QString& signalName)
{
    QStringList lines;
    lines.append(QStringLiteral("module sync;"));
    for (int index = 0; index < 120; ++index) {
        lines.append(
            QStringLiteral("  logic %1_%2;")
                .arg(signalName)
                .arg(index));
    }
    lines.append(QStringLiteral("endmodule"));
    return lines.join(QLatin1Char('\n'))
        + QLatin1Char('\n');
}

QString lexicalPath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

#ifdef Q_OS_WIN
bool createDirectoryJunction(const QString& target,
                             const QString& junction,
                             QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    QProcess process;
    process.start(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/s"),
            QStringLiteral("/c"),
            QStringLiteral("mklink"),
            QStringLiteral("/J"),
            QDir::toNativeSeparators(junction),
            QDir::toNativeSeparators(target),
        });
    if (!process.waitForStarted(5000)
        || !process.waitForFinished(10000)
        || process.exitStatus()
               != QProcess::NormalExit
        || process.exitCode() != 0
        || !QFileInfo(junction).isDir()) {
        if (failureReason) {
            const QByteArray output =
                process.readAllStandardOutput()
                + process.readAllStandardError();
            *failureReason =
                QString::fromLocal8Bit(output).trimmed();
            if (failureReason->isEmpty()) {
                *failureReason = process.errorString();
            }
        }
        return false;
    }
    return true;
}
#endif
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    QTemporaryDir temporary;
    const QString fileName =
        temporary.filePath(
            QStringLiteral("external_sync.sv"));
    const QString initialText =
        longFixture(QStringLiteral("initial"));
    const QString externalText =
        longFixture(QStringLiteral("external"));
    expect("fixture created",
           temporary.isValid()
               && replaceFile(fileName, initialText));

    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(&host);
    layout->addWidget(tabs);
    TabManager manager(tabs);
    manager.enableSplitLayout(&host);
    host.resize(900, 420);
    host.show();

    expect("opened document is tracked",
           manager.openFileInTab(fileName));
    MyCodeEditor* left = manager.getCurrentEditor();
    expect("second view shares the document",
           manager.splitCurrentView(
               EditorSplitDirection::Right));
    MyCodeEditor* right = manager.getCurrentEditor();
    SharedDocument* document =
        manager.sharedDocumentForEditor(left);
    ExternalDocumentSyncController* sync =
        manager.externalDocumentSyncController();
    QCoreApplication::processEvents();
    expect("file and parent directory are watched",
           document
               && sync
               && sync->isFileWatchedForTesting(fileName)
               && sync->isDirectoryWatchedForTesting(
                   QFileInfo(fileName).absolutePath()));

    QTextCursor leftSelection(left->document());
    const int leftAnchor =
        initialText.indexOf(QStringLiteral("initial_10"));
    leftSelection.setPosition(leftAnchor);
    leftSelection.setPosition(
        leftAnchor
            + QStringLiteral("initial_10").size(),
        QTextCursor::KeepAnchor);
    left->setTextCursor(leftSelection);
    QTextCursor rightCursor(right->document());
    const int rightPosition =
        initialText.indexOf(QStringLiteral("initial_90"));
    rightCursor.setPosition(rightPosition);
    right->setTextCursor(rightCursor);
    right->verticalScrollBar()->setValue(
        right->verticalScrollBar()->maximum());
    document->captureViewState(left);
    document->captureViewState(right);
    const SharedDocumentViewState leftBefore =
        document->viewState(left);
    const SharedDocumentViewState rightBefore =
        document->viewState(right);
    const std::uint64_t revisionBefore =
        document->textRevision();

    QSignalSpy reloadSpy(
        sync,
        &ExternalDocumentSyncController::documentReloaded);
    QSignalSpy documentSavedSpy(
        manager.getDocumentModel(),
        &DocumentModel::documentSaved);
    QSignalSpy documentEditedSpy(
        manager.getDocumentModel(),
        &DocumentModel::documentEdited);
    QSignalSpy dirtyChangedSpy(
        document,
        &SharedDocument::dirtyChanged);
    expect("atomic external replacement succeeds",
           replaceFile(fileName, externalText));
    const ExternalDocumentSyncResult reload =
        sync->processFileChange(fileName);
    QCoreApplication::processEvents();
    expect("clean document reloads immediately",
           reload.outcome
                   == ExternalDocumentSyncOutcome::Reloaded
               && left->toPlainText() == externalText
               && right->toPlainText() == externalText
               && left->document() == right->document()
               && left->document()
                      == document->textDocument()
               && !document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Current
               && document->textRevision()
                      == revisionBefore + 1
               && reloadSpy.size() == 1
               && documentSavedSpy.size() == 1
               && documentEditedSpy.isEmpty()
               && dirtyChangedSpy.isEmpty()
               && !manager.getDocumentForEditor(left).dirty);
    expect("independent cursor selection and scroll survive reload",
           document->viewState(left).cursorPosition
                   == leftBefore.cursorPosition
               && document->viewState(left).anchorPosition
                      == leftBefore.anchorPosition
               && document->viewState(right).cursorPosition
                      == rightBefore.cursorPosition
               && document->viewState(right).anchorPosition
                      == rightBefore.anchorPosition
               && document->viewState(right)
                      .verticalScrollValue
                      == rightBefore.verticalScrollValue);
    expect("atomic replacement is re-subscribed",
           sync->isFileWatchedForTesting(fileName)
               && sync->isDirectoryWatchedForTesting(
                   QFileInfo(fileName).absolutePath()));
    expect("reprocessing the same disk generation is a no-op",
           sync->processFileChange(fileName).outcome
               == ExternalDocumentSyncOutcome::Unchanged
               && reloadSpy.size() == 1);

    QTextCursor localEdit(left->document());
    localEdit.setPosition(
        externalText.indexOf(
            QStringLiteral("external_20")));
    localEdit.insertText(QStringLiteral("local_"));
    QCoreApplication::processEvents();
    const QString localText = left->toPlainText();
    const std::uint64_t dirtyRevision =
        document->textRevision();
    const bool undoAvailable =
        document->textDocument()->isUndoAvailable();
    const SharedDocumentViewState leftDirtyState =
        document->viewState(left);
    const QString secondExternalText =
        longFixture(QStringLiteral("second_external"));

    QSignalSpy conflictSpy(
        sync,
        &ExternalDocumentSyncController::
            documentConflictDetected);
    expect("second atomic external replacement succeeds",
           replaceFile(fileName, secondExternalText));
    const ExternalDocumentSyncResult conflict =
        sync->processFileChange(fileName);
    QCoreApplication::processEvents();
    expect("dirty document enters conflict without overwrite",
           conflict.outcome
                   == ExternalDocumentSyncOutcome::Conflict
               && document->externalState()
                      == SharedDocumentExternalState::Conflict
               && document->dirty()
               && left->toPlainText() == localText
               && right->toPlainText() == localText
               && document->textRevision() == dirtyRevision
               && document->textDocument()
                      ->isUndoAvailable()
                      == undoAvailable
               && document->viewState(left).cursorPosition
                      == leftDirtyState.cursorPosition
               && conflictSpy.size() == 1);

    left->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QWidget* focusBeforeReview =
        QApplication::focusWidget();
    const int splitCountBeforeReview =
        manager.splitCount();
    const ExternalDocumentConflictReview firstReview =
        manager.externalConflictReview(fileName);
    expect("conflict review captures exact local and external generations",
           firstReview.valid
               && firstReview.externalAvailable
               && firstReview.documentId
                      == document->documentId()
               && firstReview.documentRevision
                      == dirtyRevision
               && firstReview.localText == localText
               && firstReview.externalText
                      == secondExternalText
               && !firstReview.externalFingerprint.isEmpty());
    QCoreApplication::processEvents();
    expect("opening or cancelling a review is non-mutating",
           left->toPlainText() == localText
               && document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Conflict
               && QApplication::focusWidget()
                      == focusBeforeReview
               && manager.splitCount()
                      == splitCountBeforeReview);

    QTextCursor editAfterReview(left->document());
    editAfterReview.movePosition(QTextCursor::End);
    editAfterReview.insertText(
        QStringLiteral("// changed after review\n"));
    const ExternalDocumentConflictActionResult
        staleLocalDecision =
            sync->keepLocal(firstReview);
    expect("review rejects a changed local revision",
           staleLocalDecision.status
                   == ExternalDocumentConflictActionStatus::
                       StaleDocument
               && staleLocalDecision.currentReview.valid
               && document->externalState()
                      == SharedDocumentExternalState::Conflict
               && left->toPlainText().contains(
                   QStringLiteral(
                       "changed after review")));
    left->undo();
    QCoreApplication::processEvents();
    expect("stale local decision does not consume local undo",
           left->toPlainText() == localText
               && document->dirty());

    left->undo();
    QCoreApplication::processEvents();
    expect("conflict detection leaves the shared undo stack intact",
           left->toPlainText() == externalText
               && right->toPlainText() == externalText
               && document->externalState()
                      == SharedDocumentExternalState::Conflict);
    left->redo();
    QCoreApplication::processEvents();
    expect("redo remains available after conflict",
           left->toPlainText() == localText
               && right->toPlainText() == localText);

    expect("unreviewed conflict cannot overwrite the source",
           !manager.saveCurrentTab()
               && document->dirty()
               && readFile(fileName)
                      == secondExternalText);

    const QString thirdExternalText =
        longFixture(QStringLiteral("third_external"));
    expect("external file can change again while review is open",
           replaceFile(fileName, thirdExternalText));
    const ExternalDocumentConflictActionResult staleReload =
        sync->reloadExternal(firstReview);
    QCoreApplication::processEvents();
    expect("old review is rejected after a newer external generation",
           staleReload.status
                   == ExternalDocumentConflictActionStatus::
                       ExternalChanged
               && staleReload.currentReview.valid
               && staleReload.currentReview.externalText
                      == thirdExternalText
               && document->dirty()
               && left->toPlainText() == localText
               && QApplication::focusWidget()
                      == focusBeforeReview
               && manager.splitCount()
                      == splitCountBeforeReview);

    const ExternalDocumentConflictActionResult kept =
        manager.keepLocalExternalConflict(
            staleReload.currentReview);
    expect("keep local acknowledges only the reviewed disk generation",
           kept.applied()
               && document->dirty()
               && left->toPlainText() == localText
               && document->externalState()
                      == SharedDocumentExternalState::
                          ExternallyModified);
    const QString afterKeepExternalText =
        longFixture(
            QStringLiteral(
                "changed_after_keep"));
    expect("external source can change after keep local",
           replaceFile(fileName,
                       afterKeepExternalText));
    expect("kept-local authorization cannot overwrite a newer generation",
           !manager.saveCurrentTab()
               && document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Conflict
               && left->toPlainText() == localText
               && readFile(fileName)
                      == afterKeepExternalText);
    const ExternalDocumentConflictReview
        refreshedKeepReview =
            manager.externalConflictReview(fileName);
    expect("latest external generation must be kept explicitly",
           manager.keepLocalExternalConflict(
               refreshedKeepReview).applied());
    expect("guarded save succeeds after explicit keep local",
           manager.saveCurrentTab());
    QCoreApplication::processEvents();
    expect("successful save restores Current state",
           !document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Current
               && readFile(fileName)
                      == left->toPlainText()
               && sync->isFileWatchedForTesting(fileName)
               && sync->processFileChange(fileName).outcome
                      == ExternalDocumentSyncOutcome::Unchanged);

    QTextCursor reloadLocalEdit(left->document());
    reloadLocalEdit.setPosition(
        left->toPlainText().indexOf(
            QStringLiteral("local_external_20")));
    reloadLocalEdit.insertText(
        QStringLiteral("reload_candidate_"));
    QCoreApplication::processEvents();
    const QString reloadCandidateLocalText =
        left->toPlainText();
    const QString reloadExternalText =
        longFixture(QStringLiteral("reload_external"));
    expect("reload scenario external replacement succeeds",
           replaceFile(fileName, reloadExternalText)
               && sync->processFileChange(fileName).outcome
                      == ExternalDocumentSyncOutcome::Conflict);
    const ExternalDocumentConflictReview reloadReview =
        manager.externalConflictReview(fileName);
    const ExternalDocumentConflictActionResult reloaded =
        manager.reloadExternalConflict(reloadReview);
    QCoreApplication::processEvents();
    expect("explicit reload accepts the reviewed external generation",
           reloaded.applied()
               && left->toPlainText()
                      == reloadExternalText
               && right->toPlainText()
                      == reloadExternalText
               && !document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Current
               && QApplication::focusWidget()
                      == focusBeforeReview
               && manager.splitCount()
                      == splitCountBeforeReview);
    left->undo();
    QCoreApplication::processEvents();
    expect("explicit reload keeps rejected dirty text in shared undo",
           left->toPlainText()
                   == reloadCandidateLocalText
               && right->toPlainText()
                      == reloadCandidateLocalText
               && document->dirty());
    left->redo();
    QCoreApplication::processEvents();
    expect("redo returns to the accepted external generation",
           left->toPlainText()
                   == reloadExternalText
               && right->toPlainText()
                      == reloadExternalText);

    left->undo();
    QCoreApplication::processEvents();
    const QString saveAsExternalText =
        longFixture(QStringLiteral("save_as_external"));
    expect("save-as scenario enters a fresh conflict",
           replaceFile(fileName, saveAsExternalText)
               && sync->processFileChange(fileName).outcome
                      == ExternalDocumentSyncOutcome::Conflict);
    const ExternalDocumentConflictReview saveAsReview =
        manager.externalConflictReview(fileName);
    QString samePathFailure;
    expect("Save Local As cannot bypass conflict by selecting the source path",
           !manager.saveExternalConflictLocalAs(
               saveAsReview,
               fileName,
               &samePathFailure)
               && !samePathFailure.isEmpty()
               && readFile(fileName)
                      == saveAsExternalText
               && document->dirty());
    const QString saveAsFileName =
        temporary.filePath(
            QStringLiteral("local_conflict_copy.sv"));
    QString saveAsFailure;
    expect("save local as preserves the reviewed external source",
           manager.saveExternalConflictLocalAs(
               saveAsReview,
               saveAsFileName,
               &saveAsFailure)
               && saveAsFailure.isEmpty()
               && QFileInfo(saveAsFileName).isFile()
               && readFile(fileName)
                      == saveAsExternalText
               && readFile(saveAsFileName)
                      == left->toPlainText()
               && left->toPlainText().contains(
                      QStringLiteral(
                          "reload_candidate_"))
               && document->fileName()
                      == QDir::cleanPath(
                          QFileInfo(saveAsFileName)
                              .absoluteFilePath()));

    expect("external deletion is reported without clearing text",
           QFile::remove(saveAsFileName));
    const QString beforeMissing = left->toPlainText();
    const ExternalDocumentSyncResult missing =
        sync->processFileChange(saveAsFileName);
    expect("clean unavailable file has explicit external state",
           missing.outcome
                   == ExternalDocumentSyncOutcome::Unavailable
               && document->externalState()
                      == SharedDocumentExternalState::
                          ExternallyModified
               && left->toPlainText() == beforeMissing
               && sync->isDirectoryWatchedForTesting(
                   temporary.path()));

    const QString recreatedText =
        longFixture(QStringLiteral("recreated"));
    expect("deleted file can be recreated atomically",
           replaceFile(saveAsFileName, recreatedText));
    expect("recreated file reloads and is watched again",
           sync->processFileChange(saveAsFileName).outcome
                   == ExternalDocumentSyncOutcome::Reloaded
               && left->toPlainText() == recreatedText
               && right->toPlainText() == recreatedText
               && !document->dirty()
               && document->externalState()
                      == SharedDocumentExternalState::Current
               && sync->isFileWatchedForTesting(
                      saveAsFileName));

    QTextCursor unavailableLocalEdit(left->document());
    unavailableLocalEdit.movePosition(QTextCursor::End);
    unavailableLocalEdit.insertText(
        QStringLiteral("// preserve while unavailable\n"));
    const QString unavailableLocalText =
        left->toPlainText();
    expect("dirty source can become unavailable",
           QFile::remove(saveAsFileName)
               && sync->processFileChange(
                      saveAsFileName).outcome
                      == ExternalDocumentSyncOutcome::
                          Unavailable
               && document->externalState()
                      == SharedDocumentExternalState::Conflict);
    const ExternalDocumentConflictReview
        unavailableReview =
            manager.externalConflictReview(
                saveAsFileName);
    const QString rescuedFileName =
        temporary.filePath(
            QStringLiteral(
                "rescued_local_copy.sv"));
    QString rescueFailure;
    expect("unavailable conflict permits guarded Save Local As",
           unavailableReview.valid
               && !unavailableReview.externalAvailable
               && unavailableReview.localText
                      == unavailableLocalText
               && manager.saveExternalConflictLocalAs(
                   unavailableReview,
                   rescuedFileName,
                   &rescueFailure)
               && rescueFailure.isEmpty()
               && readFile(rescuedFileName)
                      == left->toPlainText()
               && left->toPlainText().contains(
                      QStringLiteral(
                          "preserve while unavailable")));

#ifdef Q_OS_WIN
    QTemporaryDir junctionTemporary;
    const QString junctionTargetDirectory =
        junctionTemporary.filePath(
            QStringLiteral("junction_target"));
    const QString junctionAliasDirectory =
        junctionTemporary.filePath(
            QStringLiteral("junction_alias"));
    const QString junctionTargetFile =
        QDir(junctionTargetDirectory)
            .absoluteFilePath(
                QStringLiteral("junction_sync.sv"));
    const QString junctionAliasFile =
        QDir(junctionAliasDirectory)
            .absoluteFilePath(
                QStringLiteral("junction_sync.sv"));
    const QString junctionInitialText =
        longFixture(QStringLiteral("junction_initial"));
    expect("Windows Junction end-to-end fixture is writable",
           junctionTemporary.isValid()
               && QDir().mkpath(
                      junctionTargetDirectory)
               && replaceFile(
                      junctionTargetFile,
                      junctionInitialText));

    QString junctionFailure;
    if (!createDirectoryJunction(
            junctionTargetDirectory,
            junctionAliasDirectory,
            &junctionFailure)) {
        std::printf(
            "[SKIP] Windows Junction end-to-end is unavailable on "
            "this volume or security policy: %s\n",
            junctionFailure.toLocal8Bit().constData());
    } else {
        QWidget junctionHost;
        auto* junctionLayout =
            new QVBoxLayout(&junctionHost);
        junctionLayout->setContentsMargins(0, 0, 0, 0);
        auto* junctionTabs =
            new QTabWidget(&junctionHost);
        junctionLayout->addWidget(junctionTabs);
        TabManager junctionManager(junctionTabs);
        junctionManager.enableSplitLayout(
            &junctionHost);
        junctionManager.setWorkspaceScope(
            {junctionAliasDirectory},
            junctionAliasDirectory);
        junctionHost.resize(720, 360);
        junctionHost.show();

        expect("Junction workspace owns a file opened through its real path",
               junctionManager.openFileInTab(
                   junctionTargetFile)
                   && junctionManager.editorCount() == 1
                   && junctionManager
                          .workspaceSessionTabs(
                              junctionAliasDirectory)
                          .size()
                          == 1
                   && junctionManager
                          .workspaceSessionTabs(
                              junctionAliasDirectory)
                          .first()
                          .filePath
                          == lexicalPath(
                              junctionTargetFile));
        MyCodeEditor* junctionEditor =
            junctionManager.getCurrentEditor();
        SharedDocument* junctionDocument =
            junctionManager.sharedDocumentForEditor(
                junctionEditor);
        ExternalDocumentSyncController*
            junctionSync =
                junctionManager
                    .externalDocumentSyncController();
        expect("opening the Junction spelling reuses the existing document",
               junctionManager.openFileInTab(
                   junctionAliasFile)
                   && junctionManager.editorCount() == 1
                   && junctionManager
                          .sharedDocumentForEditor(
                              junctionManager
                                  .getCurrentEditor())
                          == junctionDocument);

        QTextCursor junctionLocalEdit(
            junctionEditor->document());
        junctionLocalEdit.movePosition(
            QTextCursor::End);
        junctionLocalEdit.insertText(
            QStringLiteral(
                "// local junction edit\n"));
        const QString junctionDiskConflictText =
            longFixture(
                QStringLiteral(
                    "junction_external"));
        expect("Junction conflict uses the same disk entity",
               replaceFile(
                   junctionTargetFile,
                   junctionDiskConflictText)
                   && junctionSync
                          ->processFileChange(
                              junctionAliasFile)
                          .outcome
                          == ExternalDocumentSyncOutcome::
                              Conflict);
        const ExternalDocumentConflictReview
            junctionReview =
                junctionManager
                    .externalConflictReview(
                        junctionTargetFile);
        QString junctionSaveAsFailure;
        expect("Save Local As rejects a Junction alias of the conflicted source",
               junctionReview.valid
                   && !junctionManager
                           .saveExternalConflictLocalAs(
                               junctionReview,
                               junctionAliasFile,
                               &junctionSaveAsFailure)
                   && !junctionSaveAsFailure.isEmpty()
                   && readFile(junctionTargetFile)
                          == junctionDiskConflictText
                   && junctionDocument->dirty()
                   && junctionDocument
                          ->externalState()
                          == SharedDocumentExternalState::
                              Conflict);
        expect("keeping the Junction generation authorizes only that fingerprint",
               junctionManager
                   .keepLocalExternalConflict(
                       junctionReview)
                   .applied()
                   && junctionSync
                          ->canOverwriteDocument(
                              junctionDocument)
                   && junctionDocument
                          ->externalState()
                          == SharedDocumentExternalState::
                              ExternallyModified);

        QSignalSpy junctionConflictSpy(
            junctionSync,
            &ExternalDocumentSyncController::
                documentConflictDetected);
        junctionEditor->setDocumentFileName(
            junctionAliasFile);
        QCoreApplication::processEvents();
        expect("same-entity lexical change preserves fingerprints and updates watcher path",
               junctionDocument->fileName()
                       == lexicalPath(
                           junctionAliasFile)
                   && junctionSync
                          ->watchedFilePathForTesting(
                              junctionTargetFile)
                          == lexicalPath(
                              junctionAliasFile)
                   && junctionSync
                          ->canOverwriteDocument(
                              junctionDocument)
                   && junctionDocument
                          ->externalState()
                          == SharedDocumentExternalState::
                              ExternallyModified
                   && junctionConflictSpy.isEmpty());
        expect("workspace session propagates the selected lexical Junction path",
               junctionManager
                       .workspaceSessionTabs(
                           junctionAliasDirectory)
                       .size()
                       == 1
                   && junctionManager
                          .workspaceSessionTabs(
                              junctionAliasDirectory)
                          .first()
                          .filePath
                          == lexicalPath(
                              junctionAliasFile));

        const QString junctionNewExternalText =
            longFixture(
                QStringLiteral(
                    "junction_changed_again"));
        expect("real-path notification resolves to the Junction-tracked document",
               replaceFile(
                   junctionTargetFile,
                   junctionNewExternalText));
        const ExternalDocumentSyncResult
            junctionChangedAgain =
                junctionSync->processFileChange(
                    junctionTargetFile);
        QCoreApplication::processEvents();
        expect("external signal and result retain the latest lexical path",
               junctionChangedAgain.outcome
                       == ExternalDocumentSyncOutcome::
                           Conflict
                   && junctionChangedAgain.fileName
                          == lexicalPath(
                              junctionAliasFile)
                   && junctionConflictSpy.size() == 1
                   && junctionConflictSpy
                          .first()
                          .at(1)
                          .toString()
                          == lexicalPath(
                              junctionAliasFile));
    }
#else
    std::printf(
        "[SKIP] Windows Junction end-to-end is not applicable "
        "on this platform\n");
#endif

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
