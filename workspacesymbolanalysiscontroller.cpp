#include "workspacesymbolanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

WorkspaceSymbolAnalysisController::WorkspaceSymbolAnalysisController(QObject* parent)
    : QObject(parent)
{
}

void WorkspaceSymbolAnalysisController::setDocumentModel(DocumentModel* model)
{
    if (documentModel == model)
        return;
    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);

    documentModel = model;
    if (!documentModel)
        return;

    // A workspace worker owns an immutable snapshot of every open buffer.
    // Opening, editing, or closing a document changes that transaction's
    // overlay set, so restart once from the latest complete DocumentModel
    // snapshot. OpenDocumentAnalysisController suppresses its redundant
    // per-document task while this replacement is active.
    connect(documentModel,
            &DocumentModel::documentOpened,
            this,
            [this](const DocumentSnapshot& snapshot) {
                if (snapshot.saved && !snapshot.dirty) {
                    const QString openText =
                        documentModel
                            ? documentModel->documentTextForFile(
                                  snapshot.fileName)
                            : QString();
                    const QString indexedText =
                        SemanticIndex::getInstance()
                            ->getCachedFileContent(snapshot.fileName);
                    if (!openText.isNull()
                        && !indexedText.isNull()
                        && openText == indexedText) {
                        return;
                    }
                }
                restartActiveWorkspaceAnalysisForDocumentChange();
            });
    connect(documentModel,
            &DocumentModel::documentEdited,
            this,
            [this](const DocumentSnapshot&) {
                restartActiveWorkspaceAnalysisForDocumentChange();
            });
    connect(documentModel,
            &DocumentModel::documentClosed,
            this,
            [this](const QString&, const QString&) {
                restartActiveWorkspaceAnalysisForDocumentChange();
            });
}

void WorkspaceSymbolAnalysisController::
    restartActiveWorkspaceAnalysisForDocumentChange()
{
    if (!workspaceAnalysisActive
        || !symbolAnalyzer
        || !activeRequestedProject.isOpen()) {
        return;
    }

    requestQueue.queueLatest(activeRequestedProject);
    ++workspaceStartGeneration;
    const WorkspaceAnalysisRequestTelemetry telemetry =
        requestQueue.telemetry();
    QPointer<WorkspaceSymbolAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
    emit workspaceAnalysisRequestQueued(telemetry);
    if (!self || !analyzer || symbolAnalyzer != analyzer
        || !workspaceAnalysisActive) {
        return;
    }
    analyzer->expireWorkspaceAnalysis();
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
    connect(projectModel,
            &QObject::destroyed,
            this,
            [this]() {
                projectModel = nullptr;
                clearProjectSemanticState();
            });
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
                QPointer<WorkspaceSymbolAnalysisController> self(this);
                emit fileSymbolAnalysisFinished(fileName, symbolCount);
                if (!self)
                    return;
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
                QPointer<WorkspaceSymbolAnalysisController> self(this);
                emit diagnosticsRefreshRequested(QString());
                if (!self)
                    return;
                onWorkspaceSymbolAnalysisCompleted(filesAnalyzed, totalSymbols);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::workspaceAnalysisExpired,
            this,
            &WorkspaceSymbolAnalysisController::onWorkspaceSymbolAnalysisExpired);
    connect(symbolAnalyzer,
            &QObject::destroyed,
            this,
            [this]() {
                symbolAnalyzer = nullptr;
                ++workspaceStartGeneration;
                requestQueue.clear();
                activeRequestedProject = ProjectSnapshot();
                activeProject = ProjectSnapshot();
                workspaceAnalysisActive = false;
                activeWorkspaceAnalysisComplete = true;
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
