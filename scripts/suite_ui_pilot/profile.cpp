#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#ifdef PILOT_UI_INITIALIZATION
#include "workbench_theme.hpp"
#include <QApplication>
#include <QTimer>
#endif

static void isolateProfile()
{
    static QTemporaryDir directory;
    if (!directory.isValid()) qFatal("Cannot create pilot profile");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName("SuiteUiStage3Tests");
    QCoreApplication::setApplicationName("RegMapBaseline");
#ifdef PILOT_UI_INITIALIZATION
    QTimer::singleShot(0, [] {
        WorkbenchTheme::apply(*qApp, qEnvironmentVariable("PILOT_THEME") == "dark"
            ? WorkbenchTheme::Mode::dark : WorkbenchTheme::Mode::light);
    });
#endif
}
Q_COREAPP_STARTUP_FUNCTION(isolateProfile)
