#include "analysisprogresscoordinator.h"

#include "analysisscheduler.h"
#include "relationshipprogressdialog.h"
#include "symbolanalyzer.h"

#include <QFileInfo>
#include <QTimer>

AnalysisProgressCoordinator::AnalysisProgressCoordinator(QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , dialogParent(dialogParent)
{
}

AnalysisProgressCoordinator::~AnalysisProgressCoordinator()
{
    if (progressDialog)
        progressDialog->deleteLater();
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
                Q_UNUSED(totalFiles)
                symbolAnalysisCancelled.store(false);
                const QStringList svFiles = project.systemVerilogFiles;
                showAnalysisProgress(svFiles);
                QTimer::singleShot(10, this, [this, svFiles]() {
                    showSymbolStageStarted(svFiles);
                });
            });

    connect(scheduler,
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols) {
                emit statusMessageRequested(
                    QString("Symbol analysis complete: %1 files, %2 symbols - relationship analysis running...")
                        .arg(filesAnalyzed)
                        .arg(totalSymbols),
                    3000);
                showRelationshipStageStarted(project.systemVerilogFiles);
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

void AnalysisProgressCoordinator::connectToSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchProgress,
            this,
            [this](int filesDone, int totalFiles, const QString& currentFileName) {
                if (!progressDialog || totalFiles <= 0)
                    return;
                progressDialog->progressBar->setValue(filesDone);
                progressDialog->progressBar->setMaximum(totalFiles);
                progressDialog->setSymbolAnalysisProgress(filesDone, totalFiles);

                QString shortName = QFileInfo(currentFileName).fileName();
                if (shortName.length() > 45)
                    shortName = "..." + shortName.right(42);
                progressDialog->currentFileLabel->setText(
                    QString("Symbol analysis: %1 / %2 - %3")
                        .arg(filesDone)
                        .arg(totalFiles)
                        .arg(shortName));
            });
}

bool AnalysisProgressCoordinator::isSymbolAnalysisCancelled() const
{
    return symbolAnalysisCancelled.load();
}

void AnalysisProgressCoordinator::showAnalysisProgress(const QStringList& files)
{
    if (progressDialog) {
        progressDialog->disconnect();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }

    progressDialog = new RelationshipProgressDialog(dialogParent);
    progressDialog->setAutoClose(false);
    progressDialog->setMinimumDuration(0);
    progressDialog->setShowDetails(true);

    connect(progressDialog,
            &RelationshipProgressDialog::cancelled,
            this,
            [this]() {
                symbolAnalysisCancelled.store(true);
                if (scheduler)
                    scheduler->cancelWorkspaceRelationshipAnalysis();
                emit statusMessageRequested("Analysis cancelled", 3000);
            });

    connect(progressDialog,
            &RelationshipProgressDialog::finished,
            this,
            [this]() {
                emit statusMessageRequested("Symbol relationship analysis complete", 3000);
            });

    progressDialog->startAnalysis(files.size());
    progressDialog->statusLabel->setText("Initializing analysis environment...");
    progressDialog->currentFileLabel->setText(
        QString("Preparing to analyze %1 SystemVerilog files").arg(files.size()));
    progressDialog->progressBar->setFormat("Initializing...");

    if (progressDialog->config.showDetails) {
        progressDialog->logProgress("System initialization complete");
        progressDialog->logProgress("Loading analysis components...");
    }

    progressDialog->update();
    progressDialog->repaint();
}

void AnalysisProgressCoordinator::showSymbolStageStarted(const QStringList& files)
{
    if (!progressDialog)
        return;

    progressDialog->statusLabel->setText("Stage 1/2: Symbol analysis running...");
    progressDialog->currentFileLabel->setText(
        "Scanning and parsing SystemVerilog file structure...");
    progressDialog->progressBar->setFormat("Symbol analysis running... Please wait");

    if (progressDialog->config.showDetails) {
        progressDialog->logProgress("Starting symbol analysis stage...");
        progressDialog->logProgress(QString("Found %1 SV files").arg(files.size()));
    }

    progressDialog->update();
    progressDialog->repaint();
}

void AnalysisProgressCoordinator::showRelationshipStageStarted(const QStringList& files)
{
    if (!progressDialog)
        return;

    progressDialog->statusLabel->setText("Stage 2/2: Relationship analysis running...");
    progressDialog->currentFileLabel->setText(
        "Analyzing symbol dependencies between files...");
    progressDialog->progressBar->setFormat(QString("%v / %1 files (%p%)").arg(files.size()));

    if (progressDialog->config.showDetails) {
        progressDialog->logProgress("Starting relationship analysis stage...");
        progressDialog->logProgress("Analyzing module instantiation relationships...");
        progressDialog->logProgress("Analyzing variable assignment relationships...");
        progressDialog->logProgress("Analyzing task/function call relationships...");
    }

    progressDialog->update();
    progressDialog->repaint();
}

void AnalysisProgressCoordinator::showRelationshipAnalysisFinished(
    const WorkspaceRelationshipAnalysisResult& result)
{
    const int totalFiles = result.totalFiles > 0
        ? result.totalFiles
        : result.fileRelationships.size();

    if (progressDialog) {
        progressDialog->statusLabel->setText("All analysis complete!");
        if (progressDialog->config.showDetails) {
            progressDialog->logProgress("Relationship analysis complete!");
            progressDialog->logProgress(QString("Processed %1 files").arg(totalFiles));
        }
    }

    QTimer::singleShot(200, this, [this, totalFiles]() {
        if (progressDialog)
            progressDialog->finishAnalysis();
        emit statusMessageRequested(
            QString("Relationship analysis complete: %1 files").arg(totalFiles),
            5000);
    });
}

void AnalysisProgressCoordinator::showRelationshipProgress(
    const QString& fileName,
    int relationshipsFound)
{
    if (progressDialog) {
        progressDialog->updateProgress(fileName, relationshipsFound);

        const QString shortName = QFileInfo(fileName).fileName();
        if (progressDialog->config.showDetails) {
            progressDialog->logProgress(
                QString("%1: found %2 relationships").arg(shortName).arg(relationshipsFound));
        }
    }

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
    if (!progressDialog)
        return;

    progressDialog->statusLabel->setText(
        QString("Stage 2/2: Relationship analysis running (%1/%2)")
            .arg(processedFiles)
            .arg(totalFiles));
}

void AnalysisProgressCoordinator::showRelationshipError(const QString& fileName, const QString& error)
{
    if (progressDialog && progressDialog->isVisible())
        progressDialog->showError(fileName, error);
    emit relationshipAnalysisErrorReported(fileName, error);
}

void AnalysisProgressCoordinator::showRelationshipCancelled()
{
    if (progressDialog)
        progressDialog->finishAnalysis();
    emit statusMessageRequested("Relationship analysis cancelled", 3000);
}
