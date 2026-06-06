#include "navigationmanager.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "symbolanalyzer.h"
#include "definitionservice.h"
#include "semanticindex.h"
#include <utility>
#include <QSet>

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent)
{
    // 预分配缓存空间以提高性能
    cachedFileList.reserve(100);
    moduleHierarchyCache.reserve(50);
    symbolsByTypeCache.reserve(10);
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

    // 断开旧连接
    if (connectedTabManager) {
        disconnect(connectedTabManager, nullptr, this, nullptr);
    }

    connectedTabManager = tabManager;

    if (connectedTabManager) {
        // 连接TabManager信号
        connect(connectedTabManager, &TabManager::activeTabChanged,
                this, [this](MyCodeEditor* editor) {
                    if (editor) {
                        currentFileName = editor->getFileName();
                        onTabChanged(currentFileName);
                    }
                });

        connect(connectedTabManager, &TabManager::tabCreated,
                this, [this](MyCodeEditor*) {
                    // 当创建新标签页时，可能需要刷新文件层次结构
                    if (currentView == FileHierarchyView) {
                        refreshFileHierarchy();
                    }
                });

        connect(connectedTabManager, &TabManager::tabClosed,
                this, [this](const QString&) {
                    // 标签页关闭时刷新视图
                    refreshCurrentView();
                });
    }
}

void NavigationManager::connectToWorkspaceManager(WorkspaceManager* workspaceManager)
{
    if (connectedWorkspaceManager == workspaceManager) return;

    // 断开旧连接
    if (connectedWorkspaceManager) {
        disconnect(connectedWorkspaceManager, nullptr, this, nullptr);
    }

    connectedWorkspaceManager = workspaceManager;

    if (connectedWorkspaceManager) {
        // 连接WorkspaceManager信号
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
                    // 文件扫描完成后刷新所有视图
                    cachedFileList.clear();
                    moduleHierarchyCache.clear();
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::fileChanged,
                this, [this](const QString& filePath) {
                    // 文件变化时仅刷新该文件对应的子树（局部更新）
                    if (currentView == ModuleHierarchyView) {
                        updateModuleHierarchyDataForFile(filePath);
                        if (navigationWidget && moduleHierarchyCache.contains(filePath)) {
                            navigationWidget->updateModuleHierarchyForFile(filePath, moduleHierarchyCache[filePath]);
                        }
                        emit dataRefreshed(ModuleHierarchyView);
                    } else if (currentView == SymbolHierarchyView && currentFileName == filePath) {
                        symbolsByTypeCache.clear();
                        refreshSymbolHierarchy();
                    }
                });
    }
}

void NavigationManager::connectToSymbolAnalyzer(SymbolAnalyzer* symbolAnalyzer)
{
    if (connectedSymbolAnalyzer == symbolAnalyzer) return;

    // 断开旧连接
    if (connectedSymbolAnalyzer) {
        disconnect(connectedSymbolAnalyzer, nullptr, this, nullptr);
    }

    connectedSymbolAnalyzer = symbolAnalyzer;

    if (connectedSymbolAnalyzer) {
        // 连接SymbolAnalyzer信号
        connect(connectedSymbolAnalyzer, &SymbolAnalyzer::analysisCompleted,
                this, &NavigationManager::onSymbolAnalysisCompleted);

        connect(connectedSymbolAnalyzer, &SymbolAnalyzer::batchAnalysisCompleted,
                this, [this](int filesAnalyzed, int totalSymbols) {
                    Q_UNUSED(filesAnalyzed)
                    Q_UNUSED(totalSymbols)
                    // 批量分析完成后刷新模块和符号视图
                    if (currentView == ModuleHierarchyView || currentView == SymbolHierarchyView) {
                        symbolsByTypeCache.clear();
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
        navigationWidget->updateSymbolHierarchy(symbolsByTypeCache);
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

    // 重新应用过滤器
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

    // 如果当前是符号视图，突出显示当前文件的符号
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

    // 清除缓存并刷新所有视图
    cachedFileList.clear();
    moduleHierarchyCache.clear();
    symbolsByTypeCache.clear();

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

    // 局部更新：仅刷新受影响的文件对应子树，避免全量重绘
    switch (currentView) {
    case FileHierarchyView:
        // 文件列表未变，不刷新
        break;
    case ModuleHierarchyView: {
        updateModuleHierarchyDataForFile(fileName);
        if (navigationWidget && moduleHierarchyCache.contains(fileName)) {
            navigationWidget->updateModuleHierarchyForFile(fileName, moduleHierarchyCache[fileName]);
        }
        emit dataRefreshed(ModuleHierarchyView);
        break;
    }
    case SymbolHierarchyView:
        // 符号视图按当前文件展示，仅当分析的是当前文件时刷新
        if (currentFileName == fileName) {
            symbolsByTypeCache.clear();
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

    // 刷新当前视图
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

    // 连接NavigationWidget的信号
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
            this, &NavigationManager::setSearchFilter);}

void NavigationManager::updateFileHierarchyData()
{
    if (!cachedFileList.isEmpty() && !shouldRefreshCache()) {
        return; // 使用缓存的数据
    }

    cachedFileList = getSystemVerilogFiles();

    // 应用搜索过滤器
    if (!searchFilter.isEmpty()) {
        cachedFileList = filterFiles(cachedFileList, searchFilter);
    }
}

void NavigationManager::updateModuleHierarchyData()
{
    moduleHierarchyCache.clear();

    QList<sym_list::SymbolInfo> modules =
        SemanticIndex::getInstance()->getSymbolsByType(sym_list::sym_module);

    // 构建模块层次结构
    // TODO: 实现模块实例化关系解析
    // 现在简单按文件分组
    for (const sym_list::SymbolInfo& module : std::as_const(modules)) {
        QString fileName = module.fileName;
        if (!fileName.isEmpty()) {
            moduleHierarchyCache[fileName].append(module.symbolName);
        }
    }

    // 应用搜索过滤器
    if (!searchFilter.isEmpty()) {
        QHash<QString, QStringList> filteredCache;
        for (auto it = moduleHierarchyCache.begin(); it != moduleHierarchyCache.end(); ++it) {
            QStringList filteredModules;
            for (const QString& moduleName : std::as_const(it.value())) {
                if (moduleName.contains(searchFilter, Qt::CaseInsensitive)) {
                    filteredModules.append(moduleName);
                }
            }
            if (!filteredModules.isEmpty()) {
                filteredCache[it.key()] = filteredModules;
            }
        }
        moduleHierarchyCache = filteredCache;
    }
}

void NavigationManager::updateModuleHierarchyDataForFile(const QString& fileName)
{
    if (fileName.isEmpty()) return;

    QList<sym_list::SymbolInfo> modules =
        SemanticIndex::getInstance()->getSymbolsByType(sym_list::sym_module);

    QStringList modulesInFile;
    for (const sym_list::SymbolInfo& module : std::as_const(modules)) {
        if (module.fileName == fileName) {
            modulesInFile.append(module.symbolName);
        }
    }

    if (!searchFilter.isEmpty()) {
        QStringList filtered;
        for (const QString& moduleName : std::as_const(modulesInFile)) {
            if (moduleName.contains(searchFilter, Qt::CaseInsensitive)) {
                filtered.append(moduleName);
            }
        }
        modulesInFile = filtered;
    }

    moduleHierarchyCache[fileName] = modulesInFile;
}

void NavigationManager::updateSymbolHierarchyData()
{
    symbolsByTypeCache.clear();

    // 大纲展示的符号类型（顺序由 NavigationWidget 的 orderedTypes 决定）
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

    // 有当前文件则只取该文件符号，否则取全部
    QList<sym_list::SymbolInfo> symbols = currentFileName.isEmpty()
        ? SemanticIndex::getInstance()->getSymbols()
        : SemanticIndex::getInstance()->getSymbols(currentFileName);

    // task/function 名构成「子程序作用域」：其内部符号（形参 / 返回值 / 局部变量）的 moduleScope
    // 等于子程序名，应从大纲排除，避免函数内部变量混入模块级 逻辑/寄存器 等分组。
    QSet<QString> subroutineScopes;
    for (const sym_list::SymbolInfo& s : std::as_const(symbols)) {
        if (s.symbolType == sym_list::sym_task || s.symbolType == sym_list::sym_function)
            subroutineScopes.insert(s.symbolName);
    }

    QHash<sym_list::sym_type_e, QStringList> byType;
    for (const sym_list::SymbolInfo& s : std::as_const(symbols)) {
        const bool isSubroutine = (s.symbolType == sym_list::sym_task
                                   || s.symbolType == sym_list::sym_function);
        if (!isSubroutine && subroutineScopes.contains(s.moduleScope))
            continue;  // 子程序内部符号，不进大纲
        byType[s.symbolType].append(s.symbolName);
    }

    for (sym_list::sym_type_e symbolType : symbolTypes) {
        QStringList symbolNames = byType.value(symbolType);
        if (symbolNames.isEmpty()) continue;

        if (!searchFilter.isEmpty())
            symbolNames = filterFiles(symbolNames, searchFilter);

        if (!symbolNames.isEmpty())
            symbolsByTypeCache[symbolType] = symbolNames;
    }
}

bool NavigationManager::shouldRefreshCache() const
{
    // 如果有工作空间，检查是否需要刷新
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return cachedFileList.isEmpty();
    }

    // 如果没有工作空间，检查打开的标签页
    if (connectedTabManager) {
        QStringList openFiles = connectedTabManager->getOpenSystemVerilogFiles();
        return cachedFileList != openFiles;
    }

    return true;
}

QStringList NavigationManager::getSystemVerilogFiles() const
{
    // 优先使用工作空间文件
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return connectedWorkspaceManager->getSystemVerilogFiles();
    }

    // 否则使用打开的标签页文件
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
