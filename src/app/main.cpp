#include <zeroslack/ui/applicationwindow.h>

#include "applicationthememanager.h"
#include "symbolrelationshipengine.h"
#include "version.h"
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
#include "automationhost.h"
#endif

#ifdef ZEROSLACK_HAS_SUITEAPP
#include "suiteappintegration.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#if defined(ZEROSLACK_ELA_BUILD)
    // Keep the storage identity so existing settings and sessions remain in place.
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack-Ela"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ZeroSlack"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack"));
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption();
#if defined(ZEROSLACK_ELA_BUILD)
    const QCommandLineOption versionOption({QStringLiteral("v"), QStringLiteral("version")},
        QStringLiteral("Displays version information."));
    parser.addOption(versionOption);
#else
    parser.addVersionOption();
#endif
    parser.addPositionalArgument(QStringLiteral("files"),
        QStringLiteral("Open source files without adding a workspace."), QStringLiteral("[files…]"));
    const QCommandLineOption isolatedProfileOption(QStringLiteral("isolated-profile"),
        QStringLiteral("Use an independent local profile and do not register SuiteApp IPC."), QStringLiteral("directory"));
    parser.addOption(isolatedProfileOption);
    parser.addOption({QStringLiteral("no-ui-animations"), QStringLiteral("Disable supported application animations; some Ela control animations remain.")});
    parser.process(a);
    const bool isolated = parser.isSet(isolatedProfileOption);
    const QString isolatedProfile = isolated ? QFileInfo(parser.value(isolatedProfileOption)).absoluteFilePath() : QString();
    if (isolated && (parser.value(isolatedProfileOption).isEmpty() || !QDir().mkpath(isolatedProfile))) return 2;
    if (isolated) {
        qputenv("ZEROSLACK_PROFILE_ROOT", isolatedProfile.toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", (isolatedProfile + "/editing-time.json").toUtf8());
        qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", (isolatedProfile + "/simulation").toUtf8());
        qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", (isolatedProfile + "/legacy.ini").toUtf8());
        qputenv("XIPS_ASSET_ADAPTER_STATE", (isolatedProfile + "/asset-adapter").toUtf8());
        qputenv("XIPS_LIBRARY", (isolatedProfile + "/library").toUtf8());
    }
#if defined(ZEROSLACK_ELA_BUILD)
    const bool iniProfile = true;
#else
    const bool iniProfile = isolated;
#endif
    if (iniProfile) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const auto settings = isolated ? isolatedProfile : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings + "/system");
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (settings + "/workspace-sessions.ini").toUtf8());
    }
#if defined(ZEROSLACK_ELA_BUILD)
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << QGuiApplication::applicationDisplayName() << ' '
                            << QCoreApplication::applicationVersion() << Qt::endl;
        return 0;
    }
#endif
    auto& theme = ApplicationThemeManager::instance();
    theme.setAnimationsEnabled(!parser.isSet(QStringLiteral("no-ui-animations")));
    theme.applyToApplication();
    a.setWindowIcon(QIcon(QStringLiteral(":/images/zeroslack_app.png")));

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    auto applicationWindow = ZeroSlack::createApplicationWindow();
    QMainWindow& w = *applicationWindow;
#if defined(ZEROSLACK_ELA_BUILD)
    w.setWindowTitle(QStringLiteral("ZeroSlack — %1").arg(QString::fromLatin1(APP_VERSION)));
#endif
    w.show();
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
    ZeroSlackAutomationHost automation(&w);
    QString automationError;
    if (!automation.start(&automationError)) qWarning().noquote() << automationError;
#endif
    if (isolated) w.setWindowTitle(QStringLiteral("ZeroSlack — isolated profile: %1").arg(QFileInfo(isolatedProfile).fileName()));
    const QStringList files = parser.positionalArguments();
    QTimer::singleShot(0, &w, [&w, files] {
        for (const QString& file : files)
            ZeroSlack::openApplicationDocument(w, QFileInfo(file).absoluteFilePath());
    });
#ifdef ZEROSLACK_HAS_SUITEAPP
    ZeroSlackSuiteIntegration suiteIntegration(&w);
    QTimer::singleShot(0, &a, [&suiteIntegration, isolated]() {
        if (!isolated) suiteIntegration.start();
    });
#endif
    return a.exec();
}
