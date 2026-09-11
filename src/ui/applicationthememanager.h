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

signals:
    void themeAboutToChange(ThemeMode previousMode,
                            ThemeMode nextMode);
    void themeChanged(ThemeMode mode);

private:
    ApplicationThemeManager();

    ThemeMode currentMode = ThemeMode::Light;
};

#endif // APPLICATIONTHEMEMANAGER_H
