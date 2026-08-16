#ifndef ANALYSISSCHEDULER_H
#define ANALYSISSCHEDULER_H

#include "zeroslackexport.h"

#include "documentmodel.h"
#include "projectmodel.h"
#include "semanticanalysisrequest.h"
#include "relationshipanalysiscontroller.h"
#include "relationshipanalysisqueue.h"
#include "relationshipresultpublisher.h"
#include "relationshipanalysisworker.h"
#include "workspacesymbolanalysiscontroller.h"

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <functional>

class SymbolAnalyzer;
class SymbolRelationshipEngine;
class SmartRelationshipBuilder;
class DiagnosticsRefreshController;
class AnalysisSchedulerTestAccess;

class ZEROSLACK_API AnalysisScheduler : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisScheduler(QObject* parent = nullptr);
    ~AnalysisScheduler() override;

    void shutdown();

    void setDocumentModel(DocumentModel* model);
    void setProjectModel(ProjectModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setOpenFileContentProvider(std::function<QString(const QString&)> provider);
    void setWorkspaceOpenProvider(std::function<bool()> provider);
    void setWorkspaceSymbolCancelProvider(std::function<bool()> provider);
    void setCurrentFileProvider(std::function<QString()> provider);
    void setSemanticAnalysisRuntimePolicy(
        const SemanticAnalysisRuntimePolicy& policy);
    SemanticAnalysisRuntimePolicy semanticAnalysisRuntimePolicy() const;
    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    void setRelationshipBuilder(SmartRelationshipBuilder* builder);

    void scheduleRelationshipAnalysis(const QString& fileName,
                                      const QString& content,
                                      int delayMs);
    void cancelAllScheduledRelationshipAnalyses();
    bool hasScheduledRelationshipAnalysis(const QString& fileName) const;
    void requestRelationshipAnalysis(const QString& fileName, const QString& content);
    void cancelRelationshipAnalysis();
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceAnalysis();
    void requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceRelationshipAnalysis();
    void handleExternalFileChanged(const QString& fileName, int debounceMs);
    void handleDocumentClosed(const QString& fileName);
    DocumentSemanticStatus semanticStatus(const QString& fileName) const;
    bool isSemanticAnalysisActive() const;

signals:
    void documentRefreshRequested(const QString& fileName);
    void diagnosticsRefreshRequested(const QString& fileName);
    void relationshipDataInvalidated();
    void relationshipDataRefreshRequested();
    void fileSymbolAnalysisStarted(const QString& fileName);
    void fileSymbolAnalysisFinished(const QString& fileName, int symbolCount);
    void workspaceAnalysisPlanPrepared(const WorkspaceAnalysisPlan& plan);
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceSymbolAnalysisProgress(const QString& fileName,
                                         int filesDone,
                                         int totalFiles);
    void workspaceSymbolAnalysisFinished(const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols);
    void workspaceSymbolAnalysisDeferred(const ProjectSnapshot& project,
                                         int totalFiles,
                                         qint64 totalBytes,
                                         qint64 largestFileBytes);
    void workspaceAnalysisRequestQueued(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void workspaceAnalysisRequestResolved(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void workspaceSymbolAnalysisCancelled(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void semanticAnalysisPlanPrepared(const IncrementalAnalysisPlan& plan);
    void semanticAnalysisTelemetry(const SemanticAnalysisTelemetry& telemetry);
    void semanticAnalysisRuntimePolicyChanged(
        const SemanticAnalysisRuntimePolicy& policy);
    void documentSemanticStateChanged(const DocumentSemanticStatus& status);
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
    friend class AnalysisSchedulerTestAccess;

    // These collaborators are externally owned. They can be declared after
    // the scheduler and therefore be destroyed first; guarded handles make
    // shutdown and queued callbacks observe that destruction immediately.
    QPointer<DocumentModel> documentModel;
    QPointer<ProjectModel> projectModel;
    QPointer<SymbolAnalyzer> symbolAnalyzer;
    RelationshipAnalysisController* relationshipAnalysis = nullptr;
    RelationshipAnalysisQueue* relationshipAnalysisQueue = nullptr;
    RelationshipResultPublisher* relationshipResultPublisher = nullptr;
    WorkspaceSymbolAnalysisController* workspaceSymbolAnalysis = nullptr;
    DiagnosticsRefreshController* diagnosticsRefresh = nullptr;
    std::function<QString(const QString&)> openFileContentProvider;
    std::function<bool()> workspaceOpenProvider;
    std::function<QString()> currentFileProvider;
    QHash<QString, DocumentSemanticStatus> semanticStatuses;
    QHash<QString, QTimer*> externalFileTimers;
    struct SelfWriteStamp {
        qint64 size = -1;
        qint64 modifiedMs = -1;
        qint64 recordedMs = -1;
    };
    QHash<QString, SelfWriteStamp> selfWriteStamps;
    struct PendingCleanSemanticChange {
        QString fileName;
        QString text;
        std::uint64_t documentRevision = 0;
        SemanticAnalysisReason reason = SemanticAnalysisReason::Unknown;
    };
    QHash<QString, PendingCleanSemanticChange> pendingCleanSemanticChanges;
    ProjectSnapshot lastScheduledProject;
    QString lastProjectSignature;
    bool workspaceInitialAnalysisScheduled = false;
    std::uint64_t nextSemanticGeneration = 0;
    SemanticAnalysisRuntimePolicy semanticRuntimePolicy;
    bool shuttingDown = false;

    static constexpr int kOpenDocumentRelationshipAnalysisDebounceMs = 2000;

    void onDocumentOpened(const DocumentSnapshot& snapshot);
    void onDocumentEdited(const DocumentSnapshot& snapshot);
    void onDocumentSaved(const DocumentSnapshot& snapshot);
    void onProjectChanged(const ProjectSnapshot& project);
    void onProjectClosed();
    void requestSemanticAnalysis(SemanticAnalysisReason reason,
                                 SemanticChangeImpact impactHint,
                                 const QString& triggerFile,
                                 const QStringList& changedFiles,
                                 const ProjectSnapshot& project = {});
    void setDocumentSemanticState(const QString& fileName,
                                  DocumentSemanticState state,
                                  std::uint64_t documentRevision,
                                  std::uint64_t analysisGeneration = 0,
                                  const QString& error = {});
    void onSemanticAnalysisStarted(const SemanticAnalysisRequest& request);
    void onSemanticAnalysisFinished(const SemanticAnalysisRequest& request,
                                    const IncrementalAnalysisPlan& plan);
    void onSemanticAnalysisFailed(const SemanticAnalysisRequest& request,
                                  const QString& error);
    void onSemanticAnalysisDropped(
        const SemanticAnalysisRequest& request,
        SemanticAnalysisRequestDisposition disposition);
    ProjectSnapshot projectForAnalysis(const QString& triggerFile) const;
    QString normalizedFileName(const QString& fileName) const;
    bool isSelfWriteWatcherEvent(const QString& fileName) const;
    void scheduleExternalFileAnalysis(const QString& fileName,
                                      int debounceMs);
    void rememberPendingCleanSemanticChange(
        const QString& fileName,
        const QString& text,
        std::uint64_t documentRevision,
        SemanticAnalysisReason reason);
    void acknowledgePublishedCleanSemanticChanges(
        const SemanticAnalysisRequest& request);
    void stabilizeSemanticStatesWhenDisabled();

    QString contentForOpenFile(const QString& fileName) const;
    void setupRelationshipAnalysis();
    void setupWorkspaceSymbolAnalysis();
    void setupDiagnosticsRefreshAndWorkspaceRequests();
};

#endif // ANALYSISSCHEDULER_H
