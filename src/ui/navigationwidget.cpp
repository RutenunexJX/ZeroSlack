#include "uicontrols.h"
#include "uitypography.h"
#include "navigationwidget.h"
#include "applicationthememanager.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "navigationrowstyle.h"
#include <QToolButton>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTreeWidgetItemIterator>

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
    if (activeTab() == SourceFilesTab) setActiveTab(FileTab);
    if (searchLineEdit)
        searchLineEdit->setFocus(Qt::ShortcutFocusReason);
}

QString NavigationWidget::searchFilter(NavigationTab tab) const
{
    return searchFilterForIndex(tab);
}

void NavigationWidget::setSearchFilter(NavigationTab tab,
                                       const QString& filter)
{
    const int index = static_cast<int>(tab);
    if (index != FileTab && index != DesignTab) return;
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
    updateDesignSummary(report);
    populateDesignTree();
}

void NavigationWidget::setDesignHierarchyPending(bool pending)
{
    if (designTreeWidget)
        designTreeWidget->setEnabled(!pending);

}

void NavigationWidget::updateDesignSummary(const DesignHierarchyReport& report, bool structureChanged)
{
    currentDesignHierarchy = report;
    if (!structureChanged && designTreeWidget) {
        QHash<QString, DesignHierarchyNode> nodes;
        for (const auto& node : report.nodes) nodes.insert(node.id, node);
        for (QTreeWidgetItemIterator it(designTreeWidget); *it; ++it) {
            const int payloadId = (*it)->data(0, Qt::UserRole + 1).toInt();
            auto payload = designItemPayloads.find(payloadId);
            if (payload == designItemPayloads.end()) continue;
            const auto next = nodes.constFind(payload->id);
            if (next == nodes.cend()) continue;
            const bool moved = payload->definitionFile != next->definitionFile
                || payload->definitionLine != next->definitionLine
                || payload->instanceFile != next->instanceFile || payload->instanceLine != next->instanceLine;
            payload.value() = next.value();
            if (moved) {
                const QString location = next->isTop
                    ? QStringLiteral("%1:%2").arg(next->definitionFile).arg(next->definitionLine)
                    : QStringLiteral("instance %1:%2\nmodule %3:%4").arg(next->instanceFile).arg(next->instanceLine)
                          .arg(next->definitionFile).arg(next->definitionLine);
                (*it)->setToolTip(0, next->unresolved ? location + '\n' + next->unresolvedReason : location);
            }
        }
    }
    setDesignHierarchyPending(false);
    designTreeRefreshPending |= structureChanged;

}

void NavigationWidget::clearDesignHierarchy()
{
    currentDesignHierarchy = {};
    setDesignHierarchyPending(false);
    populateDesignTree();
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
    searchLineEdit->setVisible(index != SourceFilesTab);
    if (index == SourceFilesTab) { emit viewChanged(index); return; }
    (index == DesignTab ? designTabLayout : fileTabLayout)->insertWidget(0, searchLineEdit);
    searchLineEdit->show();
    if (searchLineEdit) {
        const QSignalBlocker blocker(searchLineEdit);
        searchLineEdit->setText(searchFilterForIndex(index));
        searchLineEdit->setPlaceholderText(
            index == DesignTab
                ? QStringLiteral("Search instances or modules...")
                : QStringLiteral("Search files or paths..."));
    }
    if (index == DesignTab && designTreeRefreshPending)
        populateDesignTree();
    else if (index == FileTab && fileTreeRefreshPending)
        populateFileTree();
    emit viewChanged(index);
}

void NavigationWidget::onSearchTextChanged(const QString& text)
{
    const int index = tabWidget ? tabWidget->currentIndex() : FileTab;
    if (index != FileTab && index != DesignTab) return;
    const QString normalized = text.trimmed();
    if (searchFilterForIndex(index) == normalized
        && !(index == DesignTab ? designTreeRefreshPending : fileTreeRefreshPending))
        return;
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
    const int payloadId = item ? item->data(0, Qt::UserRole + 1).toInt() : -1;
    emit designNodeContextMenuRequested(designItemPayloads.value(payloadId),
        designTreeWidget->viewport()->mapToGlobal(pos));
}

void NavigationWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    searchLineEdit = UiControls::lineEdit(this);
    searchLineEdit->setObjectName(QStringLiteral("navigationSearchLineEdit"));
    searchLineEdit->setPlaceholderText("Search files or paths...");
    searchLineEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(searchLineEdit);

    tabWidget = UiControls::tabWidget(this);
    tabWidget->setObjectName(QStringLiteral("navigationTabs"));
    tabWidget->setIconSize(QSize(20, 20));
    tabWidget->tabBar()->setProperty("compactIconTabs", true);
    mainLayout->insertWidget(0, tabWidget);
    mainLayout->removeWidget(searchLineEdit);

    setupFileTab();
    setupDesignTab();
    fileTabLayout->insertWidget(0, searchLineEdit);

    setLayout(mainLayout);
}

void NavigationWidget::setupFileTab()
{
    fileTab = new QWidget();
    fileTabLayout = new QVBoxLayout(fileTab);
    fileTabLayout->setContentsMargins(2, 2, 2, 2);
    fileTabLayout->setSpacing(2);

    fileTreeWidget = UiControls::treeWidget(fileTab);
    UiControls::enableTreeTransitions(fileTreeWidget);
    fileTreeWidget->setObjectName(
        QStringLiteral("navigationFileTree"));
    fileTreeWidget->setHeaderLabel("Files");
    fileTreeWidget->setAlternatingRowColors(false);
    fileTreeWidget->setProperty("workspaceNavigationList", true);
    fileTreeWidget->setMouseTracking(true);
    new NavigationRowStyle(fileTreeWidget, false);
    fileTreeWidget->setSortingEnabled(false);
    fileTreeWidget->header()->hide();

    fileTreePopulationTimer = new QTimer(this);
    fileTreePopulationTimer->setSingleShot(false);
    connect(fileTreePopulationTimer,
            &QTimer::timeout,
            this,
            &NavigationWidget::processFileTreePopulationChunk);

    fileTabLayout->addWidget(fileTreeWidget);
    fileTab->setLayout(fileTabLayout);

    const auto index = tabWidget->addTab(fileTab, RoundedIcons::icon(RoundedIcons::File), QString());
    tabWidget->setTabToolTip(index, tr("Files"));
    tabWidget->tabBar()->setAccessibleTabName(index, tr("Files"));
}

void NavigationWidget::setupDesignTab()
{
    designTab = new QWidget();
    designTabLayout = new QVBoxLayout(designTab);
    designTabLayout->setContentsMargins(2, 2, 2, 2);
    designTabLayout->setSpacing(8);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addStretch();
    auto* add = UiControls::toolButton(designTab);
    add->setObjectName(QStringLiteral("addDesignProject"));
    add->setIcon(RoundedIcons::icon(RoundedIcons::ZoomIn));
    add->setToolTip(tr("Add design project from a module"));
    add->setAccessibleName(tr("Add design project"));
    actions->addWidget(add);
    connect(add, &QToolButton::clicked, this, &NavigationWidget::addDesignProjectRequested);
    designTabLayout->addLayout(actions);

    designTreeWidget = UiControls::treeWidget(designTab);
    designTreeWidget->setObjectName(QStringLiteral("navigationDesignTree"));
    UiControls::enableTreeTransitions(designTreeWidget);
    designTreeWidget->setColumnCount(1);
    designTreeWidget->setHeaderHidden(true);
    auto* rows = new NavigationRowStyle(designTreeWidget, true);
    rows->activateProject = [this](const QString& id) { emit designProjectActivated(id); };
    const auto expansion = [this](QTreeWidgetItem* item, bool expanded) {
        if (populatingDesignTree || !designSearchFilter.isEmpty()) return;
        const auto id = item->data(0, NavigationRowStyle::ProjectRole).toString();
        if (!id.isEmpty()) emit designExpansionChanged(id, expanded);
    };
    connect(designTreeWidget, &QTreeWidget::itemExpanded, this, [expansion](QTreeWidgetItem* item) { expansion(item, true); });
    connect(designTreeWidget, &QTreeWidget::itemCollapsed, this, [expansion](QTreeWidgetItem* item) { expansion(item, false); });
    designTreeWidget->setAlternatingRowColors(false);
    designTreeWidget->setProperty("workspaceNavigationList", true);
    designTreeWidget->setMouseTracking(true);

    designTreeWidget->setSortingEnabled(false);
    designTreeWidget->header()->setStretchLastSection(true);
    designTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    designTabLayout->addWidget(designTreeWidget);

    designTab->setLayout(designTabLayout);
    const auto index = tabWidget->addTab(designTab, RoundedIcons::icon(RoundedIcons::Hierarchy), QString());
    tabWidget->setTabToolTip(index, tr("Design"));
    tabWidget->tabBar()->setAccessibleTabName(index, tr("Design"));
}

QString NavigationWidget::searchFilterForIndex(int index) const
{
    return index == DesignTab ? designSearchFilter : index == FileTab ? fileSearchFilter : QString();
}

int NavigationWidget::activeTab() const { return tabWidget->currentIndex(); }
void NavigationWidget::setSourceFilesWidget(QWidget* widget)
{
    if (!widget || tabWidget->count() > SourceFilesTab) return;
    const auto index = tabWidget->addTab(widget, RoundedIcons::icon(RoundedIcons::BuildInputs), QString());
    tabWidget->setTabToolTip(index, tr("Build inputs"));
    tabWidget->tabBar()->setAccessibleTabName(index, tr("Build inputs"));
}

void NavigationWidget::setStoredSearchFilter(int index,
                                             const QString& filter)
{
    if (index == DesignTab) {
        designTreeRefreshPending |= designSearchFilter != filter;
        designSearchFilter = filter;
    } else if (index == FileTab) {
        fileTreeRefreshPending |= fileSearchFilter != filter;
        fileSearchFilter = filter;
    }
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
    connect(designTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onDesignTreeDoubleClicked);
    designTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(designTreeWidget, &QTreeWidget::customContextMenuRequested,
            this, &NavigationWidget::onDesignTreeContextMenuRequested);

}
