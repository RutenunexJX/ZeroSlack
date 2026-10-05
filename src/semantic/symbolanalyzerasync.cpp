#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "semanticindexsnapshot.h"
#include "symbolanalyzerworkspace.h"

#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QScopeGuard>
#include <algorithm>
#include <utility>

namespace {
QString normalizedAsyncAnalysisFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
}

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    semanticAnalysisThreadPool.setMaxThreadCount(1);
    semanticAnalysisThreadPool.setThreadPriority(QThread::LowPriority);
    semanticAnalysisThreadPool.setObjectName(
        QStringLiteral("ZeroSlackSemanticAnalysis"));
    semanticRetirementThreadPool.setMaxThreadCount(1);
    semanticRetirementThreadPool.setThreadPriority(QThread::LowestPriority);
    semanticRetirementThreadPool.setObjectName(
        QStringLiteral("ZeroSlackSemanticRetirement"));
    workspaceRetirementTimer = new QTimer(this);
    workspaceRetirementTimer->setSingleShot(true);
    connect(workspaceRetirementTimer, &QTimer::timeout,
            this, &SymbolAnalyzer::flushDeferredWorkspaceRetirements);
    connect(this, &SymbolAnalyzer::workspaceAnalysisExpired,
            this, &SymbolAnalyzer::launchPendingFileAnalysis, Qt::QueuedConnection);
    connect(this, &SymbolAnalyzer::batchAnalysisCompleted,
            this, &SymbolAnalyzer::launchPendingFileAnalysis, Qt::QueuedConnection);
    connect(this, &SymbolAnalyzer::semanticAnalysisDropped,
            this, &SymbolAnalyzer::launchPendingFileAnalysis, Qt::QueuedConnection);
    connect(this, &SymbolAnalyzer::semanticAnalysisFailed,
            this, &SymbolAnalyzer::launchPendingFileAnalysis, Qt::QueuedConnection);
    auto completeFiles = [this](const SemanticAnalysisRequest& request, const QString& error) {
        for (const auto& file : request.compatibilityCompletionFiles) {
            fileAnalysisCancelFlags.remove(normalizedAsyncAnalysisFileName(file));
            if (!error.isEmpty())
                emit fileAnalysisRejected(file, request.documentRevisions.value(file), error);
        }
    };
    connect(this, &SymbolAnalyzer::semanticAnalysisCommitted, this,
        [completeFiles](const auto& request, const auto&) { completeFiles(request, {}); });
    connect(this, &SymbolAnalyzer::semanticAnalysisFailed, this,
        [completeFiles](const auto& request, const auto& error) { completeFiles(request, error); });
    connect(this, &SymbolAnalyzer::semanticAnalysisDropped, this,
        [completeFiles](const auto& request, auto) { completeFiles(request, QStringLiteral("Semantic request expired")); });
    workspacePublicationTimer = new QTimer(this);
    workspacePublicationTimer->setSingleShot(true);
    connect(workspacePublicationTimer,
            &QTimer::timeout,
            this,
            &SymbolAnalyzer::publishPendingWorkspaceAnalysis);
}

SymbolAnalyzer::~SymbolAnalyzer()
{
    shutdown();
    // A synchronous publication observer can call shutdown before its caller
    // hands the detached result to the pool. Drain these late releases too.
    waitForPublicationRetirements();
}

void SymbolAnalyzer::setWorkspaceWorkerStartGateForTesting(
    WorkspaceWorkerStartGateForTesting gate)
{
    workspaceWorkerStartGateForTesting = std::move(gate);
}

void SymbolAnalyzer::setPublicationRetirementGateForTesting(
    PublicationRetirementGateForTesting gate)
{
    publicationRetirementGateForTesting = std::move(gate);
}

int SymbolAnalyzer::pendingPublicationRetirementsForTesting() const
{
    return pendingPublicationRetirements->load(std::memory_order_acquire)
        + static_cast<int>(deferredWorkspaceRetirements.size());
}

int SymbolAnalyzer::publicationRetirementEnqueueCountForTesting() const
{
    return publicationRetirementEnqueueCount.load(std::memory_order_acquire);
}

int SymbolAnalyzer::rejectedPublicationRetirementsForTesting() const
{
    return rejectedPublicationRetirements.load(std::memory_order_acquire);
}

void SymbolAnalyzer::startAnalyzeProjectAsync(
    const ProjectSnapshot& project,
    std::function<bool()> isCancelled,
    const QList<OpenDocumentContent>& openDocuments)
{
    if (shutdownStarted || !project.isOpen() || (isCancelled && isCancelled()))
        return;
    startSemanticAnalysisAsync(projectRequest(project, openDocuments), std::move(isCancelled));
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndWait()
{
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    if (!workspaceAnalysisWatcher) {
        workspaceAnalysisCancelFlag.reset();
        return;
    }

    QFutureWatcher<WorkspaceAnalysisResult>* watcher =
        workspaceAnalysisWatcher;
    const auto request = std::exchange(workspaceAnalysisRequest, std::nullopt);
    const auto cancellation = workspaceAnalysisCancelFlag;
    workspaceAnalysisWatcher = nullptr;
    disconnect(watcher, nullptr, this, nullptr);
    QFuture<WorkspaceAnalysisResult> future = watcher->future();
    future.waitForFinished();
    if (future.resultCount() > 0)
        retireWorkspaceResult(future.takeResult());
    delete watcher;
    if (workspaceAnalysisCancelFlag == cancellation)
        workspaceAnalysisCancelFlag.reset();
    // Disconnecting the watcher transfers its terminal obligation here. Send
    // it after releasing the slot, including during shutdown, so a synchronous
    // adapter can leave its nested event loop without waiting for global exit.
    if (request)
        emit semanticAnalysisDropped(*request, SemanticAnalysisRequestDisposition::Cancelled);
}

void SymbolAnalyzer::expireWorkspaceAnalysis()
{
    ++workspaceAnalysisGeneration;
    const bool hadPendingPublication =
        pendingWorkspacePublication != nullptr;
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    cancelWorkspacePublication();
    if (!workspaceAnalysisWatcher || !workspaceAnalysisWatcher->isRunning()) {
        if (hadPendingPublication)
            emit workspaceAnalysisExpired();
        return;
    }

    // Cancellation is cooperative. Keep the actual result available for retirement.
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndInvalidate()
{
    cancelAllAnalysesAndWait();
    EffectiveValueService::getInstance()->clearPublishedFacts();
}

void SymbolAnalyzer::requestCancelAllAnalyses()
{
    const auto pendingRequest = pendingCompatibilityRequest;
    ++workspaceAnalysisGeneration;
    ++workspaceEpoch;
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    for (const auto& cancellation :
         std::as_const(fileAnalysisCancelFlags)) {
        if (cancellation)
            cancellation->store(true, std::memory_order_relaxed);
    }
    overlayProject = {};
    pendingCompatibilityRequest.reset();
    pendingCompatibilityCancellation = {};
    pendingFileDocuments.clear();
    pendingFileOrder.clear();
    cancelWorkspacePublication();
    if (pendingRequest)
        emit semanticAnalysisDropped(*pendingRequest, SemanticAnalysisRequestDisposition::Cancelled);
}

void SymbolAnalyzer::cancelAllAnalysesAndWait()
{
    requestCancelAllAnalyses();
    cancelWorkspaceAnalysisAndWait();
    waitForPublicationRetirements();
}

void SymbolAnalyzer::shutdown()
{
    if (shutdownStarted)
        return;

    // Closing this gate precedes every wait. No caller can start a new worker
    // or publication while teardown drains already-owned work.
    shutdownStarted = true;
    requestCancelAllAnalyses();
    cancelWorkspaceAnalysisAndWait();
    const QStringList retainedKeys = retainedWorkspaceLru;
    for (const QString& key : retainedKeys)
        forgetRetainedState(key);
    if (activeWorkspaceState) {
        SemanticPublicationRetirementPayload retired;
        retired.workspaceState = std::exchange(activeWorkspaceState, {});
        retirePublicationState(std::move(retired));
    }

    // Shutdown may precede the next GUI turn. Dispatch still-owned workspace
    // snapshots before sealing, so cancellation never destroys them inline or
    // leaves their release dependent on another event-loop iteration.
    flushDeferredWorkspaceRetirements();
    // Worker watchers and both zero-delay timers are detached or cancelled.
    // Sealing disables worker gates and new semantic work. A publication
    // observer may still hand its detached result back after this call unwinds;
    // the owned retirement pool accepts that disposal and is drained at destruction.
    publicationRetirementQueueOpen = false;
    waitForPublicationRetirements();
    publicationRetirementGateForTesting = {};
}

void SymbolAnalyzer::analyzeFileContentAsync(
    const QString& fileName,
    const QString& content,
    std::uint64_t documentRevision)
{
    if (shutdownStarted)
        return;
    analyzeOverlayDocumentsAsync(
        {{fileName, content, documentRevision}});
}

void SymbolAnalyzer::analyzeStandaloneFileContentAsync(
    const QString& fileName, const QString& content, std::uint64_t documentRevision)
{
    analyzeOverlayDocumentsAsync({{fileName, content, documentRevision}}, true);
}

void SymbolAnalyzer::analyzeOverlayDocumentsAsync(
    const QList<OpenDocumentContent>& documents, bool standalone)
{
    if (shutdownStarted)
        return;
    QList<OpenDocumentContent> overlays;
    for (const OpenDocumentContent& document : documents) {
        if (!document.fileName.isEmpty()
            && isSystemVerilogFile(document.fileName)
            && !document.content.isNull()) {
            overlays.append(document);
        }
    }
    if (overlays.isEmpty())
        return;

    if (hasWorkspaceAnalysisInFlight()) {
        qsizetype queuedBytes = 0;
        for (auto it = pendingFileDocuments.cbegin(); it != pendingFileDocuments.cend(); ++it)
            queuedBytes += it->document.content.size() * qsizetype(sizeof(QChar));
        for (const auto& document : overlays) {
            const QString key = normalizedAsyncAnalysisFileName(document.fileName);
            if (const auto previous = fileAnalysisCancelFlags.value(key))
                previous->store(true, std::memory_order_relaxed);
            const qsizetype oldBytes = pendingFileDocuments.value(key).document.content.size() * qsizetype(sizeof(QChar));
            const qsizetype newBytes = document.content.size() * qsizetype(sizeof(QChar));
            if ((!pendingFileDocuments.contains(key) && pendingFileDocuments.size() >= maximumPendingFileDocuments)
                || queuedBytes - oldBytes + newBytes > maximumPendingFileBytes) {
                emit fileAnalysisRejected(document.fileName, document.documentRevision,
                    QStringLiteral("Semantic request queue budget exceeded"));
                continue;
            }
            queuedBytes += newBytes - oldBytes;
            if (!pendingFileDocuments.contains(key))
                pendingFileOrder.append(key);
            pendingFileDocuments.insert(key, {document, standalone, workspaceEpoch});
        }
        return;
    }

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    for (const auto& overlay : overlays) {
        const auto key = normalizedAsyncAnalysisFileName(overlay.fileName);
        if (const auto previous = fileAnalysisCancelFlags.value(key))
            previous->store(true, std::memory_order_relaxed);
        fileAnalysisCancelFlags.insert(key, cancellation);
    }
    const auto request = documentRequest(overlays, standalone, true);
    startSemanticAnalysisAsync(request, [cancellation] {
        return cancellation->load(std::memory_order_relaxed);
    });
}

void SymbolAnalyzer::cancelFileAnalysis(const QString& fileName)
{
    const QString analysisKey =
        normalizedAsyncAnalysisFileName(fileName);
    if (analysisKey.isEmpty())
        return;

    if (const auto cancellation =
            fileAnalysisCancelFlags.value(analysisKey)) {
        cancellation->store(true, std::memory_order_relaxed);
    }

    pendingFileDocuments.remove(analysisKey);
    pendingFileOrder.removeAll(analysisKey);
}

void SymbolAnalyzer::launchPendingFileAnalysis()
{
    if (shutdownStarted || hasWorkspaceAnalysisInFlight())
        return;
    if (pendingCompatibilityRequest) {
        auto request = std::exchange(pendingCompatibilityRequest, std::nullopt);
        auto cancelled = std::exchange(pendingCompatibilityCancellation, {});
        startSemanticAnalysisAsync(*request, std::move(cancelled));
        return;
    }
    QList<OpenDocumentContent> documents;
    bool standalone = false;
    while (!pendingFileOrder.isEmpty()) {
        const QString key = pendingFileOrder.takeFirst();
        const auto next = pendingFileDocuments.take(key);
        if (!next.standalone && next.epoch != workspaceEpoch) {
            emit fileAnalysisRejected(next.document.fileName, next.document.documentRevision,
                QStringLiteral("Workspace changed before semantic analysis"));
            continue;
        }
        if (!documents.isEmpty() && next.standalone) {
            pendingFileOrder.prepend(key);
            pendingFileDocuments.insert(key, next);
            break;
        }
        standalone = next.standalone;
        documents.append(next.document);
        if (standalone)
            break;
    }
    if (!documents.isEmpty())
        analyzeOverlayDocumentsAsync(documents, standalone);
}
