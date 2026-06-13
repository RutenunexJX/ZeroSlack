#include "analysisscheduler.h"

#include <QTimer>
#include <utility>

AnalysisScheduler::AnalysisScheduler(QObject* parent)
    : QObject(parent)
{
    openDocumentAnalysis = new OpenDocumentAnalysisController(this);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::documentRefreshRequested,
            this,
            &AnalysisScheduler::documentRefreshRequested);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::relationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestRelationshipAnalysis);
    connect(openDocumentAnalysis,
            &OpenDocumentAnalysisController::relationshipAnalysisScheduled,
            this,
            &AnalysisScheduler::scheduleRelationshipAnalysis);

    relationshipAnalysisQueue = new RelationshipAnalysisQueue(this);
    relationshipAnalysisQueue->setContentProvider([this](const QString& fileName) {
        return contentForOpenFile(fileName);
    });
    connect(relationshipAnalysisQueue,
            &RelationshipAnalysisQueue::relationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestRelationshipAnalysis);

    relationshipAnalysis = new RelationshipAnalysisController(this);
    relationshipAnalysis->setRelationshipQueue(relationshipAnalysisQueue);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisProgress,
            this,
            &AnalysisScheduler::relationshipAnalysisProgress);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisError,
            this,
            &AnalysisScheduler::relationshipAnalysisError);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisCancelled,
            this,
            &AnalysisScheduler::relationshipAnalysisCancelled);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::relationshipAnalysisFinished,
            this,
            &AnalysisScheduler::relationshipAnalysisFinished);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisStarted,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisStarted);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisProgress,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisProgress);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisFinished,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisFinished);
    connect(relationshipAnalysis,
            &RelationshipAnalysisController::workspaceRelationshipAnalysisCancelled,
            this,
            &AnalysisScheduler::workspaceRelationshipAnalysisCancelled);

    relationshipResultPublisher = new RelationshipResultPublisher(this);
    relationshipAnalysis->setResultPublisher(relationshipResultPublisher);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataInvalidated,
            this,
            &AnalysisScheduler::relationshipDataInvalidated);
    connect(relationshipResultPublisher,
            &RelationshipResultPublisher::relationshipDataRefreshRequested,
            this,
            &AnalysisScheduler::relationshipDataRefreshRequested);

    workspaceSymbolAnalysis = new WorkspaceSymbolAnalysisController(this);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisStarted,
            this,
            &AnalysisScheduler::fileSymbolAnalysisStarted);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisFinished,
            this,
            &AnalysisScheduler::fileSymbolAnalysisFinished);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisStarted,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisStarted);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisProgress,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisFinished,
            this,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::diagnosticsRefreshRequested,
            this,
            &AnalysisScheduler::scheduleDiagnosticsRefresh);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisRequested,
            this,
            &AnalysisScheduler::requestWorkspaceRelationshipAnalysis);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisCancelRequested,
            this,
            &AnalysisScheduler::cancelWorkspaceRelationshipAnalysis);
    connect(workspaceSymbolAnalysis,
            &WorkspaceSymbolAnalysisController::relationshipDataClearRequested,
            relationshipResultPublisher,
            &RelationshipResultPublisher::clearAllRelationships);

    diagnosticsRefreshTimer = new QTimer(this);
    diagnosticsRefreshTimer->setSingleShot(true);
    diagnosticsRefreshTimer->setInterval(100);
    connect(diagnosticsRefreshTimer, &QTimer::timeout, this, [this]() {
        const QString fileName = pendingDiagnosticsRefreshFileName;
        pendingDiagnosticsRefreshFileName.clear();
        emit diagnosticsRefreshRequested(fileName);
    });

}

AnalysisScheduler::~AnalysisScheduler()
{
    cancelRelationshipAnalysis();
    cancelWorkspaceRelationshipAnalysis();
}

void AnalysisScheduler::setProjectModel(ProjectModel* model)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setProjectModel(model);
}

void AnalysisScheduler::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;

    symbolAnalyzer = analyzer;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setSymbolAnalyzer(analyzer);
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setSymbolAnalyzer(analyzer);
    if (relationshipAnalysis)
        relationshipAnalysis->setSymbolAnalyzer(analyzer);
}

void AnalysisScheduler::setOpenFileContentProvider(std::function<QString(const QString&)> provider)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->setOpenFileContentProvider(std::move(provider));
}

void AnalysisScheduler::setWorkspaceOpenProvider(std::function<bool()> provider)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->setWorkspaceOpenProvider(std::move(provider));
}

void AnalysisScheduler::setWorkspaceSymbolCancelProvider(std::function<bool()> provider)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setCancelProvider(std::move(provider));
}

void AnalysisScheduler::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    if (relationshipResultPublisher)
        relationshipResultPublisher->setRelationshipEngine(engine);
}

void AnalysisScheduler::setRelationshipBuilder(SmartRelationshipBuilder* builder)
{
    if (relationshipAnalysis)
        relationshipAnalysis->setRelationshipBuilder(builder);
}

void AnalysisScheduler::scheduleRelationshipAnalysis(const QString& fileName,
                                                     const QString& content,
                                                     int delayMs)
{
    if (fileName.isEmpty()
        || content.isEmpty()
        || !relationshipAnalysis
        || !relationshipAnalysis->hasRelationshipBuilder()) {
        return;
    }

    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->schedule(fileName, content, delayMs);
}

void AnalysisScheduler::cancelScheduledRelationshipAnalysis(const QString& fileName)
{
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(fileName);
}

void AnalysisScheduler::cancelAllScheduledRelationshipAnalyses()
{
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->cancelAll();
}

bool AnalysisScheduler::hasScheduledRelationshipAnalysis(
    const QString& fileName) const
{
    return relationshipAnalysisQueue
        ? relationshipAnalysisQueue->hasScheduled(fileName)
        : false;
}

void AnalysisScheduler::requestRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (relationshipAnalysis)
        relationshipAnalysis->requestSingleFileAnalysis(fileName, content);
}

void AnalysisScheduler::cancelRelationshipAnalysis()
{
    if (relationshipAnalysis)
        relationshipAnalysis->cancelSingleFileAnalysis();
}

void AnalysisScheduler::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project)
{
    if (relationshipAnalysis)
        relationshipAnalysis->requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::cancelWorkspaceRelationshipAnalysis()
{
    if (relationshipAnalysis)
        relationshipAnalysis->cancelWorkspaceAnalysis();
}

void AnalysisScheduler::scheduleDiagnosticsRefresh(const QString& fileName)
{
    pendingDiagnosticsRefreshFileName = fileName;
    if (diagnosticsRefreshTimer)
        diagnosticsRefreshTimer->start();
}
