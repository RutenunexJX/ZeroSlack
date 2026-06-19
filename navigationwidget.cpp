#include "navigationwidget.h"
#include <QHeaderView>
#include <QFileInfo>

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
    currentSymbolHierarchy = symbolOutlineGroupsWithRows(symbolGroups);
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
    if (symbolItemPayloads.contains(payloadId)) {
        const SymbolOutlineSymbolRow row = symbolItemPayloads.value(payloadId);
        emit symbolRowDoubleClicked(row);
    }
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
