#include "applicationthememanager.h"

#include "insightvisualstyle.h"
#include "roundedicons.h"
#include "uitypography.h"
#include "ElaTheme.h"
#include "ElaPushButton.h"

#include <QApplication>
#include <QCoreApplication>

ApplicationThemeManager& ApplicationThemeManager::instance()
{
    static ApplicationThemeManager manager;
    return manager;
}

ApplicationThemeManager::ApplicationThemeManager() = default;

ThemeMode ApplicationThemeManager::mode() const
{
    return currentMode;
}

const InsightTheme& ApplicationThemeManager::theme() const
{
    return InsightVisualStyle::theme(currentMode);
}

void ApplicationThemeManager::setMode(ThemeMode mode)
{
    if (currentMode == mode) {
        applyToApplication();
        return;
    }

    emit themeAboutToChange(currentMode, mode);
    currentMode = mode;
    applyToApplication();
    emit themeChanged(currentMode);
}

void ApplicationThemeManager::applyToApplication()
{
    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application)
        return;

    if (!application->property("roundedControlsInstalled").toBool()) {
        application->setStyle(new RoundedIcons::Style);
        application->setFont(UiTypography::font());
        application->setProperty("roundedControlsInstalled", true);
    }
    const InsightTheme& currentTheme = theme();
    const auto elaMode = isDarkTheme(currentMode)
        ? ElaThemeType::Dark : ElaThemeType::Light;
    eTheme->setThemeColor(elaMode, ElaThemeType::WindowBase,
                          currentTheme.appBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::WindowCentralStackBase,
                          currentTheme.canvasBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::PrimaryNormal,
                          currentTheme.accent);
    eTheme->setThemeColor(elaMode, ElaThemeType::PrimaryHover,
                          currentTheme.accent.lighter(110));
    eTheme->setThemeColor(elaMode, ElaThemeType::PrimaryPress,
                          currentTheme.accent.darker(110));
    eTheme->setThemeColor(elaMode, ElaThemeType::PopupBase,
                          currentTheme.menu.background);
    eTheme->setThemeColor(elaMode, ElaThemeType::PopupBorder,
                          currentTheme.menu.border);
    eTheme->setThemeColor(elaMode, ElaThemeType::PopupHover,
                          currentTheme.menu.itemHoverBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::DialogBase,
                          currentTheme.panelBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicText,
                          currentTheme.textPrimary);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicDetailsText,
                          currentTheme.textSecondary);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicTextNoFocus,
                          currentTheme.textMuted);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicTextDisable,
                          currentTheme.button.textDisabled);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicBorder,
                          currentTheme.border);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicBorderHover,
                          currentTheme.button.borderHover);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicBase,
                          currentTheme.panelBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicHover,
                          currentTheme.hover);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicPress,
                          currentTheme.selected);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicIndicator,
                          currentTheme.accent);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicAlternating,
                          currentTheme.itemView.alternateBackground);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicBaseLine,
                          currentTheme.border);
    eTheme->setThemeColor(elaMode, ElaThemeType::BasicDisable,
                          currentTheme.button.background);
    eTheme->setThemeMode(elaMode);

    for (QWidget* widget : application->allWidgets()) {
        auto* button = qobject_cast<ElaPushButton*>(widget);
        if (!button)
            continue;
        if (elaMode == ElaThemeType::Light) {
            button->setLightDefaultColor(currentTheme.button.background);
            button->setLightHoverColor(currentTheme.button.backgroundHover);
            button->setLightPressColor(currentTheme.button.backgroundPressed);
            button->setLightTextColor(currentTheme.button.text);
        } else {
            button->setDarkDefaultColor(currentTheme.button.background);
            button->setDarkHoverColor(currentTheme.button.backgroundHover);
            button->setDarkPressColor(currentTheme.button.backgroundPressed);
            button->setDarkTextColor(currentTheme.button.text);
        }
        button->update();
    }

    application->setPalette(
        InsightVisualStyle::applicationPalette(currentMode));
    application->setStyleSheet(
        InsightVisualStyle::applicationStyleSheet(currentMode));
}
