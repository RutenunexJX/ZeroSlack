#include "analysisprogresscoordinator.h"

#include "activitylogservice.h"
#include "analysisscheduler.h"
#include "semanticindex.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspaceanalysisplanservice.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString ageText(qint64 ageMs)
{
    return ageMs >= 0
        ? QStringLiteral("%1 ms").arg(ageMs)
        : QStringLiteral("n/a");
}

QString currentFilePlanText(bool currentFileInWorkspace)
{
    return currentFileInWorkspace
        ? QStringLiteral(", current file prioritized")
        : QStringLiteral(", current file outside workspace");
}

QString priorityBandText(const WorkspaceAnalysisPlan& plan)
{
    return plan.bandSummaryText();
}

QString normalizedFilePath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString bandSuffix(const QString& band)
{
    return band.isEmpty()
        ? QString()
        : QStringLiteral(" [%1]").arg(band);
}

SemanticAnalysisBandReport analysisBandReportForProject(
    const ProjectSnapshot& project)
{
    QList<SemanticSymbolRecord> records;
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    for (const QString& fileName : project.systemVerilogFiles) {
        const QList<SemanticSymbolRecord> fileRecords =
            semanticIndex->getSymbolRecords(fileName);
        records.append(fileRecords);
    }
    return semanticAnalysisBandReportForRecords(records);
}
}

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
                lastSymbolProgressCheckpoint = 0;
                lastRelationshipProgressCheckpoint = 0;
                showAnalysisProgress(project.systemVerilogFiles);
                showSymbolStageStarted(project.systemVerilogFiles);
                ActivityLogService::getInstance()->append(
                    QStringLiteral("Analyzer"),
                    ActivityLogLevel::Info,
                    QStringLiteral("Scheduled %1 files").arg(totalFiles));
            });
    connect(scheduler,
            &AnalysisScheduler::workspaceAnalysisPlanPrepared,
            this,
            &AnalysisProgressCoordinator::handleWorkspaceAnalysisPlanPrepared);
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisProgress,
            this,
            [this](const QString& fileName, int filesDone, int totalFiles) {
                handleWorkspaceSymbolProgress(filesDone, totalFiles, fileName);
            });

    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            &AnalysisProgressCoordinator::handleWorkspaceSymbolAnalysisFinished);
    connect(scheduler,
            &AnalysisScheduler::workspaceAnalysisRequestQueued,
            this,
            &AnalysisProgressCoordinator::handleWorkspaceAnalysisRequestQueued);
    connect(scheduler,
            &AnalysisScheduler::workspaceAnalysisRequestResolved,
            this,
            &AnalysisProgressCoordinator::handleWorkspaceAnalysisRequestResolved);
    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisCancelled,
            this,
            &AnalysisProgressCoordinator::handleWorkspaceSymbolAnalysisCancelled);

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
    connect(scheduler,
            &AnalysisScheduler::workspaceRelationshipAnalysisCancelled,
            this,
            &AnalysisProgressCoordinator::showWorkspaceRelationshipCancelled);
}

void AnalysisProgressCoordinator::handleWorkspaceSymbolProgress(
    int filesDone,
    int totalFiles,
    const QString& currentFileName)
{
    if (totalFiles <= 0)
        return;

    const QString shortName = QFileInfo(currentFileName).fileName();
    const QString band = workspaceSymbolBandForFile(currentFileName);
    emit statusMessageRequested(
        QString("Symbol analysis: %1 / %2%3 - %4")
            .arg(filesDone)
            .arg(totalFiles)
            .arg(bandSuffix(band))
            .arg(shortName),
        1000);
    logProgressCheckpoint(QStringLiteral("Symbol analysis"),
                          filesDone,
                          totalFiles,
                          currentFileName,
        &lastSymbolProgressCheckpoint);
}

void AnalysisProgressCoordinator::handleWorkspaceAnalysisPlanPrepared(
    const WorkspaceAnalysisPlan& plan)
{
    const int totalFiles = plan.project.systemVerilogFiles.size();
    rememberWorkspacePlanBands(plan);
    if (totalFiles <= 0)
        return;

    const QString message =
        QStringLiteral("Workspace plan prepared: %1 files, %2 priority, %3 background, %4 open, %5 protected, %6%7")
            .arg(totalFiles)
            .arg(plan.priorityFileCount)
            .arg(plan.backgroundFileCount)
            .arg(plan.openFiles.size())
            .arg(plan.protectedFiles.size())
            .arg(priorityBandText(plan))
            .arg(currentFilePlanText(plan.currentFileInWorkspace));
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        message);
    emit statusMessageRequested(
        QStringLiteral("Workspace plan: %1 priority (%2/%3/%4) / %5 files")
            .arg(plan.priorityFileCount)
            .arg(plan.currentFilePriorityFiles.size())
            .arg(plan.dirtyOpenPriorityFiles.size())
            .arg(plan.cleanOpenPriorityFiles.size())
            .arg(totalFiles),
        3000);
}

void AnalysisProgressCoordinator::rememberWorkspacePlanBands(
    const WorkspaceAnalysisPlan& plan)
{
    workspaceSymbolBandByFile = plan.fileBandsByNormalizedPath;
    for (const QString& fileName : plan.project.systemVerilogFiles) {
        const QString normalized = normalizedFilePath(fileName);
        if (normalized.isEmpty()
            || workspaceSymbolBandByFile.contains(normalized)) {
            continue;
        }
        const QString band = plan.bandForFile(fileName);
        if (!band.isEmpty())
            workspaceSymbolBandByFile.insert(normalized, band);
    }
}

QString AnalysisProgressCoordinator::workspaceSymbolBandForFile(
    const QString& fileName) const
{
    return workspaceSymbolBandByFile.value(normalizedFilePath(fileName));
}

void AnalysisProgressCoordinator::handleWorkspaceAnalysisRequestQueued(
    const WorkspaceAnalysisRequestTelemetry& telemetry)
{
    const QString message =
        QStringLiteral("Workspace request queued: active age %1, pending age %2, pending updates %3")
            .arg(ageText(telemetry.activeAgeMs),
                 ageText(telemetry.pendingAgeMs))
            .arg(telemetry.pendingUpdateCount);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        message);
    if (telemetry.active && telemetry.pending) {
        emit statusMessageRequested(
            QStringLiteral("Workspace analysis queued: latest request will replace stale work"),
            3000);
    }
}

void AnalysisProgressCoordinator::handleWorkspaceAnalysisRequestResolved(
    const WorkspaceAnalysisRequestTelemetry& telemetry)
{
    QString message =
        QStringLiteral("Workspace request resolved: active wait %1")
            .arg(ageText(telemetry.lastFinishedActiveAgeMs));
    if (telemetry.lastTakenPendingUpdateCount > 0) {
        message += QStringLiteral(", pending wait %1, pending updates %2")
            .arg(ageText(telemetry.lastTakenPendingAgeMs))
            .arg(telemetry.lastTakenPendingUpdateCount);
        message += QStringLiteral(", restarting latest request");
    }

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        message);
    if (telemetry.lastTakenPendingUpdateCount > 0) {
        emit statusMessageRequested(
            QStringLiteral("Workspace analysis restarting latest request"),
            3000);
    }
}

void AnalysisProgressCoordinator::handleWorkspaceSymbolAnalysisCancelled(
    const WorkspaceAnalysisRequestTelemetry& telemetry)
{
    symbolAnalysisCancelled.store(true);
    QString message =
        QStringLiteral("Workspace symbol analysis cancelled: active wait %1")
            .arg(ageText(telemetry.lastFinishedActiveAgeMs));
    if (telemetry.lastTakenPendingUpdateCount > 0) {
        message += QStringLiteral(", discarded pending wait %1, pending updates %2")
            .arg(ageText(telemetry.lastTakenPendingAgeMs))
            .arg(telemetry.lastTakenPendingUpdateCount);
    }

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Warning,
        message);
    emit statusMessageRequested(
        QStringLiteral("Workspace symbol analysis cancelled"),
        3000);
}

void AnalysisProgressCoordinator::handleWorkspaceSymbolAnalysisFinished(
    const ProjectSnapshot& project,
    int filesAnalyzed,
    int totalSymbols)
{
    emit statusMessageRequested(
        QString("Symbol analysis complete: %1 files, %2 symbols")
            .arg(filesAnalyzed)
            .arg(totalSymbols),
        3000);
    showRelationshipStageStarted(project.systemVerilogFiles);

    QString message =
        QStringLiteral("Parsed %1 files, %2 symbols")
            .arg(filesAnalyzed)
            .arg(totalSymbols);
    const SemanticAnalysisBandReport report =
        analysisBandReportForProject(project);
    if (!report.bands.isEmpty())
        message += QStringLiteral("; %1").arg(report.summaryText());

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        message);
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
    lastRelationshipProgressCheckpoint = 0;
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
    const int processedFiles = result.processedFiles > 0
        ? result.processedFiles
        : result.fileRelationships.size();

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        QStringLiteral("Relationship analysis complete: %1/%2 files, %3 relationships, elapsed %4")
            .arg(processedFiles)
            .arg(totalFiles)
            .arg(result.relationshipCount)
            .arg(ageText(result.elapsedMs)));
    emit statusMessageRequested(
        QStringLiteral("Relationship analysis complete: %1/%2 files, %3 relationships")
            .arg(processedFiles)
            .arg(totalFiles)
            .arg(result.relationshipCount),
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
    logProgressCheckpoint(QStringLiteral("Relationship analysis"),
                          processedFiles,
                          totalFiles,
                          QString(),
                          &lastRelationshipProgressCheckpoint);
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

void AnalysisProgressCoordinator::showWorkspaceRelationshipCancelled()
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Warning,
        QStringLiteral("Workspace relationship analysis cancelled"));
    emit statusMessageRequested("Workspace relationship analysis cancelled", 3000);
}

void AnalysisProgressCoordinator::logProgressCheckpoint(
    const QString& label,
    int processedFiles,
    int totalFiles,
    const QString& currentFileName,
    int* lastCheckpoint)
{
    if (!lastCheckpoint || totalFiles <= 0 || processedFiles <= 0)
        return;

    const int percent = (processedFiles * 100) / totalFiles;
    const int checkpoint = (percent / 25) * 25;
    if (checkpoint <= *lastCheckpoint
        || checkpoint < 25
        || checkpoint >= 100) {
        return;
    }

    *lastCheckpoint = checkpoint;
    QString message =
        QStringLiteral("%1 progress: %2% (%3/%4)")
            .arg(label)
            .arg(checkpoint)
            .arg(processedFiles)
            .arg(totalFiles);
    const QString shortName = QFileInfo(currentFileName).fileName();
    if (!shortName.isEmpty())
        message += QStringLiteral("%1 - %2")
                       .arg(bandSuffix(workspaceSymbolBandForFile(currentFileName)),
                            shortName);

    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        message);
}
