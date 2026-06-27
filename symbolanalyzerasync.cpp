#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "symbolanalyzerworkspace.h"
#include "workspacemanager.h"

#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
#include <utility>

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    workspaceAnalysisWatcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    connect(workspaceAnalysisWatcher,
            &QFutureWatcher<WorkspaceAnalysisResult>::finished,
            this,
            &SymbolAnalyzer::onWorkspaceAnalysisFinished);
    workspacePublicationTimer = new QTimer(this);
    workspacePublicationTimer->setSingleShot(false);
    connect(workspacePublicationTimer,
            &QTimer::timeout,
            this,
            &SymbolAnalyzer::processWorkspacePublicationChunk);
}

SymbolAnalyzer::~SymbolAnalyzer()
{
    cancelWorkspacePublication();
    cancelWorkspaceAnalysisAndWait();
    if (workspaceAnalysisWatcher) {
        workspaceAnalysisWatcher->deleteLater();
        workspaceAnalysisWatcher = nullptr;
    }
}

void SymbolAnalyzer::startAnalyzeWorkspaceAsync(
    WorkspaceManager* workspaceManager,
    std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;
    startAnalyzeProjectAsync(workspaceManager->projectSnapshot(), std::move(isCancelled));
}

void SymbolAnalyzer::startAnalyzeProjectAsync(
    const ProjectSnapshot& project,
    std::function<bool()> isCancelled)
{
    if (!project.isOpen())
        return;
    cancelWorkspacePublication();
    cancelWorkspaceAnalysisAndWait();

    const QStringList svFiles = project.systemVerilogFiles;
    const QStringList includeDirs = project.includeDirs;
    const QHash<QString, QString> defines = project.defines;
    const QString workspacePath = project.workspaceRoot;
    const int totalFiles = svFiles.size();
    const std::uint64_t generation = ++workspaceAnalysisGeneration;
    const QStringList protectedFiles = workspaceProtectedFiles;
    const QList<int> priorityPublicationCheckpoints =
        workspacePriorityPublicationCheckpoints;
    const QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands =
        workspaceFileAnalysisBands;

    emit analysisStarted(workspacePath);

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run([svFiles, includeDirs, defines, isCancelled, generation, protectedFiles, priorityPublicationCheckpoints, fileAnalysisBands]() {
        QElapsedTimer workerTimer;
        workerTimer.start();
        auto applyResultMetadata =
            [generation,
             &protectedFiles,
             &priorityPublicationCheckpoints,
             &fileAnalysisBands](
                WorkspaceAnalysisResult* result) {
                if (!result)
                    return;
                result->protectedFiles = protectedFiles;
                result->priorityPublicationCheckpoints =
                    priorityPublicationCheckpoints;
                result->fileAnalysisBands = fileAnalysisBands;
                result->generation = generation;
            };
        auto cancelled = [&isCancelled]() {
            return isCancelled && isCancelled();
        };
        WorkspaceAnalysisResult result;
        if (cancelled()) {
            result.cancelled = true;
            result.workerElapsedMs = workerTimer.elapsed();
            applyResultMetadata(&result);
            return result;
        }

        SlangManager symbolAnalyzer;
        QElapsedTimer stageTimer;
        stageTimer.start();
        const auto records =
            symbolAnalyzer.extractWorkspaceSymbolRecords(svFiles,
                                                         includeDirs,
                                                         defines,
                                                         isCancelled);
        const qint64 symbolExtractionMs = stageTimer.elapsed();
        if (cancelled()) {
            result.cancelled = true;
            result.symbolExtractionMs = symbolExtractionMs;
            result.workerElapsedMs = workerTimer.elapsed();
            applyResultMetadata(&result);
            return result;
        }

        stageTimer.restart();
        result =
            SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
                svFiles,
                records,
                isCancelled);
        result.symbolExtractionMs = symbolExtractionMs;
        result.resultAssemblyMs = stageTimer.elapsed();
        applyResultMetadata(&result);
        if (result.cancelled || cancelled()) {
            result.cancelled = true;
            result.workerElapsedMs = workerTimer.elapsed();
            return result;
        }

        SlangManager diagnosticsAnalyzer;
        stageTimer.restart();
        result.diagnostics =
            diagnosticsAnalyzer.extractWorkspaceDiagnostics(svFiles,
                                                           includeDirs,
                                                           defines,
                                                           isCancelled);
        result.diagnosticsExtractionMs = stageTimer.elapsed();
        result.workerElapsedMs = workerTimer.elapsed();
        if (cancelled()) {
            result.cancelled = true;
            result.diagnostics.clear();
            result.workerElapsedMs = workerTimer.elapsed();
        }
        return result;
    });

    workspaceAnalysisWatcher->setProperty("workspacePath", workspacePath);
    workspaceAnalysisWatcher->setProperty("totalFiles", totalFiles);
    workspaceAnalysisWatcher->setProperty("generation", QVariant::fromValue<qulonglong>(generation));
    workspaceAnalysisWatcher->setFuture(future);
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndWait()
{
    if (!workspaceAnalysisWatcher || !workspaceAnalysisWatcher->isRunning())
        return;

    QFuture<WorkspaceAnalysisResult> future = workspaceAnalysisWatcher->future();
    workspaceAnalysisWatcher->cancel();
    future.waitForFinished();
}

void SymbolAnalyzer::expireWorkspaceAnalysis()
{
    ++workspaceAnalysisGeneration;
    cancelWorkspacePublication();
    if (!workspaceAnalysisWatcher || !workspaceAnalysisWatcher->isRunning())
        return;

    workspaceAnalysisWatcher->future().cancel();
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndInvalidate()
{
    ++workspaceAnalysisGeneration;
    cancelWorkspacePublication();
    cancelWorkspaceAnalysisAndWait();
}

void SymbolAnalyzer::onWorkspaceAnalysisFinished()
{
    if (!workspaceAnalysisWatcher)
        return;
    if (workspaceAnalysisWatcher->isCanceled()) {
        emit workspaceAnalysisExpired();
        return;
    }

    WorkspaceAnalysisResult result = workspaceAnalysisWatcher->result();
    const QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();
    const int totalFiles = workspaceAnalysisWatcher->property("totalFiles").toInt();
    const std::uint64_t generation =
        workspaceAnalysisWatcher->property("generation").toULongLong();
    if (generation != workspaceAnalysisGeneration || result.generation != generation) {
        emit workspaceAnalysisExpired();
        return;
    }
    if (result.cancelled) {
        emit workspaceAnalysisExpired();
        return;
    }

    startWorkspacePublication(std::move(result), totalFiles, workspacePath);
}

void SymbolAnalyzer::analyzeFileContentAsync(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName))
        return;

    const std::uint64_t generation = fileAnalysisGenerations.value(fileName, 0) + 1;
    fileAnalysisGenerations.insert(fileName, generation);
    const QString expectedContentHash = contentHash(content);

    // Keep the parse task self-contained; the watcher owns only delivery back to this QObject.
    auto* watcher =
        new QFutureWatcher<FileAnalysisResult>(this);
    connect(watcher,
            &QFutureWatcher<FileAnalysisResult>::finished,
            this,
            [this, fileName, expectedContentHash, generation, watcher]() {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (fileAnalysisGenerations.value(fileName, 0) != generation)
                    return;
                if (result.generation != generation
                    || result.contentHash != expectedContentHash) {
                    return;
                }
                publishFileAnalysisResult(result.fileName,
                                          result.content,
                                          result.symbolRecords,
                                          result.diagnostics);
                emit analysisCompleted(result.fileName,
                                       result.symbolRecords.size());
            });
    watcher->setFuture(QtConcurrent::run([fileName, content, expectedContentHash, generation]() {
        FileAnalysisResult result;
        result.fileName = fileName;
        result.content = content;
        result.contentHash = expectedContentHash;
        result.generation = generation;
        SlangManager symbolAnalyzer;
        result.symbolRecords =
            symbolAnalyzer.extractSymbolRecords(fileName, content);
        SlangManager diagnosticsAnalyzer;
        result.diagnostics = diagnosticsAnalyzer.extractDiagnostics(fileName, content);
        return result;
    }));
}
