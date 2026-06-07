#include "navigationmanager.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "symbolanalyzer.h"
#include "definitionservice.h"
#include "hierarchyservice.h"
#include "searchservice.h"
#include "semanticindex.h"
#include <algorithm>
#include <utility>
#include <QFileInfo>
#include <QSet>

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent)
{
    // Reserve common cache sizes for the navigation views.
    cachedFileList.reserve(100);
    moduleHierarchyCache.reserve(50);
    symbolOutlineCache.reserve(10);
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
        connect(connectedTabManager, &TabManager::activeTabChanged,
                this, [this](MyCodeEditor* editor) {
                    if (editor) {
                        currentFileName = editor->getFileName();
                        onTabChanged(currentFileName);
                    }
                });

        connect(connectedTabManager, &TabManager::tabCreated,
                this, [this](MyCodeEditor*) {
                    // New tabs can change the file tree.
                    if (currentView == FileHierarchyView) {
                        refreshFileHierarchy();
                    }
                });

        connect(connectedTabManager, &TabManager::tabClosed,
                this, [this](const QString&) {
                    // Closed tabs can remove entries from the current view.
                    refreshCurrentView();
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
        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceOpened,
                this, &NavigationManager::onWorkspaceChanged);

        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceClosed,
                this, [this]() {
                    currentWorkspacePath.clear();
                    cachedFileList.clear();
                    moduleHierarchyCache.clear();
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::filesScanned,
                this, [this](const QStringList&) {
                    // A rescan can affect every navigation view.
                    cachedFileList.clear();
                    moduleHierarchyCache.clear();
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::fileChanged,
                this, [this](const QString& filePath) {
                    // Module hierarchy depends on global relationship edges, so refresh it as a whole.
                    if (currentView == ModuleHierarchyView) {
                        moduleHierarchyCache.clear();
                        refreshModuleHierarchy();
                    } else if (currentView == SymbolHierarchyView && currentFileName == filePath) {
                        symbolOutlineCache.clear();
                        refreshSymbolHierarchy();
                    }
                });
    }
}

void NavigationManager::connectToSymbolAnalyzer(SymbolAnalyzer* symbolAnalyzer)
{
    if (connectedSymbolAnalyzer == symbolAnalyzer) return;

    // Drop connections from the previous analyzer.
    if (connectedSymbolAnalyzer) {
        disconnect(connectedSymbolAnalyzer, nullptr, this, nullptr);
    }

    connectedSymbolAnalyzer = symbolAnalyzer;

    if (connectedSymbolAnalyzer) {
        // Wire symbol analysis results into navigation refreshes.
        connect(connectedSymbolAnalyzer, &SymbolAnalyzer::analysisCompleted,
                this, &NavigationManager::onSymbolAnalysisCompleted);

        connect(connectedSymbolAnalyzer, &SymbolAnalyzer::batchAnalysisCompleted,
                this, [this](int filesAnalyzed, int totalSymbols) {
                    Q_UNUSED(filesAnalyzed)
                    Q_UNUSED(totalSymbols)
                    // Batch analysis can change module hierarchy and symbol outline data.
                    if (currentView == ModuleHierarchyView || currentView == SymbolHierarchyView) {
                        symbolOutlineCache.clear();
                        moduleHierarchyCache.clear();
                        refreshCurrentView();
                    }
                });
    }
}

void NavigationManager::refreshFileHierarchy()
{
    if (!shouldRefreshCache()) {
        cachedFileList.clear();
    }

    updateFileHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateFileHierarchy(cachedFileList);
    }

    emit dataRefreshed(FileHierarchyView);
}

void NavigationManager::refreshModuleHierarchy()
{
    updateModuleHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateModuleHierarchy(moduleHierarchyCache);
    }

    emit dataRefreshed(ModuleHierarchyView);
}

void NavigationManager::refreshSymbolHierarchy()
{
    updateSymbolHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateSymbolHierarchy(symbolOutlineCache);
    }

    emit dataRefreshed(SymbolHierarchyView);
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
    }
}

void NavigationManager::navigateToFile(const QString& filePath, int lineNumber)
{
    if (filePath.isEmpty()) return;

    emit navigationRequested(filePath, lineNumber);
}

void NavigationManager::navigateToSymbol(const sym_list::SymbolInfo& symbol)
{
    emit symbolNavigationRequested(symbol);
}

void NavigationManager::navigateToModule(const QString& moduleName)
{
    if (moduleName.isEmpty()) return;

    DefinitionQuery query;
    query.symbolName = moduleName;
    const DefinitionResult result = DefinitionService::getInstance()->resolveDefinition(query);
    if (result.found && result.symbol.symbolType == sym_list::sym_module)
        navigateToSymbol(result.symbol);
}

void NavigationManager::setSearchFilter(const QString& filter)
{
    searchFilter = filter.trimmed();

    // Reapply the current search filter.
    refreshCurrentView();
}

void NavigationManager::clearSearchFilter()
{
    searchFilter.clear();
    refreshCurrentView();
}

void NavigationManager::highlightCurrentFileInTree()
{
    if (!navigationWidget || currentFileName.isEmpty()) return;

    navigationWidget->highlightFile(currentFileName);
}

void NavigationManager::syncWithActiveEditor()
{
    highlightCurrentFileInTree();

    // Keep the symbol view aligned with the active editor.
    if (currentView == SymbolHierarchyView && !currentFileName.isEmpty()) {
        refreshSymbolHierarchy();
    }
}

void NavigationManager::onTabChanged(const QString& fileName)
{
    currentFileName = fileName;
    syncWithActiveEditor();
}

void NavigationManager::onWorkspaceChanged(const QString& workspacePath)
{
    currentWorkspacePath = workspacePath;

    // Clear caches and refresh the active view.
    cachedFileList.clear();
    moduleHierarchyCache.clear();
    symbolOutlineCache.clear();

    refreshCurrentView();
}

void NavigationManager::onViewChanged(int index)
{
    setActiveView(static_cast<NavigationView>(index));
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
        moduleHierarchyCache.clear();
        refreshModuleHierarchy();
        break;
    }
    case SymbolHierarchyView:
        // The symbol view follows the current file.
        if (currentFileName == fileName) {
            symbolOutlineCache.clear();
            refreshSymbolHierarchy();
        }
        break;
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

void NavigationManager::onFileTreeDoubleClicked(const QString& filePath)
{
    navigateToFile(filePath);
}

void NavigationManager::onSymbolTreeDoubleClicked(const sym_list::SymbolInfo& symbol)
{
    navigateToSymbol(symbol);
}

void NavigationManager::onModuleTreeDoubleClicked(const QString& moduleName)
{
    navigateToModule(moduleName);
}

void NavigationManager::setupConnections()
{
    if (!navigationWidget) return;

    // Wire NavigationWidget signals.
    connect(navigationWidget, SIGNAL(fileDoubleClicked(QString)),
            this, SLOT(onFileTreeDoubleClicked(QString)));

    connect(navigationWidget, SIGNAL(symbolDoubleClicked(sym_list::SymbolInfo)),
            this, SLOT(onSymbolTreeDoubleClicked(sym_list::SymbolInfo)));

    connect(navigationWidget, SIGNAL(moduleDoubleClicked(QString)),
            this, SLOT(onModuleTreeDoubleClicked(QString)));

    connect(navigationWidget, SIGNAL(viewChanged(int)),
            this, SLOT(onViewChanged(int)));

    connect(navigationWidget, &NavigationWidget::viewChanged,
            this, [this](int index) {
                setActiveView(static_cast<NavigationView>(index));
            });

    connect(navigationWidget, &NavigationWidget::searchFilterChanged,
            this, &NavigationManager::setSearchFilter);
}

void NavigationManager::updateFileHierarchyData()
{
    if (!cachedFileList.isEmpty() && !shouldRefreshCache()) {
        return; // Use cached data.
    }

    cachedFileList = getSystemVerilogFiles();

    // Apply the search filter.
    if (!searchFilter.isEmpty()) {
        cachedFileList = filterFiles(cachedFileList, searchFilter);
    }
}

void NavigationManager::updateModuleHierarchyData()
{
    moduleHierarchyCache.clear();

    SearchQuery moduleQuery;
    moduleQuery.types = {sym_list::sym_module};

    QList<sym_list::SymbolInfo> modules;
    const QList<SearchResult> moduleResults =
        SearchService::getInstance()->findSymbols(moduleQuery);
    modules.reserve(moduleResults.size());
    for (const SearchResult& result : moduleResults)
        modules.append(result.symbol);

    moduleHierarchyCache = buildModuleInstantiationHierarchy(modules);
    if (moduleHierarchyCache.isEmpty())
        moduleHierarchyCache = buildModuleFileGroups(modules);
    moduleHierarchyCache = filterModuleHierarchy(moduleHierarchyCache);
}

QList<ModuleHierarchyGroup> NavigationManager::buildModuleFileGroups(
    const QList<sym_list::SymbolInfo>& modules) const
{
    QHash<QString, QStringList> groups;
    for (const sym_list::SymbolInfo& module : modules) {
        if (!module.fileName.isEmpty() && !module.symbolName.isEmpty())
            groups[module.fileName].append(module.symbolName);
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        it.value().removeDuplicates();
        it.value().sort(Qt::CaseInsensitive);
    }

    QList<ModuleHierarchyGroup> result;
    result.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        ModuleHierarchyGroup group;
        group.rootKind = ModuleHierarchyRootKind::FileGroup;
        group.rootName = it.key();
        group.rootDisplayName = QFileInfo(it.key()).fileName();
        group.rootToolTip = it.key();
        group.childModules = it.value();
        result.append(group);
    }

    std::sort(result.begin(), result.end(), [](const ModuleHierarchyGroup& a,
                                               const ModuleHierarchyGroup& b) {
        return QString::compare(a.rootDisplayName, b.rootDisplayName, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<ModuleHierarchyGroup> NavigationManager::buildModuleInstantiationHierarchy(
    const QList<sym_list::SymbolInfo>& modules) const
{
    QList<ModuleHierarchyGroup> hierarchy;
    QSet<int> childModuleIds;
    QHash<int, QStringList> childrenByParentId;

    for (const sym_list::SymbolInfo& module : modules) {
        if (module.symbolId < 0 || module.symbolName.isEmpty())
            continue;

        HierarchyQuery query;
        query.symbolId = module.symbolId;
        query.maxDepth = 1;
        query.types = {SymbolRelationshipEngine::INSTANTIATES};

        QStringList children;
        const QList<HierarchyNode> childNodes = HierarchyService::getInstance()->getChildren(query);
        for (const HierarchyNode& node : childNodes) {
            if (node.symbol.symbolType != sym_list::sym_module || node.symbol.symbolName.isEmpty())
                continue;
            children.append(node.symbol.symbolName);
            childModuleIds.insert(node.symbol.symbolId);
        }

        if (!children.isEmpty()) {
            children.removeDuplicates();
            children.sort(Qt::CaseInsensitive);
            childrenByParentId.insert(module.symbolId, children);
        }
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (!childrenByParentId.contains(module.symbolId))
            continue;
        if (!childModuleIds.contains(module.symbolId)) {
            ModuleHierarchyGroup group;
            group.rootKind = ModuleHierarchyRootKind::ModuleRoot;
            group.rootName = module.symbolName;
            group.rootDisplayName = module.symbolName;
            group.rootToolTip = QString("Module: %1").arg(module.symbolName);
            group.childModules = childrenByParentId.value(module.symbolId);
            hierarchy.append(group);
        }
    }

    if (!hierarchy.isEmpty()) {
        std::sort(hierarchy.begin(), hierarchy.end(), [](const ModuleHierarchyGroup& a,
                                                         const ModuleHierarchyGroup& b) {
            return QString::compare(a.rootDisplayName, b.rootDisplayName, Qt::CaseInsensitive) < 0;
        });
        return hierarchy;
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (childrenByParentId.contains(module.symbolId)) {
            ModuleHierarchyGroup group;
            group.rootKind = ModuleHierarchyRootKind::ModuleRoot;
            group.rootName = module.symbolName;
            group.rootDisplayName = module.symbolName;
            group.rootToolTip = QString("Module: %1").arg(module.symbolName);
            group.childModules = childrenByParentId.value(module.symbolId);
            hierarchy.append(group);
        }
    }
    std::sort(hierarchy.begin(), hierarchy.end(), [](const ModuleHierarchyGroup& a,
                                                     const ModuleHierarchyGroup& b) {
        return QString::compare(a.rootDisplayName, b.rootDisplayName, Qt::CaseInsensitive) < 0;
    });
    return hierarchy;
}

QList<ModuleHierarchyGroup> NavigationManager::filterModuleHierarchy(
    const QList<ModuleHierarchyGroup>& hierarchy) const
{
    if (searchFilter.isEmpty())
        return hierarchy;

    QList<ModuleHierarchyGroup> filtered;
    for (const ModuleHierarchyGroup& group : hierarchy) {
        const bool rootMatches = group.rootDisplayName.contains(searchFilter, Qt::CaseInsensitive)
            || group.rootName.contains(searchFilter, Qt::CaseInsensitive);
        QStringList children;
        for (const QString& moduleName : group.childModules) {
            if (rootMatches || moduleName.contains(searchFilter, Qt::CaseInsensitive))
                children.append(moduleName);
        }

        if (rootMatches || !children.isEmpty()) {
            ModuleHierarchyGroup filteredGroup = group;
            filteredGroup.childModules = children;
            filtered.append(filteredGroup);
        }
    }
    return filtered;
}

void NavigationManager::updateSymbolHierarchyData()
{
    symbolOutlineCache.clear();

    // Outline symbol types. Display order is carried by SymbolOutlineGroup order.
    static const QList<sym_list::sym_type_e> symbolTypes = {
        sym_list::sym_module,
        sym_list::sym_parameter,
        sym_list::sym_localparam,
        sym_list::sym_port_input,
        sym_list::sym_port_output,
        sym_list::sym_port_inout,
        sym_list::sym_port_ref,
        sym_list::sym_reg,
        sym_list::sym_wire,
        sym_list::sym_logic,
        sym_list::sym_typedef,
        sym_list::sym_enum,
        sym_list::sym_enum_var,
        sym_list::sym_enum_value,
        sym_list::sym_packed_struct,
        sym_list::sym_unpacked_struct,
        sym_list::sym_packed_struct_var,
        sym_list::sym_unpacked_struct_var,
        sym_list::sym_struct_member,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_inst
    };

    SearchQuery outlineQuery;
    outlineQuery.fileName = currentFileName;
    QList<sym_list::SymbolInfo> symbols;
    const QList<SearchResult> searchResults =
        SearchService::getInstance()->findSymbols(outlineQuery);
    symbols.reserve(searchResults.size());
    for (const SearchResult& result : searchResults)
        symbols.append(result.symbol);

    // Exclude symbols whose moduleScope is a task/function name so local subroutine
    // symbols do not leak into module-level outline groups.
    QSet<QString> subroutineScopes;
    for (const sym_list::SymbolInfo& s : std::as_const(symbols)) {
        if (s.symbolType == sym_list::sym_task || s.symbolType == sym_list::sym_function)
            subroutineScopes.insert(s.symbolName);
    }

    QHash<sym_list::sym_type_e, QList<sym_list::SymbolInfo>> byType;
    for (const sym_list::SymbolInfo& s : std::as_const(symbols)) {
        const bool isSubroutine = (s.symbolType == sym_list::sym_task
                                   || s.symbolType == sym_list::sym_function);
        if (!isSubroutine && subroutineScopes.contains(s.moduleScope))
            continue;  // Subroutine-local symbol; skip the outline.
        byType[s.symbolType].append(s);
    }

    for (sym_list::sym_type_e symbolType : symbolTypes) {
        QList<sym_list::SymbolInfo> outlineSymbols = byType.value(symbolType);
        if (outlineSymbols.isEmpty()) continue;

        if (!searchFilter.isEmpty()) {
            QList<sym_list::SymbolInfo> filteredSymbols;
            filteredSymbols.reserve(outlineSymbols.size());
            for (const sym_list::SymbolInfo& symbol : std::as_const(outlineSymbols)) {
                if (symbol.symbolName.contains(searchFilter, Qt::CaseInsensitive))
                    filteredSymbols.append(symbol);
            }
            outlineSymbols = filteredSymbols;
        }

        if (!outlineSymbols.isEmpty()) {
            SymbolOutlineGroup group;
            group.symbolType = symbolType;
            group.symbols = outlineSymbols;
            symbolOutlineCache.append(group);
        }
    }
}

bool NavigationManager::shouldRefreshCache() const
{
    // Workspace mode owns the file list.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return cachedFileList.isEmpty();
    }

    // Without a workspace, derive the file list from open tabs.
    if (connectedTabManager) {
        QStringList openFiles = connectedTabManager->getOpenSystemVerilogFiles();
        return cachedFileList != openFiles;
    }

    return true;
}

QStringList NavigationManager::getSystemVerilogFiles() const
{
    // Prefer workspace files.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return connectedWorkspaceManager->getSystemVerilogFiles();
    }

    // Otherwise use open tab files.
    if (connectedTabManager) {
        return connectedTabManager->getOpenSystemVerilogFiles();
    }

    return QStringList();
}

QStringList NavigationManager::filterFiles(const QStringList& files, const QString& filter) const
{
    if (filter.isEmpty()) return files;

    QStringList filteredFiles;
    filteredFiles.reserve(files.size());

    for (const QString& file : files) {
        if (file.contains(filter, Qt::CaseInsensitive)) {
            filteredFiles.append(file);
        }
    }

    return filteredFiles;
}
