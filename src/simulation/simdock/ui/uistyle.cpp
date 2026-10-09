#include "uistyle.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "uitooltips.h"
#include <ElaApplication.h>
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaPlainTextEdit.h>
#include <ElaMenu.h>
#include <ElaScrollBar.h>
#include <ElaToolButton.h>
#include <ElaToolTip.h>
#include <QHelpEvent>
#include <QScreen>
#include <QTimer>
#include <QPointer>
#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QFormLayout>
#include <QStyleOptionSpinBox>
#include <QWheelEvent>
#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QLocale>
#include <QStyleFactory>
#include <QDialog>
#include <QScrollArea>
#include <QVBoxLayout>

static void initializeSimDockResources() { Q_INIT_RESOURCE(simdock); }

namespace simdock::Ui {
namespace {
class DialogBounds final : public QObject {
public:
    explicit DialogBounds(QDialog* dialog) : QObject(dialog), m_dialog(dialog) {
        dialog->installEventFilter(this);
        fit();
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::ScreenChangeInternal)
            QTimer::singleShot(0, this, [this] { fit(); });
        return false;
    }
private:
    void fit() {
        auto* screen = m_dialog->screen();
        if (!screen) return;
        const QRect bounds = screen->availableGeometry().adjusted(8, 8, -8, -8);
        const QSize frame = m_dialog->frameGeometry().size() - m_dialog->size();
        const QSize limit = bounds.size() - frame;
        m_dialog->setMaximumSize(limit);
        m_dialog->resize(m_dialog->size().boundedTo(limit));
        const auto rect = m_dialog->frameGeometry();
        m_dialog->move(qBound(bounds.left(), rect.left(), bounds.right() - rect.width() + 1),
                       qBound(bounds.top(), rect.top(), bounds.bottom() - rect.height() + 1));
    }
    QDialog* m_dialog;
};
class TextView final : public ElaPlainTextEdit {
public:
    using ElaPlainTextEdit::ElaPlainTextEdit;
protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        auto* popup = new ElaMenu(this);
        popup->setObjectName(QStringLiteral("textContextMenu"));
        popup->setNativeMenuBehavior(true);
        popup->setAttribute(Qt::WA_DeleteOnClose);
        auto* standard = createStandardContextMenu();
        standard->QObject::setParent(popup);
        popup->addActions(standard->actions());
        popup->popup(event->globalPos());
        event->accept();
    }
};
class Number final : public ElaSpinBox {
public:
    using ElaSpinBox::ElaSpinBox;
    QSize sizeHint() const override {
        auto hint = ElaSpinBox::sizeHint();
        const auto* edit = lineEdit();
        const auto metrics = edit->fontMetrics();
        int textWidth = metrics.horizontalAdvance(specialValueText());
        for (auto value : {minimum(), maximum(), this->value()})
            textWidth = qMax(textWidth, metrics.horizontalAdvance(prefix() + textFromValue(value) + suffix()));
        QStyleOptionFrame input;
        input.initFrom(edit);
        input.lineWidth = edit->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &input, edit);
        const QSize content(textWidth + edit->textMargins().left() + edit->textMargins().right() + 4, metrics.height());
        const int inputWidth = qMax(edit->minimumSizeHint().width(),
            edit->style()->sizeFromContents(QStyle::CT_LineEdit, &input, content, edit).width());
        QStyleOptionSpinBox option;
        initStyleOption(&option);
        option.rect = QRect(QPoint(), hint);
        const int available = style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, this).width();
        hint.rwidth() += qMax(0, inputWidth - available);
        return hint;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};
class TextUnitScrollBar final : public ElaScrollBar {
public:
    using ElaScrollBar::ElaScrollBar;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (!event->pixelDelta().isNull()) {
            stopSmoothWheel();
            QScrollBar::wheelEvent(event);
        } else ElaScrollBar::wheelEvent(event);
    }
};
class WheelRouter final : public QObject {
public:
    explicit WheelRouter(QAbstractScrollArea* area) : QObject(area), m_area(area) {
        area->installEventFilter(this);
        area->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Hide) {
            for (auto* scroll : {m_area->horizontalScrollBar(), m_area->verticalScrollBar()})
                if (auto* bar = qobject_cast<ElaScrollBar*>(scroll)) bar->stopSmoothWheel();
        }
        if (event->type() != QEvent::Wheel) return false;
        auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint pixels = wheel->pixelDelta();
        if (pixels.isNull()) return false;
        if (qobject_cast<QPlainTextEdit*>(m_area)) return false;
        auto* bar = (qAbs(pixels.x()) > qAbs(pixels.y()) || wheel->modifiers().testFlag(Qt::ShiftModifier))
            ? m_area->horizontalScrollBar() : m_area->verticalScrollBar();
        wheel->ignore();
        QApplication::sendEvent(bar, wheel);
        return wheel->isAccepted();
    }
private:
    QAbstractScrollArea* m_area;
};
struct Colors {
    QColor app, panel, subtle, border, strong, text, secondary, accent;
    QColor input, button, hover, pressed, selected, disabled;
};
Colors colors()
{
    const auto& t = ApplicationThemeManager::instance().theme();
    return {t.appBackground, t.panelBackground, t.panelSubtle, t.border,
        t.borderStrong, t.textPrimary, t.textSecondary, t.accent, t.input.background,
        t.button.background, t.button.backgroundHover, t.button.backgroundPressed,
        t.itemView.selectedBackground, t.button.textDisabled};
}
}

void constrainDialog(QDialog* dialog)
{
    // Move the existing layout, rather than recreating fields or editing their
    // data. Scroll only when the screen cannot accommodate its natural minimum.
    auto* body = new QWidget;
    body->setMinimumSize(dialog->minimumSize());
    body->setLayout(dialog->layout());
    auto* scroll = new QScrollArea(dialog);
    scroll->setObjectName(QStringLiteral("dialogViewport"));
    scroll->setProperty("simdockDialogSurface", true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    smoothScrolling(scroll);
    dialog->setMinimumSize(0, 0);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(scroll);
    // Windows and scroll contents created before reparenting can retain the
    // host palette. Theme all owned scroll surfaces explicitly, locally.
    const auto theme = [dialog] {
        applyTheme(dialog);
        for (auto* area : dialog->findChildren<QScrollArea*>()) {
            if (!area->property("simdockDialogSurface").toBool()) continue;
            area->setPalette(dialog->palette());
            area->viewport()->setPalette(dialog->palette());
            if (auto* content = area->widget()) content->setPalette(dialog->palette());
        }
    };
    theme();
    QObject::connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, dialog, theme);
    new DialogBounds(dialog);
}

QFont font(Role role)
{
    QFont result;
    result.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("Segoe UI"),
        QStringLiteral("Noto Sans SC"), QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Arial")});
    result.setStyleHint(QFont::SansSerif);
    result.setKerning(true);
    result.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    result.setPixelSize(role == Role::PageTitle ? 18 : role == Role::PanelTitle ? 14
        : role == Role::Metadata || role == Role::Section ? 12 : 13);
    result.setWeight(role == Role::PageTitle || role == Role::PanelTitle || role == Role::Section
        ? QFont::DemiBold : QFont::Normal);
    return result;
}
QFont codeFont()
{
    QFont result;
    result.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
        QStringLiteral("Microsoft YaHei UI")});
    result.setStyleHint(QFont::Monospace);
    result.setPixelSize(13);
    return result;
}
void initialize() { initializeComponent(); }
void initializeComponent()
{
    static const bool initialized = [] {
        initializeSimDockResources();
        return true;
    }();
    Q_UNUSED(initialized);
}
void applyTheme(QWidget* root)
{
    if (!root) return;
    const auto t = colors();
    QPalette p;
    p.setColor(QPalette::Window, t.app);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.input);
    p.setColor(QPalette::AlternateBase, t.subtle);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.button);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::Highlight, t.selected);
    p.setColor(QPalette::HighlightedText, t.text);
    p.setColor(QPalette::PlaceholderText, t.secondary);
    p.setColor(QPalette::ToolTipBase, t.panel);
    p.setColor(QPalette::ToolTipText, t.text);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.disabled);
    const auto sheet = QStringLiteral(
        "QFrame[surface=panel] { background: %1; border: 1px solid %2; border-radius: 4px; }"
        "QFrame[surface=bar] { background: %1; border: 0; border-bottom: 1px solid %2; }"
        "QFrame[surface=status] { background: %1; border: 0; border-top: 1px solid %2; }"
        "QLabel[secondary=true] { color: %3; }"
        "QLabel[error=true] { color: %7; }"
        "QListView { background: transparent; color: %4; selection-color: %4; selection-background-color: %6; border: 0; outline: 0; }"
        "QSplitter::handle { background: transparent; }"
        "QSplitter::handle:hover { background: %2; }"
        "QPlainTextEdit[codeSurface=true] { background: %1; color: %4; border: 0; padding: 8px; }"
        "QLineEdit { padding-left: 8px; }"
        "QToolTip { background: %1; color: %4; border: 1px solid %2; padding: 6px; }"
        "QLabel#runStatus { color: %5; background: %6; padding: 4px 10px; border-radius: 4px; }")
        .arg(t.panel.name(), t.border.name(), t.secondary.name(), t.text.name(),
            t.accent.name(), t.selected.name(),
            eTheme->getThemeMode() == ElaThemeType::Dark ? QStringLiteral("#fca5a5") : QStringLiteral("#b91c1c"));
    if (root) {
        root->setPalette(p);
        root->setAttribute(Qt::WA_StyledBackground);
        root->setStyleSheet(QStringLiteral("QWidget#SimDockWorkbench { background: %1; } QStatusBar { background: %2; }")
            .arg(t.app.name(), t.panel.name()) + sheet);
    }
    
}
ElaText* label(const QString& text, QWidget* parent, Role role)
{
    auto* result = new ElaText(text, 13, parent);
    result->setFont(font(role));
    result->setWordWrap(false);
    result->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (role == Role::Metadata || role == Role::Section) {
        result->setThemeColorEnabled(false);
        result->setProperty("secondary", true);
    }
    return result;
}
ElaPushButton* button(const QString& text, QWidget* parent, bool primary)
{
    auto* result = new ElaPushButton(text, parent);
    result->setFont(font());
    result->setFixedHeight(32);
    result->setMinimumWidth(result->fontMetrics().horizontalAdvance(text) + 28);
    result->setBorderRadius(4);
    if (primary) {
        result->setLightDefaultColor(QColor("#2563eb"));
        result->setLightHoverColor(QColor("#1d4ed8"));
        result->setLightPressColor(QColor("#1e40af"));
        result->setLightTextColor(Qt::white);
        result->setDarkDefaultColor(QColor("#60a5fa"));
        result->setDarkHoverColor(QColor("#93c5fd"));
        result->setDarkPressColor(QColor("#3b82f6"));
        result->setDarkTextColor(QColor("#0b1120"));
    }
    return result;
}
ElaPlainTextEdit* textView(QWidget* parent)
{
    auto* result = new TextView(parent);
    result->setVerticalScrollBar(new TextUnitScrollBar(Qt::Vertical, result));
    result->setHorizontalScrollBar(new TextUnitScrollBar(Qt::Horizontal, result));
    result->setNativeTextBehavior(true);
    result->setFont(codeFont());
    smoothScrolling(result);
    return result;
}
ElaSpinBox* spinBox(QWidget* parent)
{
    auto* result = new Number(parent);
    result->setMinimumSize(0, 32);
    result->setMaximumSize(QWIDGETSIZE_MAX, 32);
    return result;
}
ElaToolButton* disclosure(const QString& text, QWidget* parent)
{
    auto* result = new ElaToolButton(parent);
    result->setText(text);
    result->setAccessibleName(text);
    result->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    result->setAutoRaise(true);
    result->setCheckable(true);
    result->setChecked(true);
    result->setFont(font(Role::PanelTitle));
    result->setMinimumHeight(32);
    result->setElaIcon(ElaIconType::AngleDown);
    QObject::connect(result, &QToolButton::toggled, result, [result](bool expanded) {
        result->setElaIcon(expanded ? ElaIconType::AngleDown : ElaIconType::AngleRight);
    });
    return result;
}
void smoothScrolling(QAbstractScrollArea* area)
{
    if (area->property("simdockWheelRouter").toBool()) return;
    new WheelRouter(area);
    area->setProperty("simdockWheelRouter", true);
    if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    for (auto* scroll : {area->horizontalScrollBar(), area->verticalScrollBar()}) {
        if (auto* bar = qobject_cast<ElaScrollBar*>(scroll)) {
            bar->setIsAnimation(false);
            bar->setSmoothWheelEnabled(true);
            bar->setWheelAnimationDuration(160);
        }
    }
}
void enableToolTip(QWidget* widget)
{
    if (widget->property("simdockToolTipEnabled").toBool()) return;
    widget->setProperty("simdockToolTipEnabled", true);
    UiToolTips::install();
    QObject::connect(widget, &QObject::destroyed, qApp, &UiToolTips::hideText);
}
void formRow(QFormLayout* form, const QString& text, QWidget* field)
{
    auto* caption = label(text, form->parentWidget());
    caption->setBuddy(field);
    form->addRow(caption, field);
}
void formRow(QFormLayout* form, const QString& text, QLayout* fields)
{
    auto* caption = label(text, form->parentWidget());
    if (fields->count() && fields->itemAt(0)->widget()) caption->setBuddy(fields->itemAt(0)->widget());
    form->addRow(caption, fields);
}
void normalizeControls(QWidget* root)
{
    for (auto* widget : root->findChildren<QWidget*>()) {
        if (auto* combo = qobject_cast<ElaComboBox*>(widget)) {
            QObject::connect(eTheme, &ElaTheme::themeModeChanged, combo, &ElaComboBox::finishPopupAnimation, Qt::UniqueConnection);
            QObject::connect(combo->model(), &QAbstractItemModel::modelAboutToBeReset, combo, &ElaComboBox::finishPopupAnimation, Qt::UniqueConnection);
            QObject::connect(combo->model(), &QAbstractItemModel::rowsAboutToBeRemoved, combo, &ElaComboBox::finishPopupAnimation, Qt::UniqueConnection);
        }
        if (qobject_cast<QComboBox*>(widget) || qobject_cast<QLineEdit*>(widget)
            || qobject_cast<QAbstractSpinBox*>(widget)) {
            widget->setFont(font());
            if (!qobject_cast<QAbstractSpinBox*>(widget->parentWidget())
                && !qobject_cast<QComboBox*>(widget->parentWidget()))
                widget->setFixedHeight(32);
        }
        if (auto* label = qobject_cast<QLabel*>(widget); label && !qobject_cast<ElaText*>(label))
            label->setFont(font());
    }
}
}
