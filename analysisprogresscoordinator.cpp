#include "analysisprogresscoordinator.h"

#include "activitylogservice.h"
#include "analysisscheduler.h"

#include <QFileInfo>

AnalysisProgressCoordinator::AnalysisProgressCoordinator(QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , dialogParent(dialogParent)
{
}

AnalysisProgressCoordinator::~AnalysisProgressCoordinator()
{
}

void AnalysisProgressCoordinator::connectToScheduler(AnalysisScheduler* newScheduler)
{
    if (scheduler == newScheduler)
        return;
    if (scheduler)
        disconnect(scheduler, nullptr, this, nullptr);

    scheduler = newScheduler;
    if (!scheduler)
        return;

    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot& project, int totalFiles) {
                symbolAnalysisCancelled.store(false);
                showAnalysisProgress(project.systemVerilogFiles);
                showSymbolStageStarted(project.systemVerilogFiles);
                ActivityLogService::getInstance()->append(
                    QStringLiteral("Analyzer"),
                    ActivityLogLevel::Info,
                    QStringLiteral("Scheduled %1 files").arg(totalFiles));
            });

    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols) {
                emit statusMessageRequested(
                    QString("Symbol analysis complete: %1 files, %2 symbols")
                        .arg(filesAnalyzed)
                        .arg(totalSymbols),
                    3000);
                showRelationshipStageStarted(project.systemVerilogFiles);
                ActivityLogService::getInstance()->append(
                    QStringLiteral("Analyzer"),
                    ActivityLogLevel::Info,
                    QStringLiteral("Parsed %1 files, %2 symbols")
                        .arg(filesAnalyzed)
                        .arg(totalSymbols));
            });

    connect(scheduler,
            &AnalysisScheduler::workspaceRelationshipAnalysisFinished,
            this,
            &AnalysisProgressCoordinator::showRelationshipAnalysisFinished);

    connect(scheduler,
            &AnalysisScheduler::relationshipAnalysisProgress,
            this,
            &AnalysisProgressCoordinator::showRelationshipProgress);

    connect(scheduler,
            &AnalysisScheduler::workspaceRelationshipAnalysisProgress,
            this,
            [this](const QString&, int, int processedFiles, int totalFiles) {
                showWorkspaceRelationshipProgress(processedFiles, totalFiles);
            });

    connect(scheduler,
            &AnalysisScheduler::relationshipAnalysisError,
            this,
            &AnalysisProgressCoordinator::showRelationshipError);

    connect(scheduler,
            &AnalysisScheduler::relationshipAnalysisCancelled,
            this,
            &AnalysisProgressCoordinator::showRelationshipCancelled);
}

void AnalysisProgressCoordinator::handleWorkspaceSymbolProgress(
    int filesDone,
    int totalFiles,
    const QString& currentFileName)
{
    if (totalFiles <= 0)
        return;

    const QString shortName = QFileInfo(currentFileName).fileName();
    emit statusMessageRequested(
        QString("Symbol analysis: %1 / %2 - %3")
            .arg(filesDone)
            .arg(totalFiles)
            .arg(shortName),
        1000);
}

bool AnalysisProgressCoordinator::isSymbolAnalysisCancelled() const
{
    return symbolAnalysisCancelled.load();
}

void AnalysisProgressCoordinator::showAnalysisProgress(const QStringList& files)
{
    emit statusMessageRequested(
        QString("Workspace analysis scheduled: %1 files").arg(files.size()),
        2000);
}

void AnalysisProgressCoordinator::showSymbolStageStarted(const QStringList& files)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        QStringLiteral("Symbol analysis started for %1 files").arg(files.size()));
}

void AnalysisProgressCoordinator::showRelationshipStageStarted(const QStringList& files)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        QStringLiteral("Relationship analysis started for %1 files").arg(files.size()));
}

void AnalysisProgressCoordinator::showRelationshipAnalysisFinished(
    const WorkspaceRelationshipAnalysisResult& result)
{
    const int totalFiles = result.totalFiles > 0
        ? result.totalFiles
        : result.fileRelationships.size();

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        QStringLiteral("Relationship analysis complete: %1 files").arg(totalFiles));
    emit statusMessageRequested(
        QString("Relationship analysis complete: %1 files").arg(totalFiles),
        5000);
}

void AnalysisProgressCoordinator::showRelationshipProgress(
    const QString& fileName,
    int relationshipsFound)
{
    const QString shortName = QFileInfo(fileName).fileName();
    emit statusMessageRequested(
        QString("Relationship analysis: %1 (%2 relationships)")
            .arg(shortName)
            .arg(relationshipsFound),
        1000);
}

void AnalysisProgressCoordinator::showWorkspaceRelationshipProgress(
    int processedFiles,
    int totalFiles)
{
    emit statusMessageRequested(
        QString("Stage 2/2: Relationship analysis running (%1/%2)")
            .arg(processedFiles)
            .arg(totalFiles),
        1000);
}

void AnalysisProgressCoordinator::showRelationshipError(const QString& fileName, const QString& error)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Error,
        QStringLiteral("%1: %2")
            .arg(QFileInfo(fileName).fileName(), error));
    emit relationshipAnalysisErrorReported(fileName, error);
}

void AnalysisProgressCoordinator::showRelationshipCancelled()
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Warning,
        QStringLiteral("Relationship analysis cancelled"));
    emit statusMessageRequested("Relationship analysis cancelled", 3000);
}
