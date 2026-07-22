#include "workspacesymbolanalysiscontroller.h"

#include "effectivevalueservice.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QFileInfo>

namespace {
bool sameSemanticFile(const QString& first, const QString& second)
{
    if (first.isEmpty() || second.isEmpty())
        return false;
    return QFileInfo(first).absoluteFilePath().compare(
               QFileInfo(second).absoluteFilePath(),
               Qt::CaseInsensitive)
        == 0;
}

bool requestChangesFile(const SemanticAnalysisRequest& request,
                        const QString& fileName)
{
    for (const QString& changedFile : request.changedFiles) {
        if (sameSemanticFile(changedFile, fileName))
            return true;
    }
    return false;
}

void removeFileEntry(QHash<QString, QString>* values,
                     const QString& fileName)
{
    if (!values)
        return;
    for (auto it = values->begin(); it != values->end();) {
        if (sameSemanticFile(it.key(), fileName))
            it = values->erase(it);
        else
            ++it;
    }
}

void removeFileEntry(QHash<QString, std::uint64_t>* values,
                     const QString& fileName)
{
    if (!values)
        return;
    for (auto it = values->begin(); it != values->end();) {
        if (sameSemanticFile(it.key(), fileName))
            it = values->erase(it);
        else
            ++it;
    }
}
}

std::uint64_t
WorkspaceSymbolAnalysisController::activeSemanticGenerationForFile(
    const QString& fileName) const
{
    if (!workspaceAnalysisActive || !activeSemanticRequest.isValid()
        || fileName.isEmpty()) {
        return 0;
    }
    const QString target = QFileInfo(fileName).absoluteFilePath();
    for (const QString& candidate
         : activeSemanticRequest.project.systemVerilogFiles) {
        if (QFileInfo(candidate).absoluteFilePath().compare(
                target, Qt::CaseInsensitive)
            == 0) {
            return activeSemanticRequest.generation;
        }
    }
    return 0;
}

void WorkspaceSymbolAnalysisController::notifyActiveRequestDropped(
    SemanticAnalysisRequestDisposition disposition)
{
    if (activeSemanticRequestDropNotified
        || !activeSemanticRequest.isValid()) {
        return;
    }
    activeSemanticRequestDropNotified = true;
    emit semanticAnalysisRequestDropped(activeSemanticRequest, disposition);
}

void WorkspaceSymbolAnalysisController::dropPendingRequest(
    SemanticAnalysisRequestDisposition disposition)
{
    if (!hasPendingSemanticRequest)
        return;
    const SemanticAnalysisRequest dropped = pendingSemanticRequest;
    pendingSemanticRequest = SemanticAnalysisRequest();
    hasPendingSemanticRequest = false;
    if (dropped.isValid())
        emit semanticAnalysisRequestDropped(dropped, disposition);
}

void WorkspaceSymbolAnalysisController::requestSemanticAnalysis(
    const SemanticAnalysisRequest& request)
{
    if (!request.isValid() || !symbolAnalyzer)
        return;

    if (workspaceAnalysisActive) {
        dropPendingRequest(
            SemanticAnalysisRequestDisposition::Superseded);
        pendingSemanticRequest = request;
        hasPendingSemanticRequest = true;
        if (request.project.isOpen())
            requestQueue.queueLatest(request.project);
        emit workspaceAnalysisRequestQueued(requestQueue.telemetry());
        notifyActiveRequestDropped(
            SemanticAnalysisRequestDisposition::Superseded);
        symbolAnalyzer->expireWorkspaceAnalysis();
        return;
    }

    startSemanticAnalysis(request);
}

void WorkspaceSymbolAnalysisController::invalidateSemanticAnalysis(
    const QString& fileName,
    std::uint64_t documentRevision)
{
    Q_UNUSED(documentRevision)

    const bool activeTouchesFile = workspaceAnalysisActive
        && requestChangesFile(activeSemanticRequest, fileName);
    const bool pendingTouchesFile = hasPendingSemanticRequest
        && requestChangesFile(pendingSemanticRequest, fileName);
    if (!activeTouchesFile && !pendingTouchesFile)
        return;

    if (pendingTouchesFile) {
        if (pendingSemanticRequest.impactHint
            == SemanticChangeImpact::WorkspaceConfig) {
            dropPendingRequest(
                SemanticAnalysisRequestDisposition::Invalidated);
        } else {
            pendingSemanticRequest.changedFiles.removeIf(
                [&fileName](const QString& candidate) {
                    return sameSemanticFile(candidate, fileName);
                });
            removeFileEntry(&pendingSemanticRequest.sourceOverrides,
                            fileName);
            removeFileEntry(&pendingSemanticRequest.documentRevisions,
                            fileName);
            if (sameSemanticFile(pendingSemanticRequest.triggerFile,
                                 fileName)) {
                pendingSemanticRequest.triggerFile =
                    pendingSemanticRequest.changedFiles.isEmpty()
                    ? QString()
                    : pendingSemanticRequest.changedFiles.constLast();
            }
            if (pendingSemanticRequest.changedFiles.isEmpty()) {
                dropPendingRequest(
                    SemanticAnalysisRequestDisposition::Invalidated);
            }
        }
    }

    if (!activeTouchesFile || !symbolAnalyzer)
        return;
    SemanticAnalysisRequest continuation;
    if (!hasPendingSemanticRequest
        && activeSemanticRequest.impactHint
            != SemanticChangeImpact::WorkspaceConfig) {
        continuation = activeSemanticRequest;
        continuation.changedFiles.removeIf(
            [&fileName](const QString& candidate) {
                return sameSemanticFile(candidate, fileName);
            });
        removeFileEntry(&continuation.sourceOverrides, fileName);
        removeFileEntry(&continuation.documentRevisions, fileName);
        if (sameSemanticFile(continuation.triggerFile, fileName)) {
            continuation.triggerFile = continuation.changedFiles.isEmpty()
                ? QString()
                : continuation.changedFiles.constLast();
        }
    }
    notifyActiveRequestDropped(
        SemanticAnalysisRequestDisposition::Invalidated);
    if (!continuation.changedFiles.isEmpty())
        emit semanticAnalysisContinuationRequired(continuation);
    ++workspaceStartGeneration;
    symbolAnalyzer->expireWorkspaceAnalysis();
}

void WorkspaceSymbolAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen() || project.systemVerilogFiles.isEmpty())
        return;

    SemanticAnalysisRequest request;
    request.generation = ++compatibilityRequestGeneration;
    request.reason = SemanticAnalysisReason::ExplicitRequest;
    request.impactHint = SemanticChangeImpact::WorkspaceConfig;
    request.project = project;
    request.changedFiles = project.systemVerilogFiles;
    requestSemanticAnalysis(request);
}

void WorkspaceSymbolAnalysisController::startSemanticAnalysis(
    const SemanticAnalysisRequest& request)
{
    if (!symbolAnalyzer || !request.isValid())
        return;

    ++workspaceStartGeneration;
    activeSemanticRequest = request;
    activeSemanticRequestDropNotified = false;
    activeRequestedProject = request.project;
    activeProject = request.project;
    activeIncrementalPlan = IncrementalAnalysisPlan();
    workspaceAnalysisActive = true;
    activeWorkspaceAnalysisComplete = true;
    projectSemanticStateCleared = false;
    if (request.project.isOpen())
        requestQueue.start(request.project);

    emit semanticAnalysisRequestStarted(request);
    emit workspaceSymbolAnalysisStarted(
        request.project,
        request.project.systemVerilogFiles.size());
    symbolAnalyzer->startSemanticAnalysisAsync(request);
}

void WorkspaceSymbolAnalysisController::startPendingSemanticAnalysis()
{
    if (!hasPendingSemanticRequest)
        return;
    const SemanticAnalysisRequest request = pendingSemanticRequest;
    pendingSemanticRequest = SemanticAnalysisRequest();
    hasPendingSemanticRequest = false;
    startSemanticAnalysis(request);
}

void WorkspaceSymbolAnalysisController::cancelWorkspaceAnalysis()
{
    if (!workspaceAnalysisActive && !hasPendingSemanticRequest)
        return;

    notifyActiveRequestDropped(
        SemanticAnalysisRequestDisposition::Cancelled);
    dropPendingRequest(
        SemanticAnalysisRequestDisposition::Cancelled);
    const WorkspaceAnalysisRequestTelemetry telemetry = requestQueue.cancel();
    ++workspaceStartGeneration;
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeSemanticRequest = SemanticAnalysisRequest();
    activeSemanticRequestDropNotified = false;
    activeWorkspaceAnalysisComplete = true;
    emit workspaceSymbolAnalysisCancelled(telemetry);
    if (symbolAnalyzer)
        symbolAnalyzer->expireWorkspaceAnalysis();
}

void WorkspaceSymbolAnalysisController::clearProjectSemanticState()
{
    notifyActiveRequestDropped(
        SemanticAnalysisRequestDisposition::Cancelled);
    dropPendingRequest(
        SemanticAnalysisRequestDisposition::Cancelled);
    ++workspaceStartGeneration;
    projectSemanticStateCleared = true;
    workspaceAnalysisActive = false;
    activeWorkspaceAnalysisComplete = true;
    requestQueue.clear();
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeSemanticRequest = SemanticAnalysisRequest();
    activeSemanticRequestDropNotified = false;
    activeIncrementalPlan = IncrementalAnalysisPlan();
    hasPendingSemanticRequest = false;
    activeWorkspaceRoot.clear();
    completedWorkspaceAnalysisKeys.clear();
    if (symbolAnalyzer) {
        symbolAnalyzer->setWorkspaceFileAnalysisBands({});
        symbolAnalyzer->expireWorkspaceAnalysis();
    }
    EffectiveValueService::getInstance()->clearPublishedFacts();
    emit workspaceRelationshipAnalysisCancelRequested();
    SemanticIndex::getInstance()->clearSemanticState();
    emit relationshipDataClearRequested();
    emit diagnosticsRefreshRequested(QString());
}

void WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisCompleted(
    int filesAnalyzed,
    int totalSymbols)
{
    if (!workspaceAnalysisActive)
        return;

    const SemanticAnalysisRequest request = activeSemanticRequest;
    const ProjectSnapshot project = activeProject;
    const IncrementalAnalysisPlan plan = activeIncrementalPlan;
    ProjectSnapshot ignoredPendingProject;
    requestQueue.finishAndTakePending(&ignoredPendingProject);
    const WorkspaceAnalysisRequestTelemetry telemetry =
        requestQueue.telemetry();
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeSemanticRequest = SemanticAnalysisRequest();
    activeSemanticRequestDropNotified = false;
    activeIncrementalPlan = IncrementalAnalysisPlan();

    emit workspaceAnalysisRequestResolved(telemetry);
    emit diagnosticsRefreshRequested(QString());
    emit semanticAnalysisRequestFinished(request, plan);
    emit workspaceSymbolAnalysisFinished(project,
                                         filesAnalyzed,
                                         totalSymbols);
    startPendingSemanticAnalysis();
}

void WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisExpired()
{
    if (!workspaceAnalysisActive)
        return;

    notifyActiveRequestDropped(
        SemanticAnalysisRequestDisposition::Expired);
    ProjectSnapshot ignoredPendingProject;
    requestQueue.finishAndTakePending(&ignoredPendingProject);
    const WorkspaceAnalysisRequestTelemetry telemetry =
        requestQueue.telemetry();
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeSemanticRequest = SemanticAnalysisRequest();
    activeSemanticRequestDropNotified = false;
    activeIncrementalPlan = IncrementalAnalysisPlan();
    emit workspaceAnalysisRequestResolved(telemetry);
    startPendingSemanticAnalysis();
}

void WorkspaceSymbolAnalysisController::onSemanticAnalysisFailed(
    const SemanticAnalysisRequest& request,
    const QString& error)
{
    if (!workspaceAnalysisActive
        || activeSemanticRequest.generation != request.generation) {
        return;
    }

    ProjectSnapshot ignoredPendingProject;
    requestQueue.finishAndTakePending(&ignoredPendingProject);
    workspaceAnalysisActive = false;
    activeRequestedProject = ProjectSnapshot();
    activeProject = ProjectSnapshot();
    activeSemanticRequest = SemanticAnalysisRequest();
    activeSemanticRequestDropNotified = false;
    activeIncrementalPlan = IncrementalAnalysisPlan();
    emit semanticAnalysisRequestFailed(request, error);
    startPendingSemanticAnalysis();
}
