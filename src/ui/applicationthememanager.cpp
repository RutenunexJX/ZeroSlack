#include "applicationthememanager.h"

#include "insightvisualstyle.h"
#include "roundedicons.h"
#include "uitypography.h"

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
    application->setPalette(
        InsightVisualStyle::applicationPalette(currentMode));
    application->setStyleSheet(
        InsightVisualStyle::applicationStyleSheet(currentMode));
}
