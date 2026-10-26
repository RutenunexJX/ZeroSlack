#include "navigationmanager.h"
#include "activitylogservice.h"
#include "navigationservice.h"
#include "navigationwidget.h"
#include "workspacefileoperationservice.h"
#include "workspacemanager.h"
#include "../simulation/simdock/ui/workbench.h"
#include "uidialogs.h"
#include <QMessageBox>
#include <QDir>
#include "editorfileidentity.h"

#include <QElapsedTimer>
#include <QSignalBlocker>
#include <QEvent>
#include <QTimer>
#include <QThread>

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
    ++designRequestGeneration;
    pendingDesignRequest.reset();
    if (designWatcher)
        disconnect(designWatcher, nullptr, this, nullptr);
    designThreadPool.waitForDone();
}

bool NavigationManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == navigationWidget) {
        if (event->type() == QEvent::Hide) {
            ++designRequestGeneration;
            pendingDesignRequest.reset();
        } else if (event->type() == QEvent::Show) {
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

    if (refreshWidget) {
        navigationWidget->updateDesignHierarchy(caches.designHierarchy);
        designHierarchyWidgetValid = true;
    }

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
{
    return connectedWorkbench && EditorFileIdentity::same(connectedWorkbench->workspace(), context.currentWorkspacePath)
        ? connectedWorkbench->designRoots() : QList<DesignHierarchyRoot>{};
}
void NavigationManager::connectToWorkbench(simdock::Workbench* workbench)
{
    if (connectedWorkbench == workbench) return;
    if (connectedWorkbench) disconnect(connectedWorkbench, nullptr, this, nullptr);
    connectedWorkbench = workbench;
    if (!workbench) return;
    connect(workbench, &simdock::Workbench::designProjectsChanged, this, &NavigationManager::synchronizeDesignProjects);
    connect(workbench, &QObject::destroyed, this, [this] { connectedWorkbench = nullptr; });
    synchronizeDesignProjects();
}
void NavigationManager::synchronizeDesignProjects()
{
    if (!connectedWorkbench || !EditorFileIdentity::same(connectedWorkbench->workspace(), context.currentWorkspacePath)) return;
    const auto roots = currentDesignRoots();
    const auto active = connectedWorkbench->activeProjectId();
    if (connectedWorkspaceManager) {
        QString module, file;
        for (const auto& root : roots) if (root.projectId == active) { module = root.module; file = root.file; }
        connectedWorkspaceManager->getProjectModel()->setActiveDesignProject(active, module, file);
    }
    if (roots == caches.designRoots && active == caches.activeProjectId) return;
    invalidateCurrentDesignHierarchyCache();
    if (currentView == DesignHierarchyView) refreshDesignHierarchy();
    else warmDesignHierarchyCache();
}
QString NavigationManager::setDesignTop(const QString& moduleName, const QString& definitionFile)
{
    if (!connectedWorkbench || !navigationService) return tr("The workspace project service is unavailable.");
    if (context.currentWorkspacePath.isEmpty() || !EditorFileIdentity::same(connectedWorkbench->workspace(), context.currentWorkspacePath))
        return tr("Open a workspace before adding a design project.");
    QList<SemanticSymbolRecord> matches;
    const auto scope = getSystemVerilogFiles();
    for (const auto& module : navigationService->availableModules()) {
        if (module.name != moduleName || (!definitionFile.isEmpty() && !EditorFileIdentity::same(module.location.fileName, definitionFile))) continue;
        bool inScope = false;
        for (const auto& file : scope) if (EditorFileIdentity::same(file, module.location.fileName)) { inScope = true; break; }
        if (!inScope) continue;
        matches << module;
    }
    if (matches.size() != 1) return matches.isEmpty()
        ? tr("No current module definition matches this source. Analyze the file before creating a design project.")
        : tr("Multiple definitions match this module. Choose the module from its source file.");
    const auto issue = connectedWorkbench->addDesignProject(matches.first().location.fileName, moduleName);
    if (issue.isEmpty() && navigationWidget) {
        navigationWidget->finishDesignProjectSelection({}, matches.first().location.fileName, moduleName);
        navigationWidget->setActiveTab(NavigationWidget::DesignTab);
    }
    return issue;
}
void NavigationManager::chooseDesignProject()
{
    if (!navigationWidget) return;
    navigationWidget->beginDesignProjectSelection();
    refreshDesignHierarchy();
}
void NavigationManager::clearDesignTop()
{
    if (connectedWorkbench) connectedWorkbench->openProject({});
}
QString NavigationManager::selectedDesignTopModule() const
{
    if (!connectedWorkbench) return {};
    for (const auto& root : currentDesignRoots())
        if (root.projectId == connectedWorkbench->activeProjectId()) return root.module;
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
    context.setCurrentWorkspacePath(workspacePath);
    semanticAnalysisContext = {};
    if (navigationWidget)
        navigationWidget->setWorkspaceRoot(workspacePath);

    // Workspace activation changes file scope, while Design hierarchy is
    // cached per workspace to keep tab switching lightweight.
    caches.clearFileList();
    restoreDesignHierarchyCache();
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
