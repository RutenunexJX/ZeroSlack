#pragma once

#include "uicontrols.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyleOption>
#include <QResizeEvent>
#include <QLineEdit>
#include <QAbstractButton>
#include <QScrollArea>

class CompactToolbar : public QWidget {
public:
    using QWidget::QWidget;
protected:
    bool event(QEvent* event) override {
        const bool result=QWidget::event(event);
        if (isVisible() && (event->type()==QEvent::Resize || event->type()==QEvent::LayoutRequest || event->type()==QEvent::Show)) {
            if (layout()) {
                const int height=layout()->heightForWidth(width());
                if (height>=0 && (minimumHeight()!=height || maximumHeight()!=height)) setFixedHeight(height);
            }
        }
        return result;
    }
};

// A toolbar wraps at control boundaries instead of shrinking button labels.
class CompactFlowLayout : public QLayout {
public:
    explicit CompactFlowLayout(int gap = 4) { setContentsMargins(0,0,0,0); setSpacing(gap); }
    ~CompactFlowLayout() override { while (auto* item=takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items.append(item); }
    int count() const override { return items.size(); }
    QLayoutItem* itemAt(int i) const override { return items.value(i); }
    QLayoutItem* takeAt(int i) override { return i>=0 && i<items.size() ? items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0,0,width,0),false); }
    int minimumHeightForWidth(int width) const override { return heightForWidth(width); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item:items) if (!item->isEmpty()) size=size.expandedTo(item->minimumSize());
        const auto m=contentsMargins();
        return size+QSize(m.left()+m.right(),m.top()+m.bottom());
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect,true); }
    static void replaceRows(QVBoxLayout* root) {
        if (!root) return;
        for (int i=0;i<root->count();++i) {
            auto* row=qobject_cast<QHBoxLayout*>(root->itemAt(i)->layout());
            if (!row) continue;
            root->takeAt(i);
            auto* host=new CompactToolbar(root->parentWidget());
            host->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
            auto* flow=new CompactFlowLayout(row->spacing());
            flow->setContentsMargins(row->contentsMargins());
            host->setLayout(flow);
            while (auto* item=row->takeAt(0)) {
                if (auto* widget=item->widget()) {
                    const bool hidden=widget->isHidden();
                    // Search inputs may give up width; action buttons may not.
                    if (qobject_cast<QLineEdit*>(widget)) widget->setMinimumWidth(0);
                    widget->setParent(host);
                    flow->addWidget(widget);
                    widget->setVisible(!hidden);
                    delete item;
                } else flow->addItem(item);
            }
            delete row;
            root->insertWidget(i,host);
        }
    }
    static void makeScrollable(QWidget* panel) {
        auto* content = new QWidget;
        content->setObjectName(panel->objectName() + QStringLiteral("Content"));
        content->setLayout(panel->layout());
        auto* scroll = UiControls::scrollArea(panel);
        scroll->setObjectName(panel->objectName() + QStringLiteral("Scroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        content->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(scroll);
    }
private:
    QList<QLayoutItem*> items;
    int arrange(const QRect& rect,bool apply) const {
        const auto m=contentsMargins();
        const QRect area=rect.marginsRemoved(m);
        int naturalWidth=0, controls=0, stretches=0;
        for (auto* item:items) {
            if (item->spacerItem()) { ++stretches; continue; }
            if (item->isEmpty()) continue;
            naturalWidth+=preferredSize(item).width();
            ++controls;
        }
        naturalWidth+=qMax(0,controls-1)*spacing();
        // Preserve the original alignment when the complete row fits.
        const int stretchWidth=stretches>0 && naturalWidth<=area.width()
            ? (area.width()-naturalWidth)/stretches : 0;
        int x=area.x(), y=area.y(), lineHeight=0;
        for (auto* item:items) {
            if (item->spacerItem()) { x+=stretchWidth; continue; }
            if (item->isEmpty()) continue;
            QSize size=preferredSize(item);
            auto* widget = item->widget();
            if (!qobject_cast<QAbstractButton*>(widget)) {
                const int minimum = widget ? qMax(0, widget->minimumSizeHint().width()) : 0;
                size.setWidth(qMax(minimum, qMin(size.width(), qMax(0, area.width()))));
            }
            if (x>area.x() && x+size.width()>area.x()+area.width()) {
                x=area.x(); y+=lineHeight+spacing(); lineHeight=0;
            }
            if (apply) item->setGeometry(QRect(QPoint(x,y),size));
            x+=size.width()+spacing(); lineHeight=qMax(lineHeight,size.height());
        }
        return y+lineHeight-rect.y()+m.bottom();
    }
    static QSize preferredSize(QLayoutItem* item) {
        QSize size=item->sizeHint().expandedTo(item->minimumSize());
        if (auto* widget=item->widget(); widget && widget->sizePolicy().horizontalPolicy()==QSizePolicy::Ignored)
            size.setWidth(qMax(size.width(),widget->sizeHint().width()));
        return size;
    }
};

class CompactTitleLabel : public QLabel {
public:
    explicit CompactTitleLabel(QWidget* parent=nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    }
    QSize minimumSizeHint() const override { return QSize(0,QLabel::minimumSizeHint().height()); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QStyleOption option; option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget,&option,&painter,this);
        const QRect area=contentsRect();
        const QString shown=fontMetrics().elidedText(text(),Qt::ElideRight,area.width());
        setToolTip(text());
        style()->drawItemText(&painter,area,alignment(),palette(),isEnabled(),shown,foregroundRole());
    }
};
