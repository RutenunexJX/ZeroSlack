#include "analysisscheduler.h"

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
