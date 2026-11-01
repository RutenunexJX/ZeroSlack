#include "signaltracemodels.h"
#include <QApplication>
#include <QBrush>
#include <QFileInfo>
#include "insightgraphadapter.h"
#include <QSet>
#include <QStyle>
#include <algorithm>

QVariant SignalTraceHeatModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole) return {};
    if (orientation==Qt::Vertical) return section+1;
    return insightgraph::heatHeading(section, completeValue);
}
QVariant SignalTraceHeatModel::data(const QModelIndex& index,int role) const {
    if (!index.isValid() || index.row()>=rows.size()) return {};
    const auto& row=rows[index.row()]; const int column=index.column();
    if (role==Qt::DisplayRole) {
        if (column==File) return QFileInfo(row.file).fileName();
        if (column==Module) return row.module;
        if (column>=Write && column<=Total) {
            if (!row.available) return QStringLiteral("—");
            return column==Total ? row.items.size() : row.counts[column-Write];
        }
    }
    if (role==Qt::ToolTipRole) return row.file+QStringLiteral("\nUnique source events: %1").arg(row.items.size());
    if (role==Qt::TextAlignmentRole && column>=Write && column<=Total) return int(Qt::AlignCenter);
    if (role==Qt::BackgroundRole && column>=Write && column<=Unknown && maximum && row.counts[column-Write]) {
        QColor color=QApplication::palette().color(QPalette::Highlight);
        color.setAlphaF(0.12+0.50*double(row.counts[column-Write])/maximum); return QBrush(color);
    }
    return {};
}
void SignalTraceHeatModel::setReferences(const QList<SignalTraceEvidence>& references) { items.clear(); appendReferences(references); }
void SignalTraceHeatModel::appendReferences(const QList<SignalTraceEvidence>& references) {
    QSet<QString> ids; for (const auto& item:items) ids.insert(item.id);
    for (const auto& item:references) if (!ids.contains(item.id)) { ids.insert(item.id); items.append(item); }
    rebuild();
}
void SignalTraceHeatModel::setFilter(const QString& text,int role) { setRoleMaskFilter(text,role<0?63:(1<<role)); }
void SignalTraceHeatModel::setRoleMaskFilter(const QString& text,int mask) { filter=text.trimmed();filterMask=mask&63;rebuild(); }
void SignalTraceHeatModel::sort(int column,Qt::SortOrder order) { if(column<0 || column>=Columns)return;sortColumn=column; sortOrder=order; rebuild(); }
void SignalTraceHeatModel::rebuild() {
    beginResetModel();
    auto projection = insightgraph::projectHeatmap(InsightGraphAdapter::heat(items),
        filter, filterMask, sortColumn, sortOrder);
    rows = std::move(projection.rows);
    maximum = projection.maximum;
    endResetModel();
}
QList<SignalTraceEvidence> SignalTraceHeatModel::references(const QModelIndex& index) const {
    QList<SignalTraceEvidence> result;
    for (int row=0;row<rows.size();++row) {
        if (index.isValid() && row!=index.row()) continue;
        for (int itemIndex:rows[row].items) {
            const auto& item = items.at(itemIndex);
            if (index.isValid() && index.column()>=Write && index.column()<=Unknown && int(item.role)!=index.column()-Write) continue;
            result.append(item);
        }
    }
    return result;
}
QByteArray SignalTraceHeatModel::csv() const {
    return insightgraph::heatCsv({rows, maximum}, completeValue);
}
QList<qreal> SignalTraceHeatModel::distribution(const QModelIndex& index) const {
    return insightgraph::distribution(InsightGraphAdapter::heat(references(index)));
}
