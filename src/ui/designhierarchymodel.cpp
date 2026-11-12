#include "designhierarchymodel.h"
#include "navigationrowstyle.h"
#include "editorfileidentity.h"
#include <QElapsedTimer>

DesignHierarchyModel::DesignHierarchyModel(QObject* parent) : QAbstractItemModel(parent)
{
    m_report = std::make_shared<DesignHierarchyReport>();
    m_apply.setInterval(0);
    connect(&m_apply, &QTimer::timeout, this, &DesignHierarchyModel::applyChunk);
}
DesignHierarchyModel::Row* DesignHierarchyModel::rowFor(const QModelIndex& index) const
{ return index.isValid() ? static_cast<Row*>(index.internalPointer()) : const_cast<Row*>(&m_root); }
QModelIndex DesignHierarchyModel::indexFor(Row* row) const
{ return row && row != &m_root ? createIndex(row->row, 0, row) : QModelIndex(); }
QModelIndex DesignHierarchyModel::index(int row, int column, const QModelIndex& parent) const
{
    auto* item = rowFor(parent);
    if (column || row < 0 || row >= int(item->children.size())) return {};
    return createIndex(row, 0, item->children[row].get());
}
QModelIndex DesignHierarchyModel::parent(const QModelIndex& index) const
{ return index.isValid() ? indexFor(rowFor(index)->parent) : QModelIndex(); }
int DesignHierarchyModel::rowCount(const QModelIndex& parent) const
{ return parent.column() > 0 ? 0 : int(rowFor(parent)->children.size()); }
const QList<int>& DesignHierarchyModel::children(const Row* row) const
{
    static const QList<int> empty;
    if (m_search) return row == &m_root ? m_matches : empty;
    const auto found = m_report->childrenById.constFind(row->id);
    return found == m_report->childrenById.cend() ? empty : found.value();
}
bool DesignHierarchyModel::hasChildren(const QModelIndex& parent) const
{ return !children(rowFor(parent)).isEmpty(); }
bool DesignHierarchyModel::canFetchMore(const QModelIndex& parent) const
{ return rowCount(parent) < children(rowFor(parent)).size(); }
void DesignHierarchyModel::fetchMore(const QModelIndex& parent)
{
    auto* row = rowFor(parent);
    const auto& values = children(row);
    const int first = int(row->children.size()), last = qMin(first + 256, int(values.size())) - 1;
    if (last < first) return;
    beginInsertRows(parent, first, last);
    for (int i = first; i <= last; ++i) {
        auto child = std::make_unique<Row>();
        child->id = m_report->nodes[values[i]].id; child->parent = row; child->row = i;
        m_rows.insert(child->id, child.get()); row->children.push_back(std::move(child));
    }
    endInsertRows();
}
DesignHierarchyNode DesignHierarchyModel::node(const QModelIndex& index) const
{
    if (!index.isValid()) return {};
    const int position = m_report->nodeById.value(rowFor(index)->id, -1);
    return position >= 0 ? m_report->nodes[position] : DesignHierarchyNode();
}
QVariant DesignHierarchyModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const auto value = node(index);
    if (value.id.isEmpty()) return {};
    QString name = value.instanceName.isEmpty() ? QStringLiteral("<unnamed>") : value.instanceName;
    const auto type = value.isTop && name == value.moduleType ? QString() : value.moduleType;
    if (m_search) name = value.instancePath;
    const bool simulation = m_simulation.second == value.moduleType && !m_simulation.second.isEmpty()
        && EditorFileIdentity::same(m_simulation.first, value.definitionFile);
    switch (role) {
    case Qt::DisplayRole: case Qt::AccessibleTextRole: return name + (type.isEmpty() ? QString() : " [" + type + ']');
    case Qt::UserRole: return value.id;
    case NavigationRowStyle::NameRole: return name;
    case NavigationRowStyle::TypeRole: return type;
    case NavigationRowStyle::UnresolvedRole: return value.unresolved || value.status.contains("incomplete", Qt::CaseInsensitive);
    case NavigationRowStyle::SimulationRole: return simulation;
    case Qt::DecorationRole: return RoundedIcons::icon(value.isTop ? RoundedIcons::Module : RoundedIcons::Hierarchy);
    case Qt::ToolTipRole: case Qt::AccessibleDescriptionRole:
        return value.instancePath + '\n' + value.definitionFile + ':' + QString::number(value.definitionLine)
            + (value.unresolved ? '\n' + value.unresolvedReason : QString())
            + (value.status.isEmpty() ? QString() : '\n' + value.status)
            + (simulation ? QStringLiteral("\nSimulation top") : QString());
    default: return {};
    }
}
void DesignHierarchyModel::forget(Row* row)
{
    for (const auto& child : row->children) forget(child.get());
    m_rows.remove(row->id);
}
void DesignHierarchyModel::setReport(const DesignHierarchyReport& report)
{
    const bool same = !report.structureFingerprint.isEmpty()
        && report.structureFingerprint == m_report->structureFingerprint;
    auto next = std::make_shared<DesignHierarchyReport>(report);
    // Compatibility for direct service/test consumers that supply only nodes.
    if (next->nodeById.isEmpty() && !next->nodes.isEmpty()) {
        for (int i = 0; i < next->nodes.size(); ++i) {
            const auto& node = next->nodes[i]; next->nodeById.insert(node.id, i);
            next->childrenById[node.parentId].append(i); next->nodesByPath[node.instancePath].append(i);
        }
    }
    m_exposed.clear();
    const auto exposure = [&](Row* row) {
        const auto& before = children(row);
        const auto& after = next->childrenById.value(row->id);
        int count = qMin(int(row->children.size()), int(after.size()));
        if (row->children.size() == size_t(before.size())) count = int(after.size());
        else for (int i = 0; i < after.size(); ++i)
            if (auto* retained = m_rows.value(next->nodes[after[i]].id); retained && retained->parent == row)
                count = qMax(count, i + 1);
        m_exposed.insert(row->id, count);
    };
    exposure(&m_root);
    for (auto* row : m_rows) if (!row->children.empty()) exposure(row);
    m_report = std::move(next);
    m_pending.clear(); m_pending.append(QString());
    if (!same) {
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
            if (!it.value()->children.empty()) m_pending.append(it.key());
    }
    m_apply.start();
    if (same) {
        // Positions/status changed; retained row identity and expansion stay
        // intact. Painting queries the new immutable publication on demand.
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
            auto* row = it.value();
            if (!row->children.empty()) emit dataChanged(index(0, 0, indexFor(row)), index(int(row->children.size())-1, 0, indexFor(row)));
        }
    }
}
bool DesignHierarchyModel::syncBranch(Row* row)
{
    const auto& desired = children(row);
    const int exposed = int(row->children.size());
    const int count = row == &m_root && exposed == 0 ? qMin(256, int(desired.size()))
        : qMin(m_exposed.value(row->id, exposed), int(desired.size()));
    const auto parent = indexFor(row);
    QElapsedTimer budget; budget.start();
    for (int i = 0; i < count; ++i) {
        const auto id = m_report->nodes[desired[i]].id;
        if (i < int(row->children.size()) && row->children[i]->id == id) continue;
        int existing = -1;
        if (auto* known = m_rows.value(id); known && known->parent == row) existing = known->row;
        if (existing >= 0) {
            beginMoveRows(parent, existing, existing, parent, i);
            auto item = std::move(row->children[existing]);
            row->children.erase(row->children.begin() + existing);
            row->children.insert(row->children.begin() + i, std::move(item));
            endMoveRows();
        } else {
            beginInsertRows(parent, i, i);
            auto item = std::make_unique<Row>(); item->id = id; item->parent = row; item->row = i;
            m_rows.insert(id, item.get()); row->children.insert(row->children.begin() + i, std::move(item));
            endInsertRows();
        }
        for (int j = i; j < int(row->children.size()); ++j) row->children[j]->row = j;
        if (budget.elapsed() >= 4) return false;
    }
    if (int(row->children.size()) > count) {
        beginRemoveRows(parent, count, int(row->children.size())-1);
        while (int(row->children.size()) > count) { forget(row->children.back().get()); row->children.pop_back(); }
        endRemoveRows();
    }
    if (count) emit dataChanged(index(0, 0, parent), index(count-1, 0, parent));
    return true;
}
void DesignHierarchyModel::applyChunk()
{
    QElapsedTimer budget; budget.start();
    while (!m_pending.isEmpty() && budget.elapsed() < 4) {
        const auto id = m_pending.first();
        auto* row = id.isEmpty() ? &m_root : m_rows.value(id);
        if (!row || syncBranch(row)) m_pending.removeFirst();
        else break;
    }
    if (m_pending.isEmpty()) { m_apply.stop(); emit reportApplied(); }
}
void DesignHierarchyModel::setSearchResults(std::shared_ptr<const DesignHierarchyReport> report, const QList<QString>& ids)
{
    beginResetModel();
    m_apply.stop(); m_pending.clear(); m_rows.clear(); m_root.children.clear();
    m_report = std::move(report); m_search = true; m_matches.clear();
    for (const auto& id : ids) {
        const int found = m_report->nodeById.value(id, -1);
        if (found >= 0) m_matches.append(found);
    }
    endResetModel();
    fetchMore({});
}
QModelIndex DesignHierarchyModel::indexForId(const QString& id, bool reveal)
{
    if (auto* row = m_rows.value(id)) return indexFor(row);
    const int position = m_report->nodeById.value(id, -1);
    if (!reveal || position < 0) return {};
    const auto& value = m_report->nodes[position];
    const auto parent = value.parentId.isEmpty() ? QModelIndex() : indexForId(value.parentId);
    if (!value.parentId.isEmpty() && !parent.isValid()) return {};
    while (!m_rows.contains(id) && canFetchMore(parent)) fetchMore(parent);
    return indexFor(m_rows.value(id));
}
void DesignHierarchyModel::setSimulationTarget(const QPair<QString, QString>& target)
{
    if (m_simulation == target) return;
    m_simulation = target;
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        const auto index = indexFor(it.value()); emit dataChanged(index, index, {NavigationRowStyle::SimulationRole, Qt::ToolTipRole});
    }
}
