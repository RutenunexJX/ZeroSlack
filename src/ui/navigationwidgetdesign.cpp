#include "navigationwidget.h"
#include "designhierarchymodel.h"
#include "editorfileidentity.h"
#include <QtConcurrent/QtConcurrent>
#include <QJsonArray>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QItemSelectionModel>

void NavigationWidget::setDesignTreeModel(DesignHierarchyModel* model)
{
    if (designTreeWidget->model() == model) return;
    auto* previous = designTreeWidget->selectionModel();
    designTreeWidget->setModel(model);
    if (previous) previous->deleteLater();
    connect(designTreeWidget->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        if (designStateTimer && !restoringDesignState && designTreeWidget->model() == designModel)
            designStateTimer->start();
    });
}

void NavigationWidget::setSimulationTargetProvider(std::function<QPair<QString, QString>()> provider)
{ simulationTargetProvider = std::move(provider); refreshSimulationTop(); }
void NavigationWidget::refreshSimulationTop()
{
    const auto target = simulationTargetProvider ? simulationTargetProvider() : QPair<QString, QString>();
    designModel->setSimulationTarget(target); designSearchModel->setSimulationTarget(target);
}
bool NavigationWidget::selectDesignInstance(const QString& path, bool reveal)
{
    const auto report = designModel->report();
    const auto candidates = report->nodesByPath.value(path);
    if (candidates.isEmpty()) return false;
    // A bare path is only unambiguous within its selected root. Do not guess
    // between same-named roots from different source definitions.
    int position = candidates.size() == 1 ? candidates.first() : -1;
    const auto current = designModel->node(designTreeWidget->currentIndex());
    for (int candidate : candidates)
        if (!current.rootId.isEmpty() && report->nodes[candidate].rootId == current.rootId) position = candidate;
    if (position < 0) return false;
    const auto index = designModel->indexForId(report->nodes[position].id, reveal);
    if (!index.isValid()) return false;
    if (reveal && !designSearchFilter.isEmpty()) designSearchLineEdit->clear();
    if (designTreeWidget->model() != designModel) return false;
    if (reveal) {
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) designTreeWidget->expand(parent);
        designTreeWidget->scrollTo(index); setActiveTab(DesignTab);
    }
    designTreeWidget->setCurrentIndex(index);
    return true;
}
bool NavigationWidget::selectDesignModule(const QString& module, const QString& file)
{
    const auto report = designModel->report();
    const DesignHierarchyNode* found = nullptr;
    for (const auto& node : report->nodes) {
        if (node.moduleType != module || (!file.isEmpty() && !EditorFileIdentity::same(file, node.definitionFile))) continue;
        if (!found || node.isTop) found = &node;
        if (node.isTop) break;
    }
    if (!found) return false;
    if (!designSearchFilter.isEmpty()) designSearchLineEdit->clear();
    const auto index = designModel->indexForId(found->id);
    for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) designTreeWidget->expand(parent);
    designTreeWidget->setCurrentIndex(index); designTreeWidget->scrollTo(index); setActiveTab(DesignTab);
    return index.isValid();
}
void NavigationWidget::setDesignCandidateContext(const QStringList&, bool, bool scanning)
{ designScanning = scanning; updateDesignPresentation(); }
void NavigationWidget::updateDesignCandidates(const QString&, const QList<DesignFileCandidate>&, const QStringList&)
{ updateDesignPresentation(); }
void NavigationWidget::beginDesignProjectSelection()
{ setActiveTab(DesignTab); designSearchLineEdit->setFocus(); }
void NavigationWidget::finishDesignProjectSelection(const QString& issue, const QString& file, const QString& module)
{
    if (!issue.isEmpty()) { designStateLabel->setText(issue); designStateLabel->show(); return; }
    pendingDesignFile = file; pendingDesignModule = module; revealPendingDesignProject();
}
void NavigationWidget::revealPendingDesignProject()
{
    if (pendingDesignModule.isEmpty()) return;
    if (selectDesignModule(pendingDesignModule, pendingDesignFile)) {
        pendingDesignFile.clear(); pendingDesignModule.clear();
    }
}
void NavigationWidget::startDesignSearch()
{
    if (designSearchFilter.isEmpty()) return;
    if (designSearchWatcher) { designSearchCancellation->store(true); return; }
    const auto report = designModel->report();
    const auto query = designSearchFilter;
    const auto generation = designSearchGeneration;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    designSearchCancellation = cancellation;
    auto* watcher = new QFutureWatcher<QList<QString>>(this);
    designSearchWatcher = watcher;
    designTreeWidget->setProperty("designSearchPending", true);
    connect(watcher, &QFutureWatcher<QList<QString>>::finished, this, [this, watcher, report, query, generation, cancellation] {
        const auto ids = watcher->result(); designSearchWatcher = nullptr; watcher->deleteLater();
        if (!cancellation->load() && generation == designSearchGeneration && query == designSearchFilter
            && report == designModel->report()) {
            designSearchModel->setSearchResults(report, ids);
            setDesignTreeModel(designSearchModel);
            refreshSimulationTop();
            designTreeWidget->setProperty("designSearchPending", false);
            updateDesignPresentation(); designTreeWidget->viewport()->update();
        } else if (!designSearchFilter.isEmpty()) designSearchTimer->start();
        else designTreeWidget->setProperty("designSearchPending", false);
    });
    watcher->setFuture(QtConcurrent::run([report, query, cancellation] {
        QList<QString> matches;
        for (const auto& node : report->nodes) {
            if (cancellation->load()) return QList<QString>();
            if (node.instancePath.contains(query, Qt::CaseInsensitive)
                || node.moduleType.contains(query, Qt::CaseInsensitive)
                || node.definitionFile.contains(query, Qt::CaseInsensitive)) matches.append(node.id);
        }
        return matches;
    }));
}
QJsonObject NavigationWidget::designViewState() const
{
    if (designTreeWidget->model() != designModel) return savedDesignState;
    QJsonArray expanded;
    auto expandedIds = designExpandedIds.values();
    expandedIds.sort();
    for (const auto& id : expandedIds) expanded.append(id);
    QJsonArray ancestors;
    for (auto parent=designTreeWidget->currentIndex().parent();parent.isValid();parent=parent.parent())
        ancestors.append(parent.data(Qt::UserRole).toString());
    const auto top = designTreeWidget->indexAt({4, 4});
    return {{"version", 1}, {"expanded", expanded},
        {"selected", designTreeWidget->currentIndex().data(Qt::UserRole).toString()},
        {"selectionAncestors", ancestors},
        {"scrollAnchor", top.data(Qt::UserRole).toString()},
        {"scrollOffset", top.isValid() ? designTreeWidget->visualRect(top).top() : 0},
        {"scroll", designTreeWidget->verticalScrollBar()->value()}};
}
void NavigationWidget::saveDesignViewState()
{
    if (restoringDesignState || workspaceFileTreeRootPath.isEmpty() || designModel->applying()
        || designModel->report()->nodes.isEmpty() || designTreeWidget->model() != designModel) return;
    designStateTimer->stop(); savedDesignState = designViewState();
    workspaceDesignStates.insert(workspaceFileTreeRootPath, savedDesignState);
    emit designViewStateChanged(workspaceFileTreeRootPath, savedDesignState);
}
void NavigationWidget::restoreDesignViewState(const QJsonObject& state)
{
    if (state.value("version").toInt() != 1) return;
    savedDesignState = state;
    if (designModel->applying() || designTreeWidget->model() != designModel) return;
    restoringDesignState = true;
    designExpandedIds.clear();
    for (const auto& item : state.value("expanded").toArray()) {
        const auto id = item.toString(); const auto index = designModel->indexForId(id);
        if (!index.isValid()) continue;
        designExpandedIds.insert(id); designTreeWidget->expand(index);
    }
    auto selected = designModel->indexForId(state.value("selected").toString());
    if (!selected.isValid())
        for (const auto& ancestor : state.value("selectionAncestors").toArray()) {
            selected=designModel->indexForId(ancestor.toString());
            if (selected.isValid()) break;
        }
    if (selected.isValid()) designTreeWidget->setCurrentIndex(selected);
    const auto anchor = designModel->indexForId(state.value("scrollAnchor").toString());
    if (anchor.isValid()) {
        designTreeWidget->scrollTo(anchor, QAbstractItemView::PositionAtTop);
        designTreeWidget->verticalScrollBar()->setValue(designTreeWidget->verticalScrollBar()->value()
            - state.value("scrollOffset").toInt());
    } else designTreeWidget->verticalScrollBar()->setValue(state.value("scroll").toInt());
    restoringDesignState = false;
}
void NavigationWidget::updateDesignPresentation()
{
    if (!designStateLabel) return;
    const bool empty = designTreeWidget->model()->rowCount() == 0;
    QString message;
    if (empty) {
        if (!designSearchFilter.isEmpty()) message = tr("No matches");
        else if (designScanning) message = tr("Scanning workspace…");
        else if (designAnalysisPending) message = tr("Updating design…");
        else if (workspaceFileTreeRootPath.isEmpty()) message = tr("Open a workspace to browse Design.");
        else message = tr("No module definitions available.");
    }
    designTreeWidget->setVisible(!empty);
    designStateLabel->setText(message); designStateLabel->setVisible(empty);
    designTreeWidget->setToolTip(designAnalysisPending ? tr("Updating design; the previous hierarchy remains available.") : QString());
}
