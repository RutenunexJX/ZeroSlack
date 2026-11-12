#pragma once

#include "uicontrols.h"
#include "ElaFlowLayout.h"
#include "roundedicons.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QStyleOption>
#include <QResizeEvent>
#include <QLineEdit>
#include <QAbstractButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>

// Keep the original controls and connections when a toolbar runs out of room.
class CompactOverflowToolbar : public QWidget {
public:
    CompactOverflowToolbar(QHBoxLayout* source, const QString& name,
                           QWidget* parent, QToolButton* existingMore = nullptr)
        : QWidget(parent), alwaysMore(existingMore != nullptr) {
        setObjectName(name + QStringLiteral("Toolbar"));
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->setSizeConstraint(QLayout::SetNoConstraint);
        more = existingMore ? existingMore : UiControls::toolButton(this);
        if (!existingMore) {
            more->setObjectName(name + QStringLiteral("More"));
            more->setText(QStringLiteral("More"));
            more->setIcon(RoundedIcons::icon(RoundedIcons::More));
            more->setToolButtonStyle(Qt::ToolButtonIconOnly);
            more->setToolTip(QStringLiteral("More actions and options"));
            more->setAccessibleName(more->toolTip());
            more->setMenu(UiControls::menu(more));
        }
        more->setPopupMode(QToolButton::InstantPopup);
        menu = more->menu();
        menu->installEventFilter(this);
        popup = new QWidget;
        popup->setObjectName(name + QStringLiteral("OverflowContent"));
        popupLayout = new QVBoxLayout(popup);
        popupLayout->setContentsMargins(6, 6, 6, 6);
        popupLayout->setSpacing(4);
        scroll = UiControls::scrollArea(menu);
        scroll->setObjectName(name + QStringLiteral("OverflowScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(popup);
        overflow = new QWidgetAction(menu);
        overflow->setObjectName(name + QStringLiteral("Overflow"));
        overflow->setDefaultWidget(scroll);
        menu->insertAction(menu->actions().value(0), overflow);
        while (source->count()) {
            const int stretch = source->stretch(0);
            auto* item = source->takeAt(0);
            if (auto* widget = item->widget(); widget && widget != more) {
                entries.append({widget, stretch,
                    !widget->testAttribute(Qt::WA_WState_ExplicitShowHide) || !widget->isHidden(), false});
                widget->installEventFilter(this);
                if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                    const QString description = combo->toolTip();
                    const auto updateTip = [combo, description] {
                        combo->setToolTip(description.isEmpty() ? combo->currentText()
                            : description + QLatin1Char('\n') + combo->currentText());
                    };
                    connect(combo, &QComboBox::currentTextChanged, this, updateTip);
                    updateTip();
                }
                row->addWidget(widget, qobject_cast<QLineEdit*>(widget) ? 1 : stretch);
            }
            delete item;
        }
        delete source;
        row->addWidget(more);
        connect(menu, &QMenu::aboutToShow, this, [this] { sizePopup(); });
        arranging = false;
    }
    void addOverflowWidget(QWidget* widget) {
        entries.append({widget, 0,
            !widget->testAttribute(Qt::WA_WState_ExplicitShowHide) || !widget->isHidden(), true});
        widget->installEventFilter(this);
        schedule();
    }
    void insertInlineWidget(QWidget* widget, int index = -1) {
        if (index < 0) index = entries.size();
        index = qMin(index, int(entries.size()));
        entries.insert(index, {widget, 0,
            !widget->testAttribute(Qt::WA_WState_ExplicitShowHide) || !widget->isHidden(), false});
        widget->installEventFilter(this);
        row->insertWidget(index, widget);
        schedule();
    }
    QSize minimumSizeHint() const override { return more->minimumSizeHint(); }
protected:
    bool event(QEvent* event) override {
        const bool result = QWidget::event(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest) schedule();
        return result;
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == menu && (event->type() == QEvent::Show
            || (event->type() == QEvent::Resize && menu->isVisible()))) {
            const QRect area = more->screen()->availableGeometry();
            const QRect frame = menu->frameGeometry();
            const QPoint bounded(qBound(area.left(), frame.left(), qMax(area.left(), area.right() - frame.width() + 1)),
                                 qBound(area.top(), frame.top(), qMax(area.top(), area.bottom() - frame.height() + 1)));
            if (bounded != frame.topLeft()) menu->move(menu->pos() + bounded - frame.topLeft());
        }
        if (!arranging && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)) {
            for (auto& entry : entries) if (entry.widget == watched) entry.wanted = !entry.widget->isHidden();
            schedule();
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    struct Entry { QWidget* widget; int stretch; bool wanted; bool popupOnly; };
    QList<Entry> entries;
    QHBoxLayout* row;
    QVBoxLayout* popupLayout;
    QWidget* popup;
    QScrollArea* scroll;
    QToolButton* more;
    QMenu* menu;
    QWidgetAction* overflow;
    QPointer<QWidget> movedFocus;
    bool alwaysMore = false, arranging = true, pending = false;
    static int controlWidth(QWidget* widget) {
        return qMax(widget->minimumWidth(), qobject_cast<QLineEdit*>(widget)
            ? widget->minimumSizeHint().width()
            : qMax(widget->minimumSizeHint().width(), widget->sizeHint().width()));
    }
    void schedule() {
        if (arranging || pending) return;
        pending = true;
        QTimer::singleShot(0, this, [this] { pending = false; arrange(); });
    }
    void arrange() {
        if (arranging) return;
        const QScopedValueRollback guard(arranging, true);
        int required = 0, count = 0;
        bool popupOnly = false;
        for (const auto& entry : entries) if (entry.wanted) {
            popupOnly |= entry.popupOnly;
            if (!entry.popupOnly) { required += controlWidth(entry.widget); ++count; }
        }
        required += qMax(0, count - 1) * row->spacing();
        if (alwaysMore) required += controlWidth(more) + row->spacing();
        const bool needsMore = alwaysMore || popupOnly || required > width();
        int available = width() - (needsMore ? controlWidth(more) + row->spacing() : 0);
        bool spill = false, hasOverflow = false;
        int inlineCount = 0, inlineIndex = 0, popupIndex = 0;
        for (auto& entry : entries) {
            auto* widget = entry.widget;
            const int needed = controlWidth(widget) + (inlineCount ? row->spacing() : 0);
            if (entry.wanted && !entry.popupOnly && needed > available) spill = true;
            const bool inPopup = entry.popupOnly || spill;
            const bool moving = widget->parentWidget() != (inPopup ? popup : this);
            const bool focused = widget == QApplication::focusWidget() || widget->isAncestorOf(QApplication::focusWidget());
            const int index = inPopup ? popupIndex++ : inlineIndex++;
            auto* destination = inPopup ? static_cast<QBoxLayout*>(popupLayout) : row;
            if (moving || destination->indexOf(widget) != index) {
                row->removeWidget(widget); popupLayout->removeWidget(widget);
                if (inPopup) popupLayout->insertWidget(index, widget);
                else row->insertWidget(index, widget, qobject_cast<QLineEdit*>(widget) ? 1 : entry.stretch);
            }
            if (moving) {
                widget->setVisible(entry.wanted);
                if (focused && inPopup) { movedFocus = widget; more->setFocus(Qt::OtherFocusReason); }
                else if (!inPopup && movedFocus == widget && more->hasFocus()) { widget->setFocus(Qt::OtherFocusReason); movedFocus.clear(); }
            }
            if (!entry.wanted) continue;
            if (inPopup) hasOverflow = true;
            else { available -= needed; ++inlineCount; }
        }
        more->setVisible(needsMore);
        overflow->setVisible(hasOverflow);
        const int height = row->sizeHint().height();
        if (height > 0 && minimumHeight() != height) setFixedHeight(height);
        // Reparenting can lay out buttons while More is still visible. Commit
        // the final visibility before returning from the owner arrangement.
        row->activate();
        if (needsMore) sizePopup();
    }
    void sizePopup() {
        popupLayout->activate();
        const QRect screen = more->screen()->availableGeometry();
        const QSize hint = popupLayout->sizeHint();
        scroll->setFixedSize(qMin(screen.width() - 32, hint.width() + 24),
            qMin(qMax(100, screen.height() / 2), hint.height() + 4));
    }
};

class CompactResponsiveSplitter : public QSplitter {
public:
    CompactResponsiveSplitter(int horizontalMinimum, QWidget* parent)
        : QSplitter(Qt::Horizontal, parent), horizontalMinimum(horizontalMinimum) { setChildrenCollapsible(false); }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QSplitter::resizeEvent(event);
        const auto desired = width() < horizontalMinimum ? Qt::Vertical : Qt::Horizontal;
        if (orientation() != desired) setOrientation(desired);
    }
private:
    int horizontalMinimum;
};

class CompactPreviewSection : public QWidget {
public:
    CompactPreviewSection(const QString& title, QWidget* body, QWidget* parent)
        : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        toggle = UiControls::toolButton(this);
        toggle->setObjectName(body->objectName() + QStringLiteral("Toggle"));
        toggle->setText(title);
        toggle->setToolTip(title);
        toggle->setAccessibleName(title);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setCheckable(true);
        layout->addWidget(toggle, 0, Qt::AlignLeft);
        area = UiControls::scrollArea(this);
        area->setObjectName(body->objectName() + QStringLiteral("Scroll"));
        area->setFrameShape(QFrame::NoFrame);
        area->setWidgetResizable(true);
        area->setWidget(body);
        layout->addWidget(area, 1);
        area->hide();
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(toggle, &QToolButton::toggled, this, [this](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            area->setVisible(expanded);
            setSizePolicy(QSizePolicy::Ignored, expanded ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        });
    }
    QToolButton* toggleButton() const { return toggle; }
private:
    QToolButton* toggle;
    QScrollArea* area;
};

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
    void setElideMode(Qt::TextElideMode mode) { elideMode = mode; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QStyleOption option; option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget,&option,&painter,this);
        const QRect area=contentsRect();
        const QString shown=fontMetrics().elidedText(text(),elideMode,area.width());
        setToolTip(text());
        style()->drawItemText(&painter,area,alignment(),palette(),isEnabled(),shown,foregroundRole());
    }
private:
    Qt::TextElideMode elideMode = Qt::ElideRight;
};
