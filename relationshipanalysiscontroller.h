#ifndef RELATIONSHIPANALYSISCONTROLLER_H
#define RELATIONSHIPANALYSISCONTROLLER_H

#include "zeroslackexport.h"

#include "projectmodel.h"
#include "relationshipanalysisqueue.h"
#include "relationshipanalysisworker.h"
#include "relationshipresultpublisher.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <cstdint>
#include <functional>

class SmartRelationshipBuilder;
class SymbolAnalyzer;

class ZEROSLACK_API RelationshipAnalysisController : public QObject
{
    Q_OBJECT

public:
    using WorkspaceWorkerStartGateForTesting =
        std::function<void(const std::function<bool()>& isCancelled)>;

    explicit RelationshipAnalysisController(QObject* parent = nullptr);
    ~RelationshipAnalysisController() override;

    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setRelationshipBuilder(SmartRelationshipBuilder* builder);
    void setRelationshipQueue(RelationshipAnalysisQueue* queue);
    void setResultPublisher(RelationshipResultPublisher* publisher);
    bool hasRelationshipBuilder() const;

    void requestSingleFileAnalysis(const QString& fileName, const QString& content);
    void cancelSingleFileAnalysis();
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceAnalysis();
    void requestCancelAllAnalyses();
    void waitForAllAnalyses();
    // Test-only gate. Runs on the worker thread and must return once the
    // supplied cancellation predicate becomes true.
    void setWorkspaceWorkerStartGateForTesting(
        WorkspaceWorkerStartGateForTesting gate);

signals:
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
    QPointer<SymbolAnalyzer> symbolAnalyzer;
    // Workers still capture a raw pointer only after the controller has
    // established a join-before-detach lifetime boundary. The controller-side
    // handle is guarded so an externally owned builder that is destroyed
    // first cannot leave shutdown dereferencing freed QObject storage.
    QPointer<SmartRelationshipBuilder> relationshipBuilder;
    QPointer<RelationshipAnalysisQueue> relationshipQueue;
    QPointer<RelationshipResultPublisher> resultPublisher;
    QFutureWatcher<SingleFileRelationshipAnalysisResult>* singleFileWatcher = nullptr;
    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* workspaceWatcher = nullptr;
    WorkspaceWorkerStartGateForTesting workspaceWorkerStartGateForTesting;
    std::uint64_t singleFileRequestGeneration = 0;
    std::uint64_t activeSingleFileRequestGeneration = 0;
    QString activeSingleFileKey;
    std::uint64_t workspaceRequestGeneration = 0;
    std::uint64_t activeWorkspaceRequestGeneration = 0;
    QString activeWorkspaceProjectKey;

    void handleSingleFileFinished(
        QFutureWatcher<SingleFileRelationshipAnalysisResult>* watcher,
        std::uint64_t requestGeneration,
        const QString& fileKey);
    void handleWorkspaceFinished(
        QFutureWatcher<WorkspaceRelationshipAnalysisResult>* watcher,
        std::uint64_t requestGeneration,
        const QString& projectKey);
};

#endif // RELATIONSHIPANALYSISCONTROLLER_H
