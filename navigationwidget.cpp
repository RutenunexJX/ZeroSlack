#include "navigationwidget.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>

NavigationWidget::NavigationWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

NavigationWidget::~NavigationWidget()
{
}

void NavigationWidget::setActiveTab(NavigationTab tab)
{
    tabWidget->setCurrentIndex(static_cast<int>(tab));
}

NavigationWidget::NavigationTab NavigationWidget::getActiveTab() const
{
    return static_cast<NavigationTab>(tabWidget->currentIndex());
}

void NavigationWidget::updateFileHierarchy(const QStringList& files)
{
    currentFileList = files;
    populateFileTree();
}

void NavigationWidget::updateModuleHierarchy(const QHash<QString, QStringList>& modulesByFile)
{
    currentModuleHierarchy = modulesByFile;
    populateModuleTree();
}

void NavigationWidget::updateSymbolHierarchy(const QHash<sym_list::sym_type_e, QStringList>& symbolsByType)
{
    currentSymbolHierarchy = symbolsByType;
    populateSymbolTree();
}

void NavigationWidget::highlightFile(const QString& filePath)
{
    currentHighlightedFile = filePath;

    QTreeWidgetItem* item = findItemByText(fileTreeWidget, QFileInfo(filePath).fileName());
    if (item) {
        fileTreeWidget->setCurrentItem(item);
        fileTreeWidget->scrollToItem(item);
    }
}

void NavigationWidget::highlightSymbol(const QString& symbolName)
{
    QTreeWidgetItem* item = findItemByText(symbolTreeWidget, symbolName);
    if (item) {
        symbolTreeWidget->setCurrentItem(item);
        symbolTreeWidget->scrollToItem(item);

        // 展开父节点
        if (item->parent()) {
            item->parent()->setExpanded(true);
        }
    }
}

void NavigationWidget::highlightModule(const QString& moduleName)
{
    QTreeWidgetItem* item = findItemByText(moduleTreeWidget, moduleName);
    if (item) {
        moduleTreeWidget->setCurrentItem(item);
        moduleTreeWidget->scrollToItem(item);

        // 展开父节点
        if (item->parent()) {
            item->parent()->setExpanded(true);
        }
    }
}

void NavigationWidget::setSearchText(const QString& text)
{
    searchLineEdit->setText(text);
}

QString NavigationWidget::getSearchText() const
{
    return searchLineEdit->text();
}

void NavigationWidget::onTabChanged(int index)
{
    emit viewChanged(index);
}

void NavigationWidget::onSearchTextChanged(const QString& text)
{
    currentSearchFilter = text.trimmed();
    emit searchFilterChanged(currentSearchFilter);

    // 立即应用过滤器
    applySearchFilter();
}

void NavigationWidget::onFileTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item) return;

    // 获取完整文件路径
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit fileDoubleClicked(filePath);
    }
}

void NavigationWidget::onModuleTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item) return;

    QString moduleName = item->text(0);
    if (!moduleName.isEmpty() && !item->data(0, Qt::UserRole + 1).toBool()) {
        // 确保不是文件节点（文件节点在UserRole+1中存储true）
        emit moduleDoubleClicked(moduleName);
    }
}

void NavigationWidget::onSymbolTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item || item->childCount() > 0) return; // 忽略父节点

    QString symbolName = item->text(0);
    sym_list::sym_type_e symbolType = static_cast<sym_list::sym_type_e>(
        item->data(0, Qt::UserRole).toInt());

    // 创建SymbolInfo对象用于导航
    sym_list::SymbolInfo symbol;
    symbol.symbolName = symbolName;
    symbol.symbolType = symbolType;

    // 尝试从symbol list中获取完整信息
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(symbolName);

    for (const sym_list::SymbolInfo& foundSymbol : qAsConst(symbols)) {
        if (foundSymbol.symbolType == symbolType) {
            symbol = foundSymbol;
            break;
        }
    }

    emit symbolDoubleClicked(symbol);
}

void NavigationWidget::setupUI()
{
    // 主布局
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 搜索框
    searchLineEdit = new QLineEdit(this);
    searchLineEdit->setPlaceholderText("搜索文件、模块或符号...");
    searchLineEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(searchLineEdit);

    // 标签页容器
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

    // 设置标签页
    setupFileTab();
    setupModuleTab();
    setupSymbolTab();

    setLayout(mainLayout);
}

void NavigationWidget::setupFileTab()
{
    fileTab = new QWidget();
    fileTabLayout = new QVBoxLayout(fileTab);
    fileTabLayout->setContentsMargins(2, 2, 2, 2);
    fileTabLayout->setSpacing(2);

    // 文件树控件
    fileTreeWidget = new QTreeWidget(fileTab);
    fileTreeWidget->setHeaderLabel("SystemVerilog 文件");
    fileTreeWidget->setAlternatingRowColors(true);
    fileTreeWidget->setRootIsDecorated(true);
    fileTreeWidget->setSortingEnabled(true);
    fileTreeWidget->header()->hide();

    fileTabLayout->addWidget(fileTreeWidget);
    fileTab->setLayout(fileTabLayout);

    tabWidget->addTab(fileTab, "文件");
}

void NavigationWidget::setupModuleTab()
{
    moduleTab = new QWidget();
    moduleTabLayout = new QVBoxLayout(moduleTab);
    moduleTabLayout->setContentsMargins(2, 2, 2, 2);
    moduleTabLayout->setSpacing(2);

    // 模块树控件
    moduleTreeWidget = new QTreeWidget(moduleTab);
    moduleTreeWidget->setHeaderLabel("模块层次结构");
    moduleTreeWidget->setAlternatingRowColors(true);
    moduleTreeWidget->setRootIsDecorated(true);
    moduleTreeWidget->setSortingEnabled(true);
    moduleTreeWidget->header()->hide();

    moduleTabLayout->addWidget(moduleTreeWidget);
    moduleTab->setLayout(moduleTabLayout);

    tabWidget->addTab(moduleTab, "模块");
}

void NavigationWidget::setupSymbolTab()
{
    symbolTab = new QWidget();
    symbolTabLayout = new QVBoxLayout(symbolTab);
    symbolTabLayout->setContentsMargins(2, 2, 2, 2);
    symbolTabLayout->setSpacing(2);

    // 符号树控件
    symbolTreeWidget = new QTreeWidget(symbolTab);
    symbolTreeWidget->setHeaderLabel("符号");
    symbolTreeWidget->setAlternatingRowColors(true);
    symbolTreeWidget->setRootIsDecorated(true);
    symbolTreeWidget->setSortingEnabled(true);
    symbolTreeWidget->header()->hide();

    symbolTabLayout->addWidget(symbolTreeWidget);
    symbolTab->setLayout(symbolTabLayout);

    tabWidget->addTab(symbolTab, "符号");
}

void NavigationWidget::setupConnections()
{
    // 标签页切换
    connect(tabWidget, &QTabWidget::currentChanged,
            this, &NavigationWidget::onTabChanged);

    // 搜索框
    connect(searchLineEdit, &QLineEdit::textChanged,
            this, &NavigationWidget::onSearchTextChanged);

    // 树控件双击事件
    connect(fileTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onFileTreeDoubleClicked);

    connect(moduleTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onModuleTreeDoubleClicked);

    connect(symbolTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onSymbolTreeDoubleClicked);
}

void NavigationWidget::populateFileTree()
{
    fileTreeWidget->clear();

    if (currentFileList.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(fileTreeWidget);
        emptyItem->setText(0, "没有找到 SystemVerilog 文件");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    // 按目录结构组织文件
    QHash<QString, QTreeWidgetItem*> dirItems;

    for (const QString& filePath : qAsConst(currentFileList)) {
        QFileInfo fileInfo(filePath);
        QString dirPath = fileInfo.absolutePath();
        QString fileName = fileInfo.fileName();

        // 应用搜索过滤器
        if (!currentSearchFilter.isEmpty() &&
            !fileName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
            continue;
        }

        // 获取或创建目录节点
        QTreeWidgetItem* dirItem = nullptr;
        if (dirItems.contains(dirPath)) {
            dirItem = dirItems[dirPath];
        } else {
            dirItem = new QTreeWidgetItem(fileTreeWidget);
            dirItem->setText(0, QDir(dirPath).dirName());
            dirItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            dirItem->setExpanded(true);
            dirItems[dirPath] = dirItem;
        }

        // 创建文件节点
        QTreeWidgetItem* fileItem = createFileItem(filePath);
        dirItem->addChild(fileItem);
    }

    // 如果只有一个目录，自动展开
    if (fileTreeWidget->topLevelItemCount() == 1) {
        fileTreeWidget->topLevelItem(0)->setExpanded(true);
    }

    expandCurrentFileNodes();
}

void NavigationWidget::populateModuleTree()
{
    moduleTreeWidget->clear();

    if (currentModuleHierarchy.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(moduleTreeWidget);
        emptyItem->setText(0, "没有找到模块");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    // 按文件组织模块
    for (auto it = currentModuleHierarchy.begin(); it != currentModuleHierarchy.end(); ++it) {
        const QString& fileName = it.key();
        const QStringList& modules = it.value();

        if (modules.isEmpty()) continue;

        // 创建文件节点
        QTreeWidgetItem* fileItem = new QTreeWidgetItem(moduleTreeWidget);
        fileItem->setText(0, QFileInfo(fileName).fileName());
        fileItem->setIcon(0, getFileIcon(fileName));
        fileItem->setData(0, Qt::UserRole + 1, true); // 标记为文件节点
        fileItem->setExpanded(true);

        // 添加模块子节点
        for (const QString& moduleName : modules) {
            // 应用搜索过滤器
            if (!currentSearchFilter.isEmpty() &&
                !moduleName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* moduleItem = createModuleItem(moduleName, fileName);
            fileItem->addChild(moduleItem);
        }

        // 如果文件节点没有子节点（被过滤掉了），则删除文件节点
        if (fileItem->childCount() == 0) {
            delete fileItem;
        }
    }
}

void NavigationWidget::populateSymbolTree()
{
    symbolTreeWidget->clear();

    if (currentSymbolHierarchy.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(symbolTreeWidget);
        emptyItem->setText(0, "没有找到符号");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    // 按符号类型组织
    static const QList<sym_list::sym_type_e> orderedTypes = {
        sym_list::sym_module,
        sym_list::sym_reg,
        sym_list::sym_wire,
        sym_list::sym_logic,
        sym_list::sym_task,
        sym_list::sym_function
    };

    for (sym_list::sym_type_e symbolType : orderedTypes) {
        if (!currentSymbolHierarchy.contains(symbolType)) continue;

        const QStringList& symbols = currentSymbolHierarchy[symbolType];
        if (symbols.isEmpty()) continue;

        // 创建类型节点
        QTreeWidgetItem* typeItem = new QTreeWidgetItem(symbolTreeWidget);
        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(symbolType)).arg(symbols.size()));
        typeItem->setIcon(0, getSymbolIcon(symbolType));
        typeItem->setExpanded(true);
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        // 添加符号子节点
        int addedCount = 0;
        for (const QString& symbolName : symbols) {
            // 应用搜索过滤器
            if (!currentSearchFilter.isEmpty() &&
                !symbolName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* symbolItem = createSymbolItem(symbolName, symbolType);
            typeItem->addChild(symbolItem);
            addedCount++;
        }

        // 更新类型节点的计数
        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(symbolType)).arg(addedCount));

        // 如果类型节点没有子节点（被过滤掉了），则删除类型节点
        if (typeItem->childCount() == 0) {
            delete typeItem;
        }
    }
}

void NavigationWidget::applySearchFilter()
{
    // 重新填充当前活动的树
    switch (getActiveTab()) {
    case FileTab:
        populateFileTree();
        break;
    case ModuleTab:
        populateModuleTree();
        break;
    case SymbolTab:
        populateSymbolTree();
        break;
    }
}

QTreeWidgetItem* NavigationWidget::createFileItem(const QString& filePath)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    QFileInfo fileInfo(filePath);

    item->setText(0, fileInfo.fileName());
    item->setIcon(0, getFileIcon(filePath));
    item->setData(0, Qt::UserRole, filePath); // 存储完整路径
    item->setToolTip(0, filePath);

    return item;
}

QTreeWidgetItem* NavigationWidget::createModuleItem(const QString& moduleName, const QString& fileName)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, moduleName);
    item->setIcon(0, getSymbolIcon(sym_list::sym_module));
    item->setData(0, Qt::UserRole, fileName); // 存储文件路径
    item->setData(0, Qt::UserRole + 1, false); // 标记为模块节点
    item->setToolTip(0, QString("模块: %1\n文件: %2").arg(moduleName, QFileInfo(fileName).fileName()));

    return item;
}

QTreeWidgetItem* NavigationWidget::createSymbolItem(const QString& symbolName, sym_list::sym_type_e symbolType)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, symbolName);
    item->setIcon(0, getSymbolIcon(symbolType));
    item->setData(0, Qt::UserRole, static_cast<int>(symbolType));
    item->setToolTip(0, QString("%1: %2").arg(getSymbolTypeDisplayName(symbolType), symbolName));

    return item;
}

QString NavigationWidget::getSymbolTypeDisplayName(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_module: return "模块";
    case sym_list::sym_reg: return "寄存器";
    case sym_list::sym_wire: return "线网";
    case sym_list::sym_logic: return "逻辑";
    case sym_list::sym_task: return "任务";
    case sym_list::sym_function: return "函数";
    default: return "符号";
    }
}

QIcon NavigationWidget::getFileIcon(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == "sv" || suffix == "v") {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (suffix == "vh" || suffix == "svh") {
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    }

    return style()->standardIcon(QStyle::SP_FileIcon);
}

QIcon NavigationWidget::getSymbolIcon(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_module:
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
        return style()->standardIcon(QStyle::SP_DialogApplyButton);
    case sym_list::sym_task:
    case sym_list::sym_function:
        return style()->standardIcon(QStyle::SP_MediaPlay);
    default:
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}

void NavigationWidget::expandCurrentFileNodes()
{
    if (currentHighlightedFile.isEmpty()) return;

    // 在文件树中找到当前文件并展开其父节点
    for (int i = 0; i < fileTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* dirItem = fileTreeWidget->topLevelItem(i);
        for (int j = 0; j < dirItem->childCount(); ++j) {
            QTreeWidgetItem* fileItem = dirItem->child(j);
            QString filePath = fileItem->data(0, Qt::UserRole).toString();
            if (filePath == currentHighlightedFile) {
                dirItem->setExpanded(true);
                fileTreeWidget->setCurrentItem(fileItem);
                return;
            }
        }
    }
}

QTreeWidgetItem* NavigationWidget::findItemByText(QTreeWidget* tree, const QString& text, int column)
{
    QList<QTreeWidgetItem*> items = tree->findItems(text, Qt::MatchRecursive | Qt::MatchExactly, column);
    return items.isEmpty() ? nullptr : items.first();
}
