#pragma once

#include "uicontrols.h"
#include "ElaFlowLayout.h"

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
class CompactFlowLayout : public ElaFlowLayout {
public:
    explicit CompactFlowLayout(int gap = 4) : ElaFlowLayout(0) { setSpacing(gap); }
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
