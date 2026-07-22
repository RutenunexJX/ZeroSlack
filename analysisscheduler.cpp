#include "analysisscheduler.h"

#include "relationshipresultpublisher.h"
#include "symbolanalyzer.h"

#include <utility>

AnalysisScheduler::AnalysisScheduler(QObject* parent)
    : QObject(parent)
{
    setupOpenDocumentAnalysis();
    setupRelationshipAnalysis();
    setupWorkspaceSymbolAnalysis();
    setupDiagnosticsRefreshAndWorkspaceRequests();
}

AnalysisScheduler::~AnalysisScheduler()
{
    shutdown();
}

void AnalysisScheduler::shutdown()
{
    if (shuttingDown)
        return;
    shuttingDown = true;

    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);
    documentModel = nullptr;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);
    projectModel = nullptr;
    for (QTimer* timer : std::as_const(externalFileTimers)) {
        if (timer)
            timer->stop();
    }
    externalFileTimers.clear();
    SymbolAnalyzer* analyzer = symbolAnalyzer.data();
    if (openDocumentAnalysis)
        openDocumentAnalysis->shutdown();
    if (workspaceSymbolAnalysis) {
        workspaceSymbolAnalysis->setDocumentModel(nullptr);
        workspaceSymbolAnalysis->setProjectModel(nullptr);
        workspaceSymbolAnalysis->setCancelProvider({});
        workspaceSymbolAnalysis->setCurrentFileProvider({});
    }
    cancelAllScheduledRelationshipAnalyses();

    // Phase one is deliberately non-blocking. Every worker family must see
    // cancellation before shutdown joins any future: with a saturated global
    // QThreadPool, one gated task can otherwise keep another task queued and
    // make the first wait permanent.
    cancelWorkspaceAnalysis();
    if (analyzer)
        analyzer->requestCancelAllAnalyses();
    if (relationshipAnalysis)
        relationshipAnalysis->requestCancelAllAnalyses();

    // Detach every controller before the terminal analyzer wait. Queued
    // controller callbacks can no longer start a replacement publication.
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setSymbolAnalyzer(nullptr);

    // Phase two joins only after cancellation has been broadcast globally.
    if (relationshipAnalysis)
        relationshipAnalysis->waitForAllAnalyses();
    if (analyzer)
        analyzer->shutdown();

    if (relationshipAnalysis) {
        relationshipAnalysis->setRelationshipBuilder(nullptr);
        relationshipAnalysis->setSymbolAnalyzer(nullptr);
    }
    if (relationshipResultPublisher)
        relationshipResultPublisher->setRelationshipEngine(nullptr);
    symbolAnalyzer = nullptr;
}

void AnalysisScheduler::setProjectModel(ProjectModel* model)
{
    if (shuttingDown || projectModel == model)
        return;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);
    projectModel = model;
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setProjectModel(model);
    if (!projectModel)
        return;
    connect(projectModel,
            &ProjectModel::projectChanged,
            this,
            &AnalysisScheduler::onProjectChanged);
    connect(projectModel,
            &ProjectModel::projectClosed,
            this,
            &AnalysisScheduler::onProjectClosed);
    if (projectModel->isOpen())
        onProjectChanged(projectModel->snapshot());
}

void AnalysisScheduler::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (shuttingDown)
        return;
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
    openFileContentProvider = provider;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setOpenFileContentProvider(std::move(provider));
}

void AnalysisScheduler::setWorkspaceOpenProvider(std::function<bool()> provider)
{
    workspaceOpenProvider = provider;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setWorkspaceOpenProvider(std::move(provider));
}

void AnalysisScheduler::setWorkspaceSymbolCancelProvider(std::function<bool()> provider)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setCancelProvider(std::move(provider));
}

void AnalysisScheduler::setCurrentFileProvider(std::function<QString()> provider)
{
    currentFileProvider = provider;
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setCurrentFileProvider(std::move(provider));
}

void AnalysisScheduler::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    requestSemanticAnalysis(SemanticAnalysisReason::ExplicitRequest,
                            SemanticChangeImpact::WorkspaceConfig,
                            QString(),
                            project.systemVerilogFiles,
                            project);
}

void AnalysisScheduler::cancelWorkspaceAnalysis()
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->cancelWorkspaceAnalysis();
}

bool AnalysisScheduler::isSemanticAnalysisActive() const
{
    return workspaceSymbolAnalysis
        && workspaceSymbolAnalysis->isWorkspaceAnalysisActive();
}
