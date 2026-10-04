#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "symbolanalyzer.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"

WorkspaceSymbolAnalysisController::WorkspaceSymbolAnalysisController(
    QObject* parent)
    : QObject(parent)
{
}

void WorkspaceSymbolAnalysisController::setDocumentModel(DocumentModel* model)
{
    // Document events are intentionally owned by AnalysisScheduler. This
    // executor only accepts explicit, revisioned semantic requests.
    documentModel = model;
}

void WorkspaceSymbolAnalysisController::setProjectModel(ProjectModel* model)
{
    // Project events are intentionally owned by AnalysisScheduler so opening
    // and configuration changes cannot enter analysis through a second path.
    projectModel = model;
    projectSemanticStateCleared = !projectModel || !projectModel->isOpen();
}

void WorkspaceSymbolAnalysisController::setSymbolAnalyzer(
    SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisStarted,
            this,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisStarted);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisCompleted,
            this,
            [this](const QString& fileName, int symbolCount) {
                emit fileSymbolAnalysisFinished(fileName, symbolCount);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchProgress,
            this,
            [this](int filesDone,
                   int totalFiles,
                   const QString& currentFileName) {
                emit workspaceSymbolAnalysisProgress(currentFileName,
                                                     filesDone,
                                                     totalFiles);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::semanticAnalysisCommitted,
            this,
            [this](const SemanticAnalysisRequest& request, const SemanticSnapshotToken& publication) {
                const auto current = SemanticIndex::getInstance()->snapshotToken();
                if (request.compatibilityRequest || !workspaceAnalysisActive
                    || activeSemanticRequest.generation != request.generation
                    || activeSemanticRequest.project.semanticIdentity() != request.project.semanticIdentity()
                    || !publication.snapshot || current.snapshot != publication.snapshot
                    || current.revision != publication.revision)
                    return;
                onWorkspaceSymbolAnalysisCompleted(activeIncrementalPlan.affectedFiles.size(),
                                                    publication.snapshot->symbolRecordCount());
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::workspaceAnalysisExpired,
            this,
            &WorkspaceSymbolAnalysisController::
                onWorkspaceSymbolAnalysisExpired);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::semanticAnalysisPlanPrepared,
            this,
            [this](const IncrementalAnalysisPlan& plan) {
                activeIncrementalPlan = plan;
                emit semanticAnalysisPlanPrepared(plan);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::semanticAnalysisTelemetry,
            this,
            &WorkspaceSymbolAnalysisController::semanticAnalysisTelemetry);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::semanticAnalysisFailed,
            this,
            &WorkspaceSymbolAnalysisController::onSemanticAnalysisFailed);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::semanticAnalysisDropped,
            this,
            [this](const SemanticAnalysisRequest& request,
                   SemanticAnalysisRequestDisposition disposition) {
                if (request.compatibilityRequest || !workspaceAnalysisActive
                    || activeSemanticRequest.generation != request.generation)
                    return;
                if (disposition == SemanticAnalysisRequestDisposition::InputChanged
                    && !hasPendingSemanticRequest && request.inputRetryCount < 1) {
                    pendingSemanticRequest = request;
                    ++pendingSemanticRequest.inputRetryCount;
                    pendingSemanticRequest.computationRevision = 0;
                    pendingSemanticRequest.expectedSnapshotRevision = 0;
                    hasPendingSemanticRequest = true;
                    // The logical request remains pending while a fresh capture
                    // is made. The retry cannot recursively retry forever.
                    activeSemanticRequestDropNotified = true;
                } else {
                    notifyActiveRequestDropped(disposition);
                }
                onWorkspaceSymbolAnalysisExpired();
            });
    connect(symbolAnalyzer,
            &QObject::destroyed,
            this,
            [this]() {
                symbolAnalyzer = nullptr;
                ++workspaceStartGeneration;
                requestQueue.clear();
                activeRequestedProject = ProjectSnapshot();
                activeProject = ProjectSnapshot();
                activeSemanticRequest = SemanticAnalysisRequest();
                pendingSemanticRequest = SemanticAnalysisRequest();
                hasPendingSemanticRequest = false;
                workspaceAnalysisActive = false;
                activeWorkspaceAnalysisComplete = true;
            });
}

void WorkspaceSymbolAnalysisController::setCancelProvider(
    std::function<bool()> provider)
{
    cancelProvider = std::move(provider);
}

void WorkspaceSymbolAnalysisController::setCurrentFileProvider(
    std::function<QString()> provider)
{
    currentFileProvider = std::move(provider);
}
