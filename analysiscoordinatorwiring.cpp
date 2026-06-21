#include "analysiscoordinator.h"

#include "activitylogservice.h"
#include "analysisprogresscoordinator.h"
#include "analysisscheduler.h"
#include "workspacemanager.h"

#include <QFileInfo>

void AnalysisCoordinator::connectSchedulerSignals()
{
    if (!dependencies.hasScheduler())
        return;

    AnalysisScheduler* scheduler = dependencies.schedulerObject();
    connect(scheduler,
            &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                dependencies.refreshRelationshipDataView();
            });
    connect(scheduler,
            &AnalysisScheduler::fileSymbolAnalysisStarted,
            this,
            [](const QString& fileName) {
                ActivityLogService::getInstance()->append(
                    QStringLiteral("Analyzer"),
                    ActivityLogLevel::Info,
                    QStringLiteral("File analysis started: %1")
                        .arg(QFileInfo(fileName).fileName()));
            });
    connect(scheduler,
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this, [this](const QString& fileName, int symbolCount) {
                ActivityLogService::getInstance()->append(
                    QStringLiteral("Analyzer"),
                    ActivityLogLevel::Info,
                    QStringLiteral("File analysis done: %1, %2 symbols")
                        .arg(QFileInfo(fileName).fileName())
                        .arg(symbolCount));
                dependencies.handleFileSymbolAnalysisFinished(fileName,
                                                              symbolCount);
                refreshActiveEditorForFile(fileName);
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress,
            this,
            [this](const QString& fileName, int filesDone, int totalFiles) {
                dependencies.handleWorkspaceSymbolProgress(fileName,
                                                           filesDone,
                                                           totalFiles);
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                dependencies.handleWorkspaceSymbolAnalysisFinished(
                    filesAnalyzed,
                    totalSymbols);
            });
    connect(scheduler,
            &AnalysisScheduler::relationshipAnalysisFinished,
            this, [this](const SingleFileRelationshipAnalysisResult& result) {
                showRelationshipAnalysisCompleted(result.fileName,
                                                  result.relationships.size());
            });
    connect(scheduler,
            &AnalysisScheduler::documentRefreshRequested,
            this, &AnalysisCoordinator::refreshActiveEditorForFile);
    connect(scheduler,
            &AnalysisScheduler::diagnosticsRefreshRequested,
            this, [this](const QString& fileName) {
                if (problemsRefreshHandler)
                    problemsRefreshHandler(fileName);
            });
}

void AnalysisCoordinator::connectProgressSignals()
{
    if (!dependencies.hasProgressCoordinator())
        return;

    dependencies.connectProgressToScheduler();
    AnalysisProgressCoordinator* progressCoordinator =
        dependencies.progressCoordinatorObject();
    connect(progressCoordinator,
            &AnalysisProgressCoordinator::statusMessageRequested,
            this,
            [this](const QString& message, int timeoutMs) {
                if (statusMessageHandler)
                    statusMessageHandler(message, timeoutMs);
            });
    connect(progressCoordinator,
            &AnalysisProgressCoordinator::relationshipAnalysisErrorReported,
            this,
            [this](const QString&, const QString& error) {
                showRelationshipAnalysisError(error);
            });
}

void AnalysisCoordinator::connectWorkspaceSignals()
{
    if (!dependencies.hasWorkspaceFileWatcher())
        return;

    connect(dependencies.workspaceManagerObject(),
            &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                dependencies.handleExternalFileChanged(filePath,
                                                       fileChangeDebounceMs);
            });
}

void AnalysisCoordinator::showRelationshipAnalysisCompleted(
    const QString& fileName,
    int relationshipsFound) const
{
    if (!statusMessageHandler)
        return;

    statusMessageHandler(
        QString("Smart analysis completed: %1 relationships in %2")
            .arg(relationshipsFound)
            .arg(QFileInfo(fileName).fileName()),
        2000);
}

void AnalysisCoordinator::showRelationshipAnalysisError(const QString& error) const
{
    if (statusMessageHandler)
        statusMessageHandler(QString("Analysis error: %1").arg(error), 3000);
}
