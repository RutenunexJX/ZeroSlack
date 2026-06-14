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
    m_slangManager = new SlangManager();
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
    delete m_slangManager;
    m_slangManager = nullptr;
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

    emit analysisStarted(workspacePath);

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run([this, svFiles, includeDirs, defines, isCancelled]() {
        QList<sym_list::SymbolInfo> symbols =
            m_slangManager->extractWorkspaceSymbols(svFiles, includeDirs, defines);
        WorkspaceAnalysisResult result =
            SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
                svFiles,
                symbols,
                isCancelled);
        result.diagnostics =
            m_slangManager->extractWorkspaceDiagnostics(svFiles, includeDirs, defines);
        return result;
    });

    workspaceAnalysisWatcher->setProperty("workspacePath", workspacePath);
    workspaceAnalysisWatcher->setProperty("totalFiles", totalFiles);
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

void SymbolAnalyzer::onWorkspaceAnalysisFinished()
{
    if (!workspaceAnalysisWatcher)
        return;
    if (workspaceAnalysisWatcher->isCanceled())
        return;

    const WorkspaceAnalysisResult result = workspaceAnalysisWatcher->result();
    const QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();
    const int totalFiles = workspaceAnalysisWatcher->property("totalFiles").toInt();

    const int filesAnalyzed = publishWorkspaceAnalysisResult(result, totalFiles);
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(workspacePath, result.totalSymbols);
}

void SymbolAnalyzer::analyzeFileContentAsync(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName))
        return;

    // Keep the parse task self-contained; the watcher owns only delivery back to this QObject.
    auto* watcher =
        new QFutureWatcher<QPair<QList<sym_list::SymbolInfo>, QList<SemanticDiagnostic>>>(this);
    connect(watcher,
            &QFutureWatcher<QPair<QList<sym_list::SymbolInfo>, QList<SemanticDiagnostic>>>::finished,
            this,
            [this, fileName, content, watcher]() {
                const auto result = watcher->result();
                watcher->deleteLater();
                publishFileAnalysisResult(fileName, content, result.first, result.second);
                emit analysisCompleted(fileName, result.first.size());
            });
    watcher->setFuture(QtConcurrent::run([fileName, content]() {
        SlangManager local;
        return qMakePair(local.extractSymbols(fileName, content),
                         local.extractDiagnostics(fileName, content));
    }));
}
