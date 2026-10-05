#ifndef RELATIONSHIPANALYSISCONTROLLER_H
#define RELATIONSHIPANALYSISCONTROLLER_H

#include "zeroslackexport.h"

#include "projectmodel.h"
#include "relationshipanalysisworker.h"
#include "relationshipresultpublisher.h"

#include "semanticanalysisrequest.h"

#include <atomic>
#include <memory>
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
    void setResultPublisher(RelationshipResultPublisher* publisher);
    bool hasRelationshipBuilder() const;
    void setRuntimePolicy(const SemanticAnalysisRuntimePolicy& policy);

    void requestSingleFileAnalysis(const QString& fileName, const QString& content,
                                   const ProjectSnapshot& project = {},
                                   std::uint64_t documentRevision = 0);
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
    QPointer<SmartRelationshipBuilder> relationshipBuilder;
    QPointer<RelationshipResultPublisher> resultPublisher;
    enum class RequestKind { None, SingleFile, Workspace };
    RequestKind activeKind = RequestKind::None;
    SemanticAnalysisRequest activeRequest;
    SemanticAnalysisRuntimePolicy runtimePolicy;
    std::shared_ptr<std::atomic_bool> cancellation;
    WorkspaceWorkerStartGateForTesting workspaceWorkerStartGateForTesting;

    void submit(SemanticAnalysisRequest request, RequestKind kind);
    void finish(const SemanticAnalysisRequest& request,
                const WorkspaceRelationshipAnalysisResult& publication);
    bool matches(const SemanticAnalysisRequest& request) const;
    void cancel(RequestKind kind);
};

#endif // RELATIONSHIPANALYSISCONTROLLER_H
