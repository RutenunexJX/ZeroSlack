#include "qlementinebackend.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"
#include <SuiteUi/ControlStyle.hpp>

namespace {
class ProductStyle final : public SuiteUi::ControlStyle {
public:
    ProductStyle() : ControlStyle(new RoundedIcons::Style) {}
private:
    bool isPrimary(const QWidget* widget) const override {
        return widget && widget->property("_zeroslackInsightThemeHelper").toString() == "primaryButton";
    }
};
}

QStyle* QlementineBackend::create() {
    auto* style = new ProductStyle;
    style->setObjectName(QStringLiteral("ZeroSlackSuiteUi"));
    return style;
}

void QlementineBackend::applyTheme(QStyle* style, ThemeMode mode, bool animationsEnabled) {
    const auto& t = InsightVisualStyle::theme(mode);
    auto* controls = static_cast<ProductStyle*>(style);
    controls->setControlPalette({
        {t.button.background, t.button.backgroundHover, t.button.backgroundPressed, t.panelSubtle},
        {t.button.text, t.button.textHover, t.textPrimary, t.button.textDisabled},
        {t.accent, t.accent.lighter(110), t.accent.darker(110), t.panelSubtle},
        {t.button.textChecked, t.button.textChecked, t.button.textChecked, t.button.textDisabled},
        {t.border, t.borderStrong, t.accent, t.panelSubtle}, t.focus.ring});
    controls->setAnimationsEnabled(animationsEnabled);
}
