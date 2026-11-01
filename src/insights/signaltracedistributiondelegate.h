#pragma once
#include <QStyledItemDelegate>

class SignalTraceDistributionDelegate final : public QStyledItemDelegate {
public:
    explicit SignalTraceDistributionDelegate(QObject* parent=nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const override;
};
