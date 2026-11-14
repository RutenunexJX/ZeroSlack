#include "tabmanager.h"
#include "documentreferenceservice.h"
#include "documentblocktransfer.h"
#include "retireddocument.h"
#include "associatedblockpage.h"
#include "associatedblockdragcontroller.h"
#include <QSplitter>
#include "documentblockstore.h"
#include "documenttemplatemodel.h"
#include "workspacesessionstateservice.h"

#include "actionregistry.h"
#include "documentmodel.h"
#include "documentrecoveryqueue.h"
#include "editorfileidentity.h"
#include "uicontrols.h"
#include "uidialogs.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
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
QJsonObject editorRecoveryEnvelope(const QString& text) {
    if (!text.startsWith(QStringLiteral("{\"association"))) return {};
    const auto object = QJsonDocument::fromJson(text.toUtf8()).object();
    return object.value("format") == "zeroslack.editor-recovery" && object.value("version").toInt() == 1
        && object.value("associations").isObject() && object.value("text").isString() ? object : QJsonObject();
}
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
    blockDragController = std::make_unique<AssociatedBlockDragController>(this);
    connect(blockDragController.get(),&AssociatedBlockDragController::gestureFinished,this,[this] {
        const auto documents=sharedDocuments->documents();
        for(auto* document:documents) reconcileAssociatedBlocks(document);
    });

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
    connect(splitController.get(), &EditorSplitController::pageActivated, this,
            [this](QWidget*) {
                auto* group=splitController->activeGroup();
                handleCurrentTabChanged(group,group ? group->currentIndex() : -1);
            });
    connect(splitController.get(),
            &EditorSplitController::layoutChanged,
            this,
            &TabManager::splitLayoutChanged);
    connect(splitController.get(), &EditorSplitController::pageMoved, this,
            [this](QWidget* page) {
                applyWorkspaceScope(); applyTabGrouping();
                const auto state = UiControls::tabPresentation(page);
                if (state.valid()) UiControls::setTabPresentation(splitController->groupForPage(page), page, state);
            });
    connect(splitController.get(),
            &EditorSplitController::layoutChanged,
            this,
            &TabManager::workspaceSessionStateChanged);

    previousActiveEditor = getCurrentEditor();
    lastActionEditor = previousActiveEditor;
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget*,QWidget* now) {
        if (!now || !now->isVisibleTo(now->window())) return;
        for (auto* widget=now; widget; widget=widget->parentWidget()) {
            if (auto* editor=qobject_cast<MyCodeEditor*>(widget); editor && sharedDocumentForEditor(editor)) {
                lastActionEditor=editor; setContextPage(editor); return;
            }
        }
        for (auto* group : splitController->groups()) {
            auto* page=group->currentWidget();
            if (page && (page==now || page->isAncestorOf(now))) {
                lastActionEditor.clear();
                if (validContextPage(page)) setContextPage(page);
                return;
            }
        }
    });
    QTimer::singleShot(0, this, [this] {
        scanCrashRecoveryCandidates(temporaryRecoveryWorkspace());
    });
}

TabManager::~TabManager()
{
    if (!recoveryClosing) {
        checkpointCrashRecovery();
        flushCrashRecovery();
    }
    recoveryQueue.reset();
    recoveryClosing = true;
    blockDragController.reset();
    for (auto* page : associatedBlockPages()) { splitController->unregisterPage(page); delete page; }
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
    recoveryQueue.reset();
    crashRecoveryService =
        service
        ? std::move(service)
        : std::make_unique<CrashRecoveryService>();
    recoveryDocumentStates.clear();
    recoveryClosing = false;
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
                document->documentId(),
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
            document->documentId();
    }
    return key;
}

bool TabManager::writeRecoverySnapshot(SharedDocument* document, bool force)
{
    if (recoveryClosing || !crashRecoveryService || !document || !document->dirty() || !document->textDocument())
        return false;
    auto& state = recoveryDocumentStates[document];
    const auto revision = static_cast<quint64>(document->textRevision());
    auto* associationModel = DocumentBlockModel::get(document->textDocument());
    const auto associationRevision = associationModel->revision();
    const auto key = recoveryKeyForDocument(document);
    const auto baseline = document->savedBaselineSha256();
    if (state.discarded && state.snapshotRevision == revision && state.snapshotAssociationRevision == associationRevision
        && sameRecoveryDocumentKey(state.key, key) && state.baseline == baseline) return true;
    const bool sameInput = state.hasSnapshot && sameRecoveryDocumentKey(state.key, key) && state.baseline == baseline;
    if (sameInput && !state.retryNeeded && state.snapshotAssociationRevision == associationRevision
        && (revision == state.snapshotRevision || (!force && revision < state.snapshotRevision + kCrashRecoveryRevisionInterval)))
        return true;
    if (!recoveryQueue) {
        recoveryQueue = std::make_unique<DocumentRecoveryQueue>(*crashRecoveryService,
            [this](QObject* owner, const auto& request, const auto& result) { completeRecoverySnapshot(owner, request, result); }, this);
    }
    if (state.hasSnapshot && !sameRecoveryDocumentKey(state.key, key)) {
        const auto remember = [&](const CrashRecoveryDocumentKey& old) {
            if (std::none_of(state.obsoleteKeys.cbegin(), state.obsoleteKeys.cend(),
                [&](const auto& existing) { return sameRecoveryDocumentKey(existing, old); }))
                state.obsoleteKeys.append(old);
        };
        remember(state.key);
        const auto cancelledKeys = recoveryQueue->cancel(document);
        for (const auto& old : cancelledKeys)
            remember(old);
    }
    CrashRecoverySnapshotRequest request;
    request.document = key;
    QElapsedTimer capture;
    capture.start();
    request.text = document->textDocument()->toPlainText();
    if (DocumentBlockStore::get(associationModel)->needed()) {
        QString issue; const auto associations = DocumentBlockStore::get(associationModel)->recoveryState(&issue,crashRecoveryService->recoveryRootPath());
        if (!issue.isEmpty()) { emit crashRecoveryOperationFailed(document->documentId(),issue); return false; }
        request.text = QString::fromUtf8(QJsonDocument(QJsonObject{{"format","zeroslack.editor-recovery"},{"version",1},
            {"text",request.text},{"associations",associations},{"associationRevision",QString::number(associationRevision)}}).toJson(QJsonDocument::Compact));
    }
    recoveryCaptureNs += capture.nsecsElapsed();
    ++recoveryCaptures;
    recoveryCaptureCharacters += request.text.size();
    request.documentRevision = revision;
    request.savedBaselineSha256 = baseline;
    request.savedBaselineModifiedUtc = document->savedBaselineModifiedUtc();
    state.key = key;
    state.baseline = baseline;
    state.snapshotRevision = revision;
    state.snapshotAssociationRevision = associationRevision;
    state.hasSnapshot = true;
    state.retryNeeded = false;
    state.discarded = false;
    recoveryQueue->enqueue(document, std::move(request));
    return true;
}

void TabManager::completeRecoverySnapshot(QObject* owner, const CrashRecoverySnapshotRequest& request,
                                         const CrashRecoveryWriteResult& result)
{
    if (auto* page = qobject_cast<WebDocumentSession*>(owner)) {
        if (!result.succeeded()) emit crashRecoveryOperationFailed(page->documentId(), result.reason);
        return;
    }
    auto* document = qobject_cast<SharedDocument*>(owner);
    auto found = recoveryDocumentStates.find(document);
    const auto associationRevision = editorRecoveryEnvelope(request.text).value("associationRevision").toString().toULongLong();
    if (!document || found == recoveryDocumentStates.end()
        || !sameRecoveryDocumentKey(found->key, request.document)) return;
    if (result.status == CrashRecoveryStatus::Success) {
        if (found->snapshotRevision == request.documentRevision && found->snapshotAssociationRevision == associationRevision) found->retryNeeded = false;
        QList<CrashRecoveryDocumentKey> failed;
        for (const auto& key : std::as_const(found->obsoleteKeys)) {
            if (sameRecoveryDocumentKey(key, request.document)) continue;
            const auto cleanup = crashRecoveryService->clearAfterNormalClose(key);
            if (!cleanup.succeeded()) {
                failed.append(key);
                emit crashRecoveryOperationFailed(document->documentId(), cleanup.reason);
            }
        }
        found->obsoleteKeys = std::move(failed);
        QList<CrashRecoveryCandidate> retainedClaims;
        for (const auto& candidate : std::as_const(found->claimedRecords)) {
            // Retire an abandoned generation only after our replacement is
            // durably committed. Failed writes keep the original recoverable.
            if (candidate.recoveryId == result.recoveryId) continue;
            const auto cleanup = crashRecoveryService->discard(candidate.workspacePath, candidate.recoveryId);
            if (!cleanup.succeeded()) retainedClaims.append(candidate);
        }
        found->claimedRecords = std::move(retainedClaims);
        return;
    }
    if (result.status == CrashRecoveryStatus::StaleRecord
        && DocumentBlockModel::get(document->textDocument())->revision() == associationRevision
        && document->textRevision() == request.documentRevision
        && document->savedBaselineSha256() == request.savedBaselineSha256) {
        clearRecoverySnapshot(document, true);
        return;
    }
    if (found->snapshotRevision == request.documentRevision && found->snapshotAssociationRevision == associationRevision) found->retryNeeded = true;
    emit crashRecoveryOperationFailed(document->documentId(), result.reason);
}

void TabManager::flushCrashRecovery()
{
    if (recoveryQueue) recoveryQueue->flush();
}

QVariantMap TabManager::crashRecoveryMetricsForTesting() const
{
    auto result = recoveryQueue ? recoveryQueue->metrics() : QVariantMap{};
    result.insert("captures", qulonglong(recoveryCaptures));
    result.insert("captureNs", recoveryCaptureNs);
    result.insert("capturedCharacters", qulonglong(recoveryCaptureCharacters));
    return result;
}

void TabManager::clearRecoverySnapshot(
    SharedDocument* document,
    bool normalSave,
    bool includeUntrackedCurrent)
{
    if (!crashRecoveryService || !document)
        return;

    QList<CrashRecoveryDocumentKey> keys = recoveryQueue
        ? recoveryQueue->cancel(document) : QList<CrashRecoveryDocumentKey>{};
    const RecoveryDocumentState state =
        recoveryDocumentStates.value(document);
    keys.append(state.obsoleteKeys);
    if (state.hasSnapshot)
        keys.append(state.key);
    if (keys.isEmpty() && state.claimedRecords.isEmpty()
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
    for (const auto& candidate : state.claimedRecords) {
        const auto cleanup = crashRecoveryService->discard(candidate.workspacePath, candidate.recoveryId);
        if (!cleanup.succeeded()) {
            allCleared = false;
            emit crashRecoveryOperationFailed(document->documentId(), cleanup.reason);
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
    auto result = crashRecoveryService->readComparison(
        recoveryWorkspacePath(workspaceRoot),
        recoveryId);
    if (result.succeeded() && (WebDocumentPage::supportsFile(result.candidate.originalFilePath) || result.candidate.untitledDocumentId.startsWith(QStringLiteral("web:")))) {
        const auto record = QJsonDocument::fromJson(result.recoveredText.toUtf8()).object();
        if (record.value("format") == QStringLiteral("zeroslack.web-recovery")) result.recoveredText = record.value("content").toString();
    }
    const auto envelope = editorRecoveryEnvelope(result.recoveredText);
    if (!envelope.isEmpty()) result.recoveredText = envelope.value("text").toString();
    return result;
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

    const auto recoveryRecord = QJsonDocument::fromJson(comparison.recoveredText.toUtf8()).object();
    if (RetiredDocument::isBlockDocument(comparison.candidate.originalFilePath)
        || RetiredDocument::isBlockDocument(comparison.candidate.untitledDocumentId)
        || (recoveryRecord.value("format") == "zeroslack.web-recovery" && recoveryRecord.value("kind") == "blocks")) {
        result.status = CrashRecoveryStatus::InvalidArgument;
        result.reason = RetiredDocument::message();
        return result;
    }
    SharedDocument* document = nullptr;
    if (WebDocumentPage::supportsFile(comparison.candidate.originalFilePath)
        || comparison.candidate.untitledDocumentId.startsWith(QStringLiteral("web:"))) {
        const QString source = comparison.candidate.originalFilePath.isEmpty() ? comparison.candidate.untitledDocumentId : comparison.candidate.originalFilePath;
        auto* page = new WebDocumentPage(WebDocumentPage::kindForFile(source), activeTabWidget()); QString error;
        if (!page->restoreRecovery(comparison.recoveredText, &error)) { delete page; result.status = CrashRecoveryStatus::CorruptRecord; result.reason = error; return result; }
        const auto claimed = crashRecoveryService->recoverText(comparison.candidate.workspacePath, comparison.candidate.recoveryId);
        if (!claimed.succeeded() || !sameReviewedRecoveryCandidate(comparison.candidate, claimed.candidate)) {
            delete page; result.status = claimed.succeeded() ? CrashRecoveryStatus::IdentityMismatch : claimed.status; result.reason = claimed.reason; return result;
        }
        registerWebDocument(page, activeTabWidget());
        webRecoveryKeys[page->documentSession().get()].append({claimed.candidate.workspacePath, claimed.candidate.originalFilePath, claimed.candidate.untitledDocumentId});
        result.status = CrashRecoveryStatus::Success; result.documentId = page->documentId(); result.openedView = true;
        result.reason = QStringLiteral("Recovered into an independent unsaved document. Use Save As after reviewing it; the source file was not overwritten.");
        return result;
    }
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
    const auto claimed = crashRecoveryService->recoverText(
        comparison.candidate.workspacePath, comparison.candidate.recoveryId);
    if (!claimed.succeeded() || !sameReviewedRecoveryCandidate(comparison.candidate, claimed.candidate)) {
        result.status = claimed.succeeded() ? CrashRecoveryStatus::IdentityMismatch : claimed.status;
        result.reason = claimed.reason;
        return result;
    }
    document->restoreSavedBaseline(
        comparison.candidate
            .savedBaselineSha256,
        comparison.candidate
            .savedBaselineModifiedUtc);
    const auto envelope = editorRecoveryEnvelope(comparison.recoveredText);
    if (!document->restoreUnsavedText(
            envelope.isEmpty() ? comparison.recoveredText : envelope.value("text").toString(),
            recoveredRevision)) {
        result.status =
            CrashRecoveryStatus::IoError;
        result.reason =
            QStringLiteral(
                "The recovered text could not be applied in memory.");
        return result;
    }
    if (!envelope.isEmpty() && !DocumentBlockStore::get(DocumentBlockModel::get(document->textDocument()))
        ->restoreRecovery(envelope.value("associations").toObject(), &result.reason)) {
        result.status = CrashRecoveryStatus::CorruptRecord; return result;
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
    recoveryDocumentStates[document].claimedRecords.append(claimed.candidate);
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
    QList<SharedDocument*> matched;
    for (auto it = recoveryDocumentStates.begin(); it != recoveryDocumentStates.end(); ++it) {
        QList<CrashRecoveryDocumentKey> keys = it->obsoleteKeys;
        keys.append(it->key);
        for (const auto& key : keys) {
            if (identityKey(key.workspacePath) == identityKey(workspace)
                && crashRecoveryService->recoveryIdForDocument(key) == recoveryId) {
                matched.append(it.key());
                if (recoveryQueue) recoveryQueue->cancel(it.key(), [&](const auto& queuedKey) {
                    return identityKey(queuedKey.workspacePath) == identityKey(workspace)
                        && crashRecoveryService->recoveryIdForDocument(queuedKey) == recoveryId;
                });
                break;
            }
        }
    }
    // Cancellation shares the final commit gate with the worker. Once it
    // returns, discard cannot be undone by a previously captured request.
    const auto result = crashRecoveryService->discard(workspace, recoveryId);
    if (result.succeeded()) {
        for (auto* document : matched) {
            auto& state = recoveryDocumentStates[document];
            if (crashRecoveryService->recoveryIdForDocument(state.key) == recoveryId) {
                state.hasSnapshot = false;
                state.retryNeeded = false;
                state.discarded = true;
                state.snapshotRevision = document->textRevision();
                state.snapshotAssociationRevision = DocumentBlockModel::get(document->textDocument())->revision();
            }
            state.obsoleteKeys.removeIf([&](const auto& key) {
                return crashRecoveryService->recoveryIdForDocument(key) == recoveryId;
            });
        }
    }
    return result;
}

void TabManager::checkpointCrashRecovery()
{
    for (auto* page : webDocuments()) checkpointWebRecovery(page);
    for (SharedDocument* document :
         std::as_const(observedDocuments)) {
        if (document && document->dirty())
            writeRecoverySnapshot(document, true);
    }
}

void TabManager::clearCrashRecoveryAfterNormalClose()
{
    recoveryClosing = true;
    for (auto* page : webDocuments()) clearWebRecovery(page);
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
    if (RetiredDocument::isBlockDocument(fileName)) {
        UiDialogs::warning(activeTabWidget(), QStringLiteral("Unsupported Document"), RetiredDocument::message());
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

    if (PdfDocumentPage::supportsFile(fileName)) return openPdfDocument(fileName) != nullptr;
    if (WebDocumentPage::supportsFile(fileName)) return openWebDocument(fileName) != nullptr;
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
    if (auto* page = currentAssociatedBlock()) return saveSharedDocument(page->ownerDocument());
    if (auto* page = currentWebDocument()) return page->save();
    return saveEditor(getCurrentEditor(), false);
}

bool TabManager::saveAsCurrentTab()
{
    if (auto* page = currentAssociatedBlock()) return saveSharedDocument(page->ownerDocument(), true);
    if (auto* page = currentWebDocument()) return page->save(true);
    return saveEditor(getCurrentEditor(), true);
}

void TabManager::closeTab(int index)
{
    QTabWidget* group = activeTabWidget();
    if (!group || index < 0 || index >= group->count())
        return;
    closePage(group, index);
}

void TabManager::setEditorTransitionGuard(std::function<bool(MyCodeEditor*)> guard)
{
    editorTransitionGuard = std::move(guard);
}

void TabManager::enableSplitLayout(QWidget* host)
{
    if (splitController)
        splitController->setHost(host);
}

bool TabManager::splitCurrentView(
    EditorSplitDirection direction)
{
    if (auto* current = currentPdfDocument()) {
        auto* group = splitController->createSplit(activeTabWidget(), direction);
        auto* page = group ? openPdfDocument(current->fileName(), group) : nullptr;
        if (page) page->restoreViewState(current->viewState());
        return page != nullptr;
    }
    if (auto* current = currentWebDocument()) {
        if (!current->ready() || current->busy()) return false;
        auto* created = splitController->createSplit(activeTabWidget(), direction);
        if (!created) return false;
        registerWebDocument(new WebDocumentPage(current->documentSession(), created), created);
        return true;
    }
    if (!getCurrentEditor()) return moveCurrentViewToSplit(direction);
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
    QWidget* current = activeTabWidget() ? activeTabWidget()->currentWidget() : nullptr;
    return current && splitController
        && splitController->movePageToSplit(
               current,
               direction,
               activeTabWidget())
            != nullptr;
}

bool TabManager::duplicateCurrentView()
{
    if (auto* current = currentPdfDocument()) {
        auto* page = openPdfDocument(current->fileName()); if (page) page->restoreViewState(current->viewState()); return page != nullptr;
    }
    if (auto* current = currentWebDocument()) {
        if (!current->ready() || current->busy()) return false;
        auto* group = activeTabWidget();
        registerWebDocument(new WebDocumentPage(current->documentSession(), group), group);
        return true;
    }
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
        ? splitController->groups(splitController->regionForGroup(activeTabWidget())).size()
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
bool TabManager::addSemanticReference(const QVariantMap& sourceMap,const QString& uri,const QString& caption,QString* error) {
    const auto source=PinloomSourceSelection::fromVariantMap(sourceMap);
    if(!source.isValid()) {if(error)*error="The source binding is invalid.";return false;}
    const QPointer<MyCodeEditor> origin=editorActionTarget();
    auto* document=acquireFileDocument(source.absoluteFilePath);
    if(!document)return false;
    if(!DocumentBlockTransfer::validateParticipant(document,true,error)){sharedDocuments->releaseIfUnused(document);return false;}
    const auto id=DocumentReferenceService::insertSemantic(DocumentBlockModel::get(document->textDocument()),source,
        document->textDocument()->toPlainText(),{uri,caption,true},error);
    if(id.isEmpty()){sharedDocuments->releaseIfUnused(document);return false;}
    if(document->viewCount()==0 && !createView(document,activeTabWidget())) {
        DocumentBlockModel::get(document->textDocument())->undoAssociation(false);
        sharedDocuments->releaseIfUnused(document);
        if(error)*error="The owning code view could not be opened.";return false;
    }
    if(origin)origin->setFocus(Qt::OtherFocusReason);
    return true;
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
        if (isRegistered(editor) && editor->isVisibleTo(editor->window()))
            return editor;
    }

    return isRegistered(lastActionEditor)
            && lastActionEditor->isVisibleTo(lastActionEditor->window())
        ? lastActionEditor.data() : nullptr;
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
    const SharedDocumentViewState& state,
    const QByteArray* expectedSourceSha256)
{
    SharedDocument* document =
        auxiliaryDocument(documentId, fileName, expectedSourceSha256);
    if (!document || !parentWidget)
        return nullptr;
    return createBoundView(
        document, parentWidget, state, true);
}

bool TabManager::rebindAuxiliaryView(
    MyCodeEditor* editor,
    const QString& documentId,
    const QString& fileName,
    const SharedDocumentViewState& state,
    const QByteArray* expectedSourceSha256)
{
    if (!isAuxiliaryView(editor))
        return false;

    SharedDocument* target =
        auxiliaryDocument(documentId, fileName, expectedSourceSha256);
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
    if (!document) {
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
    if (!saveSharedDocument(
            document,
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
    QList<AssociatedBlockPage*> blockTargets;
    for (auto* page:associatedBlockPages()) if (splitController->groupForPage(page)==group && group->currentWidget()!=page) blockTargets.append(page);
    QList<PdfDocumentPage*> pdfTargets;
    for (auto* page : pdfDocuments()) if (splitController->groupForPage(page) == group && page != currentPdfDocument()) pdfTargets.append(page);
    QList<WebDocumentPage*> webTargets;
    for (auto* page : webDocuments()) if (splitController->groupForPage(page) == group && page != currentWebDocument()) webTargets.append(page);
    if (!confirmWebDocuments(webTargets)) return false;
    MyCodeEditor* current = getCurrentEditor();
    QList<MyCodeEditor*> targets;
    for (int index = 0; group && index < group->count(); ++index) {
        MyCodeEditor* editor =
            qobject_cast<MyCodeEditor*>(group->widget(index));
        if (editor && editor != current && !isTabLocked(editor))
            targets.append(editor);
    }
    if (!closeEditorsAtomically(targets)) return false;
    for (auto* page : webTargets) closeWebDocument(page, false);
    for (auto* page : pdfTargets) closePdfDocument(page);
    for (auto* page:blockTargets) if(!closeAssociatedBlock(page)) return false;
    return true;
}

bool TabManager::closeTabsToRight()
{
    QTabWidget* group = activeTabWidget();
    if (!group)
        return false;
    QList<AssociatedBlockPage*> blockTargets;
    for(auto* page:associatedBlockPages()) if(splitController->groupForPage(page)==group && group->indexOf(page)>group->currentIndex()) blockTargets.append(page);
    QList<PdfDocumentPage*> pdfTargets;
    for (auto* page : pdfDocuments()) if (splitController->groupForPage(page) == group && group->indexOf(page) > group->currentIndex()) pdfTargets.append(page);
    QList<WebDocumentPage*> webTargets;
    for (auto* page : webDocuments()) if (splitController->groupForPage(page) == group && group->indexOf(page) > group->currentIndex()) webTargets.append(page);
    if (!confirmWebDocuments(webTargets)) return false;
    QList<MyCodeEditor*> targets;
    for (int index = group->currentIndex() + 1;
         index < group->count();
         ++index) {
        MyCodeEditor* editor =
            qobject_cast<MyCodeEditor*>(group->widget(index));
        if (editor && !isTabLocked(editor))
            targets.append(editor);
    }
    if (!closeEditorsAtomically(targets)) return false;
    for (auto* page : webTargets) closeWebDocument(page, false);
    for (auto* page : pdfTargets) closePdfDocument(page);
    for(auto* page:blockTargets) if(!closeAssociatedBlock(page)) return false;
    return true;
}

bool TabManager::closeAllTabs()
{
    QList<SharedDocument*> blockDocuments;
    for(auto* page:associatedBlockPages()) if(!blockDocuments.contains(page->ownerDocument())) blockDocuments.append(page->ownerDocument());
    if(!resolvePendingDocuments(blockDocuments,nullptr)) return false;
    const auto pdfTargets = pdfDocuments();
    const auto webTargets = webDocuments();
    if (!confirmWebDocuments(webTargets)) return false;
    QList<MyCodeEditor*> targets;
    for (MyCodeEditor* editor : allEditors()) {
        if (!isTabLocked(editor))
            targets.append(editor);
    }
    if (!closeEditorsAtomically(targets)) return false;
    for (auto* page : webTargets) closeWebDocument(page, false);
    for (auto* page : pdfTargets) closePdfDocument(page);
    bool closed=true;
    for(auto* page:associatedBlockPages()) closed=closeAssociatedBlock(page,false) && closed;
    return closed;
}

bool TabManager::reopenClosedTab()
{
    if (recentlyClosedTabs.isEmpty())
        return false;
    const ClosedTabState state =
        recentlyClosedTabs.takeLast();
    if (!state.associatedBlockId.isEmpty()) {
        auto* document = sharedDocuments->documentById(state.ownerDocumentId);
        if (!document && !state.fileName.isEmpty()) document = acquireFileDocument(state.fileName);
        if (!document && state.fileName.isEmpty()) {
            document = sharedDocuments->acquireUntitled(state.ownerDocumentId, state.text);
            observeDocument(document);
            QString issue;
            if (!DocumentBlockStore::get(DocumentBlockModel::get(document->textDocument()))->restoreRecovery(state.associations, &issue)) {
                emit fileSaveFailed({},issue); sharedDocuments->releaseIfUnused(document); recentlyClosedTabs.append(state); return false;
            }
        }
        auto* group=state.floatingGeometry.isValid() ? splitController->regionGroup(state.regionId) : closedTabGroup(state);
        if (document) if (auto* page=openAssociatedBlock(document, state.associatedBlockId, group,state.viewState.viewId)) {
            if (state.floatingGeometry.isValid()) if(auto* floating=splitController->floatPage(page,state.floatingGeometry.topLeft())) floating->window()->setGeometry(state.floatingGeometry);
            return true;
        }
        if (document) sharedDocuments->releaseIfUnused(document);
        recentlyClosedTabs.append(state); return false;
    }
    if (state.pdfDocument) {
        auto* page = openPdfDocument(state.fileName, closedTabGroup(state));
        if (page) page->restoreViewState(state.pdfState);
        return page != nullptr;
    }
    if (state.webDocument) {
        return openWebDocument(state.fileName, closedTabGroup(state)) != nullptr;
    }
    SharedDocument* document = nullptr;
    bool restoreUntitledAssociations=false;
    if (state.fileName.isEmpty()) {
        document = sharedDocuments->documentById(state.ownerDocumentId);
        restoreUntitledAssociations=!document;
        if (!document) document = state.ownerDocumentId.isEmpty() ? sharedDocuments->createUntitled(state.text)
            : sharedDocuments->acquireUntitled(state.ownerDocumentId,state.text);
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
        closedTabGroup(state);
    MyCodeEditor* editor =
        createView(document, group, state.viewState);
    if (!editor)
        return false;
    if (restoreUntitledAssociations && !state.associations.isEmpty()) {
        QString issue;
        if (!DocumentBlockStore::get(editor->blockModel())->restoreRecovery(state.associations,&issue)) emit fileSaveFailed({},issue);
    }
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
    if (currentWebDocument() || currentPdfDocument() || currentAssociatedBlock()) return getCurrentDocumentMetadata();
    return documentQueries.currentDocument(
        getCurrentEditor());
}

DocumentSnapshot TabManager::getCurrentDocumentMetadata() const
{
    if (auto* page=currentAssociatedBlock()) {
        DocumentSnapshot value; if (auto* document=page->ownerDocument()) {
            value.documentId=document->documentId(); value.fileName=document->fileName(); value.dirty=document->dirty();
            value.saved=!value.fileName.isEmpty(); value.textVersion=int(document->textRevision());
        } return value;
    }
    if (auto* page = currentPdfDocument()) {
        DocumentSnapshot value; value.documentId = QStringLiteral("pdf:") + identityKey(page->fileName()); value.fileName = page->fileName(); value.saved = true; return value;
    }
    if (auto* page = currentWebDocument()) {
        DocumentSnapshot value;
        value.documentId = page->documentId(); value.fileName = page->fileName();
        value.dirty = page->dirty(); value.saved = !page->fileName().isEmpty();
        return value;
    }
    return documentQueries.currentDocumentMetadata(
        getCurrentEditor());
}

DocumentSnapshot TabManager::getDocumentForEditor(
    MyCodeEditor* editor) const
{
    return documentQueries.documentForEditor(editor);
}

MyCodeEditor* TabManager::contextEditor() const
{
    return qobject_cast<MyCodeEditor*>(contextPage());
}

QWidget* TabManager::contextPage() const
{
    return validContextPage(contextDocumentPage) ? contextDocumentPage.data() : nullptr;
}

bool TabManager::validContextPage(QWidget* page) const
{
    if (!page || !splitController) return false;
    auto* group = splitController->groupForPage(page);
    const int index = group ? group->indexOf(page) : -1;
    if (index < 0 || !group->isTabVisible(index)) return false;
    if (auto* editor = qobject_cast<MyCodeEditor*>(page))
        return sharedDocumentForEditor(editor) && editorVisibleInWorkspaceScope(editor);
    QString file;
    if (auto* web = qobject_cast<WebDocumentPage*>(page)) file = web->fileName();
    else if (auto* pdf = qobject_cast<PdfDocumentPage*>(page)) file = pdf->fileName();
    else if (auto* block=qobject_cast<AssociatedBlockPage*>(page); block && block->ownerDocument()) file=block->ownerDocument()->fileName();
    else return false;
    const QString owner = workspaceForFile(file);
    return owner.isEmpty() || identityKey(owner) == identityKey(activeWorkspaceRoot);
}

void TabManager::setContextPage(QWidget* page)
{
    if (page && !validContextPage(page)) return;
    if (contextDocumentPage == page) return;
    contextDocumentPage = page;
    emit contextDocumentChanged(contextDocumentMetadata());
}

void TabManager::repairContextPage()
{
    if (contextPage() || changingWorkspaceScope) return;
    auto* active = activeTabWidget();
    if (active && validContextPage(active->currentWidget())) {
        setContextPage(active->currentWidget());
        return;
    }
    for (auto* group : splitController->groups()) {
        if (validContextPage(group->currentWidget())) {
            setContextPage(group->currentWidget());
            return;
        }
    }
    setContextPage(nullptr);
}

DocumentSnapshot TabManager::contextDocumentMetadata() const
{
    if (auto* page=qobject_cast<AssociatedBlockPage*>(contextPage())) {
        DocumentSnapshot value; if (auto* document=page->ownerDocument()) {
            value.documentId=document->documentId(); value.fileName=document->fileName(); value.dirty=document->dirty();
            value.saved=!value.fileName.isEmpty(); value.textVersion=int(document->textRevision());
        } return value;
    }
    if (auto* page = qobject_cast<WebDocumentPage*>(contextPage())) {
        DocumentSnapshot value;
        value.documentId = page->documentId(); value.fileName = page->fileName();
        value.dirty = page->dirty(); value.saved = !value.fileName.isEmpty();
        value.textVersion = int(page->documentSession()->currentRevision());
        return value;
    }
    if (auto* page = qobject_cast<PdfDocumentPage*>(contextPage())) {
        DocumentSnapshot value;
        value.documentId = QStringLiteral("pdf:") + identityKey(page->fileName());
        value.fileName = page->fileName(); return value;
    }
    return documentQueries.currentDocumentMetadata(contextEditor());
}

bool TabManager::activateOpenFile(const QString& fileName)
{
    for (auto* page : pdfDocuments()) if (EditorFileIdentity::same(page->fileName(), fileName)) {
        if (splitController->activatePage(page)) return true;
    }
    for (auto* page : webDocuments()) if (!page->fileName().isEmpty() && EditorFileIdentity::same(page->fileName(), fileName)) {
        if (splitController->activatePage(page)) return true;
    }
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
            return splitController->activatePage(editor);
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
    splitController->registerPage(page,QStringLiteral("tool:")+stableId,QStringLiteral("tool"));
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
    return splitController->activatePage(page);
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
    QStringList result = documentQueries.allOpenFileNames();
    for (auto* page : pdfDocuments()) if (!result.contains(page->fileName())) result.append(page->fileName());
    for (auto* page : webDocuments()) if (!page->fileName().isEmpty() && !result.contains(page->fileName())) result.append(page->fileName());
    return result;
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
    if (auto* document = sharedDocumentForEditor(editor)) {
        UiControls::TabPresentation state;
        state.documentId = document->documentId();
        state.viewId = editor->property("editorViewId").toString();
        state.title = tabTitleForEditor(editor, false);
        state.path = QDir::toNativeSeparators(document->fileName());
        state.dirty = document->dirty();
        state.externalChange = document->externalState() == SharedDocumentExternalState::ExternallyModified;
        state.conflict = document->externalState() == SharedDocumentExternalState::Conflict;
        state.readOnly = document->readOnly(); state.locked = isTabLocked(editor);
        UiControls::setTabPresentation(group, editor, state);
    }
    if (editor == getCurrentEditor()) {
        const QString fileName =
            documentModel->documentMetadataForEditor(editor).fileName;
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
    if (switched) setContextPage(nullptr);
    scopedWorkspaceRoots =
        lexicalTabWorkspaceRoots(workspaceRoots);
    activeWorkspaceRoot =
        lexicalPath(
            activeWorkspaceRootPath);
    const auto workspaceEditors = allEditors() + auxiliaryViews();
    for (MyCodeEditor* editor : workspaceEditors) {
        if (!editor)
            continue;
        if (switched)
            editor->exitInteractionModes(EditorModeExitReason::TabChanged);
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
            setContextPage(preferred);
            if (QTabWidget* group = splitController->groupForPage(preferred)) {
                splitController->setActiveGroup(group);
                group->setCurrentWidget(preferred);
            }
        }
    }
    changingWorkspaceScope = false;
    repairContextPage();
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
    for (auto* page : webDocuments()) if (page->dirty() && identityKey(workspaceForFile(page->fileName())) == identityKey(workspaceRoot)) return true;
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
    QList<WebDocumentPage*> webPending;
    for (auto* page : webDocuments()) if (identityKey(workspaceForFile(page->fileName())) == identityKey(workspaceRoot)) webPending.append(page);
    if (!confirmWebDocuments(webPending)) return false;
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
    for(auto* page:associatedBlockPages()) {
        auto* document=page->ownerDocument();
        if(document && identityKey(workspaceForFile(document->fileName()))==identityKey(workspaceRoot) && !documents.contains(document)) documents.append(document);
    }
    if (!resolvePendingDocuments(documents, nullptr)) return false;
    for (auto* page : webPending) closeWebDocument(page, false);
    for (auto* page : pdfDocuments()) if (identityKey(workspaceForFile(page->fileName())) == identityKey(workspaceRoot)) closePdfDocument(page);
    for (MyCodeEditor* editor : auxiliary) {
        if (documents.contains(sharedDocumentForEditor(editor)))
            closeAuxiliaryViewInternal(editor, true);
    }
    bool closed = true;
    for (MyCodeEditor* editor : pending) {
        setTabLocked(editor, false);
        closed = closeEditor(editor, false) && closed;
    }
    for(auto* page:associatedBlockPages()) if(documents.contains(page->ownerDocument())) closed=closeAssociatedBlock(page,false) && closed;
    lastWorkspaceEditors.remove(identityKey(workspaceRoot));
    if (splitController) splitController->removeEmptyGroups();
    return closed;
}

bool TabManager::hasUnsavedChanges() const
{
    for (auto* page : webDocuments()) if (page->dirty()) return true;
    for (auto* document : sharedDocuments->documents()) if (document->dirty()) return true;
    return documentQueries.hasUnsavedChanges();
}

bool TabManager::resolvePendingDocuments(
    QWidget* dialogParent)
{
    if (!confirmWebDocuments(webDocuments())) return false;
    return resolvePendingDocuments(
        sharedDocuments
            ? sharedDocuments->documents()
            : QList<SharedDocument*>(),
        dialogParent);
}

bool TabManager::prepareWorkspacePathMutation(
    const QString& sourcePath,
    bool recursive,
    WorkspacePathMutation* prepared,
    QWidget* dialogParent,
    QString* failureReason,
    const WorkspacePathMutationGuard& guard,
    const QString& renameTarget)
{
    if (prepared) *prepared = {};
    if (failureReason)
        failureReason->clear();
    if (!prepared || !sharedDocuments || sourcePath.isEmpty()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral("The selected path is unavailable.");
        }
        return false;
    }

    QList<SharedDocument*> affectedDocuments;
    QList<WebDocumentPage*> affectedWebDocuments;
    QSet<WebDocumentSession*> affectedWebSessions;
    for (auto* page : webDocuments()) if (documentPathMatchesMutation(page->fileName(), sourcePath, recursive)
        && !affectedWebSessions.contains(page->documentSession().get())) {
        affectedWebSessions.insert(page->documentSession().get()); affectedWebDocuments.append(page);
    }
    QList<WorkspacePathMutation::WebDocument> confirmedWebDocuments;
    const auto renamedPath = [&](const QString& path) {
        return renameTarget.isEmpty() ? QString() : recursive ? QDir(renameTarget).filePath(QDir(sourcePath).relativeFilePath(path)) : renameTarget;
    };
    for (auto* page : affectedWebDocuments) {
        if (renameTarget.isEmpty() && !page->documentSession()->confirmClose()) return false;
        if (!documentPathMatchesMutation(page->fileName(), sourcePath, recursive)) continue;
        const quint64 revision = page->currentRevision();
        auto lock = page->documentSession()->lockForPathMutation(revision, failureReason, !renameTarget.isEmpty());
        if (!lock) return false;
        confirmedWebDocuments.append({page, page->fileName(), revision, std::move(lock), renamedPath(page->fileName())});
    }
    QList<WorkspacePathMutation::Document> confirmedDocuments;
    for (SharedDocument* document :
         sharedDocuments->documents()) {
        if (!document
            || !documentPathMatchesMutation(
                document->documentId(),
                sourcePath,
                recursive)) {
            continue;
        }
        for (MyCodeEditor* view : document->views()) {
            if (editorTransitionGuard && !editorTransitionGuard(view)) {
                if (failureReason) *failureReason = QStringLiteral("Save the linked board before changing this file path.");
                return false;
            }
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
        WorkspacePathMutation::Document entry;
        entry.document = document;
        entry.identity = document->documentId();
        entry.revision = document->textRevision();
        for (auto* view : document->views()) entry.views.append(view);
        entry.associationRevision=DocumentBlockModel::get(document->textDocument())->revision();
        for (auto* page:associatedBlockPages()) if (page->ownerDocument()==document) entry.blocks.append(page);
        confirmedDocuments.append(std::move(entry));
    }

    if (!resolvePendingDocuments(
            affectedDocuments,
            dialogParent, guard, failureReason)) {
        if (failureReason && failureReason->isEmpty()) {
            *failureReason = QStringLiteral(
                "The file operation was cancelled because its open "
                "documents were not resolved.");
        }
        return false;
    }
    if (guard.validate && !guard.validate(failureReason)) return false;
    prepared->owner = this;
    prepared->webDocuments = confirmedWebDocuments;
    for (auto* page : pdfDocuments()) if (documentPathMatchesMutation(page->fileName(), sourcePath, recursive))
        prepared->pdfDocuments.append({page, page->fileName(), renamedPath(page->fileName())});
    for (const auto& entry : confirmedDocuments) {
        // Save As may have deliberately moved a pending document away.
        if (entry.document && entry.document->documentId() != entry.identity
            && !documentPathMatchesMutation(entry.document->documentId(), sourcePath, recursive)) continue;
        prepared->documents.append(entry);
    }
    return validateWorkspacePathMutation(*prepared, failureReason);
}

bool TabManager::validateWorkspacePathMutation(const WorkspacePathMutation& prepared,
                                              QString* failureReason) const
{
    if (failureReason) failureReason->clear();
    const auto fail = [&] {
        if (failureReason) *failureReason = QStringLiteral(
            "An affected document or view changed after path-operation preparation. Review it again.");
        return false;
    };
    if (prepared.owner != this) return fail();
    for (const auto& entry : prepared.pdfDocuments)
        if (!entry.page || entry.page->fileName() != entry.path || !splitController->groupForPage(entry.page)) return fail();
    for (const auto& entry : prepared.webDocuments)
        if (!entry.page || !entry.lock || entry.page->fileName() != entry.path || entry.page->currentRevision() != entry.revision || !entry.page->busy()) return fail();
    for (const auto& entry : prepared.documents) {
        if (!entry.document || entry.document->documentId() != entry.identity
            || entry.document->textRevision() != entry.revision
            || entry.document->views().size() != entry.views.size()
            || DocumentBlockModel::get(entry.document->textDocument())->revision()!=entry.associationRevision
            || entry.document->consumerCount()!=entry.blocks.size()) return fail();
        for (const auto& view : entry.views) {
            if (!view || sharedDocumentForEditor(view) != entry.document || isTabLocked(view)) return fail();
        }
        for (const auto& page:entry.blocks) if (!page || page->ownerDocument()!=entry.document || !splitController->groupForPage(page)) return fail();
    }
    return true;
}

bool TabManager::finalizeWorkspacePathMutation(const WorkspacePathMutation& prepared,
                                              QString* failureReason)
{
    if (!validateWorkspacePathMutation(prepared, failureReason)) return false;
    QList<QPointer<MyCodeEditor>> affectedEditors;
    for (const auto& entry : prepared.documents) affectedEditors.append(entry.views);

    closingBatch = true;
    bool closedAll = true;
    for (const auto& entry : prepared.webDocuments) {
        auto* original = entry.page.data();
        if (!original) { closedAll = false; continue; }
        const auto session = original->documentSession();
        if (!entry.target.isEmpty()) {
            if (!session->retargetAfterRename(entry.target, failureReason)) closedAll = false;
            continue;
        }
        clearWebRecovery(original);
        for (auto* page : session->views()) {
            auto* group = splitController->groupForPage(page);
            if (!group) { closedAll = false; continue; }
            const QString name = page->fileName(); group->removeTab(group->indexOf(page));
            page->releaseView(); page->deleteLater(); emit tabClosed(name);
        }
    }
    for (const auto& entry : prepared.pdfDocuments) {
        if (!entry.page) { closedAll = false; continue; }
        if (!entry.target.isEmpty()) closedAll = entry.page->openFile(entry.target, failureReason) && closedAll;
        else closedAll = closePdfDocument(entry.page, false) && closedAll;
    }
    for (const auto& guardedEditor : std::as_const(affectedEditors)) {
        auto* editor = guardedEditor.data();
        if (!editor)
            continue;
        const bool closed = isAuxiliaryView(editor)
            ? closeAuxiliaryViewInternal(editor, true)
            : closeEditor(editor, false, false);
        closedAll = closed && closedAll;
    }
    for (const auto& entry:prepared.documents)
        for (const auto& page:entry.blocks) closedAll=(page && closeAssociatedBlock(page,false,false)) && closedAll;
    closingBatch = false;
    if (splitController)
        splitController->removeEmptyGroups();
    if (!closedAll && failureReason && failureReason->isEmpty()) {
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
TabManager::workspaceSessionTabs(const QString& workspaceRoot) const
{
    QList<WorkspaceSessionTabState> states;
    if (workspaceRoot.isEmpty()) return states;
    for (const auto& pending : pendingAssociatedTabs)
        if (identityKey(pending.filePath.isEmpty() ? pending.viewState.value("workspaceRoot").toString() : workspaceForFile(pending.filePath))==identityKey(workspaceRoot)) states.append(pending);
    const auto groups = splitController->groups();
    for (auto* group : groups) for (int index = 0; index < group->count(); ++index) {
        QWidget* page = group->widget(index);
        WorkspaceSessionTabState state;
        if (auto* web = qobject_cast<WebDocumentPage*>(page)) {
            state.filePath = web->fileName(); state.kind = web->documentKind() == WebDocumentPage::Kind::Markdown ? "markdown" : "blocks";
            state.documentId = web->documentId(); state.viewId = web->viewIdentity(); state.viewState = web->viewState();
        } else if (auto* pdf = qobject_cast<PdfDocumentPage*>(page)) {
            state.filePath = pdf->fileName(); state.kind = "pdf"; state.documentId = "pdf:" + identityKey(state.filePath);
            state.viewId = pdf->viewIdentity(); state.viewState = pdf->viewState();
        } else if (auto* block = qobject_cast<AssociatedBlockPage*>(page)) {
            auto* document=block->ownerDocument(); if(!document) continue;
            state.filePath=document->fileName(); state.kind="associatedBlock"; state.documentId=document->documentId();
            state.viewId=block->viewIdentity(); state.viewState={{"blockId",block->blockId()}};
            if (state.filePath.isEmpty()) {
                if(identityKey(document->property("associatedWorkspace").toString())!=identityKey(workspaceRoot)) continue;
                QString issue;
                const auto associations=DocumentBlockStore::get(block->model())->recoveryState(&issue,crashRecoveryService->recoveryRootPath());
                if(!issue.isEmpty()) continue;
                state.viewState.insert("recovery",QJsonObject{{"text",document->textDocument()->toPlainText()},{"associations",associations}});
            }
        } else if (auto* editor = qobject_cast<MyCodeEditor*>(page)) {
            const auto snapshot = getDocumentForEditor(editor);
            state.filePath = snapshot.fileName; state.documentId = snapshot.documentId; state.kind = "code";
            if (!fileIo.isSystemVerilogFile(state.filePath)) continue;
            const auto cursor = editor->textCursor();
            state.cursorLine = cursor.blockNumber() + 1; state.cursorColumn = cursor.positionInBlock() + 1;
            state.verticalScrollValue = editor->verticalScrollBar()->value();
            state.horizontalScrollValue = editor->horizontalScrollBar()->value();
            state.viewId = editor->property("editorViewId").toString(); state.locked = isTabLocked(editor);
            QJsonArray folds;
            for (const auto& range : editor->foldingViewState().collapsedRanges)
                folds.append(QJsonObject{{"start", range.startAnchor.isNull() ? range.fallbackStartLine : range.startAnchor.blockNumber()},
                    {"end", range.endAnchor.isNull() ? range.fallbackEndLine : range.endAnchor.blockNumber()}});
            state.viewState = {{"cursor", cursor.position()}, {"anchor", cursor.anchor()}, {"folds", folds}};
            state.active = editor == getCurrentEditor() || (isTemporaryEditor(getCurrentEditor())
                && lastWorkspaceEditors.value(identityKey(workspaceRoot)) == editor);
        } else continue;
        if (!(state.kind=="associatedBlock" && state.filePath.isEmpty())
            && (identityKey(workspaceForFile(state.filePath)) != identityKey(workspaceRoot) || !QFileInfo(state.filePath).isFile())) continue;
        state.pageId = splitController->pageId(page); state.regionId = splitController->regionForGroup(group);
        state.groupId = splitController->groupId(group); state.groupIndex = groups.indexOf(group); state.tabIndex = index;
        state.active = state.active || (group == activeTabWidget() && group->currentWidget() == page);
        states.append(state);
    }
    return states;
}

QJsonObject TabManager::workspaceSessionPageLayout(const QString& workspaceRoot) const
{
    QSet<QString> documentPages;
    for (const auto& tab : workspaceSessionTabs(workspaceRoot)) documentPages.insert(tab.pageId);
    return splitController->savePageLayout([&](QWidget* page) {
        if (qobject_cast<MyCodeEditor*>(page) || qobject_cast<WebDocumentPage*>(page) || qobject_cast<PdfDocumentPage*>(page) || qobject_cast<AssociatedBlockPage*>(page))
            return documentPages.contains(splitController->pageId(page));
        return true;
    });
}

QStringList TabManager::restoreWorkspaceSessionTabs(const QString& workspaceRoot,
    const QList<WorkspaceSessionTabState>& tabs, QStringList* skippedFiles)
{
    QStringList restoredFiles;
    if (workspaceRoot.isEmpty()) return restoredFiles;
    QPointer<QWidget> activePage;
    QSet<QWidget*> restoredViews;
    QSet<QString> identities, pageIdentities;
    QHash<QString, QString> documentPaths;
    for (auto* web : webDocuments()) documentPaths.insert(web->documentId(), identityKey(web->fileName()));
    const auto targetGroup = [&](const WorkspaceSessionTabState& tab) {
        const QString region = tab.regionId.isEmpty() ? QStringLiteral("main") : tab.regionId;
        if (auto* existing = splitController->groupById(tab.groupId);
            existing && splitController->regionForGroup(existing) == region) return existing;
        auto* anchor = splitController->regionGroup(region);
        if (!anchor) anchor = splitController->initialGroup();
        if (!tab.groupId.isEmpty()) {
            auto* group = splitController->createSplit(anchor, EditorSplitDirection::Right);
            if (group) { group->setProperty("pageGroupId", tab.groupId); return group; }
        }
        // v4 group indexes described only the old central editor groups.
        auto central = splitController->groups(QStringLiteral("main"));
        const int index = qBound(0, tab.groupIndex, 127);
        while (tab.regionId.isEmpty() && central.size() <= index) {
            if (!splitController->createSplit(central.isEmpty() ? anchor : central.last(), EditorSplitDirection::Right)) break;
            central = splitController->groups(QStringLiteral("main"));
        }
        return tab.regionId.isEmpty() ? central.value(index, anchor) : anchor;
    };
    for (const auto& tab : tabs) {
        const QString path = lexicalPath(tab.filePath);
        const bool untitledBlock=tab.kind=="associatedBlock" && tab.filePath.isEmpty() && tab.documentId.startsWith("untitled:") && !tab.viewState.value("recovery").toObject().isEmpty();
        const auto skip = [&] {
            if (skippedFiles) skippedFiles->append(path.isEmpty() ? tab.documentId : path);
            if(tab.kind=="associatedBlock" && (tab.filePath.isEmpty() || identityKey(workspaceForFile(path))==identityKey(workspaceRoot))) {
                auto retained=tab; if (retained.filePath.isEmpty()) retained.viewState.insert("workspaceRoot",workspaceRoot);
                pendingAssociatedTabs.removeIf([&](const auto& prior) { return prior.pageId==tab.pageId; }); pendingAssociatedTabs.append(retained);
            }
        };
        if ((!untitledBlock && (identityKey(workspaceForFile(path)) != identityKey(workspaceRoot) || !QFileInfo(path).isFile()))
            || tab.viewId.size() > 128 || tab.pageId.size() > 128 || tab.groupId.size() > 128
            || (!tab.regionId.isEmpty() && !splitController->regions().contains(tab.regionId))
            || (!tab.viewId.isEmpty() && identities.contains(tab.viewId))) { skip(); continue; }
        if (auto* existingGroup = splitController->groupById(tab.groupId); existingGroup
            && splitController->regionForGroup(existingGroup) != (tab.regionId.isEmpty() ? QStringLiteral("main") : tab.regionId)) { skip(); continue; }
        if (!tab.viewId.isEmpty()) identities.insert(tab.viewId);
        if (!tab.pageId.isEmpty()) {
            if (pageIdentities.contains(tab.pageId)) { skip(); continue; }
            pageIdentities.insert(tab.pageId);
            if (auto* existing = splitController->pageById(tab.pageId)) {
                QString existingPath;
                if (auto* web = qobject_cast<WebDocumentPage*>(existing)) existingPath = web->fileName();
                else if (auto* pdf = qobject_cast<PdfDocumentPage*>(existing)) existingPath = pdf->fileName();
                else if (auto* block=qobject_cast<AssociatedBlockPage*>(existing); block && block->ownerDocument()) existingPath=block->ownerDocument()->fileName();
                else if (auto* editor = qobject_cast<MyCodeEditor*>(existing)) existingPath = getDocumentForEditor(editor).fileName;
                if ((!untitledBlock && !EditorFileIdentity::same(existingPath, path))
                    || (!tab.viewId.isEmpty() && existing->property("editorViewId").toString() != tab.viewId)) { skip(); continue; }
            }
        }
        const QString kind = tab.kind=="associatedBlock" && (untitledBlock || fileIo.isSystemVerilogFile(path)) ? "associatedBlock"
            : WebDocumentPage::supportsFile(path) ? "markdown"
            : PdfDocumentPage::supportsFile(path) ? "pdf" : fileIo.isSystemVerilogFile(path) ? "code" : QString();
        if (kind.isEmpty() || (!tab.kind.isEmpty() && kind != tab.kind)) { skip(); continue; }
        if ((kind == "markdown") && !tab.documentId.isEmpty()) {
            if (tab.documentId.size() > 128) { skip(); continue; }
            const auto found = documentPaths.constFind(tab.documentId);
            if (found != documentPaths.cend() && *found != identityKey(path)) { skip(); continue; }
            documentPaths.insert(tab.documentId, identityKey(path));
        }
        auto* group = targetGroup(tab);
        QWidget* page = nullptr;
        if (kind == "markdown") {
            WebDocumentPage* web = nullptr;
            for (auto* candidate : webDocuments())
                if (!restoredViews.contains(candidate) && EditorFileIdentity::same(candidate->fileName(), path)
                    && (tab.viewId.isEmpty() || candidate->viewIdentity() == tab.viewId)) { web = candidate; break; }
            QString issue;
            if (!web) web = openWebDocument(path, group, tab.viewId, tab.documentId, &issue);
            if (web) web->restoreViewState(tab.viewState);
            page = web;
        } else if (kind == "pdf") {
            PdfDocumentPage* pdf = nullptr;
            for (auto* candidate : pdfDocuments())
                if (!restoredViews.contains(candidate) && EditorFileIdentity::same(candidate->fileName(), path)
                    && (tab.viewId.isEmpty() || candidate->viewIdentity() == tab.viewId)) { pdf = candidate; break; }
            QString issue;
            if (!pdf) pdf = openPdfDocument(path, group, tab.viewId, &issue);
            if (pdf) pdf->restoreViewState(tab.viewState);
            page = pdf;
        } else if (kind=="associatedBlock") {
            const auto id=tab.viewState.value("blockId").toString();
            if(id.isEmpty() || id.size()>128 || tab.documentId.size()>1024) { skip(); continue; }
            auto* document=untitledBlock ? sharedDocuments->documentById(tab.documentId) : sharedDocuments->documentForFile(path);
            if (!document && untitledBlock) {
                const auto recovery=tab.viewState.value("recovery").toObject();
                if(!recovery.value("text").isString() || !recovery.value("associations").isObject()) { skip(); continue; }
                document=sharedDocuments->acquireUntitled(tab.documentId,recovery.value("text").toString());
                QString issue;
                if(!DocumentBlockStore::get(DocumentBlockModel::get(document->textDocument()))->restoreRecovery(recovery.value("associations").toObject(),&issue)) {
                    emit fileSaveFailed({},issue); sharedDocuments->releaseIfUnused(document); skip(); continue;
                }
                document->setProperty("associatedWorkspace",workspaceRoot); observeDocument(document);
            }
            if(!document && !untitledBlock) document=acquireFileDocument(path);
            page=openAssociatedBlock(document,id,group,tab.viewId);
            if (page) pendingAssociatedTabs.removeIf([&](const auto& prior) { return prior.pageId==tab.pageId; });
            else if (document) sharedDocuments->releaseIfUnused(document);
        } else {
            auto* document = sharedDocuments->documentForFile(path);
            if (!document) document = acquireFileDocument(path);
            if (!document) { skip(); continue; }
            MyCodeEditor* editor = nullptr;
            for (auto* candidate : document->views())
                if (!isAuxiliaryView(candidate) && !restoredViews.contains(candidate)
                    && (tab.viewId.isEmpty() || candidate->property("editorViewId").toString() == tab.viewId)) { editor = candidate; break; }
            if (!editor) { SharedDocumentViewState view; view.viewId = tab.viewId; editor = createView(document, group, view); }
            if (!editor) { skip(); continue; }
            QTextCursor cursor(editor->document());
            const int maximum = qMax(0, editor->document()->characterCount() - 1);
            if (tab.viewState.contains("cursor")) {
                cursor.setPosition(qBound(0, tab.viewState.value("anchor").toInt(), maximum));
                cursor.setPosition(qBound(0, tab.viewState.value("cursor").toInt(), maximum), QTextCursor::KeepAnchor);
            } else {
                auto block = editor->document()->findBlockByNumber(qMax(0, tab.cursorLine - 1));
                if (!block.isValid()) block = editor->document()->lastBlock();
                cursor.setPosition(block.position() + qBound(0, tab.cursorColumn - 1, block.text().size()));
            }
            editor->setTextCursor(cursor);
            EditorFoldViewState folding;
            for (const auto& value : tab.viewState.value("folds").toArray()) {
                const auto range = value.toObject(); EditorFoldAnchorState fold;
                fold.fallbackStartLine = range.value("start").toInt(-1); fold.fallbackEndLine = range.value("end").toInt(-1);
                if (fold.fallbackStartLine >= 0 && fold.fallbackEndLine > fold.fallbackStartLine) folding.collapsedRanges.append(fold);
            }
            editor->restoreFoldingViewState(folding);
            editor->verticalScrollBar()->setValue(qMax(0, tab.verticalScrollValue));
            editor->horizontalScrollBar()->setValue(qMax(0, tab.horizontalScrollValue));
            QTimer::singleShot(0, editor, [editor, vertical = tab.verticalScrollValue, horizontal = tab.horizontalScrollValue] {
                editor->verticalScrollBar()->setValue(qMax(0, vertical)); editor->horizontalScrollBar()->setValue(qMax(0, horizontal));
            });
            setTabLocked(editor, tab.locked); page = editor;
        }
        if (!page) { skip(); continue; }
        if (!tab.pageId.isEmpty() && !splitController->registerPage(page, tab.pageId, kind)) { skip(); continue; }
        restoredViews.insert(page); splitController->movePage(page, group, tab.tabIndex);
        restoredFiles.append(path); if (tab.active) activePage = page;
    }
    applyWorkspaceScope();
    if (activePage) splitController->activatePage(activePage, false);
    splitController->removeEmptyGroups();
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
                const auto* web = qobject_cast<WebDocumentPage*>(group->widget(index));
                const auto* pdf = qobject_cast<PdfDocumentPage*>(group->widget(index));
                const auto* block=qobject_cast<AssociatedBlockPage*>(group->widget(index));
                const QString webOwner = web ? workspaceForFile(web->fileName()) : pdf ? workspaceForFile(pdf->fileName())
                    : block && block->ownerDocument() ? (block->ownerDocument()->fileName().isEmpty()
                        ? block->ownerDocument()->property("associatedWorkspace").toString() : workspaceForFile(block->ownerDocument()->fileName())) : QString();
                const bool visible = (web || pdf || block) ? (webOwner.isEmpty() || identityKey(webOwner) == identityKey(activeWorkspaceRoot)) : editorVisibleInWorkspaceScope(editor);
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
    repairContextPage();
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
              ->documentMetadataForEditor(editor)
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
        lastActionEditor=editor;
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
                if (splitController->activeGroup()==group)
                    emit workspaceSessionStateChanged();
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

    splitController->registerPage(editorPointer, {}, QStringLiteral("code"));
    group->addTab(editorPointer, QString());
    splitController->activatePage(editorPointer, false);
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
    if (blockDragController) blockDragController->attachEditor(editorPointer);
    connect(editorPointer,&MyCodeEditor::editorBlockPageRequested,this,[this,editorPointer](const QString& id) {
        openAssociatedBlock(sharedDocumentForEditor(editorPointer),id,splitController->groupForPage(editorPointer));
    });
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
    const QString& fileName,
    const QByteArray* expectedSourceSha256)
{
    if (!sharedDocuments)
        return nullptr;
    SharedDocument* document = sharedDocuments->documentById(documentId);
    if (!document && !fileName.isEmpty()) document = sharedDocuments->documentForFile(fileName);
    const bool acquired = !document && !fileName.isEmpty();
    if (acquired) document = acquireFileDocument(fileName);
    // Validate the actual shared buffer before detaching or rebinding a view.
    // Re-reading the path here would miss dirty buffers and introduce another
    // disk generation between validation and acquisition.
    if (document && expectedSourceSha256
        && (expectedSourceSha256->isEmpty()
            || QCryptographicHash::hash(document->textDocument()->toPlainText().toUtf8(),
                                        QCryptographicHash::Sha256) != *expectedSourceSha256)) {
        if (acquired) sharedDocuments->releaseIfUnused(document);
        return nullptr;
    }
    return document;
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
    DocumentBlockStore::get(editor->blockModel())->bind(document->fileName(), workspaceForFile(document->fileName()));
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
    if (!sharedDocuments || RetiredDocument::isBlockDocument(fileName))
        return nullptr;
    if (SharedDocument* existing =
            sharedDocuments->documentForFile(
                fileName)) {
        if (loaded)
            *loaded = true;
        return existing;
    }
    const auto source = fileIo.readFile(qobject_cast<QWidget*>(parent()), fileName);
    if (!source.available) {
        return nullptr;
    }
    SharedDocument* document =
        sharedDocuments->acquire(
            fileName,
            source.text, &source);
    if (document) {
        document->setReadOnly(
            !QFileInfo(
                 document->fileName())
                 .isWritable());
        DocumentBlockStore::get(DocumentBlockModel::get(document->textDocument()))->bind(document->fileName(),workspaceForFile(document->fileName()));
        observeDocument(document, &source);
        if (loaded)
            *loaded = true;
    }
    return document;
}

bool TabManager::saveEditor(
    MyCodeEditor* editor,
    bool forceSaveAs,
    const QString& explicitFileName,
    WorkspaceFileSnapshot* savedGeneration)
{
    return editor && saveSharedDocument(sharedDocumentForEditor(editor), forceSaveAs, explicitFileName, savedGeneration);
}

bool TabManager::saveSharedDocument(SharedDocument* document, bool forceSaveAs,
    const QString& explicitFileName, WorkspaceFileSnapshot* savedGeneration)
{
    if (!document || sharedDocuments->documentById(document->documentId()) != document) return false;
    const QPointer<SharedDocument> guardedDocument(document);
    QPointer<MyCodeEditor> editor = document->views().value(0);
    for (auto* view : document->views()) { view->cancelSlashSearch(); view->finishBlockEditing(); }
    auto* model = DocumentBlockModel::get(document->textDocument()); model->endTextSession();
    auto* blocks = DocumentBlockStore::get(model);
    if (!forceSaveAs && !document->fileName().isEmpty()
        && (explicitFileName.isEmpty() || sameLexicalPath(explicitFileName, document->fileName()))) {
        QString reason;
        if (!document->validateSourcePath(&reason)) {
            writeRecoverySnapshot(document, true);
            emit fileSaveFailed(document->fileName(), reason);
            return false;
        }
    }
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
    if (!guardedDocument) return false;
    if (editor && !sameLexicalPath(fileName, document->fileName()) && editorTransitionGuard && !editorTransitionGuard(editor))
        return false;
    for (auto* page : webDocuments()) {
        if (!page->fileName().isEmpty() && EditorFileIdentity::same(fileName, page->fileName())) {
            emit fileSaveFailed(fileName, QStringLiteral("This file is already open in a document editor. Choose another path."));
            return false;
        }
    }
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
        document->matchesSourcePath(fileName);
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
    const QString savedText = editor ? editor->cachedDocumentText() : document->textDocument()->toPlainText();
    QByteArray savedRawFingerprint;
    // Bind the selected target once. The atomic writer uses this physical
    // path, so a rebound display alias cannot redirect its temporary file.
    const QString writePath = EditorFileIdentity::physicalPath(fileName);
    const auto revalidateOverwrite = [this, document, fileName, writePath, overwritesCurrentSource](QString* reason) {
        if (!sameLexicalPath(EditorFileIdentity::physicalPath(fileName), writePath)) {
            if (reason) *reason = QStringLiteral("The selected save target changed. Choose it again.");
            return false;
        }
        return !overwritesCurrentSource || !externalDocumentSync
            || externalDocumentSync->canOverwriteDocument(document, reason);
    };
    auto* templates = DocumentTemplateModel::get(document->textDocument());
    const auto templateMetadata = templates->serialize(fileName);
    QString metadataReadIssue;
    const auto storedMetadata = WorkspaceSessionStateService().loadDocumentTemplates(writePath, &metadataReadIssue);
    const bool needsTemplateCommit = !templates->instances().isEmpty()
        || !storedMetadata.isEmpty() || !metadataReadIssue.isEmpty();
    bool sourceWritten = false;
    const auto sourceWriter = [&](const DocumentBlockStore::Guard& publish, QString* failure) {
        sourceWritten = fileIo.writeTextFile(
            qobject_cast<QWidget*>(parent()),
            writePath,
            savedText,
            failure,
            &savedRawFingerprint,
            nullptr,
            revalidateOverwrite, document->fileFormat(), needsTemplateCommit ? &templateMetadata : nullptr, publish);
        return sourceWritten;
    };
    bool saved = false;
    if (blocks->needed()) {
        const bool writeSource = !overwritesCurrentSource || document->textDirty()
            || document->textRevision() != document->savedTextRevision() || !QFileInfo::exists(writePath);
        QString revisionIssue;
        const auto expected = DocumentBlockStore::fileRevision(writePath, &revisionIssue);
        const auto next = writeSource ? QCryptographicHash::hash(encodeDocumentText(savedText, document->fileFormat()), QCryptographicHash::Sha256) : expected;
        if (revisionIssue.isEmpty()) {
            saved = blocks->save(writePath, workspaceForFile(fileName), expected, next,
                !overwritesCurrentSource && !document->fileName().isEmpty(),
                writeSource ? DocumentBlockStore::SourceWriter(sourceWriter) : DocumentBlockStore::SourceWriter(),
                revalidateOverwrite, &saveFailure);
            if (saved) savedRawFingerprint = next;
        } else saveFailure = revisionIssue;
    } else saved = sourceWriter({}, &saveFailure);
    if (!saved) {
        writeRecoverySnapshot(document, true);
        emit fileSaveFailed(
            fileName,
            saveFailure.isEmpty()
                ? QStringLiteral("The file could not be saved.")
                : saveFailure);
        return false;
    }
    if (!sameLexicalPath(EditorFileIdentity::physicalPath(fileName), writePath)) {
        writeRecoverySnapshot(document, true);
        emit fileSaveFailed(fileName, QStringLiteral("The save target changed during commit. Review the saved file before continuing."));
        return false;
    }
    if (savedGeneration) {
        // Capture the committed generation before model/fileSaved signals can
        // re-enter application code. The path operation may authorize only it.
        *savedGeneration = WorkspaceFileOperationService::snapshot(writePath);
        if (savedGeneration->contentDigest != savedRawFingerprint || savedGeneration->objectId.isEmpty()) {
            writeRecoverySnapshot(document, true);
            emit fileSaveFailed(fileName, QStringLiteral("The saved file changed before its generation could be confirmed."));
            return false;
        }
    }
    const bool renamedDocument = !document->matchesSourcePath(fileName) || !sameLexicalPath(
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
            view->setProperty("sharedDocumentId", document->documentId());
            documentModel->refreshEditorState(view);
        }
    }
    if (sourceWritten && editor) documentModel->markSaved(editor);
    document->setReadOnly(
        !QFileInfo(fileName).isWritable());
    document->setExternalState(
        SharedDocumentExternalState::Current);
    const QDateTime savedModifiedUtc =
        QFileInfo(fileName).lastModified().toUTC();
    document->markSaved(savedRawFingerprint,
                        savedModifiedUtc);
    if (sourceWritten && externalDocumentSync)
        externalDocumentSync->noteDocumentSaved(document, savedRawFingerprint);
    clearRecoverySnapshot(document, true);
    updateTitlesForDocument(document);
    if (renamedDocument)
        applyWorkspaceScope();
    if (sourceWritten) emit fileSaved(document->fileName());
    else if (editor) emit editor->editorStatusMessageRequested("Block associations saved.");
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
    QWidget* dialogParent,
    const WorkspacePathMutationGuard& guard,
    QString* failureReason)
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
        [this, &guard, failureReason](SharedDocument* document) {
            if (!document)
                return false;
            if (guard.validate && !guard.validate(failureReason)) return false;
            WorkspaceFileSnapshot saved;
            if (!saveSharedDocument(document, false, {}, guard.acceptSave ? &saved : nullptr)) return false;
            return !guard.acceptSave || guard.acceptSave(saved, failureReason);
        });
}

bool TabManager::closeEditor(
    MyCodeEditor* editor,
    bool confirmUnsaved,
    bool remember)
{
    if (!editor || isTabLocked(editor))
        return false;
    if (!closingBatch && editorTransitionGuard && !editorTransitionGuard(editor))
        return false;
    SharedDocument* document =
        sharedDocumentForEditor(editor);
    if (!document)
        return false;
    if (confirmUnsaved
        && document->viewCount() == 1
        && document->consumerCount() == 0
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
        closed.ownerDocumentId = document->documentId();
        closed.fileName = document->fileName();
        closed.text =
            document->textDocument()->toPlainText();
        if (closed.fileName.isEmpty()) {
            QString issue; closed.associations = DocumentBlockStore::get(editor->blockModel())->recoveryState(&issue,crashRecoveryService->recoveryRootPath());
            if (!issue.isEmpty()) emit crashRecoveryOperationFailed(document->documentId(),issue);
        }
        closed.viewState =
            document->viewState(editor);
        rememberClosedGroup(closed, group);
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
    splitController->unregisterPage(editor);
    group->removeTab(index);
    // Removing a focused editor delivers a synchronous focus-out event.
    // Keep its original QTextDocument alive until that event has completed;
    // rebinding first can leave QWidgetTextControl evaluating a cursor that
    // belongs to the previous document.
    document->detachView(editor, true);
    lockedViewIds.remove(viewId);
    editor->deleteLater();
    if (document->viewCount() == 0 && document->consumerCount() == 0)
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
    if (splitController->hasPageCloseHandler(page))
        return splitController->requestPageClose(page);
    if (auto* document = qobject_cast<WebDocumentPage*>(page)) return closeWebDocument(document);
    if (auto* document = qobject_cast<PdfDocumentPage*>(page)) return closePdfDocument(document);
    if (auto* editor = qobject_cast<MyCodeEditor*>(page))
        return closeEditor(editor);
    if (!page || !page->property("toolPage").toBool())
        return false;

    const QString stableId = page->property("toolPageId").toString();
    if (!page->close())
        return false;
    splitController->unregisterPage(page);
    group->removeTab(index);
    page->deleteLater();
    splitController->removeEmptyGroups();
    emit toolPageClosed(stableId);
    emit workspaceSessionStateChanged();
    return true;
}

void TabManager::observeDocument(
    SharedDocument* document, const DocumentFileReadResult* initialFile)
{
    if (!document || observedDocuments.contains(document))
        return;
    observedDocuments.insert(document);
    auto* blockModel = DocumentBlockModel::get(document->textDocument());
    connect(blockModel,&DocumentBlockModel::presentationMoved,this,[this,source=QPointer<SharedDocument>(document)](DocumentBlockModel* target,const QString& id){
        if(source)transferAssociatedBlockContent(source,target,id);
    });
    for (const auto& block : blockModel->items()) if (block.detached) previousDetachedBlocks[document].insert(block.id);
    connect(blockModel, &DocumentBlockModel::changed, this, [this, guarded=QPointer<SharedDocument>(document)] {
        QTimer::singleShot(0, this, [this,guarded] { if (guarded) reconcileAssociatedBlocks(guarded); });
    });
    auto* associationTimer = new QTimer(document); associationTimer->setSingleShot(true); associationTimer->setInterval(500);
    connect(DocumentBlockModel::get(document->textDocument()), &DocumentBlockModel::changed, associationTimer, [associationTimer,document] {
        if (document->dirty()) associationTimer->start();
    });
    connect(associationTimer,&QTimer::timeout,this,[this,document] { if (document->dirty()) writeRecoverySnapshot(document,false); });
    if (externalDocumentSync)
        externalDocumentSync->trackDocument(document, initialFile);
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
                previousDetachedBlocks.remove(document);
                blockPageLocations.remove(document);
                if (recoveryQueue) recoveryQueue->cancel(document);
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
    for (auto* page : associatedBlockPages()) if (page->ownerDocument() == document) updateAssociatedBlockTitle(page);
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
    MyCodeEditor* editor, bool includeStatus) const
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

    if (!includeStatus) return title;
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
        QList<int> editorSlots;
        for (int index = 0;
             index < group->count();
             ++index) {
            if (MyCodeEditor* editor =
                    qobject_cast<MyCodeEditor*>(
                        group->widget(index))) {
                ordered.append(editor);
                editorSlots.append(index);
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
            if (source != editorSlots[target])
                group->tabBar()->moveTab(
                    source,
                    editorSlots[target]);
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
    if (previousActiveWebDocument && previousActiveWebDocument != group->widget(index)) checkpointWebRecovery(previousActiveWebDocument);
    previousActiveWebDocument = qobject_cast<WebDocumentPage*>(group->widget(index));
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
    lastActionEditor = editor;
    auto* page = group->widget(index);
    if (editor || qobject_cast<WebDocumentPage*>(page) || qobject_cast<PdfDocumentPage*>(page) || qobject_cast<AssociatedBlockPage*>(page))
        setContextPage(page);
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
            getCurrentDocumentMetadata());
        if (auto* page = currentWebDocument()) if (auto* host = qobject_cast<QWidget*>(parent()))
            host->setWindowTitle(page->title() + QStringLiteral(" - ZeroSlack"));
        if (auto* page = currentPdfDocument()) if (auto* host = qobject_cast<QWidget*>(parent()))
            host->setWindowTitle(page->title() + QStringLiteral(" - ZeroSlack"));
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
        if (auto* page = currentWebDocument()) return closeWebDocument(page) ? succeed() : fail(QStringLiteral("The document was not closed"));
        if (!editor) {
            auto* group=activeTabWidget();
            return group && closePage(group,group->currentIndex()) ? succeed() : fail(QStringLiteral("The selected page was not closed"));
        }
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
        if (editorCount() <= 0 && webDocuments().isEmpty())
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
    for (auto* editor : editors)
        if (editor && !isTabLocked(editor) && editorTransitionGuard && !editorTransitionGuard(editor))
            return false;
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
            && document->consumerCount() == 0
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

void TabManager::rememberClosedGroup(ClosedTabState& state, QTabWidget* group) const
{
    if (!splitController || !group) return;
    state.regionId = splitController->regionForGroup(group);
    state.groupId = splitController->groupId(group);
    state.groupIndex = splitController->groups(state.regionId).indexOf(group);
    if (auto* splitter = qobject_cast<QSplitter*>(group->parentWidget()))
        state.splitOrientation = splitter->orientation();
}

QTabWidget* TabManager::closedTabGroup(const ClosedTabState& state)
{
    if (!splitController) return tabWidget;
    if (auto* group = splitController->groupById(state.groupId)) return group;
    const auto groups = splitController->groups(state.regionId);
    if (groups.isEmpty()) return splitController->initialGroup();
    const int index = qBound(0, state.groupIndex, int(groups.size()));
    auto* neighbor = groups[qMin(index, int(groups.size()) - 1)];
    auto* restored = neighbor;
    if (neighbor->count()) {
        const bool before = index < groups.size();
        const auto direction = state.splitOrientation == Qt::Horizontal
            ? (before ? EditorSplitDirection::Left : EditorSplitDirection::Right)
            : (before ? EditorSplitDirection::Above : EditorSplitDirection::Below);
        restored = splitController->createSplit(neighbor, direction);
        if (restored && !state.groupId.isEmpty()) restored->setProperty("pageGroupId", state.groupId);
    }
    // Reusing an empty region anchor preserves its identity. Remaining close
    // records from the same removed group must follow that single replacement.
    if (restored && !state.groupId.isEmpty())
        for (auto& pending : recentlyClosedTabs)
            if (pending.groupId == state.groupId) pending.groupId = splitController->groupId(restored);
    return restored;
}
