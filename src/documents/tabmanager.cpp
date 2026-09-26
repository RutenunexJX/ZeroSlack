#include "tabmanager.h"

#include "actionregistry.h"
#include "documentmodel.h"
#include "editorfileidentity.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QMessageBox>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace {
QString lexicalPath(const QString& path)
{
    return EditorFileIdentity::normalized(path);
}

QString identityKey(const QString& path)
{
    return EditorFileIdentity::lookupKey(path);
}

bool sameLexicalPath(const QString& left,
                     const QString& right)
{
    return lexicalPath(left) == lexicalPath(right);
}

bool pathInsideWorkspaceRoot(const QString& fileName,
                             const QString& workspaceRoot)
{
    const QString fileKey = identityKey(fileName);
    const QString rootKey = identityKey(workspaceRoot);
    if (fileKey.isEmpty() || rootKey.isEmpty())
        return false;
    const QString rootPrefix = rootKey.endsWith(QLatin1Char('/'))
        ? rootKey
        : rootKey + QLatin1Char('/');
    return fileKey == rootKey || fileKey.startsWith(rootPrefix);
}

QStringList lexicalTabWorkspaceRoots(const QStringList& roots)
{
    QStringList normalized;
    QSet<QString> identities;
    normalized.reserve(roots.size());
    for (const QString& root : roots) {
        const QString path = lexicalPath(root);
        const QString key = identityKey(path);
        if (path.isEmpty() || key.isEmpty()
            || identities.contains(key)) {
            continue;
        }
        identities.insert(key);
        normalized.append(path);
    }
    return normalized;
}

QString workspaceRootForFile(
    const QString& fileName,
    const QStringList& workspaceRoots)
{
    QString best;
    qsizetype bestIdentityLength = -1;
    for (const QString& root : workspaceRoots) {
        const QString rootKey = identityKey(root);
        if (!pathInsideWorkspaceRoot(fileName, root)
            || rootKey.size() <= bestIdentityLength)
            continue;
        best = root;
        bestIdentityLength = rootKey.size();
    }
    return best;
}

HierarchyInstanceContext unboundTabInstanceContext(
    const QString& workspaceRoot)
{
    HierarchyInstanceContext context;
    context.workspacePath = lexicalPath(workspaceRoot);
    return context;
}

bool semanticRefreshMatchesDocument(
    const QString& changedFileName,
    const QString& documentFileName)
{
    if (changedFileName.isEmpty()
        || changedFileName == QStringLiteral("open_tabs")) {
        return true;
    }
    const QString changedKey = identityKey(changedFileName);
    return !changedKey.isEmpty()
        && changedKey == identityKey(documentFileName);
}

bool documentPathMatchesMutation(
    const QString& documentPath,
    const QString& sourcePath,
    bool recursive)
{
    const QString documentKey =
        EditorFileIdentity::lookupKey(documentPath);
    const QString sourceKey =
        EditorFileIdentity::lookupKey(sourcePath);
    if (documentKey.isEmpty() || sourceKey.isEmpty())
        return false;
    if (documentKey == sourceKey)
        return true;
    if (!recursive)
        return false;
    const QString prefix =
        sourceKey.endsWith(QLatin1Char('/'))
        ? sourceKey
        : sourceKey + QLatin1Char('/');
    return documentKey.startsWith(prefix);
}

bool sameRecoveryDocumentKey(
    const CrashRecoveryDocumentKey& left,
    const CrashRecoveryDocumentKey& right)
{
    return identityKey(left.workspacePath)
            == identityKey(right.workspacePath)
        && identityKey(left.originalFilePath)
            == identityKey(right.originalFilePath)
        && left.untitledDocumentId
            == right.untitledDocumentId;
}

bool sameReviewedRecoveryCandidate(
    const CrashRecoveryCandidate& reviewed,
    const CrashRecoveryCandidate& current)
{
    return reviewed.recoveryId == current.recoveryId
        && identityKey(reviewed.workspacePath)
               == identityKey(current.workspacePath)
        && identityKey(reviewed.originalFilePath)
               == identityKey(current.originalFilePath)
        && reviewed.untitledDocumentId
               == current.untitledDocumentId
        && reviewed.documentRevision
               == current.documentRevision
        && reviewed.recoveredTextSha256
               == current.recoveredTextSha256
        && reviewed.sourceState
               == current.sourceState
        && reviewed.currentSourceSha256
               == current.currentSourceSha256
        && reviewed.currentSourceModifiedUtc
               == current.currentSourceModifiedUtc
        && reviewed.sourceExists
               == current.sourceExists
        && reviewed.sourceReadable
               == current.sourceReadable
        && reviewed.sourceChangedSinceBaseline
               == current.sourceChangedSinceBaseline;
}

QString decodedSourceText(QByteArray bytes)
{
    static const QByteArray utf8Bom =
        QByteArray::fromHex("efbbbf");
    if (bytes.startsWith(utf8Bom))
        bytes.remove(0, utf8Bom.size());
    QString text = QString::fromUtf8(bytes);
    text.replace(
        QStringLiteral("\r\n"),
        QStringLiteral("\n"));
    text.replace(
        QLatin1Char('\r'),
        QLatin1Char('\n'));
    return text;
}

QStringList pathComponents(const QString& path)
{
    return QDir::cleanPath(
               QDir::fromNativeSeparators(path))
        .split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QString suffixPath(const QStringList& components, int depth)
{
    return components.mid(
        qMax(0, components.size() - depth))
        .join(QLatin1Char('/'));
}
}

QString tabGroupingModeStableId(TabGroupingMode mode)
{
    switch (mode) {
    case TabGroupingMode::Module:
        return QStringLiteral("module");
    case TabGroupingMode::Workspace:
        return QStringLiteral("workspace");
    case TabGroupingMode::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

TabGroupingMode tabGroupingModeFromStableId(
    const QString& stableId)
{
    const QString normalized = stableId.trimmed().toLower();
    if (normalized == QStringLiteral("module"))
        return TabGroupingMode::Module;
    if (normalized == QStringLiteral("workspace"))
        return TabGroupingMode::Workspace;
    return TabGroupingMode::None;
}

TabManager::TabManager(QTabWidget* initialTabWidget, QObject* parent)
    : QObject(parent)
    , tabWidget(initialTabWidget)
    , documentModel(std::make_unique<DocumentModel>(this))
    , sharedDocuments(
          std::make_unique<SharedDocumentRegistry>(this))
    , externalDocumentSync(
          std::make_unique<ExternalDocumentSyncController>(
              this))
    , unsavedDocumentManager(
          std::make_unique<UnsavedDocumentManager>())
    , crashRecoveryService(
          std::make_unique<CrashRecoveryService>())
    , splitController(
          std::make_unique<EditorSplitController>(
              initialTabWidget,
              this))
    , documentQueries(documentModel.get(), &fileIo)
{
    connect(externalDocumentSync.get(),
            &ExternalDocumentSyncController::documentReloaded,
            this,
            [this](SharedDocument* document,
                   const QString& fileName) {
                if (document) {
                    for (MyCodeEditor* view :
                         document->views()) {
                        documentModel->refreshEditorState(view);
                    }
                    const QList<MyCodeEditor*> views =
                        document->views();
                    if (!views.isEmpty()) {
                        documentModel->markSaved(
                            views.first());
                    }
                    updateTitlesForDocument(document);
                    if (!explicitExternalReloadDocuments
                             .contains(document)) {
                        clearRecoverySnapshot(
                            document,
                            true,
                            false);
                    }
                }
                emit externalFileReloaded(fileName);
            });
    connect(externalDocumentSync.get(),
            &ExternalDocumentSyncController::
                documentConflictDetected,
            this,
            [this](SharedDocument* document,
                   const QString& fileName) {
                updateTitlesForDocument(document);
                emit externalFileConflict(fileName);
            });
    connect(externalDocumentSync.get(),
            &ExternalDocumentSyncController::
                documentUnavailable,
            this,
            [this](SharedDocument* document,
                   const QString& fileName,
                   const QString& failureReason) {
                updateTitlesForDocument(document);
                emit externalFileUnavailable(
                    fileName,
                    failureReason);
            });
    if (!tabWidget)
        return;

    registerTabGroup(tabWidget);
    connect(splitController.get(),
            &EditorSplitController::groupCreated,
            this,
            [this](QTabWidget* group) {
                registerTabGroup(group);
                emit tabGroupCreated(group);
            });
    connect(splitController.get(),
            &EditorSplitController::tabCloseRequested,
            this,
            [this](QTabWidget* group, int index) {
                closePage(group, index);
            });
    connect(splitController.get(),
            &EditorSplitController::tabActionRequested,
            this,
            &TabManager::handleTabAction);
    connect(splitController.get(),
            &EditorSplitController::activeGroupChanged,
            this,
            [this](QTabWidget* group) {
                handleCurrentTabChanged(
                    group,
                    group ? group->currentIndex() : -1);
            });
    connect(splitController.get(),
            &EditorSplitController::layoutChanged,
            this,
            &TabManager::splitLayoutChanged);
    connect(splitController.get(), &EditorSplitController::pageMoved, this,
            [this] { applyWorkspaceScope(); applyTabGrouping(); });
    connect(splitController.get(),
            &EditorSplitController::layoutChanged,
            this,
            &TabManager::workspaceSessionStateChanged);

    previousActiveEditor = getCurrentEditor();
    QTimer::singleShot(0, this, [this] {
        scanCrashRecoveryCandidates(temporaryRecoveryWorkspace());
    });
}

TabManager::~TabManager()
{
    const QList<MyCodeEditor*> auxiliary = auxiliaryViews();
    for (MyCodeEditor* editor : auxiliary)
        closeAuxiliaryView(editor);

    for (SharedDocument* document :
         std::as_const(observedDocuments)) {
        if (document)
            disconnect(document, nullptr, this, nullptr);
    }
    observedDocuments.clear();
    sharedDocuments.reset();
}

void TabManager::setCrashRecoveryService(
    std::unique_ptr<CrashRecoveryService> service)
{
    crashRecoveryService =
        service
        ? std::move(service)
        : std::make_unique<CrashRecoveryService>();
    recoveryDocumentStates.clear();
    recoveryScannedWorkspaceKeys.clear();
    checkpointCrashRecovery();
}

CrashRecoveryService*
TabManager::crashRecoveryServiceForTesting() const
{
    return crashRecoveryService.get();
}

QString TabManager::recoveryWorkspaceForDocument(
    const SharedDocument* document) const
{
    if (document && !document->fileName().isEmpty()) {
        const QString matchingRoot =
            workspaceRootForFile(
                document->fileName(),
                scopedWorkspaceRoots);
        if (!matchingRoot.isEmpty())
            return lexicalPath(matchingRoot);
    }
    return temporaryRecoveryWorkspace();
}

QString TabManager::temporaryRecoveryWorkspace() const
{
    // A stable recovery namespace, never the current project or source folder.
    return lexicalPath(QDir(crashRecoveryService->recoveryRootPath())
                           .filePath(QStringLiteral("standalone")));
}

QString TabManager::recoveryWorkspacePath(
    const QString& requestedWorkspace) const
{
    if (!requestedWorkspace.trimmed().isEmpty()) {
        return lexicalPath(
            requestedWorkspace);
    }
    if (!activeWorkspaceRoot.isEmpty())
        return lexicalPath(activeWorkspaceRoot);
    if (!scopedWorkspaceRoots.isEmpty()) {
        return lexicalPath(
            scopedWorkspaceRoots.first());
    }
    return temporaryRecoveryWorkspace();
}

CrashRecoveryDocumentKey
TabManager::recoveryKeyForDocument(
    const SharedDocument* document) const
{
    CrashRecoveryDocumentKey key;
    if (!document)
        return key;
    key.workspacePath =
        recoveryWorkspaceForDocument(document);
    if (document->fileName().isEmpty()) {
        key.untitledDocumentId =
            document->documentId();
    } else {
        key.originalFilePath =
            document->fileName();
    }
    return key;
}

bool TabManager::writeRecoverySnapshot(
    SharedDocument* document,
    bool force)
{
    if (!crashRecoveryService || !document
        || !document->dirty()
        || !document->textDocument()) {
        return false;
    }

    RecoveryDocumentState& state =
        recoveryDocumentStates[document];
    const quint64 revision =
        static_cast<quint64>(
            document->textRevision());
    if (!force && state.hasSnapshot
        && revision
               < state.snapshotRevision
                   + kCrashRecoveryRevisionInterval) {
        return true;
    }

    CrashRecoverySnapshotRequest request;
    request.document =
        recoveryKeyForDocument(document);
    request.text =
        document->textDocument()->toPlainText();
    request.documentRevision = revision;
    request.savedBaselineSha256 =
        document->savedBaselineSha256();
    request.savedBaselineModifiedUtc =
        document->savedBaselineModifiedUtc();

    const CrashRecoveryWriteResult result =
        crashRecoveryService->writeSnapshot(request);
    if (result.status == CrashRecoveryStatus::Success) {
        const RecoveryDocumentState previous = state;
        state.key = request.document;
        state.snapshotRevision = revision;
        state.hasSnapshot = true;
        if (previous.hasSnapshot
            && !sameRecoveryDocumentKey(
                   previous.key,
                   state.key)) {
            const CrashRecoveryOperationResult cleanup =
                crashRecoveryService
                    ->clearAfterNormalClose(
                        previous.key);
            if (!cleanup.succeeded()) {
                emit crashRecoveryOperationFailed(
                    document->documentId(),
                    cleanup.reason);
            }
        }
        return true;
    }

    if (result.status == CrashRecoveryStatus::StaleRecord
        && CrashRecoveryService::sha256(
               request.text.toUtf8())
               == document->savedBaselineSha256()) {
        clearRecoverySnapshot(document, true);
        return true;
    }

    emit crashRecoveryOperationFailed(
        document->documentId(),
        result.reason);
    return false;
}

void TabManager::clearRecoverySnapshot(
    SharedDocument* document,
    bool normalSave,
    bool includeUntrackedCurrent)
{
    if (!crashRecoveryService || !document)
        return;

    QList<CrashRecoveryDocumentKey> keys;
    const RecoveryDocumentState state =
        recoveryDocumentStates.value(document);
    if (state.hasSnapshot)
        keys.append(state.key);
    if (keys.isEmpty()
        && !includeUntrackedCurrent) {
        return;
    }
    const CrashRecoveryDocumentKey current =
        recoveryKeyForDocument(document);
    bool currentAlreadyIncluded = false;
    for (const CrashRecoveryDocumentKey& key :
         std::as_const(keys)) {
        if (sameRecoveryDocumentKey(key, current)) {
            currentAlreadyIncluded = true;
            break;
        }
    }
    if (!currentAlreadyIncluded)
        keys.append(current);

    bool allCleared = true;
    for (const CrashRecoveryDocumentKey& key :
         std::as_const(keys)) {
        const CrashRecoveryOperationResult result =
            normalSave
            ? crashRecoveryService
                  ->clearAfterNormalSave(key)
            : crashRecoveryService
                  ->clearAfterNormalClose(key);
        if (!result.succeeded()) {
            allCleared = false;
            emit crashRecoveryOperationFailed(
                document->documentId(),
                result.reason);
        }
    }
    if (allCleared)
        recoveryDocumentStates.remove(document);
}

CrashRecoveryListResult
TabManager::listCrashRecoveryCandidates(
    const QString& workspaceRoot) const
{
    if (!crashRecoveryService) {
        CrashRecoveryListResult result;
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason =
            QStringLiteral(
                "Crash recovery service is unavailable.");
        return result;
    }
    return crashRecoveryService->listCandidates(
        recoveryWorkspacePath(workspaceRoot));
}

CrashRecoveryReadResult
TabManager::compareCrashRecoveryCandidate(
    const QString& recoveryId,
    const QString& workspaceRoot) const
{
    if (!crashRecoveryService) {
        CrashRecoveryReadResult result;
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason =
            QStringLiteral(
                "Crash recovery service is unavailable.");
        return result;
    }
    return crashRecoveryService->readComparison(
        recoveryWorkspacePath(workspaceRoot),
        recoveryId);
}

CrashRecoveryRecoverResult
TabManager::recoverCrashRecoveryText(
    const QString& recoveryId,
    const QString& workspaceRoot) const
{
    if (!crashRecoveryService) {
        CrashRecoveryRecoverResult result;
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason =
            QStringLiteral(
                "Crash recovery service is unavailable.");
        return result;
    }
    return crashRecoveryService->recoverText(
        recoveryWorkspacePath(workspaceRoot),
        recoveryId);
}

CrashRecoveryApplyResult
TabManager::applyCrashRecoveryCandidate(
    const CrashRecoveryCandidate& reviewedCandidate)
{
    CrashRecoveryApplyResult result;
    if (!crashRecoveryService
        || !sharedDocuments
        || reviewedCandidate.recoveryId.isEmpty()
        || reviewedCandidate.workspacePath.isEmpty()) {
        result.status =
            CrashRecoveryStatus::InvalidArgument;
        result.reason =
            QStringLiteral(
                "A reviewed recovery candidate is required.");
        return result;
    }

    const CrashRecoveryReadResult comparison =
        crashRecoveryService->readComparison(
            reviewedCandidate.workspacePath,
            reviewedCandidate.recoveryId);
    if (comparison.status
            != CrashRecoveryStatus::Success) {
        result.status = comparison.status;
        result.reason = comparison.reason;
        return result;
    }
    if (!sameReviewedRecoveryCandidate(
            reviewedCandidate,
            comparison.candidate)) {
        result.status =
            CrashRecoveryStatus::IdentityMismatch;
        result.reason =
            QStringLiteral(
                "The source or recovery snapshot changed after review.");
        return result;
    }

    SharedDocument* document = nullptr;
    if (!comparison.candidate
             .originalFilePath.isEmpty()) {
        document =
            sharedDocuments->documentForFile(
                comparison.candidate
                    .originalFilePath);
        if (!document) {
            if (comparison.candidate.sourceExists
                && comparison.candidate
                       .sourceReadable) {
                document = acquireFileDocument(
                    comparison.candidate
                        .originalFilePath);
            } else {
                document = sharedDocuments->acquire(
                    comparison.candidate
                        .originalFilePath,
                    QString());
                if (document)
                    document->setReadOnly(false);
            }
        }
    } else {
        document =
            sharedDocuments->documentById(
                comparison.candidate
                    .untitledDocumentId);
        if (!document) {
            document =
                sharedDocuments->acquireUntitled(
                    comparison.candidate
                        .untitledDocumentId);
        }
    }

    if (!document) {
        result.status =
            CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral(
                "The recovery target document could not be created.");
        return result;
    }
    if (document->dirty()) {
        result.status =
            CrashRecoveryStatus::IdentityMismatch;
        result.reason =
            QStringLiteral(
                "The open document changed after recovery review.");
        return result;
    }
    if (comparison.candidate.sourceReadable
        && document->textDocument()
                   ->toPlainText()
               != decodedSourceText(
                      comparison
                          .currentSourceBytes)) {
        result.status =
            CrashRecoveryStatus::IdentityMismatch;
        result.reason =
            QStringLiteral(
                "The open document no longer matches the reviewed source.");
        return result;
    }

    if (document->viewCount() == 0) {
        if (!createView(
                document,
                activeTabWidget())) {
            sharedDocuments->releaseIfUnused(
                document);
            result.status =
                CrashRecoveryStatus::IoError;
            result.reason =
                QStringLiteral(
                    "The recovered document view could not be created.");
            return result;
        }
        result.openedView = true;
    }

    const std::uint64_t recoveredRevision =
        std::max<std::uint64_t>(
            document->textRevision() + 1,
            comparison.candidate
                .documentRevision);
    document->restoreSavedBaseline(
        comparison.candidate
            .savedBaselineSha256,
        comparison.candidate
            .savedBaselineModifiedUtc);
    if (!document->restoreUnsavedText(
            comparison.recoveredText,
            recoveredRevision)) {
        result.status =
            CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral(
                "The recovered text could not be applied in memory.");
        return result;
    }
    document->setExternalState(
        comparison.candidate.sourceState
                == CrashRecoverySourceState::
                    ExternallyModified
            ? SharedDocumentExternalState::Conflict
            : SharedDocumentExternalState::Current);
    for (MyCodeEditor* view : document->views())
        documentModel->refreshEditorState(view);
    updateTitlesForDocument(document);
    applyWorkspaceScope();
    writeRecoverySnapshot(document, true);

    result.status = CrashRecoveryStatus::Success;
    result.reason =
        QStringLiteral(
            "Recovered text was applied in memory without writing the source file.");
    result.documentId = document->documentId();
    result.fileName = document->fileName();
    result.documentRevision =
        static_cast<quint64>(
            document->textRevision());
    return result;
}

CrashRecoveryOperationResult
TabManager::discardCrashRecoveryCandidate(
    const QString& recoveryId,
    const QString& workspaceRoot)
{
    if (!crashRecoveryService) {
        CrashRecoveryOperationResult result;
        result.status =
            CrashRecoveryStatus::StorageUnavailable;
        result.reason =
            QStringLiteral(
                "Crash recovery service is unavailable.");
        return result;
    }

    const QString workspace =
        recoveryWorkspacePath(workspaceRoot);
    const CrashRecoveryReadResult comparison =
        crashRecoveryService->readComparison(
            workspace,
            recoveryId);
    const CrashRecoveryOperationResult result =
        crashRecoveryService->discard(
            workspace,
            recoveryId);
    if (result.succeeded()
        && comparison.status
               == CrashRecoveryStatus::Success) {
        for (auto it =
                 recoveryDocumentStates.begin();
             it != recoveryDocumentStates.end();
             ++it) {
            const CrashRecoveryDocumentKey& key =
                it.value().key;
            const bool sameDocument =
                (!comparison.candidate
                      .originalFilePath.isEmpty()
                 && identityKey(key.originalFilePath)
                        == identityKey(
                            comparison.candidate
                                .originalFilePath))
                || (!comparison.candidate
                         .untitledDocumentId.isEmpty()
                    && key.untitledDocumentId
                           == comparison.candidate
                                  .untitledDocumentId);
            if (sameDocument
                && identityKey(key.workspacePath)
                       == identityKey(workspace)) {
                it.value().hasSnapshot = false;
            }
        }
    }
    return result;
}

void TabManager::checkpointCrashRecovery()
{
    for (SharedDocument* document :
         std::as_const(observedDocuments)) {
        if (document && document->dirty())
            writeRecoverySnapshot(document, true);
    }
}

void TabManager::clearCrashRecoveryAfterNormalClose()
{
    for (SharedDocument* document :
         std::as_const(observedDocuments)) {
        if (document)
            clearRecoverySnapshot(document, false);
    }
}

void TabManager::scanCrashRecoveryCandidates(
    const QString& workspaceRoot)
{
    if (!crashRecoveryService
        || workspaceRoot.isEmpty()) {
        return;
    }
    const QString scanKey =
        identityKey(workspaceRoot);
    if (recoveryScannedWorkspaceKeys
            .contains(scanKey)) {
        return;
    }
    const CrashRecoveryListResult result =
        listCrashRecoveryCandidates(workspaceRoot);
    if (!result.succeeded()) {
        emit crashRecoveryOperationFailed(
            QString(),
            result.reason);
        return;
    }
    recoveryScannedWorkspaceKeys.insert(scanKey);
    if (!result.candidates.isEmpty()
        || !result.isolatedRecords.isEmpty()) {
        emit crashRecoveryCandidatesAvailable(
            recoveryWorkspacePath(workspaceRoot),
            result.candidates.size(),
            result.isolatedRecords.size());
    }
}

void TabManager::createNewTab()
{
    SharedDocument* document =
        sharedDocuments->createUntitled();
    MyCodeEditor* editor =
        createView(document, activeTabWidget());
    if (editor)
        applyWorkspaceScope();
}

bool TabManager::openFileInTab(const QString& requestedFileName)
{
    QString fileName = requestedFileName;
    if (fileName.isEmpty()) {
        fileName = fileIo.promptOpenFile(
            qobject_cast<QWidget*>(parent()));
        if (fileName.isEmpty())
            return false;
    }
    const QString owner = workspaceForFile(fileName);
    if (!owner.isEmpty() && identityKey(owner) != identityKey(activeWorkspaceRoot))
        emit workspaceActivationRequested(owner);
    if (activateOpenFile(fileName)) {
        if (MyCodeEditor* editor = getCurrentEditor()) {
            editor->setHierarchyInstanceContext(
                unboundTabInstanceContext(
                    workspaceForFile(fileName)));
        }
        return true;
    }

    SharedDocument* document =
        acquireFileDocument(fileName);
    if (!document)
        return false;
    MyCodeEditor* editor =
        createView(document, activeTabWidget());
    if (!editor)
        return false;
    applyWorkspaceScope();
    return true;
}

bool TabManager::saveCurrentTab()
{
    return saveEditor(getCurrentEditor(), false);
}

bool TabManager::saveAsCurrentTab()
{
    return saveEditor(getCurrentEditor(), true);
}

void TabManager::closeTab(int index)
{
    QTabWidget* group = activeTabWidget();
    if (!group || index < 0 || index >= group->count())
        return;
    closePage(group, index);
}

void TabManager::enableSplitLayout(QWidget* host)
{
    if (splitController)
        splitController->setHost(host);
}

bool TabManager::splitCurrentView(
    EditorSplitDirection direction)
{
    MyCodeEditor* current = getCurrentEditor();
    SharedDocument* document =
        sharedDocumentForEditor(current);
    QTabWidget* source = activeTabWidget();
    if (!current || !document || !source || !splitController)
        return false;
    QTabWidget* created =
        splitController->createSplit(source, direction);
    if (!created)
        return false;
    const SharedDocumentViewState state =
        document->viewState(current);
    return createView(document, created, state) != nullptr;
}

bool TabManager::moveCurrentViewToSplit(
    EditorSplitDirection direction)
{
    MyCodeEditor* current = getCurrentEditor();
    return current && splitController
        && splitController->movePageToSplit(
               current,
               direction,
               activeTabWidget())
            != nullptr;
}

bool TabManager::duplicateCurrentView()
{
    MyCodeEditor* current = getCurrentEditor();
    SharedDocument* document =
        sharedDocumentForEditor(current);
    if (!current || !document)
        return false;
    return createView(
               document,
               activeTabWidget(),
               document->viewState(current))
        != nullptr;
}

bool TabManager::mergeCurrentSplit()
{
    return splitController
        && splitController->mergeGroup(activeTabWidget());
}

void TabManager::toggleCurrentSplitMaximized()
{
    if (splitController)
        splitController->toggleActiveGroupMaximized();
}

void TabManager::equalizeSplitSizes()
{
    if (splitController)
        splitController->equalizeSplitSizes();
}

int TabManager::splitCount() const
{
    return splitController
        ? splitController->groupCount()
        : (tabWidget ? 1 : 0);
}

EditorSplitController* TabManager::editorSplitController() const
{
    return splitController.get();
}

SharedDocument* TabManager::sharedDocumentForEditor(
    MyCodeEditor* editor) const
{
    return sharedDocuments
        ? sharedDocuments->documentForView(editor)
        : nullptr;
}

MyCodeEditor* TabManager::editorActionTarget(
    const QString& preferredViewId) const
{
    const auto registeredEditors =
        [this]() {
            return openEditors() + auxiliaryViews();
        };
    const auto isRegistered =
        [this](MyCodeEditor* editor) {
            return editor
                && sharedDocumentForEditor(editor);
        };

    if (!preferredViewId.isEmpty()) {
        for (MyCodeEditor* editor : registeredEditors()) {
            if (editor
                && editor->property("editorViewId").toString()
                       == preferredViewId) {
                return editor;
            }
        }
        return nullptr;
    }

    QWidget* focused = QApplication::focusWidget();
    for (QWidget* widget = focused;
         widget;
         widget = widget->parentWidget()) {
        auto* editor = qobject_cast<MyCodeEditor*>(widget);
        if (isRegistered(editor))
            return editor;
    }

    MyCodeEditor* current = getCurrentEditor();
    return isRegistered(current) ? current : nullptr;
}

bool TabManager::saveEditorView(
    MyCodeEditor* editor,
    bool forceSaveAs,
    const QString& explicitFileName)
{
    return sharedDocumentForEditor(editor)
        && saveEditor(editor,
                      forceSaveAs,
                      explicitFileName);
}

MyCodeEditor* TabManager::createAuxiliaryView(
    const QString& documentId,
    const QString& fileName,
    QWidget* parentWidget,
    const SharedDocumentViewState& state)
{
    SharedDocument* document =
        auxiliaryDocument(documentId, fileName);
    if (!document || !parentWidget)
        return nullptr;
    return createBoundView(
        document, parentWidget, state, true);
}

bool TabManager::rebindAuxiliaryView(
    MyCodeEditor* editor,
    const QString& documentId,
    const QString& fileName,
    const SharedDocumentViewState& state)
{
    if (!isAuxiliaryView(editor))
        return false;

    SharedDocument* target =
        auxiliaryDocument(documentId, fileName);
    SharedDocument* previous =
        sharedDocumentForEditor(editor);
    if (!target)
        return false;
    if (target == previous)
        return true;

    const SharedDocumentViewState previousState =
        previous ? previous->viewState(editor)
                 : SharedDocumentViewState();
    editor->exitInteractionModes(
        EditorModeExitReason::TabChanged);
    editor->closeSemanticPopup();
    documentModel->unregisterEditor(editor);
    if (previous)
        previous->detachView(editor, false);

    if (bindEditorToDocument(editor, target, state))
        return true;

    if (previous)
        bindEditorToDocument(editor, previous, previousState);
    return false;
}

bool TabManager::closeAuxiliaryView(MyCodeEditor* editor)
{
    return closeAuxiliaryViewInternal(editor, false);
}

bool TabManager::closeAuxiliaryViewInternal(
    MyCodeEditor* editor,
    bool releaseUnusedDocument)
{
    if (!isAuxiliaryView(editor))
        return false;

    SharedDocument* document =
        sharedDocumentForEditor(editor);
    emit auxiliaryViewAboutToClose(editor);
    editor->hide();
    editor->exitInteractionModes(
        EditorModeExitReason::DocumentClosed);
    editor->closeSemanticPopup();
    documentModel->unregisterEditor(editor);
    if (document)
        document->detachView(editor, false);
    auxiliaryEditors.remove(editor);
    editor->removeEventFilter(this);
    // The editor still references the shared QTextDocument while its widget
    // and text-control state are destroyed. Keep the SharedDocument alive
    // through this synchronous teardown instead of cloning the full text.
    delete editor;
    if (releaseUnusedDocument && document && sharedDocuments)
        sharedDocuments->releaseIfUnused(document);
    return true;
}

bool TabManager::saveAuxiliaryView(
    MyCodeEditor* editor,
    bool forceSaveAs)
{
    return isAuxiliaryView(editor)
        && saveEditorView(editor, forceSaveAs);
}

bool TabManager::isAuxiliaryView(MyCodeEditor* editor) const
{
    return editor && auxiliaryEditors.contains(editor);
}

QList<MyCodeEditor*> TabManager::auxiliaryViews() const
{
    QList<MyCodeEditor*> result;
    result.reserve(auxiliaryEditors.size());
    for (MyCodeEditor* editor : auxiliaryEditors) {
        if (editor)
            result.append(editor);
    }
    return result;
}

ExternalDocumentSyncController*
TabManager::externalDocumentSyncController() const
{
    return externalDocumentSync.get();
}

ExternalDocumentConflictReview
TabManager::externalConflictReview(
    const QString& fileName)
{
    return externalDocumentSync
        ? externalDocumentSync->conflictReview(fileName)
        : ExternalDocumentConflictReview();
}

ExternalDocumentConflictActionResult
TabManager::keepLocalExternalConflict(
    const ExternalDocumentConflictReview& review)
{
    if (!externalDocumentSync) {
        ExternalDocumentConflictActionResult result;
        result.failureReason =
            QStringLiteral(
                "External document synchronization is unavailable.");
        return result;
    }
    ExternalDocumentConflictActionResult result =
        externalDocumentSync->keepLocal(review);
    if (result.applied()) {
        if (SharedDocument* document =
                sharedDocuments->documentForFile(
                    review.fileName)) {
            updateTitlesForDocument(document);
            writeRecoverySnapshot(document, true);
        }
    }
    return result;
}

ExternalDocumentConflictActionResult
TabManager::reloadExternalConflict(
    const ExternalDocumentConflictReview& review)
{
    if (!externalDocumentSync) {
        ExternalDocumentConflictActionResult result;
        result.failureReason =
            QStringLiteral(
                "External document synchronization is unavailable.");
        return result;
    }
    SharedDocument* document =
        sharedDocuments->documentForFile(
            review.fileName);
    if (document && document->dirty())
        writeRecoverySnapshot(document, true);
    if (document)
        explicitExternalReloadDocuments.insert(
            document);
    const ExternalDocumentConflictActionResult result =
        externalDocumentSync->reloadExternal(review);
    if (document)
        explicitExternalReloadDocuments.remove(
            document);
    return result;
}

bool TabManager::saveExternalConflictLocalAs(
    const ExternalDocumentConflictReview& review,
    const QString& requestedTargetFileName,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!externalDocumentSync || !sharedDocuments) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "External document synchronization is unavailable.");
        }
        return false;
    }

    SharedDocument* document =
        sharedDocuments->documentForFile(
            review.fileName);
    if (!document || document->views().isEmpty()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The reviewed document is no longer open.");
        }
        return false;
    }

    QString targetFileName = requestedTargetFileName;
    if (targetFileName.isEmpty()) {
        targetFileName =
            fileIo.resolveSaveFileName(
                qobject_cast<QWidget*>(parent()),
                document->fileName(),
                true);
    }
    if (targetFileName.isEmpty()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("Save As was cancelled.");
        }
        return false;
    }
    if (EditorFileIdentity::same(
            targetFileName,
            document->fileName())) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Choose a different path, or keep the local version before overwriting this source.");
        }
        return false;
    }

    const ExternalDocumentConflictActionResult validation =
        externalDocumentSync
            ->validateConflictReviewForSaveAs(review);
    if (!validation.applied()) {
        if (failureReason)
            *failureReason = validation.failureReason;
        return false;
    }
    if (!saveEditor(
            document->views().first(),
            true,
            targetFileName)) {
        if (failureReason && failureReason->isEmpty()) {
            *failureReason =
                QStringLiteral("The local version could not be saved.");
        }
        return false;
    }
    return true;
}

bool TabManager::closeOtherTabs()
{
    QTabWidget* group = activeTabWidget();
    MyCodeEditor* current = getCurrentEditor();
    QList<MyCodeEditor*> targets;
    for (int index = 0; group && index < group->count(); ++index) {
        MyCodeEditor* editor =
            qobject_cast<MyCodeEditor*>(group->widget(index));
        if (editor && editor != current && !isTabLocked(editor))
            targets.append(editor);
    }
    return closeEditorsAtomically(targets);
}

bool TabManager::closeTabsToRight()
{
    QTabWidget* group = activeTabWidget();
    if (!group)
        return false;
    QList<MyCodeEditor*> targets;
    for (int index = group->currentIndex() + 1;
         index < group->count();
         ++index) {
        MyCodeEditor* editor =
            qobject_cast<MyCodeEditor*>(group->widget(index));
        if (editor && !isTabLocked(editor))
            targets.append(editor);
    }
    return closeEditorsAtomically(targets);
}

bool TabManager::closeAllTabs()
{
    QList<MyCodeEditor*> targets;
    for (MyCodeEditor* editor : allEditors()) {
        if (!isTabLocked(editor))
            targets.append(editor);
    }
    return closeEditorsAtomically(targets);
}

bool TabManager::reopenClosedTab()
{
    if (recentlyClosedTabs.isEmpty())
        return false;
    const ClosedTabState state =
        recentlyClosedTabs.takeLast();
    SharedDocument* document = nullptr;
    if (state.fileName.isEmpty()) {
        document =
            sharedDocuments->createUntitled(state.text);
    } else {
        document =
            sharedDocuments->documentForFile(
                state.fileName);
        if (!document)
            document = acquireFileDocument(state.fileName);
    }
    if (!document)
        return false;

    QTabWidget* group =
        ensureGroupIndex(state.groupIndex);
    MyCodeEditor* editor =
        createView(document, group, state.viewState);
    if (!editor)
        return false;
    if (state.locked)
        setTabLocked(editor, true);
    return true;
}

bool TabManager::setTabLocked(
    MyCodeEditor* editor,
    bool locked)
{
    if (!editor)
        return false;
    const QString viewId =
        editor->property("editorViewId").toString();
    if (viewId.isEmpty())
        return false;
    editor->setProperty("editorTabLocked", locked);
    if (lockedViewIds.contains(viewId) == locked)
        return true;
    if (locked)
        lockedViewIds.insert(viewId);
    else
        lockedViewIds.remove(viewId);
    updateTabTitle(editor);
    emit workspaceSessionStateChanged();
    return true;
}

bool TabManager::isTabLocked(
    MyCodeEditor* editor) const
{
    return editor
        && lockedViewIds.contains(
            editor->property(
                      "editorViewId")
                .toString());
}

void TabManager::setTabGroupingMode(TabGroupingMode mode)
{
    if (groupingMode == mode)
        return;
    groupingMode = mode;
    updateAllTabTitles();
    emit workspaceSessionStateChanged();
}

TabGroupingMode TabManager::tabGroupingMode() const
{
    return groupingMode;
}

MyCodeEditor* TabManager::getCurrentEditor() const
{
    QTabWidget* group = activeTabWidget();
    return group
        ? qobject_cast<MyCodeEditor*>(
              group->currentWidget())
        : nullptr;
}

MyCodeEditor* TabManager::getEditorAt(int index) const
{
    QTabWidget* group = activeTabWidget();
    if (!group || index < 0 || index >= group->count())
        return nullptr;
    return qobject_cast<MyCodeEditor*>(group->widget(index));
}

DocumentSnapshot TabManager::getCurrentDocument() const
{
    return documentQueries.currentDocument(
        getCurrentEditor());
}

DocumentSnapshot TabManager::getCurrentDocumentMetadata() const
{
    return documentQueries.currentDocumentMetadata(
        getCurrentEditor());
}

DocumentSnapshot TabManager::getDocumentForEditor(
    MyCodeEditor* editor) const
{
    return documentQueries.documentForEditor(editor);
}

bool TabManager::activateOpenFile(const QString& fileName)
{
    if (!sharedDocuments || !splitController)
        return false;
    SharedDocument* document =
        sharedDocuments->documentForFile(fileName);
    if (!document)
        return false;
    for (MyCodeEditor* editor : document->views()) {
        QTabWidget* group =
            splitController->groupForPage(editor);
        const int index =
            group ? group->indexOf(editor) : -1;
        if (group && index >= 0
            && group->isTabVisible(index)) {
            splitController->setActiveGroup(group);
            group->setCurrentIndex(index);
            editor->setFocus();
            return true;
        }
    }
    return false;
}

QWidget* TabManager::toolPage(const QString& stableId) const
{
    if (!splitController || stableId.trimmed().isEmpty())
        return nullptr;
    for (QTabWidget* group : splitController->groups()) {
        if (!group)
            continue;
        for (int index = 0; index < group->count(); ++index) {
            QWidget* page = group->widget(index);
            if (page
                && page->property("toolPageId").toString()
                    == stableId) {
                return page;
            }
        }
    }
    return nullptr;
}

QWidget* TabManager::openToolPage(QWidget* page,
                                  const QString& stableId,
                                  const QString& title)
{
    if (!page || stableId.trimmed().isEmpty()
        || !splitController) {
        return nullptr;
    }
    if (QWidget* existing = toolPage(stableId)) {
        if (existing != page)
            page->deleteLater();
        QTabWidget* group = splitController->groupForPage(existing);
        if (group) {
            splitController->setActiveGroup(group);
            group->setCurrentWidget(existing);
        }
        return existing;
    }

    QTabWidget* group = activeTabWidget();
    if (!group)
        group = splitController->initialGroup();
    if (!group)
        return nullptr;
    page->setProperty("toolPage", true);
    page->setProperty("toolPageId", stableId);
    page->setParent(group);
    const int index = group->addTab(page, title);
    group->setTabToolTip(index, title);
    splitController->setActiveGroup(group);
    group->setCurrentIndex(index);
    emit workspaceSessionStateChanged();
    return page;
}

bool TabManager::activateToolPage(const QString& stableId)
{
    QWidget* page = toolPage(stableId);
    if (!page || !splitController)
        return false;
    QTabWidget* group = splitController->groupForPage(page);
    if (!group)
        return false;
    splitController->setActiveGroup(group);
    group->setCurrentWidget(page);
    return true;
}

bool TabManager::closeActiveToolPage(const QString& stableId)
{
    QWidget* page = toolPage(stableId);
    QTabWidget* group = activeTabWidget();
    return page && group && group->currentWidget() == page
        && closePage(group, group->indexOf(page));
}

QString TabManager::getPlainTextFromCurrentTab() const
{
    return documentQueries.plainTextFromCurrent(
        getCurrentEditor());
}

QString TabManager::getPlainTextFromOpenFile(
    const QString& fileName) const
{
    return documentQueries.plainTextFromFile(fileName);
}

QStringList TabManager::getAllOpenFileNames() const
{
    return documentQueries.allOpenFileNames();
}

QStringList TabManager::getOpenSystemVerilogFiles() const
{
    return documentQueries.openSystemVerilogFiles();
}

int TabManager::editorCount() const
{
    return allEditors().size();
}

DocumentModel* TabManager::getDocumentModel() const
{
    return documentModel.get();
}

void TabManager::refreshSemanticPresentations(
    const QString& changedFileName)
{
    MyCodeEditor* editor = getCurrentEditor();
    if (!editor)
        return;
    const DocumentSnapshot document =
        getDocumentForEditor(editor);
    if (semanticRefreshMatchesDocument(
            changedFileName,
            document.fileName)) {
        editor->refreshSemanticPresentation();
    }
}

void TabManager::updateTabTitle(MyCodeEditor* editor)
{
    if (!editor || !splitController)
        return;
    QTabWidget* group =
        splitController->groupForPage(editor);
    const int index =
        group ? group->indexOf(editor) : -1;
    if (!group || index < 0)
        return;
    group->setTabText(index, tabTitleForEditor(editor));
    group->setTabToolTip(index, tabToolTipForEditor(editor));
    group->tabBar()->setTabData(
        index,
        editor->property("editorViewId"));
    if (editor == getCurrentEditor()) {
        const QString fileName =
            getDocumentForEditor(editor).fileName;
        if (QWidget* parentWidget =
                qobject_cast<QWidget*>(parent())) {
            parentWidget->setWindowFilePath(fileName.isEmpty() ? QString() : QFileInfo(fileName).absoluteFilePath());
            parentWidget->setWindowTitle(
                fileName.isEmpty()
                    ? QStringLiteral("untitled")
                    : fileName);
        }
    }
}

void TabManager::setWorkspaceScope(
    const QStringList& workspaceRoots,
    const QString& activeWorkspaceRootPath)
{
    checkpointCrashRecovery();
    const QString previousRoot = identityKey(activeWorkspaceRoot);
    const bool switched = previousRoot != identityKey(activeWorkspaceRootPath);
    if (MyCodeEditor* editor = getCurrentEditor()) {
        if (!previousRoot.isEmpty() && !isTemporaryEditor(editor))
            lastWorkspaceEditors.insert(previousRoot, editor);
    }
    changingWorkspaceScope = true;
    scopedWorkspaceRoots =
        lexicalTabWorkspaceRoots(workspaceRoots);
    activeWorkspaceRoot =
        lexicalPath(
            activeWorkspaceRootPath);
    const auto workspaceEditors = allEditors() + auxiliaryViews();
    for (MyCodeEditor* editor : workspaceEditors) {
        if (!editor)
            continue;
        const HierarchyInstanceContext current =
            editor->hierarchyInstanceContext();
        const DocumentSnapshot document =
            getDocumentForEditor(editor);
        const QString owner = workspaceForFile(document.fileName);
        editor->setProperty("standaloneDocument", owner.isEmpty());
        if (identityKey(current.workspacePath) != identityKey(owner))
            editor->setHierarchyInstanceContext(unboundTabInstanceContext(owner));
    }
    applyWorkspaceScope();
    if (switched) {
        MyCodeEditor* preferred = lastWorkspaceEditors.value(identityKey(activeWorkspaceRoot));
        if (!preferred || !editorVisibleInWorkspaceScope(preferred)) {
            preferred = nullptr;
            for (MyCodeEditor* editor : allEditors()) {
                if (identityKey(workspaceForFile(getDocumentForEditor(editor).fileName))
                    == identityKey(activeWorkspaceRoot) && !isTemporaryEditor(editor)) {
                    preferred = editor;
                    break;
                }
            }
        }
        if (preferred) {
            if (QTabWidget* group = splitController->groupForPage(preferred)) {
                splitController->setActiveGroup(group);
                group->setCurrentWidget(preferred);
            }
        }
    }
    changingWorkspaceScope = false;
    updateAllTabTitles();
    scanCrashRecoveryCandidates(
        recoveryWorkspacePath(QString()));
    scanCrashRecoveryCandidates(temporaryRecoveryWorkspace());
    checkpointCrashRecovery();
}

QString TabManager::workspaceForFile(const QString& fileName) const
{
    return workspaceRootForFile(fileName, scopedWorkspaceRoots);
}

bool TabManager::isTemporaryEditor(MyCodeEditor* editor) const
{
    const auto* document = sharedDocumentForEditor(editor);
    return document && workspaceForFile(document->fileName()).isEmpty();
}

bool TabManager::workspaceHasUnsavedChanges(const QString& workspaceRoot) const
{
    for (const auto* document : sharedDocuments->documents()) {
        if (document->dirty()
            && identityKey(workspaceForFile(document->fileName())) == identityKey(workspaceRoot))
            return true;
    }
    return false;
}

bool TabManager::closeTabsInWorkspace(
    const QString& workspaceRoot)
{
    if (workspaceRoot.isEmpty())
        return false;
    QList<MyCodeEditor*> pending;
    for (MyCodeEditor* editor : allEditors()) {
        const DocumentSnapshot snapshot =
            getDocumentForEditor(editor);
        if (identityKey(workspaceForFile(snapshot.fileName)) == identityKey(workspaceRoot)) {
            pending.append(editor);
        }
    }
    QList<SharedDocument*> documents;
    for (MyCodeEditor* editor : pending) {
        auto* document = sharedDocumentForEditor(editor);
        if (document && !documents.contains(document)) documents.append(document);
    }
    const auto auxiliary = auxiliaryViews();
    for (MyCodeEditor* editor : auxiliary) {
        auto* document = sharedDocumentForEditor(editor);
        if (document && identityKey(workspaceForFile(document->fileName())) == identityKey(workspaceRoot)
            && !documents.contains(document)) documents.append(document);
    }
    if (!resolvePendingDocuments(documents, nullptr)) return false;
    for (MyCodeEditor* editor : auxiliary) {
        if (documents.contains(sharedDocumentForEditor(editor)))
            closeAuxiliaryViewInternal(editor, true);
    }
    bool closed = true;
    for (MyCodeEditor* editor : pending) {
        setTabLocked(editor, false);
        closed = closeEditor(editor, false) && closed;
    }
    lastWorkspaceEditors.remove(identityKey(workspaceRoot));
    if (splitController) splitController->removeEmptyGroups();
    return closed;
}

bool TabManager::hasUnsavedChanges() const
{
    return documentQueries.hasUnsavedChanges();
}

bool TabManager::resolvePendingDocuments(
    QWidget* dialogParent)
{
    return resolvePendingDocuments(
        sharedDocuments
            ? sharedDocuments->documents()
            : QList<SharedDocument*>(),
        dialogParent);
}

bool TabManager::prepareWorkspacePathMutation(
    const QString& sourcePath,
    bool recursive,
    QWidget* dialogParent,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!sharedDocuments || sourcePath.isEmpty()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("The selected path is unavailable.");
        }
        return false;
    }

    QList<SharedDocument*> affectedDocuments;
    for (SharedDocument* document :
         sharedDocuments->documents()) {
        if (!document
            || !documentPathMatchesMutation(
                document->fileName(),
                sourcePath,
                recursive)) {
            continue;
        }
        for (MyCodeEditor* view : document->views()) {
            if (view && isTabLocked(view)) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "Unlock every tab backed by the selected path "
                        "before renaming or deleting it.");
                }
                return false;
            }
        }
        affectedDocuments.append(document);
    }

    if (!resolvePendingDocuments(
            affectedDocuments,
            dialogParent)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The file operation was cancelled because its open "
                "documents were not resolved.");
        }
        return false;
    }
    return true;
}

bool TabManager::finalizeWorkspacePathMutation(
    const QString& sourcePath,
    bool recursive,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!sharedDocuments || sourcePath.isEmpty())
        return true;

    QList<MyCodeEditor*> affectedEditors;
    for (SharedDocument* document :
         sharedDocuments->documents()) {
        if (!document
            || !documentPathMatchesMutation(
                document->fileName(),
                sourcePath,
                recursive)) {
            continue;
        }
        for (MyCodeEditor* view : document->views()) {
            if (!view)
                continue;
            if (isTabLocked(view)) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "A tab became locked while the path operation "
                        "was being applied.");
                }
                return false;
            }
            affectedEditors.append(view);
        }
    }

    closingBatch = true;
    bool closedAll = true;
    for (MyCodeEditor* editor :
         std::as_const(affectedEditors)) {
        if (!editor)
            continue;
        const bool closed = isAuxiliaryView(editor)
            ? closeAuxiliaryViewInternal(editor, true)
            : closeEditor(editor, false, false);
        closedAll = closed && closedAll;
    }
    closingBatch = false;
    if (splitController)
        splitController->removeEmptyGroups();
    if (!closedAll && failureReason) {
        *failureReason = QStringLiteral(
            "The file operation completed, but an affected editor "
            "view could not be closed.");
    }
    return closedAll;
}

UnsavedDocumentManager*
TabManager::unsavedDocumentManagerForTesting() const
{
    return unsavedDocumentManager.get();
}

QList<WorkspaceSessionTabState>
TabManager::workspaceSessionTabs(
    const QString& workspaceRoot) const
{
    QList<WorkspaceSessionTabState> states;
    if (workspaceRoot.isEmpty())
        return states;
    const QList<QTabWidget*> groups =
        splitController
        ? splitController->groups()
        : QList<QTabWidget*>{tabWidget};
    for (int groupPosition = 0;
         groupPosition < groups.size();
         ++groupPosition) {
        QTabWidget* group = groups.at(groupPosition);
        for (int index = 0;
             group && index < group->count();
             ++index) {
            MyCodeEditor* editor =
                qobject_cast<MyCodeEditor*>(
                    group->widget(index));
            if (!editor)
                continue;
            const DocumentSnapshot snapshot =
                getDocumentForEditor(editor);
            const QString filePath =
                lexicalPath(
                    snapshot.fileName);
            if (identityKey(workspaceForFile(filePath)) != identityKey(workspaceRoot)
                || !QFileInfo(filePath).isFile()
                || !fileIo.isSystemVerilogFile(
                    filePath)) {
                continue;
            }
            const QTextCursor cursor =
                editor->textCursor();
            WorkspaceSessionTabState state;
            state.filePath = filePath;
            state.cursorLine =
                cursor.blockNumber() + 1;
            state.cursorColumn =
                cursor.positionInBlock() + 1;
            state.verticalScrollValue =
                editor->verticalScrollBar()
                ? editor->verticalScrollBar()->value()
                : 0;
            state.horizontalScrollValue =
                editor->horizontalScrollBar()
                ? editor->horizontalScrollBar()->value()
                : 0;
            state.active =
                editor == getCurrentEditor()
                || (isTemporaryEditor(getCurrentEditor())
                    && lastWorkspaceEditors.value(identityKey(workspaceRoot)) == editor);
            state.viewId =
                editor->property(
                          "editorViewId")
                    .toString();
            state.groupIndex = groupPosition;
            state.tabIndex = index;
            state.locked = isTabLocked(editor);
            states.append(state);
        }
    }
    return states;
}

QStringList TabManager::restoreWorkspaceSessionTabs(
    const QString& workspaceRoot,
    const QList<WorkspaceSessionTabState>& tabs,
    QStringList* skippedFiles)
{
    QStringList restoredFiles;
    if (workspaceRoot.isEmpty())
        return restoredFiles;

    QPointer<MyCodeEditor> activeEditor;
    QSet<MyCodeEditor*> restoredViews;
    for (const WorkspaceSessionTabState& tab : tabs) {
        const QString filePath =
            lexicalPath(tab.filePath);
        if (identityKey(workspaceForFile(filePath)) != identityKey(workspaceRoot)
            || !QFileInfo(filePath).isFile()
            || !fileIo.isSystemVerilogFile(filePath)) {
            if (skippedFiles)
                skippedFiles->append(filePath);
            continue;
        }

        SharedDocument* document =
            sharedDocuments->documentForFile(filePath);
        if (!document)
            document = acquireFileDocument(filePath);
        if (!document) {
            if (skippedFiles)
                skippedFiles->append(filePath);
            continue;
        }
        MyCodeEditor* existing = nullptr;
        for (MyCodeEditor* view : document->views()) {
            if (!isAuxiliaryView(view) && !restoredViews.contains(view)
                && (tab.viewId.isEmpty() || view->property("editorViewId").toString() == tab.viewId)) {
                existing = view;
                break;
            }
        }
        if (existing) {
            restoredViews.insert(existing);
            restoredFiles.append(filePath);
            if (tab.active) activeEditor = existing;
            continue;
        }
        QTabWidget* group =
            ensureGroupIndex(tab.groupIndex);
        SharedDocumentViewState viewState;
        viewState.viewId = tab.viewId;
        MyCodeEditor* editor =
            createView(document, group, viewState);
        if (!editor) {
            if (skippedFiles)
                skippedFiles->append(filePath);
            continue;
        }
        restoredViews.insert(editor);

        QTextBlock block =
            editor->document()->findBlockByNumber(
                qMax(0, tab.cursorLine - 1));
        if (!block.isValid())
            block = editor->document()->lastBlock();
        if (block.isValid()) {
            const int column =
                qBound(0,
                       tab.cursorColumn - 1,
                       qMax(0, block.text().size()));
            QTextCursor cursor(block);
            cursor.setPosition(
                block.position() + column);
            editor->setTextCursor(cursor);
        }
        if (QScrollBar* bar =
                editor->verticalScrollBar()) {
            bar->setValue(
                qMax(0,
                     tab.verticalScrollValue));
        }
        if (QScrollBar* bar =
                editor->horizontalScrollBar()) {
            bar->setValue(
                qMax(0,
                     tab.horizontalScrollValue));
        }
        if (tab.locked)
            setTabLocked(editor, true);
        restoredFiles.append(filePath);
        if (tab.active)
            activeEditor = editor;
    }

    if (activeEditor) {
        QTabWidget* group =
            splitController->groupForPage(
                activeEditor);
        if (group) {
            splitController->setActiveGroup(group);
            group->setCurrentWidget(activeEditor);
        }
    }
    applyWorkspaceScope();
    return restoredFiles;
}

void TabManager::applyWorkspaceScope()
{
    if (!splitController)
        return;
    bool activeChanged = false;
    for (QTabWidget* group : splitController->groups()) {
        const int previousIndex =
            group->currentIndex();
        int firstVisibleIndex = -1;
        bool currentStillVisible = false;
        {
            const QSignalBlocker blocker(group);
            for (int index = 0;
                 index < group->count();
                 ++index) {
                MyCodeEditor* editor =
                    qobject_cast<MyCodeEditor*>(
                        group->widget(index));
                const bool visible =
                    editorVisibleInWorkspaceScope(
                        editor);
                group->setTabVisible(index, visible);
                if (visible
                    && firstVisibleIndex < 0) {
                    firstVisibleIndex = index;
                }
                if (visible
                    && index
                           == group->currentIndex()) {
                    currentStillVisible = true;
                }
            }
            if (!currentStillVisible
                && firstVisibleIndex >= 0) {
                group->setCurrentIndex(
                    firstVisibleIndex);
            }
        }
        activeChanged =
            activeChanged
            || group->currentIndex()
                   != previousIndex;
    }
    splitController->syncFloatingVisibility();
    if (activeChanged) {
        QTabWidget* group = activeTabWidget();
        handleCurrentTabChanged(
            group,
            group ? group->currentIndex() : -1);
    }
}

bool TabManager::editorVisibleInWorkspaceScope(
    MyCodeEditor* editor) const
{
    if (!editor
        || activeWorkspaceRoot.isEmpty()
        || scopedWorkspaceRoots.isEmpty()) {
        return true;
    }
    const QString fileName =
        documentModel
        ? documentModel
              ->documentForEditor(editor)
              .fileName
        : QString();
    if (fileName.isEmpty())
        return true;
    const QString owner = workspaceForFile(fileName);
    return owner.isEmpty() || identityKey(owner) == identityKey(activeWorkspaceRoot);
}

void TabManager::onTabCloseRequested(int index)
{
    closeTab(index);
}

void TabManager::onCurrentTabChanged(int index)
{
    handleCurrentTabChanged(
        activeTabWidget(),
        index);
}

bool TabManager::eventFilter(
    QObject* watched,
    QEvent* event)
{
    auto* editor =
        qobject_cast<MyCodeEditor*>(watched);
    if (editor
        && (event->type() == QEvent::FocusIn
            || event->type()
                   == QEvent::MouseButtonPress)
        && splitController) {
        QTabWidget* group =
            splitController->groupForPage(editor);
        if (group) {
            splitController->setActiveGroup(group);
            if (group->currentWidget() != editor)
                group->setCurrentWidget(editor);
        }
    }
    return QObject::eventFilter(watched, event);
}

QList<MyCodeEditor*> TabManager::allEditors() const
{
    QList<MyCodeEditor*> result;
    if (!splitController)
        return result;
    for (QTabWidget* group :
         splitController->groups()) {
        for (int index = 0;
             group && index < group->count();
             ++index) {
            if (MyCodeEditor* editor =
                    qobject_cast<MyCodeEditor*>(
                        group->widget(index))) {
                result.append(editor);
            }
        }
    }
    return result;
}

QList<MyCodeEditor*> TabManager::openEditors() const
{
    return allEditors();
}

QTabWidget* TabManager::activeTabWidget() const
{
    return splitController
        ? splitController->activeGroup()
        : tabWidget;
}

void TabManager::registerTabGroup(QTabWidget* group)
{
    if (!group
        || group->property(
                    "tabManagerConnectionsBound")
               .toBool()) {
        return;
    }
    group->setProperty(
        "tabManagerConnectionsBound",
        true);
    connect(group,
            &QTabWidget::currentChanged,
            this,
            [this, group](int index) {
                handleCurrentTabChanged(
                    group,
                    index);
            });
    if (QTabBar* bar = group->tabBar()) {
        connect(bar,
                &QTabBar::tabMoved,
                this,
                [this]() {
                    if (groupingTabs) return;
                    QTimer::singleShot(0, this, [this]() {
                        applyTabGrouping();
                        emit workspaceSessionStateChanged();
                    });
                });
    }
}

MyCodeEditor* TabManager::createView(
    SharedDocument* document,
    QTabWidget* group,
    const SharedDocumentViewState& state)
{
    if (!document || !group)
        return nullptr;

    MyCodeEditor* editorPointer = createBoundView(
        document, group, state, false);
    if (!editorPointer)
        return nullptr;

    const int index =
        group->addTab(editorPointer, QString());
    group->setCurrentIndex(index);
    splitController->setActiveGroup(group);
    // A newly opened document can introduce a basename conflict for tabs
    // that were already present. Refresh every title so both sides adopt the
    // same shortest unique suffix, then apply the active grouping once.
    updateAllTabTitles();
    emit tabCreated(editorPointer);
    return editorPointer;
}

MyCodeEditor* TabManager::createBoundView(
    SharedDocument* document,
    QWidget* parentWidget,
    const SharedDocumentViewState& state,
    bool auxiliary)
{
    if (!document || !parentWidget)
        return nullptr;
    std::unique_ptr<MyCodeEditor> editor(
        new MyCodeEditor(parentWidget));
    MyCodeEditor* editorPointer = editor.get();
    if (!bindEditorToDocument(
            editorPointer, document, state)) {
        return nullptr;
    }
    connect(editorPointer,
            &MyCodeEditor::fileNameChanged,
            this,
            [this, editorPointer](
                const QString& fileName) {
                SharedDocument* sharedDocument =
                    sharedDocumentForEditor(
                        editorPointer);
                if (!sharedDocument
                    || fileName.isEmpty()
                    || sameLexicalPath(
                        fileName,
                        sharedDocument->fileName())) {
                    return;
                }

                const QString previousFileName =
                    sharedDocument->fileName();
                if (!sharedDocuments
                    || !sharedDocuments
                            ->renameDocument(
                                sharedDocument,
                                fileName)) {
                    const QSignalBlocker blocker(
                        editorPointer);
                    editorPointer
                        ->setDocumentFileName(
                            previousFileName);
                    return;
                }

                for (MyCodeEditor* view :
                     sharedDocument->views()) {
                    view->setProperty(
                        "sharedDocumentId",
                        sharedDocument
                            ->documentId());
                    documentModel
                        ->refreshEditorState(view);
                }
                updateTitlesForDocument(
                    sharedDocument);
                if (externalDocumentSync) {
                    externalDocumentSync
                        ->trackDocument(
                            sharedDocument);
                }
                if (sharedDocument->dirty()) {
                    writeRecoverySnapshot(
                        sharedDocument,
                        true);
                }
                applyWorkspaceScope();
                emit workspaceSessionStateChanged();
            });
    if (!auxiliary) {
        connect(editorPointer,
                &QPlainTextEdit::cursorPositionChanged,
                this,
                &TabManager::workspaceSessionStateChanged);
        if (QScrollBar* bar =
                editorPointer->verticalScrollBar()) {
            connect(bar,
                    &QScrollBar::valueChanged,
                    this,
                    &TabManager::workspaceSessionStateChanged);
        }
        if (QScrollBar* bar =
                editorPointer->horizontalScrollBar()) {
            connect(bar,
                    &QScrollBar::valueChanged,
                    this,
                    &TabManager::workspaceSessionStateChanged);
        }
    }
    editorPointer->installEventFilter(this);
    editor.release();
    if (auxiliary) {
        auxiliaryEditors.insert(editorPointer);
        connect(editorPointer,
                &QObject::destroyed,
                this,
                [this, editorPointer]() {
                    auxiliaryEditors.remove(editorPointer);
                });
        emit auxiliaryViewCreated(editorPointer);
    }
    return editorPointer;
}

SharedDocument* TabManager::auxiliaryDocument(
    const QString& documentId,
    const QString& fileName)
{
    if (!sharedDocuments)
        return nullptr;
    if (!documentId.isEmpty()) {
        if (SharedDocument* document =
                sharedDocuments->documentById(documentId)) {
            return document;
        }
    }
    if (!fileName.isEmpty()) {
        if (SharedDocument* document =
                sharedDocuments->documentForFile(fileName)) {
            return document;
        }
        return acquireFileDocument(fileName);
    }
    return nullptr;
}

bool TabManager::bindEditorToDocument(
    MyCodeEditor* editor,
    SharedDocument* document,
    const SharedDocumentViewState& state)
{
    if (!editor || !document)
        return false;
    const QString viewId =
        document->attachView(editor, state);
    if (viewId.isEmpty())
        return false;
    editor->setProperty("editorViewId", viewId);
    editor->setProperty(
        "sharedDocumentId", document->documentId());
    editor->acceptLoadedTextAsSemanticBaseline();
    editor->setHierarchyInstanceContext(
        unboundTabInstanceContext(workspaceForFile(document->fileName())));
    editor->setProperty("standaloneDocument", workspaceForFile(document->fileName()).isEmpty());
    documentModel->registerEditor(
        editor, document->fileName());
    observeDocument(document);
    return true;
}

SharedDocument* TabManager::acquireFileDocument(
    const QString& fileName,
    bool* loaded)
{
    if (loaded)
        *loaded = false;
    if (!sharedDocuments)
        return nullptr;
    if (SharedDocument* existing =
            sharedDocuments->documentForFile(
                fileName)) {
        if (loaded)
            *loaded = true;
        return existing;
    }
    QString text;
    if (!fileIo.readTextFile(
            qobject_cast<QWidget*>(parent()),
            fileName,
            &text)) {
        return nullptr;
    }
    SharedDocument* document =
        sharedDocuments->acquire(
            fileName,
            text);
    if (document) {
        document->setReadOnly(
            !QFileInfo(
                 document->fileName())
                 .isWritable());
        observeDocument(document);
        if (loaded)
            *loaded = true;
    }
    return document;
}

bool TabManager::saveEditor(
    MyCodeEditor* editor,
    bool forceSaveAs,
    const QString& explicitFileName)
{
    SharedDocument* document =
        sharedDocumentForEditor(editor);
    if (!editor || !document)
        return false;
    if (document->readOnly() && !forceSaveAs)
        forceSaveAs = true;
    if (!forceSaveAs
        && explicitFileName.isEmpty()
        && !document->fileName().isEmpty()
        && !document->dirty()
        && document->textRevision()
               == document->savedTextRevision()
        && document->externalState()
               == SharedDocumentExternalState::Current
        && QFileInfo::exists(document->fileName())) {
        return true;
    }
    const QString fileName =
        explicitFileName.isEmpty()
        ? fileIo.resolveSaveFileName(
              qobject_cast<QWidget*>(parent()),
              document->fileName(),
              forceSaveAs)
        : explicitFileName;
    if (fileName.isEmpty())
        return false;
    SharedDocument* conflictingDocument =
        sharedDocuments
        ? sharedDocuments->documentForFile(
              fileName)
        : nullptr;
    if (conflictingDocument
        && conflictingDocument != document) {
        return false;
    }
    const bool overwritesCurrentSource =
        !document->fileName().isEmpty()
        && EditorFileIdentity::same(
            fileName,
            document->fileName());
    QString overwriteFailure;
    // Validate immediately before the first atomic attempt; recovery from a
    // Windows rename conflict must validate again before any later attempt.
    if (overwritesCurrentSource
        && externalDocumentSync
        && !externalDocumentSync->canOverwriteDocument(
            document,
            &overwriteFailure)) {
        writeRecoverySnapshot(document, true);
        emit fileSaveFailed(
            fileName,
            overwriteFailure.isEmpty()
                ? QStringLiteral(
                      "The source changed externally; resolve the conflict or use Save As.")
                : overwriteFailure);
        return false;
    }
    QString saveFailure;
    const QString& savedText = editor->cachedDocumentText();
    QByteArray savedRawFingerprint;
    QByteArray savedLogicalFingerprint;
    std::function<bool(QString*)> revalidateOverwrite;
    if (overwritesCurrentSource && externalDocumentSync) {
        revalidateOverwrite = [this, document](QString* reason) {
            return externalDocumentSync->canOverwriteDocument(document, reason);
        };
    }
    if (!fileIo.writeTextFile(
            qobject_cast<QWidget*>(parent()),
            fileName,
            savedText,
            &saveFailure,
            &savedRawFingerprint,
            &savedLogicalFingerprint,
            revalidateOverwrite)) {
        writeRecoverySnapshot(document, true);
        emit fileSaveFailed(
            fileName,
            saveFailure.isEmpty()
                ? QStringLiteral("The file could not be saved.")
                : saveFailure);
        return false;
    }
    const bool renamedDocument = !sameLexicalPath(
        fileName,
        document->fileName());
    if (renamedDocument) {
        if (!sharedDocuments->renameDocument(
                document,
                fileName)) {
            return false;
        }
        for (MyCodeEditor* view :
             document->views()) {
            documentModel->refreshEditorState(view);
            view->setProperty(
                "sharedDocumentId",
                document->documentId());
        }
    }
    documentModel->markSaved(editor);
    document->setReadOnly(
        !QFileInfo(fileName).isWritable());
    document->setExternalState(
        SharedDocumentExternalState::Current);
    const QDateTime savedModifiedUtc =
        QFileInfo(fileName).lastModified().toUTC();
    document->markSaved(savedRawFingerprint,
                        savedModifiedUtc);
    clearRecoverySnapshot(document, true);
    if (externalDocumentSync)
        externalDocumentSync->noteDocumentSaved(
            document, savedLogicalFingerprint);
    updateTitlesForDocument(document);
    if (renamedDocument)
        applyWorkspaceScope();
    emit fileSaved(document->fileName());
    return true;
}

bool TabManager::confirmCloseDocument(
    SharedDocument* document,
    QString* savedFileName)
{
    if (savedFileName)
        savedFileName->clear();
    if (!document)
        return true;
    const bool resolved = resolvePendingDocuments(
        {document},
        qobject_cast<QWidget*>(parent()));
    if (resolved && savedFileName && !document->dirty())
        *savedFileName = document->fileName();
    return resolved;
}

bool TabManager::resolvePendingDocuments(
    const QList<SharedDocument*>& documents,
    QWidget* dialogParent)
{
    if (!unsavedDocumentManager)
        return documents.isEmpty();
    for (SharedDocument* document : documents) {
        if (document && document->dirty())
            writeRecoverySnapshot(document, true);
    }
    return unsavedDocumentManager->resolve(
        documents,
        dialogParent
            ? dialogParent
            : qobject_cast<QWidget*>(parent()),
        [this](SharedDocument* document) {
            if (!document || document->views().isEmpty())
                return false;
            return saveEditor(document->views().first(), false);
        });
}

bool TabManager::closeEditor(
    MyCodeEditor* editor,
    bool confirmUnsaved,
    bool remember)
{
    if (!editor || isTabLocked(editor))
        return false;
    SharedDocument* document =
        sharedDocumentForEditor(editor);
    if (!document)
        return false;
    if (confirmUnsaved
        && document->viewCount() == 1
        && !confirmCloseDocument(document)) {
        return false;
    }

    QTabWidget* group =
        splitController->groupForPage(editor);
    const int index =
        group ? group->indexOf(editor) : -1;
    if (!group || index < 0)
        return false;

    if (remember) {
        ClosedTabState closed;
        closed.fileName = document->fileName();
        closed.text =
            document->textDocument()->toPlainText();
        closed.viewState =
            document->viewState(editor);
        closed.groupIndex =
            groupIndex(group);
        closed.locked = isTabLocked(editor);
        recentlyClosedTabs.append(closed);
        while (recentlyClosedTabs.size() > 20)
            recentlyClosedTabs.removeFirst();
    }

    const QString fileName =
        document->fileName();
    const QString viewId =
        editor->property(
                  "editorViewId")
            .toString();
    editor->exitInteractionModes(
        EditorModeExitReason::DocumentClosed);
    editor->closeSemanticPopup();
    documentModel->unregisterEditor(editor);
    group->removeTab(index);
    // Removing a focused editor delivers a synchronous focus-out event.
    // Keep its original QTextDocument alive until that event has completed;
    // rebinding first can leave QWidgetTextControl evaluating a cursor that
    // belongs to the previous document.
    document->detachView(editor, true);
    lockedViewIds.remove(viewId);
    editor->deleteLater();
    if (document->viewCount() == 0)
        clearRecoverySnapshot(document, false);
    sharedDocuments->releaseIfUnused(document);
    splitController->removeEmptyGroups();
    applyWorkspaceScope();
    updateAllTabTitles();
    emit tabClosed(fileName);
    emit workspaceSessionStateChanged();
    return true;
}

bool TabManager::closePage(QTabWidget* group, const int index)
{
    if (!group || index < 0 || index >= group->count())
        return false;
    QWidget* page = group->widget(index);
    if (auto* editor = qobject_cast<MyCodeEditor*>(page))
        return closeEditor(editor);
    if (!page || !page->property("toolPage").toBool())
        return false;

    const QString stableId = page->property("toolPageId").toString();
    if (!page->close())
        return false;
    group->removeTab(index);
    page->deleteLater();
    splitController->removeEmptyGroups();
    emit toolPageClosed(stableId);
    emit workspaceSessionStateChanged();
    return true;
}

void TabManager::observeDocument(
    SharedDocument* document)
{
    if (!document || observedDocuments.contains(document))
        return;
    observedDocuments.insert(document);
    if (externalDocumentSync)
        externalDocumentSync->trackDocument(document);
    connect(document,
            &SharedDocument::statusChanged,
            this,
            [this, document]() {
                updateTitlesForDocument(document);
            });
    connect(document,
            &SharedDocument::textRevisionChanged,
            this,
            [this, document](std::uint64_t) {
                if (document->dirty()) {
                    writeRecoverySnapshot(
                        document,
                        false);
                }
            });
    connect(document,
            &SharedDocument::dirtyChanged,
            this,
            [this, document](bool dirty) {
                updateTitlesForDocument(document);
                if (dirty) {
                    writeRecoverySnapshot(
                        document,
                        false);
                } else if (
                    !explicitExternalReloadDocuments
                         .contains(document)) {
                    clearRecoverySnapshot(
                        document,
                        true,
                        false);
                }
            });
    connect(document,
            &SharedDocument::identityChanged,
            this,
            [this, document](const QString& previousDocumentId,
                   const QString& previousFileName,
                   const QString& documentId,
                   const QString& fileName) {
                emit documentIdentityChanged(
                    previousDocumentId,
                    previousFileName,
                    documentId,
                    fileName);
                for (MyCodeEditor* view : document->views()) {
                    const QString owner = workspaceForFile(fileName);
                    view->setProperty("standaloneDocument", owner.isEmpty());
                    view->setHierarchyInstanceContext(unboundTabInstanceContext(owner));
                }
            });
    connect(document,
            &QObject::destroyed,
            this,
            [this, document]() {
                observedDocuments.remove(document);
                explicitExternalReloadDocuments.remove(
                    document);
                recoveryDocumentStates.remove(
                    document);
            });
}

void TabManager::updateTitlesForDocument(
    SharedDocument* document)
{
    if (!document)
        return;
    for (MyCodeEditor* editor : document->views())
        updateTabTitle(editor);
    applyTabGrouping();
}

void TabManager::updateAllTabTitles()
{
    for (MyCodeEditor* editor : allEditors())
        updateTabTitle(editor);
    applyTabGrouping();
}

QString TabManager::shortestDistinctTitle(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return QStringLiteral("untitled");
    const QStringList components =
        pathComponents(fileName);
    if (components.isEmpty())
        return fileName;
    QList<QStringList> conflicts;
    for (SharedDocument* document :
         sharedDocuments->documents()) {
        if (!document
            || document->fileName().isEmpty()
            || identityKey(document->fileName())
                   == identityKey(fileName)
            || QFileInfo(document->fileName())
                       .fileName()
                       .compare(
                           QFileInfo(fileName)
                               .fileName(),
                           Qt::CaseInsensitive)
                   != 0) {
            continue;
        }
        conflicts.append(
            pathComponents(
                document->fileName()));
    }
    if (conflicts.isEmpty())
        return components.last();
    for (int depth = 2;
         depth <= components.size();
         ++depth) {
        const QString candidate =
            suffixPath(components, depth);
        bool unique = true;
        for (const QStringList& other :
             std::as_const(conflicts)) {
            if (suffixPath(other, depth)
                    .compare(
                        candidate,
                        Qt::CaseInsensitive)
                == 0) {
                unique = false;
                break;
            }
        }
        if (unique)
            return candidate;
    }
    return QDir::cleanPath(
        QDir::fromNativeSeparators(fileName));
}

QString TabManager::tabTitleForEditor(
    MyCodeEditor* editor) const
{
    SharedDocument* document =
        sharedDocumentForEditor(editor);
    if (!document)
        return QStringLiteral("untitled");
    QString title =
        shortestDistinctTitle(
            document->fileName());
    const QList<MyCodeEditor*> views =
        document->views();
    if (views.size() > 1) {
        title += QStringLiteral(" ·%1")
                     .arg(views.indexOf(editor) + 1);
    }
    const QString groupingKey =
        groupingKeyForEditor(editor);
    if (!groupingKey.isEmpty())
        title = groupingKey + QStringLiteral(" • ") + title;
    if (isTemporaryEditor(editor))
        title = QStringLiteral("TEMP  ") + title;

    QStringList markers;
    if (document->dirty())
        markers.append(QStringLiteral("●"));
    if (document->externalState()
        == SharedDocumentExternalState::ExternallyModified) {
        markers.append(QStringLiteral("↻"));
    } else if (document->externalState()
               == SharedDocumentExternalState::Conflict) {
        markers.append(QStringLiteral("⚠"));
    }
    if (document->readOnly())
        markers.append(QStringLiteral("RO"));
    if (isTabLocked(editor))
        markers.append(QStringLiteral("🔒"));
    if (!markers.isEmpty())
        title = markers.join(QLatin1Char(' '))
            + QLatin1Char(' ') + title;
    return title;
}

QString TabManager::tabToolTipForEditor(
    MyCodeEditor* editor) const
{
    SharedDocument* document =
        sharedDocumentForEditor(editor);
    if (!document)
        return QStringLiteral("Untitled document");
    documentModel->refreshEditorState(editor);
    const DocumentSnapshot snapshot =
        documentModel->documentMetadataForEditor(
            editor);
    const QString workspace =
        workspaceRootForFile(
            document->fileName(),
            scopedWorkspaceRoots);
    QStringList rows;
    rows.append(
        document->fileName().isEmpty()
            ? QStringLiteral("Untitled document")
            : QDir::toNativeSeparators(
                  document->fileName()));
    rows.append(
        QStringLiteral("Module: %1")
            .arg(snapshot.currentModuleName.isEmpty()
                     ? QStringLiteral("—")
                     : snapshot.currentModuleName));
    rows.append(
        QStringLiteral("Workspace: %1")
            .arg(workspace.isEmpty()
                     ? QStringLiteral("—")
                     : QDir::toNativeSeparators(
                           workspace)));
    if (workspace.isEmpty())
        rows.append(tr("TEMP — outside all open workspaces; saves to the original file"));
    return rows.join(QLatin1Char('\n'));
}

QString TabManager::groupingKeyForEditor(
    MyCodeEditor* editor) const
{
    if (!editor || groupingMode == TabGroupingMode::None)
        return QString();
    const DocumentSnapshot snapshot =
        documentModel->documentMetadataForEditor(
            editor);
    if (groupingMode == TabGroupingMode::Module) {
        return snapshot.currentModuleName.isEmpty()
            ? QStringLiteral("(no module)")
            : snapshot.currentModuleName;
    }
    const QString workspace =
        workspaceRootForFile(
            snapshot.fileName,
            scopedWorkspaceRoots);
    return workspace.isEmpty()
        ? QStringLiteral("(external)")
        : QFileInfo(workspace).fileName();
}

void TabManager::applyTabGrouping()
{
    if (!splitController || groupingTabs) {
        return;
    }
    const QScopedValueRollback<bool> groupingGuard(groupingTabs, true);
    for (QTabWidget* group :
         splitController->groups()) {
        QList<MyCodeEditor*> ordered;
        for (int index = 0;
             index < group->count();
             ++index) {
            if (MyCodeEditor* editor =
                    qobject_cast<MyCodeEditor*>(
                        group->widget(index))) {
                ordered.append(editor);
            }
        }
        std::stable_sort(
            ordered.begin(),
            ordered.end(),
            [this](MyCodeEditor* left,
                   MyCodeEditor* right) {
                const bool leftTemporary = isTemporaryEditor(left);
                const bool rightTemporary = isTemporaryEditor(right);
                if (leftTemporary != rightTemporary) return !leftTemporary;
                if (groupingMode == TabGroupingMode::None) return false;
                return groupingKeyForEditor(left)
                    .compare(
                        groupingKeyForEditor(right),
                        Qt::CaseInsensitive)
                    < 0;
            });
        const QSignalBlocker blocker(group);
        for (int target = 0;
             target < ordered.size();
             ++target) {
            const int source =
                group->indexOf(
                    ordered.at(target));
            if (source != target)
                group->tabBar()->moveTab(
                    source,
                    target);
        }
        int firstTemporary = -1;
        bool hasWorkspaceTab = false;
        for (int index = 0; index < group->count(); ++index) {
            if (!group->isTabVisible(index)) continue;
            auto* editor = qobject_cast<MyCodeEditor*>(group->widget(index));
            if (!editor) continue;
            if (isTemporaryEditor(editor)) {
                if (firstTemporary < 0 && hasWorkspaceTab) firstTemporary = index;
            } else hasWorkspaceTab = true;
        }
        group->tabBar()->setProperty("temporaryTabBoundary", firstTemporary);
        group->tabBar()->update();
    }
}

void TabManager::handleCurrentTabChanged(
    QTabWidget* group,
    int index)
{
    if (!group)
        return;
    if (previousActiveEditor) {
        SharedDocument* previousDocument =
            sharedDocumentForEditor(
                previousActiveEditor);
        if (previousDocument
            && previousDocument->dirty()) {
            writeRecoverySnapshot(
                previousDocument,
                true);
        }
    }
    if (splitController
        && splitController->activeGroup()
               != group) {
        splitController->setActiveGroup(group);
    }
    MyCodeEditor* editor =
        qobject_cast<MyCodeEditor*>(
            group->widget(index));
    if (previousActiveEditor
        && previousActiveEditor != editor) {
        previousActiveEditor
            ->exitInteractionModes(
                EditorModeExitReason::TabChanged);
    }
    previousActiveEditor = editor;
    if (editor) {
        if (!changingWorkspaceScope && !isTemporaryEditor(editor)) {
            const QString owner = workspaceForFile(getDocumentForEditor(editor).fileName);
            lastWorkspaceEditors.insert(identityKey(owner), editor);
        }
        editor->refreshSemanticPresentation();
        documentModel->refreshEditorState(editor);
        updateTabTitle(editor);
        const DocumentSnapshot snapshot =
            getDocumentForEditor(editor);
        emit activeTabChanged(editor);
        emit activeDocumentChanged(snapshot);
    } else {
        emit activeTabChanged(nullptr);
        emit activeDocumentChanged(
            DocumentSnapshot());
    }
    emit workspaceSessionStateChanged();
}

void TabManager::setRegisteredTabActionRequestHandler(
    RegisteredActionRequestHandler handler)
{
    registeredTabActionRequestHandler =
        std::move(handler);
}

bool TabManager::requestTabAction(
    const QString& actionId,
    QTabWidget* group,
    int index,
    QString* failureReason)
{
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::TabContextMenu)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unknown editor Tab Action: %1")
                .arg(actionId);
        }
        return false;
    }
    if (group && splitController)
        splitController->setActiveGroup(group);
    if (group && index >= 0
        && index < group->count()) {
        group->setCurrentIndex(index);
    }
    if (registeredTabActionRequestHandler) {
        return registeredTabActionRequestHandler(
            actionId, failureReason);
    }
    return executeRegisteredTabAction(
        actionId, failureReason);
}

void TabManager::handleTabAction(
    const QString& actionId,
    QTabWidget* group,
    int index)
{
    requestTabAction(actionId, group, index);
}

bool TabManager::executeRegisteredTabAction(
    const QString& actionId,
    QString* failureReason)
{
    const auto fail =
        [failureReason](const QString& reason) {
            if (failureReason)
                *failureReason = reason;
            return false;
        };
    const auto succeed =
        [failureReason]() {
            if (failureReason)
                failureReason->clear();
            return true;
        };
    MyCodeEditor* editor = getCurrentEditor();

    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabClose)) {
        if (!editor)
            return fail(QStringLiteral("No editor tab is selected"));
        if (isTabLocked(editor))
            return fail(QStringLiteral("The selected tab is locked"));
        return closeEditor(editor)
            ? succeed()
            : fail(QStringLiteral("The selected tab was not closed"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabCloseOthers)) {
        return closeOtherTabs()
            ? succeed()
            : fail(QStringLiteral("Other tabs were not closed"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabCloseRight)) {
        return closeTabsToRight()
            ? succeed()
            : fail(QStringLiteral("Tabs to the right were not closed"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabCloseAll)) {
        if (editorCount() <= 0)
            return fail(QStringLiteral("No editor tabs are open"));
        return closeAllTabs()
            ? succeed()
            : fail(QStringLiteral("All tabs were not closed"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewReopenClosedTab)) {
        return reopenClosedTab()
            ? succeed()
            : fail(QStringLiteral("No recently closed tab is available"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabDuplicate)) {
        return duplicateCurrentView()
            ? succeed()
            : fail(QStringLiteral("The selected view could not be duplicated"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorSplitLeft)) {
        return splitCurrentView(EditorSplitDirection::Left)
            ? succeed()
            : fail(QStringLiteral("The editor could not split left"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorSplitRight)) {
        return splitCurrentView(EditorSplitDirection::Right)
            ? succeed()
            : fail(QStringLiteral("The editor could not split right"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorSplitAbove)) {
        return splitCurrentView(EditorSplitDirection::Above)
            ? succeed()
            : fail(QStringLiteral("The editor could not split above"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorSplitBelow)) {
        return splitCurrentView(EditorSplitDirection::Below)
            ? succeed()
            : fail(QStringLiteral("The editor could not split below"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorSplitMerge)) {
        return mergeCurrentSplit()
            ? succeed()
            : fail(QStringLiteral("The current split could not be merged"));
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewEditorTabToggleLocked)) {
        if (!editor)
            return fail(QStringLiteral("No editor tab is selected"));
        return setTabLocked(editor, !isTabLocked(editor))
            ? succeed()
            : fail(QStringLiteral("The selected tab lock did not change"));
    }
    return fail(QStringLiteral(
        "Unsupported editor Tab Action: %1")
        .arg(actionId));
}

bool TabManager::closeEditorsAtomically(
    const QList<MyCodeEditor*>& editors)
{
    if (editors.isEmpty())
        return true;
    QSet<SharedDocument*> closingDocuments;
    QHash<SharedDocument*, int> closingViewCounts;
    for (MyCodeEditor* editor : editors) {
        if (!editor || isTabLocked(editor))
            continue;
        SharedDocument* document =
            sharedDocumentForEditor(editor);
        if (!document)
            continue;
        closingDocuments.insert(document);
        closingViewCounts[document] += 1;
    }
    QList<SharedDocument*> pendingDocuments;
    for (SharedDocument* document :
         std::as_const(closingDocuments)) {
        if (document
            && closingViewCounts.value(document)
                   >= document->viewCount()) {
            pendingDocuments.append(document);
        }
    }
    if (!resolvePendingDocuments(
            pendingDocuments,
            qobject_cast<QWidget*>(parent()))) {
        return false;
    }
    closingBatch = true;
    bool closedAll = true;
    for (MyCodeEditor* editor : editors) {
        if (editor && !isTabLocked(editor)) {
            closedAll =
                closeEditor(editor, false)
                && closedAll;
        }
    }
    closingBatch = false;
    if (splitController)
        splitController->removeEmptyGroups();
    return closedAll;
}

int TabManager::groupIndex(QTabWidget* group) const
{
    return splitController
        ? splitController->groups().indexOf(group)
        : 0;
}

QTabWidget* TabManager::ensureGroupIndex(int index)
{
    if (!splitController)
        return tabWidget;
    const int targetIndex = qMax(0, index);
    while (splitController->groupCount()
           <= targetIndex) {
        const QList<QTabWidget*> groups =
            splitController->groups();
        QTabWidget* source =
            groups.isEmpty()
            ? tabWidget
            : groups.last();
        if (splitController->initialGroup())
            source = splitController->initialGroup();
        if (!splitController->createSplit(
                source,
                EditorSplitDirection::Right)) {
            break;
        }
    }
    const QList<QTabWidget*> groups =
        splitController->groups();
    return groups.isEmpty()
        ? tabWidget
        : groups.at(
              qBound(0,
                     targetIndex,
                     groups.size() - 1));
}
