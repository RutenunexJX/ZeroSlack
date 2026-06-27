#include "navigationmanager.h"
#include "activitylogservice.h"
#include "navigationservice.h"
#include "navigationwidget.h"

#include <QElapsedTimer>
#include <QFileInfo>

namespace {
QString navigationViewName(NavigationManager::NavigationView view)
{
    switch (view) {
    case NavigationManager::FileHierarchyView:
        return QStringLiteral("file hierarchy");
    case NavigationManager::ModuleHierarchyView:
        return QStringLiteral("module hierarchy");
    case NavigationManager::SymbolHierarchyView:
        return QStringLiteral("symbol outline");
    case NavigationManager::DesignHierarchyView:
        return QStringLiteral("design hierarchy");
    }
    return QStringLiteral("navigation");
}

bool isOpenTabsAnalysisFile(const QString& fileName)
{
    return QFileInfo(fileName).fileName() == QStringLiteral("open_tabs");
}
}

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent)
{
    navigationService = NavigationService::getInstance();
    caches.reserveDefaults();
}

NavigationManager::~NavigationManager()
{
}

void NavigationManager::setNavigationWidget(NavigationWidget* widget)
{
    if (navigationWidget == widget) return;

    navigationWidget = widget;
    if (navigationWidget) {
        setupConnections();
        refreshCurrentView();
    }
}

void NavigationManager::setNavigationService(NavigationService* service)
{
    navigationService = service ? service : NavigationService::getInstance();
    caches.clearModuleHierarchy();
    caches.clearSymbolOutline();
    refreshCurrentView();
}

void NavigationManager::refreshFileHierarchy()
{
    QElapsedTimer timer;
    timer.start();
    const bool sourceChanged = updateFileHierarchyData();
    const bool filterChanged =
        caches.fileHierarchyFilter != context.searchFilter;
    const QStringList visibleFiles = context.searchFilter.isEmpty()
        ? caches.fileList
        : filterFiles(caches.fileList, context.searchFilter);
    const bool refreshWidget =
        !caches.fileHierarchyValid || sourceChanged || filterChanged;

    if (navigationWidget && refreshWidget) {
        navigationWidget->updateFileHierarchy(visibleFiles);
    }
    caches.fileHierarchyFilter = context.searchFilter;
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

void NavigationManager::refreshModuleHierarchy()
{
    QElapsedTimer timer;
    timer.start();
    const bool changed = updateModuleHierarchyData();

    if (navigationWidget && changed) {
        navigationWidget->updateModuleHierarchy(caches.moduleHierarchy);
    }

    emit dataRefreshed(ModuleHierarchyView);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 %2")
            .arg(changed ? QStringLiteral("Rebuilt") : QStringLiteral("Kept cached"),
                 navigationViewName(ModuleHierarchyView)),
        static_cast<int>(timer.elapsed()));
}

void NavigationManager::refreshSymbolHierarchy()
{
    QElapsedTimer timer;
    timer.start();
    const bool changed = updateSymbolHierarchyData();

    if (navigationWidget && changed) {
        navigationWidget->updateSymbolHierarchy(caches.symbolOutline);
    }

    emit dataRefreshed(SymbolHierarchyView);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 %2")
            .arg(changed ? QStringLiteral("Rebuilt") : QStringLiteral("Kept cached"),
                 navigationViewName(SymbolHierarchyView)),
        static_cast<int>(timer.elapsed()));
}

void NavigationManager::refreshDesignHierarchy(bool force)
{
    QElapsedTimer timer;
    timer.start();
    const bool changed = updateDesignHierarchyData(force);

    if (navigationWidget
        && (changed || force || currentView == DesignHierarchyView)) {
        navigationWidget->updateDesignHierarchy(caches.designHierarchy);
    }

    emit dataRefreshed(DesignHierarchyView);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Navigation"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 %2")
            .arg(changed || force ? QStringLiteral("Rebuilt") : QStringLiteral("Kept cached"),
                 navigationViewName(DesignHierarchyView)),
        static_cast<int>(timer.elapsed()));
}

void NavigationManager::warmDesignHierarchyCache()
{
    if (currentView == DesignHierarchyView)
        return;

    const bool changed = updateDesignHierarchyData(false);
    if (changed && navigationWidget) {
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
    case ModuleHierarchyView:
        refreshModuleHierarchy();
        break;
    case SymbolHierarchyView:
        refreshSymbolHierarchy();
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

void NavigationManager::navigateToSymbol(const SymbolOutlineSymbolRow& row)
{
    emit symbolRowNavigationRequested(row);
}

void NavigationManager::navigateToModule(const QString& moduleName)
{
    if (moduleName.isEmpty()) return;
    if (!navigationService) return;

    const NavigationModuleTarget target =
        navigationService->resolveModuleTarget(moduleName);
    if (target.found)
        navigateToSymbol(target.symbolRow);
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

void NavigationManager::setSearchFilter(const QString& filter)
{
    context.setSearchFilter(filter);

    // Reapply the current search filter.
    refreshCurrentView();
}

void NavigationManager::clearSearchFilter()
{
    context.clearSearchFilter();
    refreshCurrentView();
}

void NavigationManager::highlightCurrentFileInTree()
{
    if (!navigationWidget || context.currentFileName.isEmpty()) return;

    navigationWidget->highlightFile(context.currentFileName);
}

void NavigationManager::syncWithActiveEditor()
{
    highlightCurrentFileInTree();

    // Keep the symbol view aligned with the active editor.
    if (currentView == SymbolHierarchyView
        && !context.currentFileName.isEmpty()) {
        refreshSymbolHierarchy();
    }
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

    // Workspace activation changes file/module/symbol scope, while Design
    // hierarchy is cached per workspace to keep tab switching lightweight.
    caches.clearFileList();
    caches.clearModuleHierarchy();
    caches.clearSymbolOutline();
    restoreDesignHierarchyCache();

    refreshCurrentView();
}

void NavigationManager::onViewChanged(int index)
{
    setActiveView(index == NavigationWidget::DesignTab
                      ? DesignHierarchyView
                      : FileHierarchyView);
}

void NavigationManager::onSearchFilterChanged(const QString& filter)
{
    setSearchFilter(filter);
}

void NavigationManager::onSymbolAnalysisCompleted(const QString& fileName, int symbolCount)
{
    Q_UNUSED(symbolCount)

    // Symbol analysis can update either the full module graph or the current symbol outline.
    switch (currentView) {
    case FileHierarchyView:
        // The file list is unchanged.
        break;
    case ModuleHierarchyView: {
        caches.clearModuleHierarchy();
        refreshModuleHierarchy();
        break;
    }
    case SymbolHierarchyView:
        // The symbol view follows the current file.
        if (context.currentFileName == fileName) {
            caches.clearSymbolOutline();
            refreshSymbolHierarchy();
        }
        break;
    case DesignHierarchyView:
        if (isOpenTabsAnalysisFile(fileName)) {
            if (navigationService && caches.designHierarchyValid) {
                const std::uint64_t snapshotRevision =
                    navigationService->semanticSnapshotRevision();
                caches.designSnapshotGeneration = snapshotRevision;
                caches.designHierarchy.snapshotGeneration = snapshotRevision;
            }
            break;
        }
        invalidateCurrentDesignHierarchyCache();
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

    // Batch analysis can change module hierarchy and symbol outline data.
    if (currentView == ModuleHierarchyView) {
        caches.clearSymbolOutline();
        caches.clearModuleHierarchy();
        refreshCurrentView();
    } else if (currentView == SymbolHierarchyView) {
        caches.clearSymbolOutline();
        caches.clearModuleHierarchy();
        refreshSymbolHierarchy();
    } else if (currentView == DesignHierarchyView) {
        invalidateCurrentDesignHierarchyCache();
        refreshDesignHierarchy();
    }
}

void NavigationManager::setActiveView(NavigationView view)
{
    if (currentView == view) return;

    currentView = view;
    emit viewChanged(currentView);

    // Refresh the newly active view.
    refreshCurrentView();
}
