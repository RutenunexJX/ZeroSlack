#ifndef ANALYSISSCHEDULER_H
#define ANALYSISSCHEDULER_H

#include "documentmodel.h"
#include "opendocumentanalysiscontroller.h"
#include "projectmodel.h"
#include "relationshipanalysiscontroller.h"
#include "relationshipanalysisqueue.h"
#include "relationshipresultpublisher.h"
#include "relationshipanalysisworker.h"
#include "workspacesymbolanalysiscontroller.h"

#include <QObject>
#include <QString>
#include <functional>

class SymbolAnalyzer;
class SymbolRelationshipEngine;
class SmartRelationshipBuilder;
class DiagnosticsRefreshController;

class AnalysisScheduler : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisScheduler(QObject* parent = nullptr);
    ~AnalysisScheduler() override;

    void setDocumentModel(DocumentModel* model);
    void setProjectModel(ProjectModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setOpenFileContentProvider(std::function<QString(const QString&)> provider);
    void setWorkspaceOpenProvider(std::function<bool()> provider);
    void setWorkspaceSymbolCancelProvider(std::function<bool()> provider);
    void setCurrentFileProvider(std::function<QString()> provider);
    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    void setRelationshipBuilder(SmartRelationshipBuilder* builder);

    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);
    void scheduleRelationshipAnalysis(const QString& fileName,
                                      const QString& content,
                                      int delayMs);
    void cancelScheduledRelationshipAnalysis(const QString& fileName);
    void cancelAllScheduledRelationshipAnalyses();
    bool hasScheduledRelationshipAnalysis(const QString& fileName) const;
    void requestRelationshipAnalysis(const QString& fileName, const QString& content);
    void cancelRelationshipAnalysis();
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceRelationshipAnalysis();
    void handleExternalFileChanged(const QString& fileName, int debounceMs);
    void handleDocumentClosed(const QString& fileName);

signals:
    void documentRefreshRequested(const QString& fileName);
    void diagnosticsRefreshRequested(const QString& fileName);
    void relationshipDataInvalidated();
    void relationshipDataRefreshRequested();
    void fileSymbolAnalysisStarted(const QString& fileName);
    void fileSymbolAnalysisFinished(const QString& fileName, int symbolCount);
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceSymbolAnalysisProgress(const QString& fileName,
                                         int filesDone,
                                         int totalFiles);
    void workspaceSymbolAnalysisFinished(const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols);
    void workspaceAnalysisRequestQueued(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void workspaceAnalysisRequestResolved(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void relationshipAnalysisProgress(const QString& fileName, int relationshipsFound);
    void relationshipAnalysisError(const QString& fileName, const QString& error);
    void relationshipAnalysisCancelled();
    void relationshipAnalysisFinished(const SingleFileRelationshipAnalysisResult& result);
    void workspaceRelationshipAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceRelationshipAnalysisProgress(const QString& fileName,
                                               int relationshipsFound,
                                               int processedFiles,
                                               int totalFiles);
    void workspaceRelationshipAnalysisFinished(const WorkspaceRelationshipAnalysisResult& result);
    void workspaceRelationshipAnalysisCancelled();

private:
    DocumentModel* documentModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    OpenDocumentAnalysisController* openDocumentAnalysis = nullptr;
    RelationshipAnalysisController* relationshipAnalysis = nullptr;
    RelationshipAnalysisQueue* relationshipAnalysisQueue = nullptr;
    RelationshipResultPublisher* relationshipResultPublisher = nullptr;
    WorkspaceSymbolAnalysisController* workspaceSymbolAnalysis = nullptr;
    DiagnosticsRefreshController* diagnosticsRefresh = nullptr;

    static constexpr int kOpenDocumentRelationshipAnalysisDebounceMs = 2000;

    void onDocumentOpened(const DocumentSnapshot& snapshot);
    void onDocumentEdited(const DocumentSnapshot& snapshot);
    void onDocumentSaved(const DocumentSnapshot& snapshot);

    QString contentForOpenFile(const QString& fileName) const;
    void setupOpenDocumentAnalysis();
    void setupRelationshipAnalysis();
    void setupWorkspaceSymbolAnalysis();
    void setupDiagnosticsRefreshAndWorkspaceRequests();
    void refreshOpenDocumentsForForegroundAnalysis();
};

#endif // ANALYSISSCHEDULER_H
