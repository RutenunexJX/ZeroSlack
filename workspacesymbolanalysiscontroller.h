#ifndef WORKSPACESYMBOLANALYSISCONTROLLER_H
#define WORKSPACESYMBOLANALYSISCONTROLLER_H

#include "projectmodel.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

class DocumentModel;
class SymbolAnalyzer;

class WorkspaceSymbolAnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceSymbolAnalysisController(QObject* parent = nullptr);

    void setProjectModel(ProjectModel* model);
    void setDocumentModel(DocumentModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setCancelProvider(std::function<bool()> provider);
    void setCurrentFileProvider(std::function<QString()> provider);

    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceAnalysis();
    void clearProjectSemanticState();

signals:
    void fileSymbolAnalysisStarted(const QString& fileName);
    void fileSymbolAnalysisFinished(const QString& fileName, int symbolCount);
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceAnalysisPlanPrepared(const WorkspaceAnalysisPlan& plan);
    void workspaceSymbolAnalysisProgress(const QString& fileName,
                                         int filesDone,
                                         int totalFiles);
    void workspaceSymbolAnalysisFinished(const ProjectSnapshot& project,
                                         int filesAnalyzed,
                                         int totalSymbols);
    void workspaceSymbolAnalysisDeferred(const ProjectSnapshot& project,
                                         int totalFiles,
                                         qint64 totalBytes,
                                         qint64 largestFileBytes);
    void diagnosticsRefreshRequested(const QString& fileName);
    void workspaceRelationshipAnalysisRequested(const ProjectSnapshot& project);
    void workspaceRelationshipAnalysisCancelRequested();
    void relationshipDataClearRequested();
    void workspaceAnalysisRequestQueued(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void workspaceAnalysisRequestResolved(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void workspaceSymbolAnalysisCancelled(
        const WorkspaceAnalysisRequestTelemetry& telemetry);

private:
    ProjectModel* projectModel = nullptr;
    DocumentModel* documentModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    std::function<bool()> cancelProvider;
    std::function<QString()> currentFileProvider;
    WorkspaceAnalysisRequestQueue requestQueue;
    ProjectSnapshot activeProject;
    QString activeWorkspaceRoot;
    QSet<QString> completedWorkspaceAnalysisKeys;
    bool workspaceAnalysisActive = false;
    bool activeWorkspaceAnalysisComplete = true;
    bool projectSemanticStateCleared = true;

    void onProjectChanged(const ProjectSnapshot& project);
    void onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void onWorkspaceSymbolAnalysisExpired();
    void startWorkspaceAnalysis(const ProjectSnapshot& project);
};

#endif // WORKSPACESYMBOLANALYSISCONTROLLER_H
