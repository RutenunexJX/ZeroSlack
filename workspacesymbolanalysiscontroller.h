#ifndef WORKSPACESYMBOLANALYSISCONTROLLER_H
#define WORKSPACESYMBOLANALYSISCONTROLLER_H

#include "projectmodel.h"

#include <QObject>
#include <functional>

class SymbolAnalyzer;

class WorkspaceSymbolAnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceSymbolAnalysisController(QObject* parent = nullptr);

    void setProjectModel(ProjectModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setCancelProvider(std::function<bool()> provider);

    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void clearProjectSemanticState();

signals:
    void fileSymbolAnalysisStarted(const QString& fileName);
    void fileSymbolAnalysisFinished(const QString& fileName, int symbolCount);
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceSymbolAnalysisProgress(const QString& fileName,
                                         int filesDone,
                                         int totalFiles);
    void workspaceSymbolAnalysisFinished(const ProjectSnapshot& project,
                                         int filesAnalyzed,
                                         int totalSymbols);
    void diagnosticsRefreshRequested(const QString& fileName);
    void workspaceRelationshipAnalysisRequested(const ProjectSnapshot& project);
    void workspaceRelationshipAnalysisCancelRequested();
    void relationshipDataClearRequested();

private:
    ProjectModel* projectModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    std::function<bool()> cancelProvider;
    ProjectSnapshot activeProject;
    bool workspaceAnalysisActive = false;
    bool projectSemanticStateCleared = true;

    void onProjectChanged(const ProjectSnapshot& project);
    void onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);
};

#endif // WORKSPACESYMBOLANALYSISCONTROLLER_H
