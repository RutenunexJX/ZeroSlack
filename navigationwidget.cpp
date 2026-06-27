#include "navigationwidget.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>

NavigationWidget::NavigationWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

NavigationWidget::~NavigationWidget()
{
    cancelFileTreePopulation();
}

void NavigationWidget::setActiveTab(NavigationTab tab)
{
    if (tabWidget)
        tabWidget->setCurrentIndex(static_cast<int>(tab));
}

NavigationWidget::NavigationTab NavigationWidget::getActiveTab() const
{
    if (!tabWidget)
        return FileTab;
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
    if (moduleTreeWidget)
        populateModuleTree();
}

void NavigationWidget::updateSymbolHierarchy(const QList<SymbolOutlineGroup>& symbolGroups)
{
    currentSymbolHierarchy = symbolGroups;
    if (symbolTreeWidget)
        populateSymbolTree();
}

void NavigationWidget::updateDesignHierarchy(const DesignHierarchyReport& report)
{
    currentDesignHierarchy = report;
    designParticipatingFiles = report.participatingFiles;
    refreshDesignHeader();
    populateDesignTree();
    if (hideUnrelatedFiles)
        populateFileTree();
    else
        refreshFileTreeDirectoryDimming();
}

void NavigationWidget::clearDesignHierarchy()
{
    currentDesignHierarchy = {};
    designParticipatingFiles.clear();
    refreshDesignHeader();
    populateDesignTree();
    if (hideUnrelatedFiles)
        populateFileTree();
    else
        refreshFileTreeDirectoryDimming();
}

void NavigationWidget::setDesignParticipatingFiles(const QSet<QString>& fileNames)
{
    designParticipatingFiles = fileNames;
    if (hideUnrelatedFiles)
        populateFileTree();
    else
        refreshFileTreeDirectoryDimming();
}

void NavigationWidget::highlightFile(const QString& filePath)
{
    if (currentHighlightedFile == filePath) {
        QTreeWidgetItem* current =
            fileTreeWidget ? fileTreeWidget->currentItem() : nullptr;
        if (current
            && current->data(0, Qt::UserRole).toString() == filePath) {
            return;
        }
    }
    currentHighlightedFile = filePath;

    QTreeWidgetItem* item = findFileItemByPath(filePath);
    if (item) {
        if (fileTreeWidget->currentItem() != item) {
            fileTreeWidget->setCurrentItem(item);
            fileTreeWidget->scrollToItem(item, QAbstractItemView::EnsureVisible);
        }
    }
}

void NavigationWidget::highlightSymbol(const QString& symbolName)
{
    if (!symbolTreeWidget)
        return;
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
    if (!moduleTreeWidget)
        return;
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

void NavigationWidget::onDesignTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item)
        return;
    const int payloadId = item->data(0, Qt::UserRole + 1).toInt();
    if (designItemPayloads.contains(payloadId))
        emit designNodeDoubleClicked(designItemPayloads.value(payloadId));
}

void NavigationWidget::onFileTreeContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = fileTreeWidget->itemAt(pos);
    if (!item)
        return;
    const QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty())
        emit fileContextMenuRequested(filePath, fileTreeWidget->viewport()->mapToGlobal(pos));
}

void NavigationWidget::onModuleTreeContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = moduleTreeWidget->itemAt(pos);
    if (!item || item->data(0, Qt::UserRole + 1).toBool())
        return;
    const QString moduleName = item->text(0);
    if (!moduleName.isEmpty())
        emit moduleContextMenuRequested(moduleName,
                                        moduleTreeWidget->viewport()->mapToGlobal(pos));
}

void NavigationWidget::onDesignTreeContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = designTreeWidget->itemAt(pos);
    if (!item)
        return;
    const int payloadId = item->data(0, Qt::UserRole + 1).toInt();
    if (designItemPayloads.contains(payloadId)) {
        emit designNodeContextMenuRequested(
            designItemPayloads.value(payloadId),
            designTreeWidget->viewport()->mapToGlobal(pos));
    }
}

void NavigationWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    searchLineEdit = new QLineEdit(this);
    searchLineEdit->setPlaceholderText("Search files or design...");
    searchLineEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(searchLineEdit);

    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

    setupFileTab();
    setupDesignTab();

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
    fileTreeWidget->setSortingEnabled(false);
    fileTreeWidget->header()->hide();

    fileTreePopulationTimer = new QTimer(this);
    fileTreePopulationTimer->setSingleShot(false);
    connect(fileTreePopulationTimer,
            &QTimer::timeout,
            this,
            &NavigationWidget::processFileTreePopulationChunk);

    hideUnrelatedFilesCheckBox = new QCheckBox(QStringLiteral("Hide unrelated"), fileTab);
    hideUnrelatedFilesCheckBox->setChecked(false);
    fileTabLayout->addWidget(hideUnrelatedFilesCheckBox);
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

void NavigationWidget::setupDesignTab()
{
    designTab = new QWidget();
    designTabLayout = new QVBoxLayout(designTab);
    designTabLayout->setContentsMargins(2, 2, 2, 2);
    designTabLayout->setSpacing(4);

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(4);
    designTopLabel = new QLabel(designTab);
    designTopLabel->setWordWrap(true);
    designClearButton = new QPushButton(QStringLiteral("Clear"), designTab);
    designRefreshButton = new QPushButton(QStringLiteral("Refresh"), designTab);
    topLayout->addWidget(designTopLabel, 1);
    topLayout->addWidget(designClearButton);
    topLayout->addWidget(designRefreshButton);
    designTabLayout->addLayout(topLayout);

    designTreeWidget = new QTreeWidget(designTab);
    designTreeWidget->setColumnCount(2);
    designTreeWidget->setHeaderLabels({QStringLiteral("Instance"),
                                       QStringLiteral("Module")});
    designTreeWidget->setAlternatingRowColors(true);
    designTreeWidget->setRootIsDecorated(true);
    designTreeWidget->setSortingEnabled(false);
    designTreeWidget->header()->setStretchLastSection(true);
    designTreeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    designTabLayout->addWidget(designTreeWidget);

    designTab->setLayout(designTabLayout);
    tabWidget->addTab(designTab, "Design");
    refreshDesignHeader();
}

void NavigationWidget::setupConnections()
{
    connect(tabWidget, &QTabWidget::currentChanged,
            this, &NavigationWidget::onTabChanged);

    connect(searchLineEdit, &QLineEdit::textChanged,
            this, &NavigationWidget::onSearchTextChanged);

    connect(fileTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onFileTreeDoubleClicked);
    fileTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileTreeWidget, &QTreeWidget::customContextMenuRequested,
            this, &NavigationWidget::onFileTreeContextMenuRequested);
    connect(hideUnrelatedFilesCheckBox,
            &QCheckBox::toggled,
            this,
            [this](bool checked) {
                hideUnrelatedFiles = checked;
                populateFileTree();
            });

    connect(designTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onDesignTreeDoubleClicked);
    designTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(designTreeWidget, &QTreeWidget::customContextMenuRequested,
            this, &NavigationWidget::onDesignTreeContextMenuRequested);

    connect(designClearButton, &QPushButton::clicked,
            this, &NavigationWidget::clearDesignTopRequested);
    connect(designRefreshButton, &QPushButton::clicked,
            this, &NavigationWidget::refreshDesignHierarchyRequested);
}
