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
    if (symbolAnalyzer)
        symbolAnalyzer->requestCancelAllAnalyses();
    if (relationshipAnalysis)
        relationshipAnalysis->requestCancelAllAnalyses();

    // Phase two joins only after cancellation has been broadcast globally.
    if (relationshipAnalysis)
        relationshipAnalysis->waitForAllAnalyses();
    if (symbolAnalyzer)
        symbolAnalyzer->cancelAllAnalysesAndWait();

    if (relationshipAnalysis) {
        relationshipAnalysis->setRelationshipBuilder(nullptr);
        relationshipAnalysis->setSymbolAnalyzer(nullptr);
    }
    if (relationshipResultPublisher)
        relationshipResultPublisher->setRelationshipEngine(nullptr);
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setSymbolAnalyzer(nullptr);
    symbolAnalyzer = nullptr;
}

void AnalysisScheduler::setProjectModel(ProjectModel* model)
{
    if (!shuttingDown && workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setProjectModel(model);
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

void AnalysisScheduler::setCurrentFileProvider(std::function<QString()> provider)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setCurrentFileProvider(std::move(provider));
}

void AnalysisScheduler::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::cancelWorkspaceAnalysis()
{
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->cancelWorkspaceAnalysis();
}
