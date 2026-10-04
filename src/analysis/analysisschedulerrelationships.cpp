#include "analysisscheduler.h"

void AnalysisScheduler::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    if (relationshipResultPublisher)
        relationshipResultPublisher->setRelationshipEngine(engine);
}

void AnalysisScheduler::setRelationshipBuilder(SmartRelationshipBuilder* builder)
{
    if (!shuttingDown && relationshipAnalysis)
        relationshipAnalysis->setRelationshipBuilder(builder);
}

void AnalysisScheduler::scheduleRelationshipAnalysis(const QString& fileName,
                                                     const QString& content,
                                                     int delayMs)
{
    if (fileName.isEmpty()
        || content.isNull()
        || !relationshipAnalysis
        || !relationshipAnalysis->hasRelationshipBuilder()) {
        return;
    }

    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->schedule(fileName, content, delayMs);
}

void AnalysisScheduler::cancelAllScheduledRelationshipAnalyses()
{
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->cancelAll();
}

bool AnalysisScheduler::hasScheduledRelationshipAnalysis(
    const QString& fileName) const
{
    return relationshipAnalysisQueue
        ? relationshipAnalysisQueue->hasScheduled(fileName)
        : false;
}

void AnalysisScheduler::requestRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (shuttingDown || !semanticRuntimePolicy.enabled)
        return;
    if (relationshipAnalysis) {
        const auto document = documentModel
            ? documentModel->cachedDocumentForFile(fileName) : DocumentSnapshot{};
        relationshipAnalysis->requestSingleFileAnalysis(fileName, content,
            belongsToActiveWorkspace(fileName) && projectModel && projectModel->isOpen()
                ? projectForAnalysis(fileName) : ProjectSnapshot{},
            document.textVersion);
    }
}

void AnalysisScheduler::cancelRelationshipAnalysis()
{
    if (relationshipAnalysis)
        relationshipAnalysis->cancelSingleFileAnalysis();
}

void AnalysisScheduler::requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project)
{
    if (shuttingDown || !semanticRuntimePolicy.enabled || !project.isOpen()
        || !relationshipAnalysis
        || !relationshipAnalysis->hasRelationshipBuilder()) {
        return;
    }

    relationshipAnalysis->requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::cancelWorkspaceRelationshipAnalysis()
{
    if (relationshipAnalysis)
        relationshipAnalysis->cancelWorkspaceAnalysis();
}
