#include "navigationmanager.h"
#include "activitylogservice.h"
#include "navigationservice.h"
#include "navigationwidget.h"
#include "workspacefileoperationservice.h"
#include "workspacemanager.h"
#include "../workspace/designprojectservice.h"
#include "uidialogs.h"
#include <QMessageBox>
#include <QDir>
#include "editorfileidentity.h"

#include <QElapsedTimer>
#include <QSignalBlocker>
#include <QEvent>
#include <QTimer>
#include <QThread>
#include <algorithm>

namespace {
QString navigationViewName(NavigationManager::NavigationView view)
{
    switch (view) {
    case NavigationManager::FileHierarchyView:
        return QStringLiteral("file hierarchy");
    case NavigationManager::DesignHierarchyView:
        return QStringLiteral("design hierarchy");
    }
    return QStringLiteral("navigation");
}

}

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent),
      fileOperationService(
          std::make_unique<
              WorkspaceFileOperationService>())
{
    navigationService = NavigationService::getInstance();
    designThreadPool.setMaxThreadCount(1);
    designThreadPool.setThreadPriority(QThread::LowPriority);
    designThreadPool.setObjectName(QStringLiteral("ZeroSlackDesignDerivation"));
}

NavigationManager::~NavigationManager()
{
    designShuttingDown = true;
    if (activeDesignRequest) activeDesignRequest->cancelled->store(true);
    ++designRequestGeneration;
    pendingDesignRequest.reset();
    if (designWatcher)
        disconnect(designWatcher, nullptr, this, nullptr);
    designThreadPool.waitForDone();
}

bool NavigationManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == navigationWidget) {
        if (event->type() == QEvent::Show) {
            QTimer::singleShot(0, this, [this] {
                refreshCurrentView();
                warmDesignHierarchyCache();
            });
        }
    }
    return QObject::eventFilter(watched, event);
}

void NavigationManager::setNavigationWidget(NavigationWidget* widget)
{
    if (navigationWidget == widget) return;

    if (navigationWidget)
        navigationWidget->removeEventFilter(this);
    navigationWidget = widget;
    designHierarchyWidgetValid = false;
    if (navigationWidget) {
        navigationWidget->installEventFilter(this);
        navigationWidget->setWorkspaceRoot(
            context.currentWorkspacePath);
        setupConnections();
        refreshCurrentView();
    }
}

void NavigationManager::setNavigationService(NavigationService* service)
{
    navigationService = service ? service : NavigationService::getInstance();
    caches.clearDesignHierarchy();
    designHierarchyWidgetValid = false;
    refreshCurrentView();
}

void NavigationManager::refreshFileHierarchy()
{
    QElapsedTimer timer;
    timer.start();
    const bool sourceChanged = updateFileHierarchyData();
    const QString searchFilter =
        context.searchFilter(FileHierarchyView);
    const bool filterChanged =
        caches.fileHierarchyFilter != searchFilter;
    const bool refreshWidget =
        !caches.fileHierarchyValid || sourceChanged || filterChanged;

    if (navigationWidget && refreshWidget) {
        navigationWidget->updateFileHierarchy(
            caches.fileList,
            connectedWorkspaceManager
                ? connectedWorkspaceManager
                      ->virtualSourceGroups()
                : QList<
                      WorkspaceVirtualSourceGroup>{});
    }
    caches.fileHierarchyFilter = searchFilter;
    caches.fileHierarchyValid = true;

    emit dataRefreshed(FileHierarchyView);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 %2")
            .arg(refreshWidget ? QStringLiteral("Rebuilt") : QStringLiteral("Kept cached"),
                 navigationViewName(FileHierarchyView)),
        static_cast<int>(timer.elapsed()));
}

void NavigationManager::refreshDesignHierarchy(bool force)
{
    if (!navigationWidget || !navigationWidget->isVisible())
        return;
    QElapsedTimer timer;
    timer.start();
    const bool changed = updateDesignHierarchyData(force);
    if (!caches.designHierarchyValid)
        return;
    const bool refreshWidget = navigationWidget
        && (changed || force || !designHierarchyWidgetValid);

    if (!refreshWidget)
        return;
    navigationWidget->updateDesignHierarchy(caches.designHierarchy);
    designHierarchyWidgetValid = true;

    emit dataRefreshed(DesignHierarchyView);
    SemanticAnalysisTelemetry telemetry = semanticAnalysisContext;
    telemetry.stage = SemanticAnalysisStage::Navigation;
    telemetry.uiRefreshMs = timer.elapsed();
    telemetry.detail = QStringLiteral(
        "hierarchyRebuild=%1 widgetRefresh=%2")
        .arg(changed || force ? 1 : 0)
        .arg(refreshWidget ? 1 : 0);
    emit navigationTelemetry(telemetry);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 %2")
            .arg(changed || force ? QStringLiteral("Rebuilt") : QStringLiteral("Kept cached"),
                 navigationViewName(DesignHierarchyView)),
        static_cast<int>(timer.elapsed()));
}

void NavigationManager::setSemanticAnalysisContext(
    const SemanticAnalysisTelemetry& telemetry)
{
    semanticAnalysisContext = telemetry;
}

void NavigationManager::warmDesignHierarchyCache()
{
    if (currentView == DesignHierarchyView)
        return;

    const bool changed = updateDesignHierarchyData(false);
    if (changed && navigationWidget) {
        designHierarchyWidgetValid = false;
        navigationWidget->updateDesignSummary(caches.designHierarchy);
    }
}

void NavigationManager::refreshCurrentView()
{
    switch (currentView) {
    case FileHierarchyView:
        refreshFileHierarchy();
        break;
    case DesignHierarchyView:
        refreshDesignHierarchy();
        break;
    }
}

void NavigationManager::navigateToFile(const QString& filePath, int lineNumber)
{
    if (filePath.isEmpty()) return;

    emit navigationRequested(filePath, lineNumber);
}

QList<DesignHierarchyRoot> NavigationManager::currentDesignRoots() const
{ return caches.designRoots; }
void NavigationManager::connectToDesignProjects(DesignProjectService* service)
{
    if (designProjects == service) return;
    if (designProjects) disconnect(designProjects, nullptr, this, nullptr);
    designProjects = service;
    if (!service) return;
    connect(service, &DesignProjectService::designProjectsChanged, this, &NavigationManager::synchronizeDesignProjects);
    connect(service, &QObject::destroyed, this, [this] { designProjects = nullptr; });
    synchronizeDesignProjects();
}

void NavigationManager::setSimulationHandlers(std::function<QPair<QString,QString>()> target,
                                              std::function<QString(const QString&,const QString&)> select)
{
    simulationTarget=std::move(target);
    selectSimulationTop=std::move(select);
    if (navigationWidget) navigationWidget->setSimulationTargetProvider(simulationTarget);
}
void NavigationManager::synchronizeDesignProjects()
{
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        auto* model = connectedWorkspaceManager->getProjectModel();
        if (!model->snapshot().explicitDesignSelection) model->setActiveDesignProject({}, {}, {});
    }
    if (designProjects && navigationWidget && EditorFileIdentity::same(designProjects->workspace(), context.currentWorkspacePath))
        navigationWidget->restoreDesignViewState(designProjects->viewState());
}
QString NavigationManager::setDesignTop(const QString& moduleName, const QString& definitionFile)
{
    if (!navigationService || !navigationWidget) return tr("Design is unavailable.");
    QList<SemanticSymbolRecord> matches;
    const auto scope = getSystemVerilogFiles();
    for (const auto& module : navigationService->availableModules()) {
        if (module.name != moduleName || (!definitionFile.isEmpty() && !EditorFileIdentity::same(module.location.fileName, definitionFile))) continue;
        if (std::any_of(scope.cbegin(), scope.cend(), [&](const auto& file) { return EditorFileIdentity::same(file, module.location.fileName); })) matches.append(module);
    }
    if (matches.size() != 1) return matches.isEmpty() ? tr("No current module definition matches this source.")
        : tr("Multiple definitions match this module; choose its source file.");
    navigationWidget->setActiveTab(NavigationWidget::DesignTab);
    navigationWidget->finishDesignProjectSelection({}, matches.first().location.fileName, moduleName);
    refreshDesignHierarchy();
    return {};
}
void NavigationManager::chooseDesignProject()
{ if (navigationWidget) navigationWidget->beginDesignProjectSelection(); }
void NavigationManager::clearDesignTop()
{
    caches.activeProjectId.clear();
    if (connectedWorkspaceManager) connectedWorkspaceManager->getProjectModel()->setActiveDesignProject({}, {}, {});
}
QString NavigationManager::selectedDesignTopModule() const
{
    for (const auto& node : caches.designHierarchy.nodes) if (node.isTop && node.rootId == caches.activeProjectId) return node.moduleType;
    return {};
}

void NavigationManager::setSearchFilter(const QString& filter)
{
    setSearchFilter(currentView, filter);
}

void NavigationManager::setSearchFilter(NavigationView view,
                                        const QString& filter)
{
    const QString normalized =
        filter.trimmed();
    const NavigationWidget::NavigationTab tab =
        view == DesignHierarchyView
        ? NavigationWidget::DesignTab
        : NavigationWidget::FileTab;
    if (navigationWidget
        && navigationWidget->searchFilter(tab)
               != normalized) {
        const QSignalBlocker blocker(
            navigationWidget);
        navigationWidget->setSearchFilter(
            tab, normalized);
    }
    context.setSearchFilter(view, normalized);
}

void NavigationManager::highlightCurrentFileInTree()
{
    if (!navigationWidget || context.currentFileName.isEmpty()) return;

    navigationWidget->highlightFile(context.currentFileName);
}

void NavigationManager::syncWithActiveEditor()
{
    highlightCurrentFileInTree();
}

void NavigationManager::onTabChanged(const QString& fileName)
{
    context.setCurrentFileName(fileName);
    syncWithActiveEditor();
}

void NavigationManager::onWorkspaceChanged(const QString& workspacePath)
{
    saveDesignHierarchyCache();
    ++designRequestGeneration;
    pendingDesignRequest.reset();
    if (activeDesignRequest) activeDesignRequest->cancelled->store(true);
    pendingNavigationNode.clear();
    context.setCurrentWorkspacePath(workspacePath);
    semanticAnalysisContext = {};
    if (navigationWidget)
        navigationWidget->setWorkspaceRoot(workspacePath);

    // Workspace activation changes file scope, while Design hierarchy is
    // cached per workspace to keep tab switching lightweight.
    caches.clearFileList();
    restoreDesignHierarchyCache();
    synchronizeDesignProjects();
    designHierarchyWidgetValid = false;
    if (navigationWidget)
        navigationWidget->clearDesignHierarchy();

    refreshCurrentView();
}

void NavigationManager::onViewChanged(int index)
{
    if (index != NavigationWidget::FileTab && index != NavigationWidget::DesignTab) return;
    setActiveView(index == NavigationWidget::DesignTab
                      ? DesignHierarchyView
                      : FileHierarchyView);
}

void NavigationManager::onSymbolAnalysisCompleted(const QString& fileName, int symbolCount)
{
    Q_UNUSED(fileName)
    Q_UNUSED(symbolCount)

    switch (currentView) {
    case FileHierarchyView:
        // The file list is unchanged.
        break;
    case DesignHierarchyView:
        if (navigationService
            && caches.designHierarchyValid
            && caches.designSnapshotGeneration
                   == navigationService->semanticSnapshotRevision()) {
            break;
        }
        refreshDesignHierarchy();
        break;
    }
}

void NavigationManager::onBatchSymbolAnalysisCompleted(
    int filesAnalyzed,
    int totalSymbols)
{
    Q_UNUSED(filesAnalyzed)
    Q_UNUSED(totalSymbols)

    if (currentView == DesignHierarchyView) {
        if (navigationService
            && caches.designHierarchyValid
            && caches.designSnapshotGeneration
                   == navigationService->semanticSnapshotRevision()) {
            return;
        }
        refreshDesignHierarchy();
    }
}

void NavigationManager::setActiveView(NavigationView view)
{
    if (currentView == view) return;

    currentView = view;

    // Refresh the newly active view.
    refreshCurrentView();
}
