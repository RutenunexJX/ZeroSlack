#include "analysiscommandcoordinator.h"

#include "analysisscheduler.h"

AnalysisCommandCoordinator::AnalysisCommandCoordinator(AnalysisScheduler* scheduler,
                                                       QObject* parent)
    : QObject(parent)
    , scheduler(scheduler)
{
}

AnalysisCommandCoordinator::~AnalysisCommandCoordinator()
{
    cancelRelationshipWork();
}

void AnalysisCommandCoordinator::requestSingleFileRelationshipAnalysis(
    const QString& fileName,
    const QString& content)
{
    if (scheduler)
        scheduler->requestRelationshipAnalysis(fileName, content);
}

void AnalysisCommandCoordinator::scheduleOpenFileAnalysis(
    const QString& fileName,
    int delayMs)
{
    if (scheduler)
        scheduler->scheduleOpenFileAnalysis(fileName, delayMs);
}

void AnalysisCommandCoordinator::cancelScheduledOpenFileAnalysis(
    const QString& fileName)
{
    if (scheduler)
        scheduler->cancelScheduledOpenFileAnalysis(fileName);
}

void AnalysisCommandCoordinator::cancelRelationshipWork()
{
    if (!scheduler)
        return;

    scheduler->cancelRelationshipAnalysis();
    scheduler->cancelWorkspaceRelationshipAnalysis();
}
