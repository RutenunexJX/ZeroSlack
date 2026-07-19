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

void NavigationWidget::focusSearch()
{
    if (searchLineEdit)
        searchLineEdit->setFocus(Qt::ShortcutFocusReason);
}

void NavigationWidget::updateFileHierarchy(const QStringList& files)
{
    currentFileList = files;
    populateFileTree();
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
