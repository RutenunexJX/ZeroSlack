#ifndef APPLICATIONTHEMEMANAGER_H
#define APPLICATIONTHEMEMANAGER_H

#include "zeroslackexport.h"

#include <QObject>
#include <QMetaType>
#include <QPointer>
#include <QSet>

struct InsightTheme;
class QStyle;
class QWidget;

enum class UiStyleBackend { Classic, Qlementine };

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
    // Select once, before creating widgets. Runtime replacement invalidates
    // style-owned animations; changing the backend requires a restart.
    bool selectBackend(UiStyleBackend backend);
    UiStyleBackend backend() const { return currentBackend; }
    static bool qlementineAvailable();
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return animateControls; }
    void preserveClassicSurface(QWidget* root);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void themeAboutToChange(ThemeMode previousMode,
                            ThemeMode nextMode);
    void themeChanged(ThemeMode mode);

private:
    ApplicationThemeManager();
    void synchronizeSurface(QWidget* widget);
    void queueSurfaceUpdate(QWidget* widget);

    ThemeMode currentMode = ThemeMode::Light;
    UiStyleBackend currentBackend = UiStyleBackend::Classic;
    bool installed = false;
    bool animateControls = true;
    bool updatingSurface = false;
    QPointer<QStyle> backendStyle;
    QPointer<QStyle> classicStyle;
    QSet<QWidget*> classicRoots;
};

#endif // APPLICATIONTHEMEMANAGER_H
