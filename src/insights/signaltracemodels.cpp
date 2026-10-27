#include "signaltracemodels.h"
#include <QApplication>
#include <QBrush>
#include <QFileInfo>
#include <QPainter>
#include <QSet>
#include <QStyle>
#include <algorithm>

QVariant SignalTraceHeatModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole) return {};
    if (orientation==Qt::Vertical) return section+1;
    if(section==Total && !completeValue)return QStringLiteral("Loaded total");
    static const QStringList names{"File","Module","Write","Read","Port","Control","Clock / reset","Unknown","Total","File distribution"};
    return names.value(section);
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
    beginResetModel(); rows.clear(); maximum=0; QHash<QString,int> indexes;
    for (const auto& item:items) {
        if (!(filterMask & (1<<int(item.role)))) continue;
        if (!filter.isEmpty() && !(item.range.fileName+item.moduleName+item.text+SignalTraceService::roleName(item.role)).contains(filter,Qt::CaseInsensitive)) continue;
        const QString key=item.range.fileName+QLatin1Char('\n')+item.moduleName;
        if (!indexes.contains(key)) { indexes[key]=rows.size(); rows.append({item.range.fileName,item.moduleName,{},{},true}); }
        auto& row=rows[indexes[key]]; row.items.append(item); ++row.counts[int(item.role)];
        row.available &= !item.sourceHash.isEmpty() && item.fileLineCount>0;
    }
    for (const auto& row:rows) for (int count:row.counts) maximum=qMax(maximum,count);
    std::stable_sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b) {
        int compared=0;
        if (sortColumn==File || sortColumn==Distribution) compared=QString::compare(a.file,b.file,Qt::CaseInsensitive);
        else if (sortColumn==Module) compared=QString::compare(a.module,b.module,Qt::CaseInsensitive);
        else compared=(sortColumn==Total ? a.items.size() : a.counts[sortColumn-Write])
            -(sortColumn==Total ? b.items.size() : b.counts[sortColumn-Write]);
        if (!compared) compared=QString::compare(a.file+a.module,b.file+b.module);
        return sortOrder==Qt::AscendingOrder ? compared<0 : compared>0;
    });
    endResetModel();
}
QList<SignalTraceEvidence> SignalTraceHeatModel::references(const QModelIndex& index) const {
    QList<SignalTraceEvidence> result;
    for (int row=0;row<rows.size();++row) {
        if (index.isValid() && row!=index.row()) continue;
        for (const auto& item:rows[row].items) {
            if (index.isValid() && index.column()>=Write && index.column()<=Unknown && int(item.role)!=index.column()-Write) continue;
            result.append(item);
        }
    }
    return result;
}
QByteArray SignalTraceHeatModel::csv() const {
    auto escape=[](QString value) { value.replace('"',QStringLiteral("\"\"")); return QLatin1Char('"')+value+QLatin1Char('"'); };
    QStringList lines; QStringList headings;
    for (int c=0;c<Distribution;++c) headings<<escape(headerData(c,Qt::Horizontal).toString());
    headings<<escape(QStringLiteral("Coverage"));
    lines<<headings.join(',');
    for (int r=0;r<rows.size();++r) {
        QStringList values;
        for (int c=0;c<Distribution;++c) values<<escape(c==File ? rows[r].file : data(index(r,c)).toString());
        values<<escape(completeValue ? QStringLiteral("Complete") : QStringLiteral("Partial: loaded reference pages only"));
        lines<<values.join(',');
    }
    return (lines.join(QStringLiteral("\r\n"))+QStringLiteral("\r\n")).toUtf8();
}
void SignalTraceDistributionDelegate::paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const {
    QStyledItemDelegate::paint(painter,option,index);
    const auto* model=dynamic_cast<const SignalTraceHeatModel*>(index.model());
    if (!model) return;
    const auto items=model->references(index);
    const qreal margin=option.fontMetrics.height()*0.45;
    const QRectF rect=QRectF(option.rect).adjusted(margin,margin,-margin,-margin);
    painter->save(); painter->setPen(option.palette.color(QPalette::Mid)); painter->drawLine(rect.left(),rect.center().y(),rect.right(),rect.center().y());
    painter->setPen(QPen(option.palette.color(QPalette::Highlight),qMax(1.0,option.fontMetrics.height()/9.0)));
    for (const auto& item:items) {
        if (item.fileLineCount<1) continue;
        const qreal x=rect.left()+rect.width()*qBound(0.0,double(item.range.line-1)/qMax(1,item.fileLineCount-1),1.0);
        painter->drawLine(QPointF(x,rect.top()),QPointF(x,rect.bottom()));
    }
    painter->restore();
}
QSize SignalTraceDistributionDelegate::sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const {
    auto size=QStyledItemDelegate::sizeHint(option,index); size.setHeight(qMax(size.height(),option.fontMetrics.height()*2)); return size;
}
