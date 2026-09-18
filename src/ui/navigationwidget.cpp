#include "uitypography.h"
#include "navigationwidget.h"
#include "applicationthememanager.h"
#include "editorfileidentity.h"
#include "ElaCheckBox.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>

NavigationWidget::NavigationWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this,
            [this](ThemeMode) {
                refreshThemePresentation();
            });
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

QString NavigationWidget::searchFilter(NavigationTab tab) const
{
    return tab == DesignTab ? designSearchFilter : fileSearchFilter;
}

void NavigationWidget::setSearchFilter(NavigationTab tab,
                                       const QString& filter)
{
    const int index = static_cast<int>(tab);
    const QString normalized = filter.trimmed();
    setStoredSearchFilter(index, normalized);
    if (!tabWidget || tabWidget->currentIndex() != index)
        return;
    if (searchLineEdit && searchLineEdit->text() != normalized)
        searchLineEdit->setText(normalized);
    else
        onSearchTextChanged(normalized);
}

void NavigationWidget::setWorkspaceRoot(
    const QString& workspaceRoot)
{
    const QString normalized =
        EditorFileIdentity::normalized(workspaceRoot);
    if (workspaceFileTreeRootPath == normalized)
        return;
    workspaceFileTreeRootPath = normalized;
    explicitlyTrackedPaths.clear();
    populateFileTree();
}

void NavigationWidget::setVirtualSourceGroups(
    const QList<WorkspaceVirtualSourceGroup>& groups)
{
    if (virtualSourceGroups == groups)
        return;
    virtualSourceGroups = groups;
    populateFileTree();
}

void NavigationWidget::registerWorkspacePath(
    const QString& path,
    bool directory)
{
    const QString normalized =
        EditorFileIdentity::normalized(path);
    if (normalized.isEmpty())
        return;
    explicitlyTrackedPaths.insert(normalized, directory);
    populateFileTree();
}

void NavigationWidget::unregisterWorkspacePath(
    const QString& path,
    bool recursive)
{
    const QString normalized =
        EditorFileIdentity::normalized(path);
    if (normalized.isEmpty())
        return;
    const QString normalizedKey =
        EditorFileIdentity::lookupKey(normalized);
    const QString prefix =
        normalizedKey.endsWith(QLatin1Char('/'))
        ? normalizedKey
        : normalizedKey + QLatin1Char('/');
    for (auto it = explicitlyTrackedPaths.begin();
         it != explicitlyTrackedPaths.end();) {
        const QString itemKey =
            EditorFileIdentity::lookupKey(it.key());
        const bool matches =
            itemKey == normalizedKey
            || (recursive
                && itemKey.startsWith(prefix));
        if (matches)
            it = explicitlyTrackedPaths.erase(it);
        else
            ++it;
    }
    populateFileTree();
}

void NavigationWidget::renameWorkspacePath(
    const QString& sourcePath,
    const QString& targetPath,
    bool recursive)
{
    const QString source =
        EditorFileIdentity::normalized(sourcePath);
    const QString target =
        EditorFileIdentity::normalized(targetPath);
    if (source.isEmpty() || target.isEmpty())
        return;
    QHash<QString, bool> replacements;
    const QString sourcePrefix =
        source.endsWith(QLatin1Char('/'))
        ? source
        : source + QLatin1Char('/');
    const QString sourceKey =
        EditorFileIdentity::lookupKey(source);
    const QString sourceKeyPrefix =
        sourceKey.endsWith(QLatin1Char('/'))
        ? sourceKey
        : sourceKey + QLatin1Char('/');
    for (auto it = explicitlyTrackedPaths.begin();
         it != explicitlyTrackedPaths.end();) {
        QString replacement;
        const QString itemKey =
            EditorFileIdentity::lookupKey(it.key());
        if (itemKey == sourceKey) {
            replacement = target;
        } else if (recursive
                   && itemKey.startsWith(
                          sourceKeyPrefix)) {
            const QString itemPath =
                EditorFileIdentity::normalized(
                    it.key());
            const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
                Qt::CaseInsensitive;
#else
                Qt::CaseSensitive;
#endif
            if (!itemPath.startsWith(
                    sourcePrefix, sensitivity)) {
                ++it;
                continue;
            }
            replacement =
                target
                + itemPath.mid(source.size());
        }
        if (replacement.isEmpty()) {
            ++it;
            continue;
        }
        replacements.insert(
            EditorFileIdentity::normalized(
                replacement),
            it.value());
        it = explicitlyTrackedPaths.erase(it);
    }
    if (replacements.isEmpty()) {
        replacements.insert(
            target,
            QFileInfo(target).isDir());
    }
    for (auto it = replacements.cbegin();
         it != replacements.cend();
         ++it) {
        explicitlyTrackedPaths.insert(
            it.key(), it.value());
    }
    populateFileTree();
}

void NavigationWidget::updateFileHierarchy(const QStringList& files)
{
    currentFileList = files;
    populateFileTree();
}

void NavigationWidget::updateFileHierarchy(
    const QStringList& files,
    const QList<WorkspaceVirtualSourceGroup>& groups)
{
    currentFileList = files;
    virtualSourceGroups = groups;
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
    if (searchLineEdit) {
        const QSignalBlocker blocker(searchLineEdit);
        searchLineEdit->setText(searchFilterForIndex(index));
        searchLineEdit->setPlaceholderText(
            index == DesignTab
                ? QStringLiteral("Search instances or modules...")
                : QStringLiteral("Search files or paths..."));
    }
    if (index == DesignTab)
        populateDesignTree();
    else
        populateFileTree();
    emit viewChanged(index);
}

void NavigationWidget::onSearchTextChanged(const QString& text)
{
    const int index = tabWidget ? tabWidget->currentIndex() : FileTab;
    const QString normalized = text.trimmed();
    setStoredSearchFilter(index, normalized);
    if (index == DesignTab)
        populateDesignTree();
    else
        populateFileTree();
    emit searchFilterChanged(index, normalized);
}

void NavigationWidget::onFileTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column)

    if (!item) return;

    const QString filePath =
        item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()
        && item->data(0, FileTreeKindRole).toInt()
               == FileItem) {
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
    const QPoint globalPos =
        fileTreeWidget->viewport()->mapToGlobal(pos);
    if (!item) {
        emit fileTreeNodeContextMenuRequested(
            QString(), true, globalPos);
        return;
    }
    const QString filePath = item->data(0, Qt::UserRole).toString();
    const FileTreeItemKind kind =
        static_cast<FileTreeItemKind>(
            item->data(0, FileTreeKindRole).toInt());
    if (kind == VirtualSourceGroupItem)
        return;
    if (filePath.isEmpty()) {
        emit fileTreeNodeContextMenuRequested(
            QString(), true, globalPos);
        return;
    }
    emit fileTreeNodeContextMenuRequested(
        filePath,
        kind == DirectoryItem,
        globalPos);
    if (kind == FileItem)
        emit fileContextMenuRequested(filePath, globalPos);
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
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    searchLineEdit = new ElaLineEdit(this);
    searchLineEdit->setObjectName(QStringLiteral("navigationSearchLineEdit"));
    searchLineEdit->setPlaceholderText("Search files or paths...");
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
    fileTreeWidget->setObjectName(
        QStringLiteral("navigationFileTree"));
    fileTreeWidget->setHeaderLabel("Files");
    fileTreeWidget->setAlternatingRowColors(false);
    fileTreeWidget->setProperty("workspaceNavigationList", true);
    fileTreeWidget->setMouseTracking(true);
    fileTreeWidget->setRootIsDecorated(true);
    fileTreeWidget->setSortingEnabled(false);
    fileTreeWidget->header()->hide();

    fileTreePopulationTimer = new QTimer(this);
    fileTreePopulationTimer->setSingleShot(false);
    connect(fileTreePopulationTimer,
            &QTimer::timeout,
            this,
            &NavigationWidget::processFileTreePopulationChunk);

    hideUnrelatedFilesCheckBox = new ElaCheckBox(QStringLiteral("Hide unrelated"), fileTab);
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
    designTabLayout->setSpacing(8);

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(4);
    designTopLabel = new QLabel(designTab);
    designTopLabel->setWordWrap(true);
    designClearButton = new ElaPushButton(QStringLiteral("Clear"), designTab);
    designRefreshButton = new ElaPushButton(QStringLiteral("Refresh"), designTab);
    UiTypography::apply(designTopLabel, UiTypography::Role::Metadata);
    designTabLayout->addWidget(designTopLabel);
    topLayout->addStretch(1);
    topLayout->addWidget(designClearButton);
    topLayout->addWidget(designRefreshButton);
    designTabLayout->addLayout(topLayout);

    designTreeWidget = new QTreeWidget(designTab);
    designTreeWidget->setColumnCount(2);
    designTreeWidget->setHeaderLabels({QStringLiteral("Instance"),
                                       QStringLiteral("Module")});
    designTreeWidget->setAlternatingRowColors(false);
    designTreeWidget->setProperty("workspaceNavigationList", true);
    designTreeWidget->setMouseTracking(true);
    designTreeWidget->setRootIsDecorated(true);
    designTreeWidget->setSortingEnabled(false);
    designTreeWidget->header()->setStretchLastSection(true);
    designTreeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    designTabLayout->addWidget(designTreeWidget);

    designTab->setLayout(designTabLayout);
    tabWidget->addTab(designTab, "Design");
    refreshDesignHeader();
}

QString NavigationWidget::searchFilterForIndex(int index) const
{
    return index == DesignTab ? designSearchFilter : fileSearchFilter;
}

void NavigationWidget::setStoredSearchFilter(int index,
                                             const QString& filter)
{
    if (index == DesignTab)
        designSearchFilter = filter;
    else
        fileSearchFilter = filter;
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
