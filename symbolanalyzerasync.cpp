#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "symbolanalyzerworkspace.h"
#include "workspacemanager.h"

#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <utility>

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    workspaceAnalysisWatcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    connect(workspaceAnalysisWatcher,
            &QFutureWatcher<WorkspaceAnalysisResult>::finished,
            this,
            &SymbolAnalyzer::onWorkspaceAnalysisFinished);
}

SymbolAnalyzer::~SymbolAnalyzer()
{
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
    cancelWorkspaceAnalysisAndWait();

    const QStringList svFiles = project.systemVerilogFiles;
    const QStringList includeDirs = project.includeDirs;
    const QHash<QString, QString> defines = project.defines;
    const QString workspacePath = project.workspaceRoot;
    const int totalFiles = svFiles.size();
    const std::uint64_t generation = ++workspaceAnalysisGeneration;
    const QStringList protectedFiles = workspaceProtectedFiles;

    emit analysisStarted(workspacePath);

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run([svFiles, includeDirs, defines, isCancelled, generation, protectedFiles]() {
        SlangManager symbolAnalyzer;
        const auto records =
            symbolAnalyzer.extractWorkspaceSymbolRecords(svFiles, includeDirs, defines);
        WorkspaceAnalysisResult result =
            SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
                svFiles,
                records,
                isCancelled);
        SlangManager diagnosticsAnalyzer;
        result.diagnostics =
            diagnosticsAnalyzer.extractWorkspaceDiagnostics(svFiles, includeDirs, defines);
        result.protectedFiles = protectedFiles;
        result.generation = generation;
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
    if (!workspaceAnalysisWatcher || !workspaceAnalysisWatcher->isRunning())
        return;

    workspaceAnalysisWatcher->future().cancel();
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndInvalidate()
{
    ++workspaceAnalysisGeneration;
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

    const WorkspaceAnalysisResult result = workspaceAnalysisWatcher->result();
    const QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();
    const int totalFiles = workspaceAnalysisWatcher->property("totalFiles").toInt();
    const std::uint64_t generation =
        workspaceAnalysisWatcher->property("generation").toULongLong();
    if (generation != workspaceAnalysisGeneration || result.generation != generation) {
        emit workspaceAnalysisExpired();
        return;
    }

    const int filesAnalyzed = publishWorkspaceAnalysisResult(result, totalFiles);
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(workspacePath, result.totalSymbols);
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
