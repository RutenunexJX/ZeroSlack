#ifndef WORKSPACESYMBOLANALYSISCONTROLLER_H
#define WORKSPACESYMBOLANALYSISCONTROLLER_H

#include "zeroslackexport.h"

#include "projectmodel.h"
#include "semanticanalysisrequest.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <cstdint>
#include <functional>

class DocumentModel;
class SymbolAnalyzer;

class ZEROSLACK_API WorkspaceSymbolAnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceSymbolAnalysisController(QObject* parent = nullptr);

    void setProjectModel(ProjectModel* model);
    void setDocumentModel(DocumentModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setCancelProvider(std::function<bool()> provider);
    void setCurrentFileProvider(std::function<QString()> provider);

    void requestSemanticAnalysis(const SemanticAnalysisRequest& request);
    void invalidateSemanticAnalysis(const QString& fileName,
                                    std::uint64_t documentRevision);
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceAnalysis();
    void clearProjectSemanticState();
    bool isWorkspaceAnalysisActive() const
    {
        return workspaceAnalysisActive;
    }
    std::uint64_t activeSemanticGenerationForFile(
        const QString& fileName) const;

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
    void semanticAnalysisRequestStarted(const SemanticAnalysisRequest& request);
    void semanticAnalysisRequestFinished(const SemanticAnalysisRequest& request,
                                         const IncrementalAnalysisPlan& plan);
    void semanticAnalysisRequestFailed(const SemanticAnalysisRequest& request,
                                       const QString& error);
    void semanticAnalysisRequestDropped(
        const SemanticAnalysisRequest& request,
        SemanticAnalysisRequestDisposition disposition);
    void semanticAnalysisContinuationRequired(
        const SemanticAnalysisRequest& remainingRequest);
    void semanticAnalysisPlanPrepared(const IncrementalAnalysisPlan& plan);
    void semanticAnalysisTelemetry(const SemanticAnalysisTelemetry& telemetry);

private:
    QPointer<ProjectModel> projectModel;
    QPointer<DocumentModel> documentModel;
    QPointer<SymbolAnalyzer> symbolAnalyzer;
    std::function<bool()> cancelProvider;
    std::function<QString()> currentFileProvider;
    WorkspaceAnalysisRequestQueue requestQueue;
    ProjectSnapshot activeRequestedProject;
    ProjectSnapshot activeProject;
    SemanticAnalysisRequest activeSemanticRequest;
    SemanticAnalysisRequest pendingSemanticRequest;
    IncrementalAnalysisPlan activeIncrementalPlan;
    bool hasPendingSemanticRequest = false;
    bool activeSemanticRequestDropNotified = false;
    QString activeWorkspaceRoot;
    QSet<QString> completedWorkspaceAnalysisKeys;
    bool workspaceAnalysisActive = false;
    bool activeWorkspaceAnalysisComplete = true;
    bool projectSemanticStateCleared = true;
    std::uint64_t workspaceStartGeneration = 0;
    std::uint64_t compatibilityRequestGeneration = 0;

    void onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void onWorkspaceSymbolAnalysisExpired();
    void onSemanticAnalysisFailed(const SemanticAnalysisRequest& request,
                                  const QString& error);
    void startSemanticAnalysis(const SemanticAnalysisRequest& request);
    void startPendingSemanticAnalysis();
    void notifyActiveRequestDropped(
        SemanticAnalysisRequestDisposition disposition);
    void dropPendingRequest(
        SemanticAnalysisRequestDisposition disposition);
};

#endif // WORKSPACESYMBOLANALYSISCONTROLLER_H
