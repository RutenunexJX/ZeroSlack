#include "uicontrols.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "uitypography.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaDoubleSpinBox.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaSlider.h"
#include "ElaSpinBox.h"
#include "ElaToolButton.h"
#include <QPainter>

namespace {
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }

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
        if (this->hasFocus() && this->isEnabled()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(InsightVisualStyle::theme().focus.ring, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(this->rect().adjusted(2, 2, -2, -2), 5, 5);
        }
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
}
#endif

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
    if (usesEla()) return prepare(new AccessibleElaControl<ElaSpinBox>(parent));
#endif
    return new QSpinBox(parent);
}
QDoubleSpinBox* UiControls::doubleSpinBox(QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new AccessibleElaControl<ElaDoubleSpinBox>(parent));
#endif
    return new QDoubleSpinBox(parent);
}
QSlider* UiControls::slider(Qt::Orientation orientation, QWidget* parent) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (usesEla()) return prepare(new AccessibleElaControl<ElaSlider>(orientation, parent));
#endif
    return new QSlider(orientation, parent);
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
