#include "uicontrols.h"
#include "uitypography.h"
#include "navigationwidget.h"
#include "editorsplitcontroller.h"
#include "applicationthememanager.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "navigationrowstyle.h"
#include "designhierarchymodel.h"
#include <QScrollBar>
#include <QToolButton>
#include <QEvent>
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
    if (designSearchCancellation) designSearchCancellation->store(true);
    if (designSearchWatcher) { disconnect(designSearchWatcher, nullptr, this, nullptr); designSearchWatcher->waitForFinished(); }
    cancelFileTreePopulation();
}

void NavigationWidget::setActiveTab(NavigationTab tab, bool activate)
{
    QWidget* page=tab==DesignTab ? designTab : fileTab;
    if (pageRegistry) {
        if (activate) {
            pageRegistry->restorePage(page,pageRegistry->regionGroup(QStringLiteral("left")));
            pageRegistry->activatePage(page);
        } else {
            if (auto* group=pageRegistry->groupForPage(page)) {
                const QSignalBlocker block(group); group->setCurrentWidget(page);
            }
            onTabChanged(tab==DesignTab ? DesignTab : FileTab);
        }
    } else if (tabWidget) tabWidget->setCurrentWidget(page);
}

void NavigationWidget::focusSearch()
{
    const auto tab=activeTab()==DesignTab ? DesignTab : FileTab;
    setActiveTab(tab);
    (tab==DesignTab ? designSearchLineEdit : searchLineEdit)->setFocus(Qt::ShortcutFocusReason);
}

QString NavigationWidget::searchFilter(NavigationTab tab) const
{
    return searchFilterForIndex(tab);
}

void NavigationWidget::setSearchFilter(NavigationTab tab, const QString& filter)
{
    if (tab != FileTab && tab != DesignTab) return;
    auto* search = tab == DesignTab ? designSearchLineEdit : searchLineEdit;
    if (search->text() != filter.trimmed()) search->setText(filter.trimmed());
}

void NavigationWidget::setWorkspaceRoot(
    const QString& workspaceRoot)
{
    const QString normalized =
        EditorFileIdentity::normalized(workspaceRoot);
    if (workspaceFileTreeRootPath == normalized)
        return;
    saveDesignViewState();
    workspaceFileTreeRootPath = normalized;
    if (designSearchCancellation) designSearchCancellation->store(true);
    ++designSearchGeneration;
    pendingDesignFile.clear(); pendingDesignModule.clear(); designScanning = false;
    savedDesignState = workspaceDesignStates.value(normalized);
    designExpandedIds.clear();
    designSearchLineEdit->clear();
    explicitlyTrackedPaths.clear();
    clearDesignHierarchy();
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
    saveDesignViewState();
    currentDesignHierarchy = report;
    designAnalysisPending = false;
    populateDesignTree();
}
void NavigationWidget::setDesignHierarchyPending(bool pending)
{
    designAnalysisPending = pending;
    updateDesignPresentation();
}
void NavigationWidget::setDesignProjectExpanded(const QString& id, bool expanded)
{
    const auto index = designModel->indexForId(id);
    if (index.isValid()) designTreeWidget->setExpanded(index, expanded);
}
void NavigationWidget::updateDesignSummary(const DesignHierarchyReport& report, bool structureChanged)
{
    Q_UNUSED(structureChanged)
    saveDesignViewState();
    currentDesignHierarchy = report;
    designAnalysisPending = false;
    if (activeTab() == DesignTab) populateDesignTree();
    else designTreeRefreshPending = true;
    updateDesignPresentation();
}
void NavigationWidget::clearDesignHierarchy()
{
    currentDesignHierarchy = {};
    designAnalysisPending = false;
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
    if (index!=FileTab && index!=DesignTab) return;
    lastActiveNavigationTab=index;
    refreshTabLabels();
    if (index == DesignTab && designTreeRefreshPending)
        populateDesignTree();
    else if (index == FileTab && fileTreeRefreshPending)
        populateFileTree();
    if (index == DesignTab) updateDesignPresentation();
    emit viewChanged(index);
}

void NavigationWidget::onSearchTextChanged(int index, const QString& text)
{
    if (index != FileTab && index != DesignTab) return;
    const auto normalized = text.trimmed();
    if (searchFilterForIndex(index) == normalized) return;
    if (index == DesignTab) {
        if (designSearchFilter.isEmpty()) savedDesignState = designViewState();
        designSearchFilter = normalized;
        ++designSearchGeneration;
        if (designSearchCancellation) designSearchCancellation->store(true);
        designTreeWidget->setProperty("designSearchPending", !normalized.isEmpty());
        if (normalized.isEmpty()) {
            designSearchTimer->stop();
            setDesignTreeModel(designModel);
            restoreDesignViewState(savedDesignState);
            updateDesignPresentation();
            designTreeWidget->viewport()->update();
        } else designSearchTimer->start();
    } else {
        setStoredSearchFilter(index, normalized); populateFileTree();
    }
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

void NavigationWidget::onDesignTreeDoubleClicked(const QModelIndex& index)
{
    auto* model = qobject_cast<DesignHierarchyModel*>(designTreeWidget->model());
    const auto node = model ? model->node(index) : DesignHierarchyNode();
    if (node.id.isEmpty()) return;
    if (model == designSearchModel) {
        designSearchLineEdit->clear();
        const auto current = designModel->indexForId(node.id);
        for (auto parent = current.parent(); parent.isValid(); parent = parent.parent()) designTreeWidget->expand(parent);
        designTreeWidget->setCurrentIndex(current); designTreeWidget->scrollTo(current);
    }
    saveDesignViewState();
    emit designNodeDoubleClicked(node);
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
    auto* model = qobject_cast<DesignHierarchyModel*>(designTreeWidget->model());
    emit designNodeContextMenuRequested(model->node(designTreeWidget->indexAt(pos)),
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

    designSearchLineEdit=UiControls::lineEdit(this);
    designSearchLineEdit->setObjectName(QStringLiteral("navigationDesignSearchLineEdit"));
    designSearchLineEdit->setClearButtonEnabled(true);
    designSearchLineEdit->setPlaceholderText(tr("Search instances or modules..."));
    tabWidget = UiControls::navigationTabWidget(this);
    tabWidget->setObjectName(QStringLiteral("navigationTabs"));
    tabWidget->setIconSize(QSize(RoundedIcons::SidebarSize, RoundedIcons::SidebarSize));
    auto* addDocument = UiControls::railButton(tabWidget);
    addDocument->setObjectName(QStringLiteral("leftAddDocument")); addDocument->setIcon(RoundedIcons::icon(RoundedIcons::Add)); addDocument->setToolTip(QStringLiteral("New or Open Document"));
    auto* documentMenu = UiControls::menu(addDocument);
    for (const auto& entry : {qMakePair(QStringLiteral("New Markdown"), QStringLiteral("markdown")), qMakePair(QStringLiteral("New Block Document"), QStringLiteral("blocks")), qMakePair(QStringLiteral("Open File…"), QStringLiteral("open"))}) {
        auto* action = documentMenu->addAction(entry.first); action->setObjectName(QStringLiteral("leftDocument.") + entry.second);
        connect(action, &QAction::triggered, this, [this, id = entry.second] { emit documentActionRequested(id); });
    }
    addDocument->setMenu(documentMenu); addDocument->setPopupMode(QToolButton::InstantPopup);
    addDocument->setAccessibleName(QStringLiteral("New or Open Document"));
    addDocument->setMinimumSize(addDocument->minimumSizeHint());
    tabWidget->setCornerWidget(addDocument, Qt::TopRightCorner);
    mainLayout->insertWidget(0, tabWidget);
    mainLayout->removeWidget(searchLineEdit);

    setupFileTab();
    setupDesignTab();
    refreshTabLabels();
    fileTabLayout->insertWidget(0, searchLineEdit);
    designTabLayout->insertWidget(0, designSearchLineEdit);

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
    designTabLayout->setContentsMargins(2, 2, 2, 2); designTabLayout->setSpacing(4);
    designTreeWidget = new QTreeView(designTab);
    designTreeWidget->setObjectName(QStringLiteral("navigationDesignTree"));
    designTreeWidget->setHeaderHidden(true);
    designTreeWidget->setFrameShape(QFrame::NoFrame);
    designTreeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    designTreeWidget->setProperty("workspaceNavigationList", true);
    designTreeWidget->setMouseTracking(true);
    designModel = new DesignHierarchyModel(this);
    designSearchModel = new DesignHierarchyModel(this);
    setDesignTreeModel(designModel);
    designTreeWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    new NavigationRowStyle(designTreeWidget, true);
    designTreeWidget->header()->setStretchLastSection(true);
    designTabLayout->addWidget(designTreeWidget);
    designStateLabel = new QLabel(designTab);
    designStateLabel->setObjectName(QStringLiteral("designStateLabel"));
    designStateLabel->setWordWrap(true); designStateLabel->setAlignment(Qt::AlignCenter);
    designTabLayout->addWidget(designStateLabel);
    designSearchTimer = new QTimer(this); designSearchTimer->setInterval(120); designSearchTimer->setSingleShot(true);
    connect(designSearchTimer, &QTimer::timeout, this, &NavigationWidget::startDesignSearch);
    designStateTimer = new QTimer(this); designStateTimer->setInterval(250); designStateTimer->setSingleShot(true);
    connect(designStateTimer, &QTimer::timeout, this, &NavigationWidget::saveDesignViewState);
    const auto expansion = [this](const QModelIndex& index, bool expanded) {
        if (restoringDesignState || designTreeWidget->model() != designModel) return;
        const auto id = index.data(Qt::UserRole).toString();
        if (expanded) designExpandedIds.insert(id); else designExpandedIds.remove(id);
        designStateTimer->start();
    };
    connect(designTreeWidget, &QTreeView::expanded, this, [expansion](const QModelIndex& index) { expansion(index, true); });
    connect(designTreeWidget, &QTreeView::collapsed, this, [expansion](const QModelIndex& index) { expansion(index, false); });
    connect(designTreeWidget->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (!restoringDesignState) designStateTimer->start(); });
    connect(designModel, &DesignHierarchyModel::reportApplied, this, [this] {
        designTreeWidget->setProperty("designTotalNodes", designModel->report()->nodes.size());
        designTreeWidget->setProperty("designMaterializedRows", designModel->materializedCount());
        if (!savedDesignState.isEmpty() && designTreeWidget->model() == designModel) restoreDesignViewState(savedDesignState);
        revealPendingDesignProject(); updateDesignPresentation(); refreshSimulationTop();
        if (!designSearchFilter.isEmpty()) designSearchTimer->start();
    });
    const auto index = tabWidget->addTab(designTab, RoundedIcons::icon(RoundedIcons::Hierarchy), QString());
    tabWidget->setTabToolTip(index, tr("Design")); tabWidget->tabBar()->setAccessibleTabName(index, tr("Design"));
}

QString NavigationWidget::searchFilterForIndex(int index) const
{
    return index == DesignTab ? designSearchFilter : index == FileTab ? fileSearchFilter : QString();
}

void NavigationWidget::attachPageRegistry(EditorSplitController* registry)
{
    if (!registry || pageRegistry) return;
    pageRegistry=registry;
    registry->registerRegion(QStringLiteral("left"),this,tabWidget);
    const auto registerTool=[this,registry](QWidget* page,const QString& id) {
        page->setProperty("toolPage",true); page->setProperty("toolPageId",id);
        registry->registerPage(page,QStringLiteral("tool:")+id,QStringLiteral("navigation"),
            [registry,page] { return registry->parkPage(page); });
    };
    registerTool(fileTab,QStringLiteral("files"));
    registerTool(designTab,QStringLiteral("design"));
    connect(registry,&EditorSplitController::pageActivated,this,[this](QWidget* page) {
        if (page==fileTab) onTabChanged(FileTab);
        else if (page==designTab) onTabChanged(DesignTab);
    });
    refreshTabLabels();
}

int NavigationWidget::activeTab() const
{
    return lastActiveNavigationTab;
}
void NavigationWidget::refreshTabLabels()
{
    for (auto* page : {fileTab,designTab}) {
        auto* group=pageRegistry ? pageRegistry->groupForPage(page) : tabWidget;
        const int index=group ? group->indexOf(page) : -1;
        if (index<0) continue;
        const auto title=page==fileTab ? tr("Files") : tr("Design");
        group->setTabToolTip(index,title); group->tabBar()->setAccessibleTabName(index,title);
        group->setTabText(index,title);
    }
}

void NavigationWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type()==QEvent::LanguageChange && tabWidget) refreshTabLabels();
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
            this,[this](int index) {
                if (!pageRegistry) onTabChanged(tabWidget->widget(index)==designTab ? DesignTab : FileTab);
            });

    connect(searchLineEdit,&QLineEdit::textChanged,this,[this](const QString& text) { onSearchTextChanged(FileTab,text); });
    connect(designSearchLineEdit,&QLineEdit::textChanged,this,[this](const QString& text) { onSearchTextChanged(DesignTab,text); });

    connect(fileTreeWidget, &QTreeWidget::itemDoubleClicked,
            this, &NavigationWidget::onFileTreeDoubleClicked);
    fileTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileTreeWidget, &QTreeWidget::customContextMenuRequested,
            this, &NavigationWidget::onFileTreeContextMenuRequested);
    connect(designTreeWidget, &QTreeView::clicked, this, &NavigationWidget::onDesignTreeDoubleClicked);
    connect(designTreeWidget, &QTreeView::activated, this, &NavigationWidget::onDesignTreeDoubleClicked);
    designTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(designTreeWidget, &QTreeView::customContextMenuRequested, this, &NavigationWidget::onDesignTreeContextMenuRequested);
}
