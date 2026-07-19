#include "analysiscoordinator.h"

#include "analysisscheduler.h"
#include "documentsnapshot.h"
#include <QPointer>
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
    const QPointer<AnalysisCoordinator> self(this);
    scheduler->setWorkspaceOpenProvider([self]() {
        return self && self->dependencies.isWorkspaceOpen();
    });
    scheduler->setWorkspaceSymbolCancelProvider([self]() {
        return self
            && self->dependencies.isWorkspaceSymbolAnalysisCancelled();
    });
    scheduler->setCurrentFileProvider([self]() {
        return self ? self->dependencies.currentDocument().fileName
                    : QString();
    });
}

void AnalysisCoordinator::refreshActiveEditorForFile(const QString& fileName) const
{
    dependencies.refreshSemanticPresentations(fileName);
    const QPointer<const AnalysisCoordinator> self(this);
    QTimer::singleShot(0, this, [self, fileName]() {
        if (self)
            self->dependencies.refreshSemanticPresentations(fileName);
    });
}
