#include "navigationmanager.h"
#include "activitylogservice.h"
#include "navigationservice.h"
#include "navigationwidget.h"
#include "workspacefileoperationservice.h"
#include "workspacemanager.h"

#include <QElapsedTimer>
#include <QSignalBlocker>

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
}

NavigationManager::~NavigationManager()
{
}

void NavigationManager::setNavigationWidget(NavigationWidget* widget)
{
    if (navigationWidget == widget) return;

    navigationWidget = widget;
    designHierarchyWidgetValid = false;
    if (navigationWidget) {
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
    QElapsedTimer timer;
    timer.start();
    const bool changed = updateDesignHierarchyData(force);
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
        navigationWidget->setDesignParticipatingFiles(
            caches.designHierarchy.participatingFiles);
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

void NavigationManager::setDesignTop(const QString& moduleName)
{
    if (moduleName.isEmpty())
        return;
    caches.designTopModule = moduleName;
    caches.designTopInferred = false;
    invalidateCurrentDesignHierarchyCache();
    refreshDesignHierarchy(true);
    if (navigationWidget)
        navigationWidget->setActiveTab(NavigationWidget::DesignTab);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("Selected Design Top %1").arg(moduleName));
}

void NavigationManager::clearDesignTop()
{
    caches.designTopModule.clear();
    caches.designTopInferred = true;
    invalidateCurrentDesignHierarchyCache();
    refreshDesignHierarchy(true);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("Cleared Design Top override"));
}

QString NavigationManager::selectedDesignTopModule() const
{
    return caches.designTopModule;
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
    context.setCurrentWorkspacePath(workspacePath);
    if (navigationWidget)
        navigationWidget->setWorkspaceRoot(workspacePath);

    // Workspace activation changes file scope, while Design hierarchy is
    // cached per workspace to keep tab switching lightweight.
    caches.clearFileList();
    restoreDesignHierarchyCache();
    designHierarchyWidgetValid = false;

    refreshCurrentView();
}

void NavigationManager::onViewChanged(int index)
{
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

    if (semanticAnalysisContext.impact
            == SemanticChangeImpact::TriviaOnly
        || semanticAnalysisContext.impact
               == SemanticChangeImpact::LocalBody) {
        return;
    }

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
