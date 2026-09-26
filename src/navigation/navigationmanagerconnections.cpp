#include "uicontrols.h"
#include <memory>
#include "navigationmanager.h"

#include "navigationservice.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "roundedicons.h"

#include <QAction>
#include <QMenu>

#include <algorithm>
#include <utility>

void NavigationManager::setTemporaryEditorOpenHandler(
    std::function<bool(const EditorLocation&)> handler)
{
    temporaryEditorOpenHandler = std::move(handler);
}

void NavigationManager::connectToTabManager(TabManager* tabManager)
{
    if (connectedTabManager == tabManager) return;

    // Drop connections from the previous manager.
    if (connectedTabManager) {
        disconnect(connectedTabManager, nullptr, this, nullptr);
    }

    connectedTabManager = tabManager;

    if (connectedTabManager) {
        // Wire tab lifecycle events into navigation refreshes.
        connect(connectedTabManager, &TabManager::activeDocumentChanged,
                this, [this](const DocumentSnapshot& document) {
                    context.setCurrentFileName(document.fileName);
                    onTabChanged(context.currentFileName);
                });

        connect(connectedTabManager, &TabManager::tabCreated,
                this, [this](MyCodeEditor*) {
                    // Opening an existing workspace file does not change the
                    // file hierarchy; activeDocumentChanged handles highlight.
                    highlightCurrentFileInTree();
                });

        connect(connectedTabManager, &TabManager::tabClosed,
                this, [this](const QString&) {
                    if (connectedWorkspaceManager
                        && connectedWorkspaceManager->isWorkspaceOpen()) {
                        return;
                    }

                    caches.clearFileList();
                    if (currentView == FileHierarchyView)
                        refreshFileHierarchy();
                });
    }
}

void NavigationManager::connectToWorkspaceManager(WorkspaceManager* workspaceManager)
{
    if (connectedWorkspaceManager == workspaceManager) return;

    // Drop connections from the previous manager.
    if (connectedWorkspaceManager) {
        disconnect(connectedWorkspaceManager, nullptr, this, nullptr);
    }

    connectedWorkspaceManager = workspaceManager;

    if (connectedWorkspaceManager) {
        // Wire workspace events into navigation refreshes.
        connect(connectedWorkspaceManager,
                &WorkspaceManager::workspaceActivated,
                this,
                [this](int, const QString&, const QString& path) {
                    onWorkspaceChanged(path);
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceClosed,
                this, [this]() {
                    if (connectedWorkspaceManager
                        && connectedWorkspaceManager->isWorkspaceOpen()) {
                        return;
                    }
                    context.clearCurrentWorkspacePath();
                    if (navigationWidget)
                        navigationWidget->setWorkspaceRoot(QString());
                    caches.clearFileList();
                    caches.clearDesignHierarchy();
                    designHierarchyCacheByScope.clear();
                    caches.designTopModule.clear();
                    caches.designTopInferred = true;
                    if (navigationWidget)
                        navigationWidget->clearDesignHierarchy();
                    if (currentView == DesignHierarchyView) {
                        return;
                    }
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::filesScanned,
                this, [this](const QStringList&) {
                    caches.clearFileList();
                    invalidateCurrentDesignHierarchyCache();
                    if (currentView == FileHierarchyView)
                        refreshFileHierarchy();
                    else if (currentView == DesignHierarchyView)
                        refreshDesignHierarchy();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::fileChanged,
                this, [this](const QString&) {
                    if (currentView == DesignHierarchyView) {
                        invalidateCurrentDesignHierarchyCache();
                        refreshDesignHierarchy();
                    }
                });
        if (connectedWorkspaceManager->isWorkspaceOpen()) {
            onWorkspaceChanged(
                connectedWorkspaceManager
                    ->getWorkspacePath());
        }
    }
}

void NavigationManager::onFileTreeDoubleClicked(const QString& filePath)
{
    navigateToFile(filePath);
}

void NavigationManager::onFileContextMenuRequested(
    const QString& filePath,
    const QPoint& globalPos)
{
    onFileTreeNodeContextMenuRequested(
        filePath, false, globalPos);
}

QList<DesignHierarchyContextAction>
NavigationManager::designNodeContextActions(
    const DesignHierarchyNode& node) const
{
    struct Spec {
        const char* actionId;
        bool enabled;
        bool separatorBefore;
    };
    const Spec specs[] = {
        {ActionIds::NavigationDesignGoInstantiation,
         !node.isTop && !node.instanceFile.isEmpty(),
         false},
        {ActionIds::NavigationDesignGoDefinition,
         !node.definitionFile.isEmpty(),
         false},
        {ActionIds::ViewTemporaryEditorOpen,
         !node.definitionFile.isEmpty()
             || !node.instanceFile.isEmpty(),
         false},
        {ActionIds::NavigationDesignSetTop,
         !node.moduleType.isEmpty(),
         false},
    };
    ActionAvailabilityContext contextAvailability;
    contextAvailability.workspaceAvailable =
        !context.currentWorkspacePath.isEmpty();
    contextAvailability.semanticCurrent =
        semanticAnalysisContext.generation > 0;
    contextAvailability.hierarchyBound =
        !node.instancePath.isEmpty();

    QList<DesignHierarchyContextAction> result;
    result.reserve(
        static_cast<qsizetype>(
            sizeof(specs) / sizeof(specs[0])));
    for (const Spec& spec : specs) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(spec.actionId));
        if (!descriptor
            || !descriptor->hasSurface(
                ActionSurface::ContextMenu)) {
            continue;
        }
        const ActionAliasDescriptor contextAlias =
            descriptor->aliasForSurface(
                ActionSurface::ContextMenu);
        DesignHierarchyContextAction item;
        item.actionId = descriptor->id;
        item.label = contextAlias.label.isEmpty()
            ? descriptor->canonicalName
            : contextAlias.label;
        item.executionRoute =
            descriptor->executionRoute;
        item.enabled = spec.enabled
            && evaluateActionAvailability(
                   *descriptor,
                   contextAvailability)
                   .executable;
        item.separatorBefore =
            spec.separatorBefore;
        result.append(item);
    }
    return result;
}

ActionExecutionResult
NavigationManager::requestDesignNodeAction(
    const QString& actionId,
    const DesignHierarchyNode& node)
{
    ActionExecutionResult failure;
    failure.handled = true;
    const QList<DesignHierarchyContextAction> actions =
        designNodeContextActions(node);
    const auto selected = std::find_if(
        actions.cbegin(),
        actions.cend(),
        [&actionId](
            const DesignHierarchyContextAction& action) {
            return action.actionId == actionId;
        });
    if (selected == actions.cend()) {
        failure.failureReason = QStringLiteral(
            "Unknown design-hierarchy Action: %1")
                                    .arg(actionId);
        return failure;
    }
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor || !selected->enabled) {
        failure.failureReason = descriptor
            ? descriptor->unavailableReason
            : QStringLiteral(
                  "The design-hierarchy Action is unavailable.");
        return failure;
    }

    ActionInvocation invocation;
    invocation.workspaceId =
        context.currentWorkspacePath;
    invocation.parameters.insert(
        QStringLiteral("nodeId"), node.id);
    invocation.parameters.insert(
        QStringLiteral("rootModule"),
        node.rootModule);
    invocation.parameters.insert(
        QStringLiteral("instancePath"),
        node.instancePath);
    invocation.parameters.insert(
        QStringLiteral("moduleType"),
        node.moduleType);
    if (actionId
        == QString::fromLatin1(
            ActionIds::NavigationDesignGoInstantiation)) {
        invocation.parameters.insert(
            QStringLiteral("path"),
            node.instanceFile);
        invocation.parameters.insert(
            QStringLiteral("line"),
            node.instanceLine);
    } else if (actionId
               == QString::fromLatin1(
                    ActionIds::NavigationDesignGoDefinition)) {
        invocation.parameters.insert(
            QStringLiteral("path"),
            node.definitionFile);
        invocation.parameters.insert(
            QStringLiteral("line"),
            node.definitionLine);
    } else if (actionId
               == QString::fromLatin1(
                   ActionIds::ViewTemporaryEditorOpen)) {
        const bool useDefinition =
            !node.definitionFile.isEmpty();
        invocation.parameters.insert(
            QStringLiteral("path"),
            useDefinition
                ? node.definitionFile
                : node.instanceFile);
        invocation.parameters.insert(
            QStringLiteral("line"),
            useDefinition
                ? node.definitionLine
                : node.instanceLine);
        invocation.parameters.insert(
            QStringLiteral("column"), 1);
        invocation.parameters.insert(
            QStringLiteral("symbolId"),
            node.moduleType);
        invocation.parameters.insert(
            QStringLiteral("sourceLinkId"),
            node.id);
    }
    return executeAction(
        *descriptor, *this, invocation);
}

void NavigationManager::onDesignNodeContextMenuRequested(
    const DesignHierarchyNode& node,
    const QPoint& globalPos)
{
    std::unique_ptr<QMenu> menuOwner(UiControls::menu(navigationWidget));
    QMenu& menu = *menuOwner;
    menu.setObjectName(QStringLiteral("navigationDesignContextMenu"));
    for (const DesignHierarchyContextAction& item :
         designNodeContextActions(node)) {
        if (item.separatorBefore)
            menu.addSeparator();
        QAction* action = menu.addAction(item.label);
        action->setObjectName(
            QStringLiteral("navigationContext.%1")
                .arg(item.actionId));
        action->setProperty(
            "actionId", item.actionId);
        action->setProperty(
            "executionRoute",
            item.executionRoute);
        action->setEnabled(item.enabled);
    }
    menu.addSeparator();
    auto* refresh = menu.addAction(RoundedIcons::icon(RoundedIcons::Refresh), tr("Refresh"));
    refresh->setObjectName(QStringLiteral("refreshDesignAction"));
    refresh->setEnabled(navigationService && !getSystemVerilogFiles().isEmpty());
    auto* automatic = menu.addAction(tr("Use automatic design tops"));
    automatic->setObjectName(QStringLiteral("automaticDesignTopsAction"));
    automatic->setEnabled(!caches.designTopInferred);
    QAction* selected = menu.exec(globalPos);
    if (!selected)
        return;
    if (selected == refresh) { refreshDesignHierarchy(true); return; }
    if (selected == automatic) { clearDesignTop(); return; }
    const QString actionId =
        selected->property("actionId").toString();
    if (!actionId.isEmpty())
        requestDesignNodeAction(actionId, node);
}

void NavigationManager::onDesignNodeDoubleClicked(const DesignHierarchyNode& node)
{
    if (!node.definitionFile.isEmpty()) {
        navigateToDesignNodeFile(node.definitionFile,
                                 node.definitionLine,
                                 node);
        return;
    }
    if (!node.instanceFile.isEmpty())
        navigateToDesignNodeFile(node.instanceFile,
                                 node.instanceLine,
                                 node);
}

void NavigationManager::navigateToDesignNodeFile(
    const QString& filePath,
    int lineNumber,
    const DesignHierarchyNode& node)
{
    if (filePath.isEmpty())
        return;

    HierarchyInstanceContext instanceContext;
    instanceContext.workspacePath = context.currentWorkspacePath;
    instanceContext.activeTopModule = node.rootModule;
    instanceContext.instancePath = node.instancePath;
    emit instanceNavigationRequested(filePath,
                                     lineNumber,
                                     instanceContext);
}

void NavigationManager::setupConnections()
{
    if (!navigationWidget) return;

    // Wire NavigationWidget signals.
    connect(navigationWidget, SIGNAL(fileDoubleClicked(QString)),
            this, SLOT(onFileTreeDoubleClicked(QString)));

    connect(navigationWidget,
            &NavigationWidget::
                fileTreeNodeContextMenuRequested,
            this,
            &NavigationManager::
                onFileTreeNodeContextMenuRequested);

    connect(navigationWidget,
            &NavigationWidget::designNodeContextMenuRequested,
            this,
            &NavigationManager::onDesignNodeContextMenuRequested);

    connect(navigationWidget,
            &NavigationWidget::designNodeDoubleClicked,
            this,
            &NavigationManager::onDesignNodeDoubleClicked);

    connect(navigationWidget, &NavigationWidget::viewChanged,
            this, &NavigationManager::onViewChanged);

    connect(navigationWidget,
            &NavigationWidget::searchFilterChanged,
            this,
            [this](int tabIndex, const QString& filter) {
                setSearchFilter(
                    tabIndex == NavigationWidget::DesignTab
                        ? DesignHierarchyView
                        : FileHierarchyView,
                    filter);
            });
}
