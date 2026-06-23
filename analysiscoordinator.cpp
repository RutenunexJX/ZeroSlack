#include "analysiscoordinator.h"

#include "analysisscheduler.h"
#include "documentsnapshot.h"
#include "mycodeeditor.h"

#include <QTimer>

#include <utility>

AnalysisCoordinator::AnalysisCoordinator(
    AnalysisScheduler* scheduler,
    AnalysisProgressCoordinator* progressCoordinator,
    SemanticRuntimeCoordinator* semanticRuntime,
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    QObject* parent)
    : QObject(parent)
{
    dependencies.set(scheduler,
                     progressCoordinator,
                     semanticRuntime,
                     tabManager,
                     workspaceManager,
                     navigationManager);
}

void AnalysisCoordinator::setFileChangeDebounceMs(int debounceMs)
{
    fileChangeDebounceMs = debounceMs;
}

void AnalysisCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void AnalysisCoordinator::setProblemsRefreshHandler(
    std::function<void(const QString&)> handler)
{
    problemsRefreshHandler = std::move(handler);
}

void AnalysisCoordinator::connectSignals()
{
    if (signalsConnected)
        return;

    configureScheduler();
    connectSchedulerSignals();
    connectProgressSignals();
    connectWorkspaceSignals();

    signalsConnected = true;
}

void AnalysisCoordinator::configureScheduler()
{
    if (!dependencies.hasScheduler())
        return;

    dependencies.configureScheduler();
    AnalysisScheduler* scheduler = dependencies.schedulerObject();
    scheduler->setWorkspaceOpenProvider([this]() {
        return dependencies.isWorkspaceOpen();
    });
    scheduler->setWorkspaceSymbolCancelProvider([this]() {
        return dependencies.isWorkspaceSymbolAnalysisCancelled();
    });
    scheduler->setCurrentFileProvider([this]() {
        return dependencies.currentDocument().fileName;
    });
}

void AnalysisCoordinator::refreshActiveEditorForFile(const QString& fileName) const
{
    MyCodeEditor* editor = dependencies.currentEditor();
    const DocumentSnapshot document = dependencies.currentDocument();
    if (!editor || document.fileName != fileName)
        return;

    editor->refreshScopeAndCurrentLineHighlight();
    QTimer::singleShot(0, editor, [editor]() {
        editor->refreshScopeAndCurrentLineHighlight();
    });
}
