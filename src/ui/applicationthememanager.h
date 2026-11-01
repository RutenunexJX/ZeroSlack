#ifndef APPLICATIONTHEMEMANAGER_H
#define APPLICATIONTHEMEMANAGER_H

#include "zeroslackexport.h"

#include <QObject>
#include <QMetaType>

struct InsightTheme;


enum class ThemeMode {
    Light,
    Dark,
    CatppuccinLatte,
    CatppuccinFrappe,
    CatppuccinMacchiato,
    CatppuccinMocha,
};

inline bool isDarkTheme(ThemeMode mode) {
    return mode != ThemeMode::Light && mode != ThemeMode::CatppuccinLatte;
}
Q_DECLARE_METATYPE(ThemeMode)

class ZEROSLACK_API ApplicationThemeManager final : public QObject
{
    Q_OBJECT

public:
    static ApplicationThemeManager& instance();

    ThemeMode mode() const;
    const InsightTheme& theme() const;

    void setMode(ThemeMode mode);
    void applyToApplication();
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return animateControls; }

signals:
    void themeAboutToChange(ThemeMode previousMode,
                            ThemeMode nextMode);
    void themeChanged(ThemeMode mode);
    void animationsEnabledChanged(bool enabled);

private:
    ApplicationThemeManager();

    ThemeMode currentMode = ThemeMode::Light;
    bool installed = false;
    bool animateControls = true;
};

#endif // APPLICATIONTHEMEMANAGER_H
