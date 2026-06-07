#include "navigationwidget.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <utility>

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

void NavigationWidget::updateModuleHierarchy(const QList<ModuleHierarchyGroup>& hierarchy)
{
    currentModuleHierarchy = hierarchy;
    populateModuleTree();
}

void NavigationWidget::updateSymbolHierarchy(const QList<SymbolOutlineGroup>& symbolGroups)
{
    currentSymbolHierarchy = symbolGroups;
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

    applySearchFilter();
}

void NavigationWidget::onFileTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item) return;

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
        emit moduleDoubleClicked(moduleName);
    }
}

void NavigationWidget::onSymbolTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item || item->childCount() > 0) return;

    const int payloadId = item->data(0, Qt::UserRole + 1).toInt();
    if (symbolItemPayloads.contains(payloadId))
        emit symbolDoubleClicked(symbolItemPayloads.value(payloadId));
}
void NavigationWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    searchLineEdit = new QLineEdit(this);
    searchLineEdit->setPlaceholderText("Search files, modules, or symbols...");
    searchLineEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(searchLineEdit);

    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

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

    fileTreeWidget = new QTreeWidget(fileTab);
    fileTreeWidget->setHeaderLabel("SystemVerilog Files");
    fileTreeWidget->setAlternatingRowColors(true);
    fileTreeWidget->setRootIsDecorated(true);
    fileTreeWidget->setSortingEnabled(true);
    fileTreeWidget->header()->hide();

    fileTabLayout->addWidget(fileTreeWidget);
    fileTab->setLayout(fileTabLayout);

    tabWidget->addTab(fileTab, "Files");
}

void NavigationWidget::setupModuleTab()
{
    moduleTab = new QWidget();
    moduleTabLayout = new QVBoxLayout(moduleTab);
    moduleTabLayout->setContentsMargins(2, 2, 2, 2);
    moduleTabLayout->setSpacing(2);

    moduleTreeWidget = new QTreeWidget(moduleTab);
    moduleTreeWidget->setHeaderLabel("Module Hierarchy");
    moduleTreeWidget->setAlternatingRowColors(true);
    moduleTreeWidget->setRootIsDecorated(true);
    moduleTreeWidget->setSortingEnabled(true);
    moduleTreeWidget->header()->hide();

    moduleTabLayout->addWidget(moduleTreeWidget);
    moduleTab->setLayout(moduleTabLayout);

    tabWidget->addTab(moduleTab, "Module");
}

void NavigationWidget::setupSymbolTab()
{
    symbolTab = new QWidget();
    symbolTabLayout = new QVBoxLayout(symbolTab);
    symbolTabLayout->setContentsMargins(2, 2, 2, 2);
    symbolTabLayout->setSpacing(2);

    symbolTreeWidget = new QTreeWidget(symbolTab);
    symbolTreeWidget->setHeaderLabel("Symbols");
    symbolTreeWidget->setAlternatingRowColors(true);
    symbolTreeWidget->setRootIsDecorated(true);
    symbolTreeWidget->setSortingEnabled(true);
    symbolTreeWidget->header()->hide();

    symbolTabLayout->addWidget(symbolTreeWidget);
    symbolTab->setLayout(symbolTabLayout);

    tabWidget->addTab(symbolTab, "Symbols");
}

void NavigationWidget::setupConnections()
{
    connect(tabWidget, &QTabWidget::currentChanged,
            this, &NavigationWidget::onTabChanged);

    connect(searchLineEdit, &QLineEdit::textChanged,
            this, &NavigationWidget::onSearchTextChanged);

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
        emptyItem->setText(0, "No SystemVerilog files found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    QHash<QString, QTreeWidgetItem*> dirItems;

    for (const QString& filePath : std::as_const(currentFileList)) {
        QFileInfo fileInfo(filePath);
        QString dirPath = fileInfo.absolutePath();
        QString fileName = fileInfo.fileName();

        if (!currentSearchFilter.isEmpty() &&
            !fileName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
            continue;
        }

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

        QTreeWidgetItem* fileItem = createFileItem(filePath);
        dirItem->addChild(fileItem);
    }

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
        emptyItem->setText(0, "No modules found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    for (const ModuleHierarchyGroup& group : std::as_const(currentModuleHierarchy)) {
        if (group.childModules.isEmpty())
            continue;

        QTreeWidgetItem* rootItem = new QTreeWidgetItem(moduleTreeWidget);
        const bool rootIsFile = group.rootKind == ModuleHierarchyRootKind::FileGroup;
        rootItem->setText(0, group.rootDisplayName);
        rootItem->setIcon(0, rootIsFile ? getFileIcon(group.rootName) : getSymbolIcon(sym_list::sym_module));
        rootItem->setData(0, Qt::UserRole, group.rootName);
        rootItem->setData(0, Qt::UserRole + 1, rootIsFile);
        rootItem->setToolTip(0, group.rootToolTip);
        rootItem->setExpanded(true);

        for (const QString& moduleName : group.childModules) {
            if (!currentSearchFilter.isEmpty()
                && !moduleName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* moduleItem = createModuleItem(moduleName, group.rootName);
            rootItem->addChild(moduleItem);
        }

        if (rootItem->childCount() == 0)
            delete rootItem;
    }
}

void NavigationWidget::populateSymbolTree()
{
    symbolTreeWidget->clear();
    symbolItemPayloads.clear();
    nextSymbolItemPayloadId = 1;

    if (currentSymbolHierarchy.isEmpty()) {
        QTreeWidgetItem* emptyItem = new QTreeWidgetItem(symbolTreeWidget);
        emptyItem->setText(0, "No symbols found");
        emptyItem->setFlags(Qt::ItemIsEnabled);
        return;
    }

    for (const SymbolOutlineGroup& group : std::as_const(currentSymbolHierarchy)) {
        if (group.symbols.isEmpty()) continue;

        QTreeWidgetItem* typeItem = new QTreeWidgetItem(symbolTreeWidget);
        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(group.symbolType)).arg(group.symbols.size()));
        typeItem->setIcon(0, getSymbolIcon(group.symbolType));
        typeItem->setExpanded(true);
        typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int addedCount = 0;
        for (const sym_list::SymbolInfo& symbol : group.symbols) {
            if (!currentSearchFilter.isEmpty()
                && !symbol.symbolName.contains(currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }

            QTreeWidgetItem* symbolItem = createSymbolItem(symbol);
            typeItem->addChild(symbolItem);
            addedCount++;
        }

        typeItem->setText(0, QString("%1 (%2)").arg(getSymbolTypeDisplayName(group.symbolType)).arg(addedCount));

        if (typeItem->childCount() == 0)
            delete typeItem;
    }
}
void NavigationWidget::applySearchFilter()
{
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
    item->setData(0, Qt::UserRole, filePath);
    item->setToolTip(0, filePath);

    return item;
}

QTreeWidgetItem* NavigationWidget::createModuleItem(const QString& moduleName, const QString& fileName)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    item->setText(0, moduleName);
    item->setIcon(0, getSymbolIcon(sym_list::sym_module));
    item->setData(0, Qt::UserRole, fileName);
    item->setData(0, Qt::UserRole + 1, false);
    item->setToolTip(0, QString("Module: %1\nFile: %2").arg(moduleName, QFileInfo(fileName).fileName()));

    return item;
}

QTreeWidgetItem* NavigationWidget::createSymbolItem(const sym_list::SymbolInfo& symbol)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    const int payloadId = nextSymbolItemPayloadId++;
    symbolItemPayloads.insert(payloadId, symbol);

    item->setText(0, symbol.symbolName);
    item->setIcon(0, getSymbolIcon(symbol.symbolType));
    item->setData(0, Qt::UserRole, static_cast<int>(symbol.symbolType));
    item->setData(0, Qt::UserRole + 1, payloadId);
    item->setToolTip(0, QString("%1: %2").arg(getSymbolTypeDisplayName(symbol.symbolType), symbol.symbolName));

    return item;
}

QString NavigationWidget::getSymbolTypeDisplayName(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_module: return "Module";
    case sym_list::sym_reg: return "Register";
    case sym_list::sym_wire: return "Wire";
    case sym_list::sym_logic: return "Logic";
    case sym_list::sym_task: return "Task";
    case sym_list::sym_function: return "Function";
    case sym_list::sym_parameter: return "Parameter";
    case sym_list::sym_localparam: return "Local Parameter";
    case sym_list::sym_port_input: return "Input Port";
    case sym_list::sym_port_output: return "Output Port";
    case sym_list::sym_port_inout: return "Inout Port";
    case sym_list::sym_port_ref: return "Ref Port";
    case sym_list::sym_enum: return "Enum Type";
    case sym_list::sym_inst: return "Instance";
    case sym_list::sym_packed_struct: return "Packed Struct Type";
    case sym_list::sym_unpacked_struct: return "Unpacked Struct Type";
    case sym_list::sym_packed_struct_var: return "Packed Struct Variable";
    case sym_list::sym_unpacked_struct_var: return "Unpacked Struct Variable";
    case sym_list::sym_struct_member: return "Struct Member";
    case sym_list::sym_typedef: return "Typedef";
    case sym_list::sym_enum_var: return "Enum Variable";
    case sym_list::sym_enum_value: return "Enum Value";
    default: return "Symbols";
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
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
        return style()->standardIcon(QStyle::SP_ArrowRight);
    case sym_list::sym_inst:
        return style()->standardIcon(QStyle::SP_DirIcon);
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return style()->standardIcon(QStyle::SP_FileIcon);
    default:
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}

void NavigationWidget::expandCurrentFileNodes()
{
    if (currentHighlightedFile.isEmpty()) return;

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
