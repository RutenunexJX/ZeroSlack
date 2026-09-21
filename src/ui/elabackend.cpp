#include "elabackend.h"
#include "insightvisualstyle.h"
#include "ElaApplication.h"
#include "ElaTheme.h"
#include <QApplication>

void ElaBackend::initialize()
{
    // Ela initializes resources and native helpers, but ZeroSlack owns the
    // application font and window flags. This runs before any widgets exist.
    const auto font = qApp->font();
    const bool siblings = QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    eApp->init();
    qApp->setFont(font);
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, siblings);
}

void ElaBackend::applyTheme(ThemeMode mode)
{
    const auto& t = InsightVisualStyle::theme(mode);
    const auto target = isDarkTheme(mode) ? ElaThemeType::Dark : ElaThemeType::Light;
    const auto set = [target](ElaThemeType::ThemeColor key, const QColor& value) {
        eTheme->setThemeColor(target, key, value);
    };
    using namespace ElaThemeType;
    set(WindowBase, t.appBackground);
    set(WindowCentralStackBase, t.panelBackground);
    set(PrimaryNormal, t.accent);
    set(PrimaryHover, t.accent.lighter(110));
    set(PrimaryPress, t.accent.darker(110));
    set(PopupBase, t.panelBackground);
    set(PopupHover, t.itemView.hoverBackground);
    set(PopupBorder, t.border);
    set(PopupBorderHover, t.borderStrong);
    set(DialogBase, t.input.background);
    set(DialogLayoutArea, t.panelSubtle);
    set(BasicText, t.textPrimary);
    set(BasicTextInvert, t.button.textChecked);
    set(BasicDetailsText, t.textSecondary);
    set(BasicTextNoFocus, t.textSecondary);
    set(BasicTextDisable, t.button.textDisabled);
    set(BasicTextPress, t.textPrimary);
    set(BasicTextCategory, t.textSecondary);
    set(BasicBorder, t.border);
    set(BasicBorderDeep, t.borderStrong);
    set(BasicBorderHover, t.button.borderHover);
    set(BasicBase, t.button.background);
    set(BasicBaseDeep, t.panelSubtle);
    set(BasicDisable, t.panelSubtle);
    set(BasicHover, t.button.backgroundHover);
    set(BasicPress, t.button.backgroundPressed);
    set(BasicSelectedHover, t.itemView.selectedBackground);
    set(BasicBaseLine, t.border);
    set(BasicHemline, t.border);
    set(BasicIndicator, t.borderStrong);
    set(BasicChute, t.panelSubtle);
    set(BasicAlternating, t.itemView.background);
    set(BasicBaseAlpha, t.button.background);
    set(BasicBaseDeepAlpha, t.panelSubtle);
    set(BasicHoverAlpha, t.button.backgroundHover);
    set(BasicPressAlpha, t.button.backgroundPressed);
    set(BasicSelectedAlpha, t.itemView.selectedBackground);
    set(BasicSelectedHoverAlpha, t.itemView.selectedBackground);
    set(ScrollBarHandle, t.borderStrong);
    set(ToggleSwitchNoToggledCenter, t.textSecondary);
    set(StatusDanger, t.statusBar.errorText);
    set(Win10BorderActive, t.borderStrong);
    set(Win10BorderInactive, t.border);
    // Also emits for light -> Latte / dark -> Mocha: existing controls must
    // repaint when tokens change without changing Ela's light/dark enum.
    eTheme->setThemeMode(target);
}

QString ElaBackend::styleSheet(ThemeMode mode)
{
    const auto& t = InsightVisualStyle::theme(mode);
    // No generic input/button QSS: Ela owns those controls' state painting.
    // Model views, editor tabs, and the existing window chrome stay in place.
    return InsightVisualStyle::chromeStyleSheet(mode) + QStringLiteral(
        "QTreeView, QListView, QTableView { background: %1; color: %2; border: 0;"
        " alternate-background-color: %1; selection-background-color: %3; }"
        "QTreeView::item, QListView::item { padding: 4px 5px; border-radius: 4px; }"
        "QTreeView::item:hover, QListView::item:hover { background: %4; }"
        "QTreeView::item:selected, QListView::item:selected { background: %3; color: %2; }"
        "QHeaderView::section { background: %5; color: %2; border: 0; padding: 5px; }"
        "QTabWidget::pane { border: 0; background: %1; }"
        "QAbstractSpinBox { min-height: 30px; }"
        "QPlainTextEdit[codeEditorSurface=true] { border: 0; padding: 0; }"
        "QGroupBox { border: 1px solid %6; border-radius: 6px; margin-top: 12px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }")
        .arg(t.itemView.background.name(), t.textPrimary.name(),
             t.itemView.selectedBackground.name(), t.itemView.hoverBackground.name(),
             t.itemView.headerBackground.name(), t.border.name());
}
