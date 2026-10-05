#include "ElaFlowLayoutPrivate.h"

#include "ElaFlowLayout.h"
#include <QDebug>
#include <QPropertyAnimation>
#include <QWidget>
#include <QAbstractButton>
ElaFlowLayoutPrivate::ElaFlowLayoutPrivate(QObject* parent)
    : QObject{parent}
{
}

ElaFlowLayoutPrivate::~ElaFlowLayoutPrivate()
{
}

namespace {
QSize preferredSize(QLayoutItem* item)
{
    QSize size = item->sizeHint().expandedTo(item->minimumSize());
    if (auto* widget = item->widget(); widget && widget->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored)
        size.setWidth(qMax(size.width(), widget->sizeHint().width()));
    return size;
}
}

void ElaFlowLayoutPrivate::_applyGeometry(QLayoutItem* item, const QRect& geometry) const
{
    auto animation = _animations.value(item);
    if (animation && _isAnimation && animation->endValue().toRect() == geometry)
        return;
    delete animation.data();
    _animations.remove(item);
    auto* widget = item->widget();
    if (!_isAnimation || !widget || !widget->isVisible() || item->geometry().isEmpty()
        || item->geometry() == geometry) {
        item->setGeometry(geometry);
        return;
    }
    auto* next = new QPropertyAnimation(widget, "geometry", const_cast<ElaFlowLayoutPrivate*>(this));
    _animations.insert(item, next);
    next->setStartValue(widget->geometry());
    next->setEndValue(geometry);
    next->setDuration(400);
    next->setEasingCurve(QEasingCurve::OutCubic);
    next->start(QAbstractAnimation::DeleteWhenStopped);
}

int ElaFlowLayoutPrivate::_doLayout(const QRect& rect, bool testOnly) const
{
    Q_Q(const ElaFlowLayout);
    const auto margins = q->contentsMargins();
    const QRect area = rect.marginsRemoved(margins);
    const int availableWidth = qMax(0, area.width());
    const int spaceX = qMax(0, q->horizontalSpacing());
    const int spaceY = qMax(0, q->verticalSpacing());
    int naturalWidth = 0, controls = 0, stretches = 0;
    for (auto* item : _itemList) {
        if (auto* spacer = item->spacerItem()) {
            if (spacer->expandingDirections().testFlag(Qt::Horizontal)) ++stretches;
            else naturalWidth += spacer->sizeHint().width();
            continue;
        }
        if (item->isEmpty()) continue;
        naturalWidth += preferredSize(item).width();
        ++controls;
    }
    naturalWidth += qMax(0, controls - 1) * spaceX;
    const int stretchWidth = stretches > 0 && naturalWidth <= availableWidth
        ? (availableWidth - naturalWidth) / stretches : 0;
    int x = area.x(), y = area.y(), lineHeight = 0;
    bool hasControl = false;
    for (auto* item : _itemList) {
        if (auto* spacer = item->spacerItem()) {
            const int width = spacer->expandingDirections().testFlag(Qt::Horizontal)
                ? stretchWidth : spacer->sizeHint().width();
            if (!testOnly) item->setGeometry(QRect(x, y, width, 0));
            x += width;
            continue;
        }
        if (item->isEmpty()) {
            if (!testOnly) { delete _animations.take(item).data(); }
            continue;
        }
        QSize size = preferredSize(item);
        auto* widget = item->widget();
        // Text fields can surrender width; a button must retain its label.
        if (!qobject_cast<QAbstractButton*>(widget)) {
            const int minimum = widget ? qMax(widget->minimumWidth(), qMax(0, widget->minimumSizeHint().width())) : item->minimumSize().width();
            size.setWidth(qMax(minimum, qMin(size.width(), availableWidth)));
        }
        if (item->hasHeightForWidth())
            size.setHeight(qMax(item->minimumSize().height(), item->heightForWidth(size.width())));
        int nextX = x + (hasControl ? spaceX : 0);
        if (hasControl && nextX + size.width() > area.x() + availableWidth) {
            x = nextX = area.x();
            y += lineHeight + spaceY;
            lineHeight = 0;
            hasControl = false;
        }
        if (!testOnly) _applyGeometry(item, QRect(QPoint(nextX, y), size));
        x = nextX + size.width();
        lineHeight = qMax(lineHeight, size.height());
        hasControl = true;
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

int ElaFlowLayoutPrivate::_smartSpacing(QStyle::PixelMetric pm) const
{
    Q_Q(const ElaFlowLayout);
    QObject* parent = q->parent();
    if (!parent)
    {
        return -1;
    }
    else if (parent->isWidgetType())
    {
        QWidget* pw = static_cast<QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    else
    {
        return static_cast<QLayout*>(parent)->spacing();
    }
}
