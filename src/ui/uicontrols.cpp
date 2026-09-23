#include "uicontrols.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "uitypography.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QApplication>
#include <QListWidget>
#include <QListView>
#include <QTableWidget>
#include <QMenu>
#include <QMenuBar>
#include <QHeaderView>
#include <QWheelEvent>
#include <QPainter>
#include <QStyleOptionToolButton>
#include <QContextMenuEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QLabel>
#include <QFormLayout>
#include <QStackedWidget>

namespace {
template<class Base> class WorkspaceTabBar final : public Base {
public:
    using Base::Base;
protected:
    void paintEvent(QPaintEvent* event) override {
        Base::paintEvent(event);
        const QVariant boundary = this->property("temporaryTabBoundary");
        if (!boundary.isValid() || boundary.toInt() < 0) return;
        const QRect tab = this->tabRect(boundary.toInt());
        QPainter painter(this);
        painter.setPen(this->palette().color(QPalette::Mid));
        painter.drawLine(tab.left(), tab.top() + 9, tab.left(), tab.bottom() - 9);
    }
};

class ToolButtonBadge {
public:
    virtual ~ToolButtonBadge() = default;
    virtual void setBadge(const QString& text, const QString& tone) = 0;
};

template<class Control> class BadgedToolButton final : public Control, public ToolButtonBadge {
public:
    using Control::Control;
    void setBadge(const QString& text, const QString& tone) override {
        badgeText = text.trimmed();
        badgeTone = tone.trimmed().toLower();
        refreshBadge();
    }
    QSize sizeHint() const override {
        auto hint = Control::sizeHint();
        if (this->property("zeroslackElaControl").toBool()) {
            const int iconWidth = this->icon().isNull() ? 0 : this->iconSize().width() + 12;
            hint = hint.expandedTo(QSize(this->fontMetrics().horizontalAdvance(this->text()) + iconWidth + 18,
                                        qMax(this->fontMetrics().height(), this->iconSize().height()) + 12));
            hint.rwidth() += this->property("badgeReserve").toInt();
        }
        if (!badgeText.isEmpty()) hint.setHeight(qMax(hint.height(), badgeSize().height() + 10));
        return hint;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void changeEvent(QEvent* event) override {
        Control::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
            refreshBadge();
    }
    void paintEvent(QPaintEvent* event) override {
        Control::paintEvent(event);
        if (badgeText.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(UiTypography::font(UiTypography::Role::Badge));
        const QSize size = badgeSize();
        const QRect badgeRect(this->width() - size.width() - 7,
                              (this->height() - size.height()) / 2, size.width(), size.height());
        const auto& theme = InsightVisualStyle::theme();
        QColor background = theme.statusBar.infoBackground, foreground = theme.statusBar.infoText;
        if (badgeTone == QStringLiteral("error")) {
            background = theme.statusBar.errorBackground; foreground = theme.statusBar.errorText;
        } else if (badgeTone == QStringLiteral("warning")) {
            background = theme.statusBar.warningBackground; foreground = theme.statusBar.warningText;
        } else if (badgeTone == QStringLiteral("success")) {
            background = theme.statusBar.successBackground; foreground = theme.statusBar.successText;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(badgeRect, size.height() / 2.0, size.height() / 2.0);
        painter.setPen(foreground);
        painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
    }
private:
    QSize badgeSize() const {
        const QFontMetrics metrics(UiTypography::font(UiTypography::Role::Badge));
        return QSize(qMax(18, metrics.horizontalAdvance(badgeText) + 10), qMax(18, metrics.height() + 4));
    }
    void refreshBadge() {
        const int reserve = badgeText.isEmpty() ? 0
            : qMax(qMax(35, this->fontMetrics().horizontalAdvance(badgeText) + 22), badgeSize().width() + 14);
        this->setProperty("hasBadge", !badgeText.isEmpty());
        this->setProperty("badgeReserve", reserve);
        if (!this->property("zeroslackElaControl").toBool()) {
            this->setStyleSheet(reserve ? QStringLiteral("padding-right: %1px;").arg(reserve) : QString());
        }
        this->updateGeometry();
        this->update();
    }
    QString badgeText, badgeTone;
};
}

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaDoubleSpinBox.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaRadioButton.h"
#include "ElaSlider.h"
#include "ElaSpinBox.h"
#include "ElaToolButton.h"
#include "ElaTreeView.h"
#include "ElaTabBar.h"
#include "ElaTabWidget.h"
#include "ElaCentralStackedWidget.h"
#include "ElaScrollBar.h"
#include "ElaMenu.h"
#include "ElaListView.h"
#include "ElaTableView.h"
#include "ElaPlainTextEdit.h"
#include "ElaScrollArea.h"
#include "ElaText.h"
#include <QPainter>
#include <QStyleOptionSpinBox>
#include <QStyleOptionFrame>

namespace {
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }

void paintFocusRing(QWidget* widget) {
    if (!widget->hasFocus() || !widget->isEnabled()) return;
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(InsightVisualStyle::theme().focus.ring, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(widget->rect().adjusted(2, 2, -2, -2), 5, 5);
}

template<class Control> class AccessibleElaControl : public Control {
public:
    using Control::Control;
    QSize sizeHint() const override {
        auto hint = Control::sizeHint();
        hint.setHeight(qMax(hint.height(), this->fontMetrics().height() + 14));
        return hint;
    }
    QSize minimumSizeHint() const override {
        auto hint = Control::minimumSizeHint();
        hint.setHeight(qMax(hint.height(), this->fontMetrics().height() + 10));
        return hint;
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        Control::paintEvent(event);
        paintFocusRing(this);
    }
};

class ElaRailButton final : public ElaToolButton {
public:
    using ElaToolButton::ElaToolButton;
    QSize sizeHint() const override { return iconSize().expandedTo(QSize(20, 20)) + QSize(16, 16); }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent*) override {
        // Rail entries stay icon-only, including the instant-popup Project menu.
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.features &= ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup);
        option.subControls = QStyle::SC_ToolButton;
        {
            QPainter painter(this);
            style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
        }
        paintFocusRing(this);
    }
};

class ElaTextView final : public ElaPlainTextEdit {
public:
    using ElaPlainTextEdit::ElaPlainTextEdit;
protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        auto* popup = UiControls::menu(this);
        popup->setObjectName(QStringLiteral("readOnlyTextContextMenu"));
        popup->setAttribute(Qt::WA_DeleteOnClose);
        auto* actions = createStandardContextMenu();
        actions->QObject::setParent(popup);
        // Qt supplies localized labels, selection state and the actual copy action.
        popup->addActions(actions->actions());
        popup->popup(event->globalPos());
        event->accept();
    }
};

class ElaAction final : public ElaPushButton {
public:
    using ElaPushButton::ElaPushButton;
    QSize sizeHint() const override {
        const int iconWidth = icon().isNull() ? 0 : iconSize().width() + (text().isEmpty() ? 0 : 6);
        const int textWidth = fontMetrics().size(Qt::TextShowMnemonic, text()).width();
        return ElaPushButton::sizeHint().expandedTo(QSize(textWidth + iconWidth + 24,
                                                       fontMetrics().height() + 14));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

template<class Control> Control* prepare(Control* widget) {
    widget->setProperty("zeroslackElaControl", true);
    widget->setFont(UiTypography::font());
    // Release upstream fixed sizes so forms can wrap at narrow/high-DPI sizes.
    widget->setMinimumSize(0, 0);
    widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QObject::connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
                     widget, [widget] { UiControls::refreshRole(widget); widget->update(); });
    UiControls::refreshRole(widget);
    return widget;
}

class ElaChoice final : public AccessibleElaControl<ElaComboBox> {
public:
    using AccessibleElaControl<ElaComboBox>::AccessibleElaControl;
    // Upstream blocks dismissal during its opening animation. Keep Qt's
    // immediate selection/Escape contract, with Ela painting and item delegate.
    void showPopup() override { QComboBox::showPopup(); }
    void hidePopup() override { QComboBox::hidePopup(); }
};

template<class Control> class ElaNumber final : public AccessibleElaControl<Control> {
public:
    using AccessibleElaControl<Control>::AccessibleElaControl;
    QSize sizeHint() const override {
        auto hint = AccessibleElaControl<Control>::sizeHint();
        const auto* edit = this->lineEdit();
        const auto metrics = edit->fontMetrics();
        int textWidth = metrics.horizontalAdvance(this->specialValueText());
        for (auto value : {this->minimum(), this->maximum(), this->value()})
            textWidth = qMax(textWidth, metrics.horizontalAdvance(
                this->prefix() + this->textFromValue(value) + this->suffix()));
        QStyleOptionFrame input;
        input.initFrom(edit);
        input.lineWidth = edit->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &input, edit);
        const QSize content(textWidth + edit->textMargins().left() + edit->textMargins().right() + 4,
                            metrics.height());
        const int inputWidth = qMax(edit->minimumSizeHint().width(),
            edit->style()->sizeFromContents(QStyle::CT_LineEdit, &input, content, edit).width());
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        option.rect = QRect(QPoint(), hint);
        const int available = this->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, this).width();
        hint.rwidth() += qMax(0, inputWidth - available);
        return hint;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

class ElaItemTree final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    ~ElaItemTree() override { setStyle(nullptr); }
};

void ownViewStyle(QWidget* view, QStyle* style) {
    // Shared popup/header children may still use the style during QWidget teardown.
    QObject::connect(view, &QObject::destroyed, style, &QObject::deleteLater);
    view->setStyle(style);
    view->setMouseTracking(true);
}

class PrecisionWheelRouter final : public QObject {
public:
    explicit PrecisionWheelRouter(QAbstractScrollArea* view) : QObject(view), area(view) {
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress) {
            for (auto* scroll : {area->horizontalScrollBar(), area->verticalScrollBar()})
                if (auto* bar = qobject_cast<ElaScrollBar*>(scroll)) bar->stopSmoothWheel();
        }
        if (event->type() != QEvent::Wheel) return false;
        auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint pixels = wheel->pixelDelta();
        if (pixels.isNull()) return false;
        // Qt item views can choose the wrong axis when only pixelDelta is present.
        auto* bar = (qAbs(pixels.x()) > qAbs(pixels.y()) || wheel->modifiers().testFlag(Qt::ShiftModifier))
            ? area->horizontalScrollBar() : area->verticalScrollBar();
        wheel->ignore();
        QApplication::sendEvent(bar, wheel);
        return wheel->isAccepted();
    }
private:
    QAbstractScrollArea* area;
};

}
#endif

UiTabWidget::UiTabWidget(QWidget* parent) : QTabWidget(parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        prepare(this);
        // TabManager owns close confirmation, page lifetime, and inter-group moves.
        setTabBar(UiControls::tabBar(this));
    }
#endif
}

QLabel* UiControls::label(QWidget* parent) { return label(QString(), parent); }
QLabel* UiControls::label(const QString& text, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* value = prepare(new ElaText(text, parent));
        value->setThemeColorEnabled(false);
        value->setStyleSheet(QString());
        value->setPalette(QPalette());
        value->setWordWrap(false);
        return value;
    }
#endif
    return new QLabel(text, parent);
}
void UiControls::addFormRow(QFormLayout* form, const QString& text, QWidget* field) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* caption = text.isEmpty() ? nullptr : label(text, form->parentWidget());
        if (caption) caption->setBuddy(field);
        form->addRow(caption, field);
        return;
    }
#endif
    form->addRow(text, field);
}

QPushButton* UiControls::pushButton(QWidget* parent) { return pushButton(QString(), parent); }
QPushButton* UiControls::pushButton(const QString& text, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new ElaAction(text, parent));
#endif
    return new QPushButton(text, parent);
}
QToolButton* UiControls::toolButton(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* button = prepare(new AccessibleElaControl<ElaToolButton>(parent));
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setIconSize(QSize(16, 16));
        button->setBorderRadius(6);
        QObject::connect(button, &QToolButton::toggled, button, &ElaToolButton::setIsSelected);
        return button;
    }
#endif
    return new QToolButton(parent);
}
QToolButton* UiControls::railButton(QWidget* parent) {
    QToolButton* button = nullptr;
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* ela = prepare(new ElaRailButton(parent));
        ela->setBorderRadius(8);
        QObject::connect(ela, &QToolButton::toggled, ela, &ElaToolButton::setIsSelected);
        button = ela;
    }
#endif
    if (!button) {
        button = new QToolButton(parent);
        button->setProperty("zeroslackElaControl", false);
    }
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(20, 20));
    button->setMinimumSize(36, 36);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setPopupMode(QToolButton::DelayedPopup);
    return button;
}
QToolButton* UiControls::badgedToolButton(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* button = prepare(new BadgedToolButton<AccessibleElaControl<ElaToolButton>>(parent));
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setBorderRadius(8);
        QObject::connect(button, &QToolButton::toggled, button, &ElaToolButton::setIsSelected);
        return button;
    }
#endif
    auto* button = new BadgedToolButton<QToolButton>(parent);
    button->setProperty("zeroslackElaControl", false);
    return button;
}
void UiControls::setToolButtonBadge(QToolButton* button, const QString& text, const QString& tone) {
    if (auto* badge = dynamic_cast<ToolButtonBadge*>(button)) badge->setBadge(text, tone);
}
QWidget* UiControls::pageStack(QStackedWidget*& stack, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* surface = new ElaCentralStackedWidget(parent);
        surface->setIsTransparent(true);
        surface->setIsHasRadius(false);
        stack = surface->getContainerStackedWidget();
        return surface;
    }
#endif
    stack = new QStackedWidget(parent);
    return stack;
}
void UiControls::selectPage(QStackedWidget* stack, int index, bool animate) {
    if (!stack || index < 0 || index >= stack->count()) return;
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* surface = qobject_cast<ElaCentralStackedWidget*>(stack->parentWidget())) {
        surface->doWindowStackSwitch(animate ? ElaWindowType::Popup : ElaWindowType::None, index, false);
        return;
    }
#endif
    stack->setCurrentIndex(index);
}
QLineEdit* UiControls::lineEdit(QWidget* parent) { return lineEdit(QString(), parent); }
QLineEdit* UiControls::lineEdit(const QString& text, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* edit = prepare(new AccessibleElaControl<ElaLineEdit>(parent));
        edit->setText(text);
        return edit;
    }
#endif
    return new QLineEdit(text, parent);
}
QComboBox* UiControls::comboBox(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new ElaChoice(parent));
#endif
    return new QComboBox(parent);
}
QCheckBox* UiControls::checkBox(QWidget* parent) { return checkBox(QString(), parent); }
QCheckBox* UiControls::checkBox(const QString& text, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new AccessibleElaControl<ElaCheckBox>(text, parent));
#endif
    return new QCheckBox(text, parent);
}
QSpinBox* UiControls::spinBox(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new ElaNumber<ElaSpinBox>(parent));
#endif
    return new QSpinBox(parent);
}
QRadioButton* UiControls::radioButton(const QString& text, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new AccessibleElaControl<ElaRadioButton>(text, parent));
#endif
    return new QRadioButton(text, parent);
}
QDoubleSpinBox* UiControls::doubleSpinBox(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new ElaNumber<ElaDoubleSpinBox>(parent));
#endif
    return new QDoubleSpinBox(parent);
}
QSlider* UiControls::slider(Qt::Orientation orientation, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new AccessibleElaControl<ElaSlider>(orientation, parent));
#endif
    return new QSlider(orientation, parent);
}
QTreeView* UiControls::treeView(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* tree = prepare(new ElaTreeView(parent));
        tree->setNativeItemContent(true);
        tree->setItemHeight(qMax(28, tree->fontMetrics().height() + 10));
        tree->setAnimated(false);
        for (auto* bar : {tree->horizontalScrollBar(), tree->verticalScrollBar()})
            if (auto* elaBar = qobject_cast<ElaScrollBar*>(bar)) elaBar->setIsAnimation(false);
        return tree;
    }
#endif
    return new QTreeView(parent);
}
QTreeWidget* UiControls::treeWidget(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* tree = prepare(new ElaItemTree(parent));
        tree->setStyle(ElaTreeView::createStyle(tree, qMax(28, tree->fontMetrics().height() + 10)));
        tree->setMouseTracking(true);
        return tree;
    }
#endif
    return new QTreeWidget(parent);
}
QTabWidget* UiControls::editorTabWidget(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla())
        return prepare(new ElaTabWidget(parent));
#endif
    return tabWidget(parent);
}

QTabBar* UiControls::tabBar(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* bar = prepare(new WorkspaceTabBar<ElaTabBar>(parent));
        bar->setNativeTabBehavior(true);
        bar->setSmoothScrollEnabled(true);
        bar->setTabsClosable(false);
        bar->setMovable(false);
        bar->setAcceptDrops(false);
        return bar;
    }
#endif
    return new WorkspaceTabBar<QTabBar>(parent);
}

void UiControls::enableSmoothScrolling(QAbstractScrollArea* area) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (area && usesEla()) {
        if (!area->property("zeroslackPrecisionWheelRouter").toBool()) {
            new PrecisionWheelRouter(area);
            area->setProperty("zeroslackPrecisionWheelRouter", true);
        }
        if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
            view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }
        for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
            auto* current = orientation == Qt::Horizontal ? area->horizontalScrollBar() : area->verticalScrollBar();
            auto* bar = qobject_cast<ElaScrollBar*>(current);
            if (!bar) {
                bar = new ElaScrollBar(orientation, area);
                if (orientation == Qt::Horizontal) area->setHorizontalScrollBar(bar);
                else area->setVerticalScrollBar(bar);
            }
            bar->setIsAnimation(false);
            bar->setSmoothWheelEnabled(true);
            bar->setWheelAnimationDuration(160);
        }
    }
#else
    Q_UNUSED(area)
#endif
}

void UiControls::enableTreeTransitions(QTreeView* tree) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (tree && usesEla()) tree->setAnimated(true);
#else
    Q_UNUSED(tree)
#endif
}

QTabWidget* UiControls::tabWidget(QWidget* parent) {
    return new UiTabWidget(parent);
}
QListWidget* UiControls::listWidget(QWidget* parent) {
    auto* list = new QListWidget(parent);
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        prepare(list);
        ownViewStyle(list, ElaListView::createStyle(qApp, qMax(28, list->fontMetrics().height() + 10)));
    }
#endif
    return list;
}
QListView* UiControls::listView(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* list = prepare(new ElaListView(parent));
        ownViewStyle(list, ElaListView::createStyle(qApp, qMax(28, list->fontMetrics().height() + 10)));
        return list;
    }
#endif
    return new QListView(parent);
}
QTableWidget* UiControls::tableWidget(QWidget* parent) { return tableWidget(0, 0, parent); }
QTableWidget* UiControls::tableWidget(int rows, int columns, QWidget* parent) {
    auto* table = new QTableWidget(rows, columns, parent);
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        prepare(table);
        ownViewStyle(table, ElaTableView::createStyle(qApp));
        table->setShowGrid(false);
    }
#endif
    return table;
}
QTableView* UiControls::tableView(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* table = prepare(new ElaTableView(parent));
        table->setNativeItemContent(true);
        for (auto* bar : {table->horizontalScrollBar(), table->verticalScrollBar()})
            if (auto* elaBar = qobject_cast<ElaScrollBar*>(bar)) elaBar->setIsAnimation(false);
        return table;
    }
#endif
    return new QTableView(parent);
}
QScrollArea* UiControls::scrollArea(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* area = prepare(new ElaScrollArea(parent));
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        area->setIsAnimation(Qt::Horizontal, false);
        area->setIsAnimation(Qt::Vertical, false);
        return area;
    }
#endif
    return new QScrollArea(parent);
}
QPlainTextEdit* UiControls::readOnlyText(QWidget* parent) {
    QPlainTextEdit* text = nullptr;
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* view = prepare(new ElaTextView(parent));
        view->setNativeTextBehavior(true);
        view->setStyleSheet(QString());
        // Remove the constructor's explicit palette so host and theme roles inherit.
        view->setPalette(QPalette());
        text = view;
    }
#endif
    if (!text) text = new QPlainTextEdit(parent);
    text->setReadOnly(true);
    return text;
}
QPlainTextEdit* UiControls::readOnlyText(const QString& value, QWidget* parent) {
    auto* text = readOnlyText(parent);
    text->setPlainText(value);
    return text;
}

QMenu* UiControls::menu(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) {
        auto* popup = prepare(new ElaMenu(parent));
        popup->setNativeMenuBehavior(true);
        popup->setMenuItemHeight(qMax(28, popup->fontMetrics().height() + 12));
        return popup;
    }
#endif
    return new QMenu(parent);
}
QMenu* UiControls::addMenu(QMenu* parent, const QString& title) {
    auto* child = menu(parent);
    child->setTitle(title);
    parent->addMenu(child);
    return child;
}
QMenu* UiControls::addMenu(QMenuBar* parent, const QString& title) {
    auto* child = menu(parent);
    child->setTitle(title);
    parent->addMenu(child);
    return child;
}
void UiControls::refreshRole(QWidget* widget) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* button = qobject_cast<ElaPushButton*>(widget)) {
        const auto& theme = InsightVisualStyle::theme();
        auto colors = theme.button;
        if (widget->property("_zeroslackInsightThemeHelper").toString() == QStringLiteral("primaryButton")) {
            colors.background = colors.backgroundChecked;
            colors.backgroundHover = colors.backgroundChecked.lighter(108);
            colors.backgroundPressed = colors.backgroundChecked.darker(110);
            colors.text = colors.textChecked;
        }
        button->setBorderRadius(6);
        button->setLightDefaultColor(colors.background);
        button->setDarkDefaultColor(colors.background);
        button->setLightHoverColor(colors.backgroundHover);
        button->setDarkHoverColor(colors.backgroundHover);
        button->setLightPressColor(colors.backgroundPressed);
        button->setDarkPressColor(colors.backgroundPressed);
        button->setLightTextColor(colors.text);
        button->setDarkTextColor(colors.text);
        button->update();
    }
#else
    Q_UNUSED(widget);
#endif
}
