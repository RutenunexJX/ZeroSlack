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
