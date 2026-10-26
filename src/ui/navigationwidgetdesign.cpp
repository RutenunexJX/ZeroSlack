#include "navigationwidget.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include <QDir>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <algorithm>

namespace {
constexpr int CandidateFileRole = Qt::UserRole + 60;
constexpr int CandidateModuleRole = Qt::UserRole + 61;
QString candidateKey(const QTreeWidgetItem* item)
{
    return item ? EditorFileIdentity::lookupKey(item->data(0, CandidateFileRole).toString())
        + QChar(0x1f) + item->data(0, CandidateModuleRole).toString() : QString();
}
}

void NavigationWidget::setupDesignSelector()
{
    designCandidateTree = UiControls::treeWidget(designTab);
    designCandidateTree->setObjectName(QStringLiteral("designCandidateTree"));
    designCandidateTree->setHeaderHidden(true);
    designCandidateTree->setColumnCount(1);
    designCandidateTree->setExpandsOnDoubleClick(false);
    designCandidateTree->setIconSize({18,18});
    designCandidateTree->header()->setSectionResizeMode(QHeaderView::Stretch);
    designCandidateTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(designCandidateTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        emit designNodeContextMenuRequested({}, designCandidateTree->viewport()->mapToGlobal(p));
    });
    connect(designCandidateTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) { activateDesignCandidate(item); });
    connect(designCandidateTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { activateDesignCandidate(item); });
    designTabLayout->addWidget(designCandidateTree, 1);
    designStateLabel = new QLabel(designTab);
    designStateLabel->setObjectName(QStringLiteral("designEmptyState"));
    designStateLabel->setWordWrap(true);
    designStateLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    designTabLayout->addWidget(designStateLabel, 1);
    designClearSearchButton = UiControls::pushButton(tr("Clear search"), designTab);
    designClearSearchButton->setObjectName(QStringLiteral("clearDesignSearch"));
    connect(designClearSearchButton, &QAbstractButton::clicked, this, [this] { searchLineEdit->clear(); });
    designTabLayout->addWidget(designClearSearchButton);
    designSelectionError = new QLabel(designTab);
    designSelectionError->setObjectName(QStringLiteral("designSelectionError"));
    designSelectionError->setWordWrap(true);
    designTabLayout->addWidget(designSelectionError);
    updateDesignPresentation();
}

void NavigationWidget::setDesignCandidateContext(const QStringList& files, bool hasProjects, bool scanning)
{
    QStringList previous;
    for (const auto& candidate : std::as_const(designCandidates)) previous << candidate.file;
    if (designHasProjects && !hasProjects) { designAdding = true; designSelectionExplicit = false; }
    if (hasProjects && !designSelectionExplicit) designAdding = false;
    designHasProjects = hasProjects;
    designScanning = scanning;
    if (previous != files) {
        designCandidates.clear(); designLikelyTops.clear();
        for (const auto& file : files) designCandidates.append({file, {}, false, false});
        populateDesignCandidates();
    }
    updateDesignPresentation();
}

void NavigationWidget::updateDesignCandidates(const QString& workspace,
                                              const QList<DesignFileCandidate>& files,
                                              const QStringList& likelyTops)
{
    if (!EditorFileIdentity::same(workspace, workspaceFileTreeRootPath)) return;
    designCandidates = files;
    designLikelyTops = likelyTops;
    populateDesignCandidates();
}

void NavigationWidget::beginDesignProjectSelection()
{
    if (!designAdding) {
        savedDesignScroll = designTreeWidget->verticalScrollBar()->value();
        const auto* item = designTreeWidget->currentItem();
        savedDesignSelection = item ? item->data(0, Qt::UserRole).toString() : QString();
    }
    designAdding = true;
    designSelectionExplicit = true;
    designSelectionError->clear();
    setActiveTab(DesignTab);
    populateDesignCandidates();
    updateDesignPresentation();
}

void NavigationWidget::cancelDesignProjectSelection()
{
    designAdding = false;
    designSelectionExplicit = false;
    designSelectionError->clear();
    if (designTreeRefreshPending) populateDesignTree();
    updateDesignPresentation();
    for (QTreeWidgetItemIterator it(designTreeWidget); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == savedDesignSelection) {
            designTreeWidget->setCurrentItem(*it); break;
        }
    designTreeWidget->verticalScrollBar()->setValue(savedDesignScroll);
}

void NavigationWidget::finishDesignProjectSelection(const QString& issue, const QString& file, const QString& module)
{
    if (!issue.isEmpty()) {
        designSelectionError->setText(issue);
        designSelectionError->setAccessibleName(issue);
        updateDesignPresentation();
        return;
    }
    pendingDesignFile = file; pendingDesignModule = module;
    designAdding = false;
    designSelectionExplicit = false;
    designSelectionError->clear();
    if (!designSearchFilter.isEmpty()) {
        designSearchFilter.clear();
        emit searchFilterChanged(DesignTab, {});
        populateDesignTree();
    }
    updateDesignPresentation();
    revealPendingDesignProject();
}

void NavigationWidget::revealPendingDesignProject()
{
    if (pendingDesignModule.isEmpty()) return;
    for (QTreeWidgetItemIterator it(designTreeWidget); *it; ++it) {
        const auto node = designItemPayloads.value((*it)->data(0, Qt::UserRole + 1).toInt());
        if (node.isTop && node.moduleType == pendingDesignModule && EditorFileIdentity::same(node.definitionFile, pendingDesignFile)) {
            designTreeWidget->setCurrentItem(*it);
            designTreeWidget->scrollToItem(*it, QAbstractItemView::EnsureVisible);
            pendingDesignFile.clear(); pendingDesignModule.clear();
            break;
        }
    }
}

void NavigationWidget::activateDesignCandidate(QTreeWidgetItem* item)
{
    if (!designAdding || !item || !(item->flags() & Qt::ItemIsEnabled)) return;
    const auto file = item->data(0, CandidateFileRole).toString();
    const auto module = item->data(0, CandidateModuleRole).toString();
    if (!file.isEmpty() && !module.isEmpty()) emit designModuleChosen(file, module);
    else if (!file.isEmpty() && item->childCount()) item->setExpanded(true);
}

void NavigationWidget::populateDesignCandidates()
{
    if (!designCandidateTree) return;
    const auto selected = candidateKey(designCandidateTree->currentItem());
    const int scroll = designCandidateTree->verticalScrollBar()->value();
    const QSignalBlocker blocker(designCandidateTree);
    designCandidateTree->clear();
    const QDir workspace(workspaceFileTreeRootPath);
    QList<DesignFileCandidate> files = designCandidates;
    std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
        return workspace.relativeFilePath(a.file) < workspace.relativeFilePath(b.file);
    });
    auto group = [&](const QString& text, const QString& identity) {
        auto* item = new QTreeWidgetItem(designCandidateTree, {text});
        item->setData(0, Qt::UserRole, identity);
        item->setFlags(Qt::ItemIsEnabled);
        item->setExpanded(true);
        return item;
    };
    auto addModule = [&](QTreeWidgetItem* parent, const DesignFileCandidate& file,
                         const DesignModuleCandidate& module, const QString& label) {
        auto* row = new QTreeWidgetItem(parent, {label});
        row->setData(0, CandidateFileRole, file.file);
        row->setData(0, CandidateModuleRole, module.name);
        row->setIcon(0, RoundedIcons::icon(RoundedIcons::Module));
        QString reason;
        if (module.ambiguous) reason = tr("Ambiguous definitions in this file");
        else if (module.added) reason = tr("Added");
        if (!reason.isEmpty()) {
            row->setText(0, label + QStringLiteral(" — ") + reason);
            row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
        }
        const auto description = module.name + '\n' + file.file + (reason.isEmpty() ? QString() : '\n' + reason);
        row->setToolTip(0, description);
        row->setData(0, Qt::AccessibleTextRole, row->text(0));
        row->setData(0, Qt::AccessibleDescriptionRole, description);
        return row;
    };
    auto matches = [&](const DesignFileCandidate& file, const QString& module) {
        return designAddSearchFilter.isEmpty() || workspace.relativeFilePath(file.file).contains(designAddSearchFilter, Qt::CaseInsensitive)
            || module.contains(designAddSearchFilter, Qt::CaseInsensitive);
    };
    QTreeWidgetItem* likely = nullptr;
    int recommendations = 0;
    for (const auto& name : std::as_const(designLikelyTops)) {
        for (const auto& file : files) for (const auto& module : file.modules) {
            if (recommendations == 3) break;
            if (module.name != name || module.ambiguous || !matches(file, module.name)) continue;
            if (!likely) likely = group(tr("Likely tops"), QStringLiteral("likelyTops"));
            addModule(likely, file, module, module.name + QStringLiteral(" — ") + workspace.relativeFilePath(file.file));
            ++recommendations;
        }
        if (recommendations == 3) break;
    }
    auto* all = group(tr("All files"), QStringLiteral("allFiles"));
    for (const auto& file : files) {
        QList<DesignModuleCandidate> visible;
        for (const auto& module : file.modules) if (matches(file, module.name)) visible << module;
        if (!matches(file, {}) && visible.isEmpty()) continue;
        const auto path = workspace.relativeFilePath(file.file);
        if (file.modules.size() == 1) {
            const auto& module = file.modules.first();
            addModule(all, file, module, path + QStringLiteral(" [%1]").arg(module.name));
            continue;
        }
        auto* row = new QTreeWidgetItem(all, {path});
        row->setIcon(0, RoundedIcons::icon(RoundedIcons::File));
        row->setData(0, CandidateFileRole, file.file);
        QString reason;
        if (file.modules.isEmpty()) {
            reason = !file.analyzed ? tr("Analysis not available") : file.hasDiagnostics
                ? tr("No module definitions; diagnostics available") : tr("No module definitions");
            row->setText(0, path + QStringLiteral(" — ") + reason);
        }
        row->setToolTip(0, file.file + (reason.isEmpty() ? QString() : '\n' + reason));
        row->setData(0, Qt::AccessibleDescriptionRole, row->toolTip(0));
        for (const auto& module : visible) addModule(row, file, module, module.name);
        row->setExpanded(!designAddSearchFilter.isEmpty());
    }
    if (all->childCount() == 0) delete all;
    for (QTreeWidgetItemIterator it(designCandidateTree); *it; ++it)
        if (!selected.isEmpty() && candidateKey(*it) == selected) { designCandidateTree->setCurrentItem(*it); break; }
    designCandidateTree->verticalScrollBar()->setValue(scroll);
    updateDesignPresentation();
}

void NavigationWidget::updateDesignPresentation()
{
    if (!designStateLabel) return;
    const auto query = designAdding ? designAddSearchFilter : designSearchFilter;
    bool empty = designAdding ? designCandidateTree->topLevelItemCount() == 0 : designTreeWidget->topLevelItemCount() == 0;
    QString stateText;
    if (empty) {
        if (!query.isEmpty()) stateText = tr("No matches");
        else if (workspaceFileTreeRootPath.isEmpty() && currentDesignHierarchy.nodes.isEmpty()) stateText = tr("Open a workspace to choose a module.");
        else if (designScanning) stateText = tr("Scanning workspace…");
        else if (designAnalysisPending && designHasProjects) stateText = tr("Updating design…");
        else if (designAdding) stateText = tr("No files match the workspace configuration.");
        else stateText = tr("No design projects. Use + to choose a module.");
    }
    designTreeWidget->setVisible(!designAdding && !empty);
    designCandidateTree->setVisible(designAdding && !empty);
    designStateLabel->setText(stateText);
    designStateLabel->setVisible(empty);
    designClearSearchButton->setVisible(empty && !query.isEmpty());
    designBackButton->setVisible(designAdding);
    designSelectionError->setVisible(designAdding && !designSelectionError->text().isEmpty());
    if (activeTab() == DesignTab) {
        const QSignalBlocker blocker(searchLineEdit);
        searchLineEdit->setText(query);
        searchLineEdit->setPlaceholderText(designAdding ? tr("Search files or modules...") : tr("Search instances or modules..."));
        searchLineEdit->setVisible(!query.isEmpty() || (designAdding ? !designCandidates.isEmpty() : designHasProjects || !currentDesignHierarchy.nodes.isEmpty()));
    }
}
