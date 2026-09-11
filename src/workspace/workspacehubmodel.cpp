#include "workspacehubmodel.h"

#include <QBrush>
#include <QFont>

namespace {
QString stateAwareText(const QString& title,
                       WorkspaceHubItemState state)
{
    return state == WorkspaceHubItemState::Available
        ? title
        : QStringLiteral("%1 · %2")
              .arg(title, workspaceHubStateId(state));
}

QString stateAwareDescription(const QString& summary,
                              WorkspaceHubItemState state)
{
    const QString stateText = QStringLiteral("State: %1")
        .arg(workspaceHubStateId(state));
    return summary.trimmed().isEmpty()
        ? stateText : summary + QLatin1Char('\n') + stateText;
}
}

WorkspaceHubModel::WorkspaceHubModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex WorkspaceHubModel::index(
    int row,
    int column,
    const QModelIndex& parentIndex) const
{
    if (column != 0 || row < 0)
        return {};
    if (!parentIndex.isValid()) {
        return row < snapshotValue.sections.size()
            ? createIndex(row, column, quintptr(0))
            : QModelIndex{};
    }
    if (!isSectionIndex(parentIndex))
        return {};
    const int sectionRow = parentIndex.row();
    if (sectionRow < 0 || sectionRow >= snapshotValue.sections.size()
        || row >= snapshotValue.sections.at(sectionRow).items.size()) {
        return {};
    }
    return createIndex(row, column,
                       static_cast<quintptr>(sectionRow + 1));
}

QModelIndex WorkspaceHubModel::parent(
    const QModelIndex& child) const
{
    if (!child.isValid() || isSectionIndex(child))
        return {};
    const int sectionRow = static_cast<int>(child.internalId()) - 1;
    return sectionRow >= 0 && sectionRow < snapshotValue.sections.size()
        ? createIndex(sectionRow, 0, quintptr(0))
        : QModelIndex{};
}

int WorkspaceHubModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!parentIndex.isValid())
        return snapshotValue.sections.size();
    if (!isSectionIndex(parentIndex)
        || parentIndex.row() < 0
        || parentIndex.row() >= snapshotValue.sections.size()) {
        return 0;
    }
    return snapshotValue.sections.at(parentIndex.row()).items.size();
}

int WorkspaceHubModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant WorkspaceHubModel::data(
    const QModelIndex& modelIndex,
    int role) const
{
    if (!modelIndex.isValid())
        return {};
    if (isSectionIndex(modelIndex)) {
        if (modelIndex.row() < 0
            || modelIndex.row() >= snapshotValue.sections.size()) {
            return {};
        }
        const WorkspaceHubSection& section =
            snapshotValue.sections.at(modelIndex.row());
        switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return stateAwareText(section.title, section.state);
        case Qt::ToolTipRole:
        case Qt::AccessibleDescriptionRole:
            return stateAwareDescription(
                section.statusText, section.state);
        case SummaryRole:
            return section.statusText;
        case StableKeyRole:
            return QStringLiteral("section:%1").arg(section.id);
        case SectionIdRole:
        case ProviderIdRole:
            return section.id;
        case StateRole:
            return workspaceHubStateId(section.state);
        case IsSectionRole:
            return true;
        case Qt::FontRole: {
            QFont font;
            font.setBold(true);
            return font;
        }
        default:
            return {};
        }
    }
    const WorkspaceHubItem item = itemForIndex(modelIndex);
    if (!item.isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::AccessibleTextRole:
        return stateAwareText(item.title, item.state);
    case Qt::ToolTipRole:
    case Qt::AccessibleDescriptionRole:
        return stateAwareDescription(item.summary, item.state);
    case SummaryRole:
        return item.summary;
    case StableKeyRole:
        return item.stableKey;
    case SectionIdRole:
        return sectionIdForIndex(modelIndex);
    case StateRole:
        return workspaceHubStateId(item.state);
    case ProviderIdRole:
        return item.providerId;
    case IsSectionRole:
        return false;
    default:
        return {};
    }
}

Qt::ItemFlags WorkspaceHubModel::flags(
    const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid())
        return Qt::NoItemFlags;
    if (isSectionIndex(modelIndex))
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QHash<int, QByteArray> WorkspaceHubModel::roleNames() const
{
    return {
        {StableKeyRole, QByteArrayLiteral("stableKey")},
        {SectionIdRole, QByteArrayLiteral("sectionId")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {StateRole, QByteArrayLiteral("state")},
        {ProviderIdRole, QByteArrayLiteral("providerId")},
        {IsSectionRole, QByteArrayLiteral("isSection")},
    };
}

WorkspaceHubSnapshot WorkspaceHubModel::snapshot() const
{
    return snapshotValue;
}

void WorkspaceHubModel::setSnapshot(
    const WorkspaceHubSnapshot& snapshot)
{
    beginResetModel();
    snapshotValue = snapshot;
    endResetModel();
}

WorkspaceHubItem WorkspaceHubModel::itemForIndex(
    const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid() || isSectionIndex(modelIndex))
        return {};
    const int sectionRow = static_cast<int>(modelIndex.internalId()) - 1;
    if (sectionRow < 0 || sectionRow >= snapshotValue.sections.size())
        return {};
    const WorkspaceHubSection& section =
        snapshotValue.sections.at(sectionRow);
    return modelIndex.row() >= 0 && modelIndex.row() < section.items.size()
        ? section.items.at(modelIndex.row())
        : WorkspaceHubItem{};
}

QString WorkspaceHubModel::sectionIdForIndex(
    const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid())
        return {};
    const int sectionRow = isSectionIndex(modelIndex)
        ? modelIndex.row()
        : static_cast<int>(modelIndex.internalId()) - 1;
    return sectionRow >= 0 && sectionRow < snapshotValue.sections.size()
        ? snapshotValue.sections.at(sectionRow).id
        : QString{};
}

QModelIndex WorkspaceHubModel::indexForStableKey(
    const QString& stableKey) const
{
    for (int section = 0; section < snapshotValue.sections.size(); ++section) {
        const WorkspaceHubSection& group = snapshotValue.sections.at(section);
        if (stableKey == QStringLiteral("section:%1").arg(group.id))
            return index(section, 0);
        for (int row = 0; row < group.items.size(); ++row) {
            if (group.items.at(row).stableKey == stableKey)
                return index(row, 0, index(section, 0));
        }
    }
    return {};
}

bool WorkspaceHubModel::isSectionIndex(
    const QModelIndex& modelIndex) const
{
    return modelIndex.isValid() && modelIndex.internalId() == 0;
}
