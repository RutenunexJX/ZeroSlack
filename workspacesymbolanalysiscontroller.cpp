#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "symbolanalyzer.h"

WorkspaceSymbolAnalysisController::WorkspaceSymbolAnalysisController(QObject* parent)
    : QObject(parent)
{
}

void WorkspaceSymbolAnalysisController::setDocumentModel(DocumentModel* model)
{
    documentModel = model;
}

void WorkspaceSymbolAnalysisController::setProjectModel(ProjectModel* model)
{
    if (projectModel == model)
        return;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);

    projectModel = model;
    if (!projectModel)
        return;

    connect(projectModel,
            &ProjectModel::projectChanged,
            this,
            &WorkspaceSymbolAnalysisController::onProjectChanged);
    connect(projectModel,
            &ProjectModel::projectClosed,
            this,
            &WorkspaceSymbolAnalysisController::clearProjectSemanticState);
    projectSemanticStateCleared = !projectModel->isOpen();
}

void WorkspaceSymbolAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisStarted,
            this,
            &WorkspaceSymbolAnalysisController::fileSymbolAnalysisStarted);
    connect(symbolAnalyzer,
            &SymbolAnalyzer::analysisCompleted,
            this,
            [this](const QString& fileName, int symbolCount) {
                emit fileSymbolAnalysisFinished(fileName, symbolCount);
                emit diagnosticsRefreshRequested(fileName);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchProgress,
            this,
            [this](int filesDone, int totalFiles, const QString& currentFileName) {
                emit workspaceSymbolAnalysisProgress(currentFileName,
                                                     filesDone,
                                                     totalFiles);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchAnalysisCompleted,
            this,
            [this](int filesAnalyzed, int totalSymbols) {
                emit diagnosticsRefreshRequested(QString());
                onWorkspaceSymbolAnalysisCompleted(filesAnalyzed, totalSymbols);
            });
}

void WorkspaceSymbolAnalysisController::setCancelProvider(
    std::function<bool()> provider)
{
    cancelProvider = std::move(provider);
}

void WorkspaceSymbolAnalysisController::setCurrentFileProvider(
    std::function<QString()> provider)
{
    currentFileProvider = std::move(provider);
}

QStringList WorkspaceSymbolAnalysisController::dirtyOpenDocumentFiles() const
{
    QStringList files;
    if (!documentModel)
        return files;

    for (const DocumentSnapshot& snapshot : documentModel->openDocuments()) {
        if (snapshot.fileName.isEmpty() || !snapshot.dirty)
            continue;
        files.append(snapshot.fileName);
    }
    files.removeDuplicates();
    return files;
}
