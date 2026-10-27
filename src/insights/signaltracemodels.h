#pragma once
#include "zeroslackexport.h"
#include <zeroslack/semantic/signaltraceservice.h>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>

class ZEROSLACK_API SignalTraceHeatModel final : public QAbstractTableModel {
public:
    enum Column { File, Module, Write, Read, Port, Control, Clock, Unknown, Total, Distribution, Columns };
    explicit SignalTraceHeatModel(QObject* parent=nullptr) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex& parent={}) const override { return parent.isValid() ? 0 : rows.size(); }
    int columnCount(const QModelIndex& parent={}) const override { return parent.isValid() ? 0 : Columns; }
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole) const override;
    QVariant headerData(int section,Qt::Orientation orientation,int role=Qt::DisplayRole) const override;
    void sort(int column,Qt::SortOrder order=Qt::AscendingOrder) override;
    void setReferences(const QList<SignalTraceEvidence>& references);
    void appendReferences(const QList<SignalTraceEvidence>& references);
    void setFilter(const QString& text,int role=-1);
    void setRoleMaskFilter(const QString& text,int mask);
    QList<SignalTraceEvidence> references(const QModelIndex& index={}) const;
    QByteArray csv() const;
    int maximumCount() const { return maximum; }
    void setComplete(bool complete) { completeValue=complete;emit headerDataChanged(Qt::Horizontal,Total,Total); }
    bool isComplete()const{return completeValue;}
private:
    struct Row { QString file,module; QList<SignalTraceEvidence> items; int counts[6]={}; bool available=true; };
    QList<SignalTraceEvidence> items;
    QList<Row> rows;
    QString filter;
    int sortColumn=File,maximum=0;
    int filterMask=63;
    bool completeValue=true;
    Qt::SortOrder sortOrder=Qt::AscendingOrder;
    void rebuild();
};

class SignalTraceDistributionDelegate final : public QStyledItemDelegate {
public:
    explicit SignalTraceDistributionDelegate(QObject* parent=nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const override;
};
