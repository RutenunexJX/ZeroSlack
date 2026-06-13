#ifndef RELATIONSHIPANALYSISCONTROLLER_H
#define RELATIONSHIPANALYSISCONTROLLER_H

#include "projectmodel.h"
#include "relationshipanalysisqueue.h"
#include "relationshipanalysisworker.h"
#include "relationshipresultpublisher.h"

#include <QFutureWatcher>
#include <QObject>

class SmartRelationshipBuilder;
class SymbolAnalyzer;

class RelationshipAnalysisController : public QObject
{
    Q_OBJECT

public:
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
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    SmartRelationshipBuilder* relationshipBuilder = nullptr;
    RelationshipAnalysisQueue* relationshipQueue = nullptr;
    RelationshipResultPublisher* resultPublisher = nullptr;
    QFutureWatcher<SingleFileRelationshipAnalysisResult>* singleFileWatcher = nullptr;
    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* workspaceWatcher = nullptr;

    void handleSingleFileFinished();
    void handleWorkspaceFinished();
};

#endif // RELATIONSHIPANALYSISCONTROLLER_H
