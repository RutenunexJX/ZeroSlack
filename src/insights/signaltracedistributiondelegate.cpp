#include "signaltracedistributiondelegate.h"
#include "signaltracemodels.h"
#include <QPainter>

void SignalTraceDistributionDelegate::paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const {
    QStyledItemDelegate::paint(painter,option,index);
    const auto* model=dynamic_cast<const SignalTraceHeatModel*>(index.model());
    if (!model) return;
    const auto positions=model->distribution(index);
    const qreal margin=option.fontMetrics.height()*0.45;
    const QRectF rect=QRectF(option.rect).adjusted(margin,margin,-margin,-margin);
    painter->save(); painter->setPen(option.palette.color(QPalette::Mid)); painter->drawLine(rect.left(),rect.center().y(),rect.right(),rect.center().y());
    painter->setPen(QPen(option.palette.color(QPalette::Highlight),qMax(1.0,option.fontMetrics.height()/9.0)));
    for (qreal position:positions) {
        const qreal x=rect.left()+rect.width()*position;
        painter->drawLine(QPointF(x,rect.top()),QPointF(x,rect.bottom()));
    }
    painter->restore();
}
QSize SignalTraceDistributionDelegate::sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const {
    auto size=QStyledItemDelegate::sizeHint(option,index); size.setHeight(qMax(size.height(),option.fontMetrics.height()*2)); return size;
}
