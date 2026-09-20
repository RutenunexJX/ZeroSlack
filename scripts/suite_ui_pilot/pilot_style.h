#pragma once
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QPainter>
#include <QScopedValueRollback>
#include <QStyleOptionButton>
#include <QWidget>

namespace Pilot {
// Experimental drawing policy only. Input and action ownership stay in Qt.
class Style : public oclero::qlementine::QlementineStyle {
public:
    using QlementineStyle::polish;
    using QlementineStyle::unpolish;
    using MouseState = oclero::qlementine::MouseState;
    using ColorRole = oclero::qlementine::ColorRole;
    void polish(QApplication* app) override { QCommonStyle::polish(app); }
    void polish(QWidget* widget) override {
        QCommonStyle::polish(widget);
        widget->setAttribute(Qt::WA_Hover, true);
    }
    void unpolish(QWidget* widget) override { QCommonStyle::unpolish(widget); }
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* data = nullptr) const override {
        if (hint == SH_ComboBox_Popup || hint == SH_Menu_FlashTriggeredItem || hint == SH_Menu_FadeOutOnHide)
            return false;
        return QlementineStyle::styleHint(hint, option, widget, data);
    }
    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override {
        QScopedValueRollback role(toolPrimary_, toolPrimary_ || primary(widget));
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        if (button && primary(widget)
            && (element == CE_PushButton || element == CE_PushButtonBevel || element == CE_PushButtonLabel)) {
            auto copy = *button;
            copy.features |= QStyleOptionButton::DefaultButton;
            if (copy.state.testFlag(State_On)) copy.state |= State_Sunken;
            QlementineStyle::drawControl(element, &copy, painter, widget);
        } else QlementineStyle::drawControl(element, option, painter, widget);
        if (element == CE_PushButton || element == CE_CheckBox || element == CE_RadioButton)
            focus(option, painter);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override {
        QScopedValueRollback role(toolPrimary_, toolPrimary_ || primary(widget));
        QlementineStyle::drawPrimitive(element, option, painter, widget);
        if (element == PE_PanelLineEdit) focus(option, painter);
    }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override {
        QScopedValueRollback role(toolPrimary_, toolPrimary_ || primary(widget));
        QlementineStyle::drawComplexControl(control, option, painter, widget);
        if (control == CC_ToolButton || control == CC_ComboBox || control == CC_SpinBox) focus(option, painter);
    }
    const QColor& toolButtonBackgroundColor(MouseState state, ColorRole role) const override {
        return QlementineStyle::toolButtonBackgroundColor(state, toolPrimary_ ? ColorRole::Primary : role);
    }
    const QColor& toolButtonForegroundColor(MouseState state, ColorRole role) const override {
        return QlementineStyle::toolButtonForegroundColor(state, toolPrimary_ ? ColorRole::Primary : role);
    }
protected:
    virtual bool primary(const QWidget* widget) const {
        return widget && widget->property("pilotPrimary").toBool();
    }
private:
    mutable bool toolPrimary_ = false;
    void focus(const QStyleOption* option, QPainter* painter) const {
        if (!option->state.testFlag(State_HasFocus) || !option->state.testFlag(State_Enabled)) return;
        const auto& t = theme();
        const qreal inset = t.focusBorderWidth / 2.;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(t.focusColor, t.focusBorderWidth));
        painter->drawRoundedRect(QRectF(option->rect).adjusted(inset, inset, -inset, -inset),
                                t.borderRadius, t.borderRadius);
        painter->restore();
    }
};
}
