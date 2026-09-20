#include "qlementinebackend.h"
#include "insightvisualstyle.h"
#include "uitypography.h"

#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QPainter>
#include <QStyleOptionButton>
#include <QWidget>

namespace {
using oclero::qlementine::QlementineStyle;
using oclero::qlementine::Theme;

class ProductStyle final : public QlementineStyle {
public:
    using QlementineStyle::polish;
    using QlementineStyle::unpolish;

    // QWidget owns input, popup views and focus. Do not install the upstream
    // external focus frames or combo-view replacement during construction.
    void polish(QApplication* app) override { QCommonStyle::polish(app); }
    void polish(QWidget* widget) override {
        QCommonStyle::polish(widget);
        widget->setAttribute(Qt::WA_Hover, true);
    }
    void unpolish(QWidget* widget) override { QCommonStyle::unpolish(widget); }

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* data = nullptr) const override {
        if (hint == SH_ComboBox_Popup || hint == SH_Menu_FlashTriggeredItem
            || hint == SH_Menu_FadeOutOnHide)
            return false;
        return QlementineStyle::styleHint(hint, option, widget, data);
    }

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        if (button && widget
            && widget->property("_zeroslackInsightThemeHelper").toString() == "primaryButton"
            && (element == CE_PushButton || element == CE_PushButtonBevel || element == CE_PushButtonLabel)) {
            auto primary = *button;
            primary.features |= QStyleOptionButton::DefaultButton;
            if (primary.state.testFlag(State_On)) primary.state |= State_Sunken;
            QlementineStyle::drawControl(element, &primary, painter, widget);
        } else {
            QlementineStyle::drawControl(element, option, painter, widget);
        }
        if (element == CE_PushButton || element == CE_CheckBox || element == CE_RadioButton)
            drawFocus(option, painter);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override {
        QlementineStyle::drawPrimitive(element, option, painter, widget);
        if (element == PE_PanelLineEdit) drawFocus(option, painter);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override {
        QlementineStyle::drawComplexControl(control, option, painter, widget);
        if (control == CC_ToolButton || control == CC_ComboBox || control == CC_SpinBox)
            drawFocus(option, painter);
    }

private:
    void drawFocus(const QStyleOption* option, QPainter* painter) const {
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

Theme mappedTheme(ThemeMode mode) {
    Theme q = isDarkTheme(mode) ? Theme::makeDark() : Theme::makeLight();
    const auto& t = InsightVisualStyle::theme(mode);
    q.meta.name = QString::number(int(mode));
    q.backgroundColorMain1 = t.panelBackground; q.backgroundColorMain2 = t.panelSubtle;
    q.backgroundColorMain3 = t.appBackground; q.backgroundColorMain4 = t.toolbarBackground;
    q.backgroundColorWorkspace = t.canvasBackground; q.backgroundColorTabBar = t.tab.barBackground;
    q.neutralColor = t.button.background; q.neutralColorHovered = t.button.backgroundHover;
    q.neutralColorPressed = t.button.backgroundPressed; q.neutralColorDisabled = t.panelSubtle;
    q.neutralColorTransparent = t.button.background; q.neutralColorTransparent.setAlpha(0);
    q.primaryColor = t.accent; q.primaryColorHovered = t.accent.lighter(110);
    q.primaryColorPressed = t.accent.darker(110); q.primaryColorDisabled = t.panelSubtle;
    q.primaryColorForeground = t.button.textChecked; q.primaryColorForegroundHovered = t.button.textChecked;
    q.primaryColorForegroundPressed = t.button.textChecked; q.primaryColorForegroundDisabled = t.button.textDisabled;
    q.primaryAlternativeColor = t.itemView.selectedBackground;
    q.primaryAlternativeColorHovered = t.itemView.hoverBackground;
    q.primaryAlternativeColorPressed = t.input.selectionBackground;
    q.primaryAlternativeColorDisabled = t.panelSubtle;
    q.secondaryColor = t.button.text; q.secondaryColorHovered = t.button.textHover;
    q.secondaryColorPressed = t.textPrimary; q.secondaryColorDisabled = t.button.textDisabled;
    q.secondaryColorForeground = t.panelBackground; q.secondaryColorForegroundHovered = t.panelBackground;
    q.secondaryColorForegroundPressed = t.panelBackground; q.secondaryColorForegroundDisabled = t.panelSubtle;
    q.secondaryAlternativeColor = t.textSecondary; q.secondaryAlternativeColorHovered = t.textPrimary;
    q.secondaryAlternativeColorPressed = t.textPrimary; q.secondaryAlternativeColorDisabled = t.textMuted;
    q.borderColor = t.border; q.borderColorHovered = t.borderStrong;
    q.borderColorPressed = t.accent; q.borderColorDisabled = t.panelSubtle;
    q.focusColor = t.focus.ring;
    q.statusColorSuccess = t.semantic.read; q.statusColorInfo = t.accent;
    q.statusColorWarning = t.warning; q.statusColorError = t.syntax.errorUnderline;
    q.fontRegular = UiTypography::font(); q.fontBold = UiTypography::font(UiTypography::Role::Section);
    q.fontCaption = UiTypography::font(UiTypography::Role::Metadata);
    q.fontH1 = UiTypography::font(UiTypography::Role::PageTitle);
    q.fontH2 = q.fontH1; q.fontH3 = q.fontH1;
    q.fontH4 = UiTypography::font(UiTypography::Role::PanelTitle); q.fontH5 = q.fontH4;
    q.fontMonospace = QFont(QStringLiteral("Consolas"));
    q.palette = InsightVisualStyle::applicationPalette(mode);
    return q;
}
}

QStyle* QlementineBackend::create() {
    auto* style = new ProductStyle;
    style->setObjectName(QStringLiteral("ZeroSlackQlementine"));
    // RoundedIcons supplies theme-aware, state-aware icon engines; uniform
    // recoloring also destroys multicolor branding and semantic icon colors.
    style->setAutoIconColor(oclero::qlementine::AutoIconColor::None);
    return style;
}

void QlementineBackend::applyTheme(QStyle* style, ThemeMode mode, bool animationsEnabled) {
    auto* productStyle = static_cast<ProductStyle*>(style);
    productStyle->setTheme(mappedTheme(mode));
    productStyle->setAnimationsEnabled(animationsEnabled);
}
