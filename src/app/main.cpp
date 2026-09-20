#include "mainwindow.h"
#include "applicationthememanager.h"
#include "symbolrelationshipengine.h"
#include "version.h"

#ifdef ZEROSLACK_HAS_SUITEAPP
#include "suiteappintegration.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#ifdef ZEROSLACK_PREVIEW_BUILD
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack-Qlementine-Preview"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack"));
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QString defaultStyle = ApplicationThemeManager::qlementineAvailable()
        ? QStringLiteral("qlementine") : QStringLiteral("classic");
    const QCommandLineOption styleOption(QStringLiteral("ui-style"),
        QStringLiteral("Widget style: classic or qlementine (default: %1). Qlementine uses SuiteUi in the standard build.")
            .arg(defaultStyle),
        QStringLiteral("style"), defaultStyle);
#ifdef ZEROSLACK_PREVIEW_BUILD
    // All legacy explicit ZeroSlack QSettings constructors also use this INI root.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString previewSettings = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, previewSettings);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, previewSettings + QStringLiteral("/system"));
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",
            (previewSettings + QStringLiteral("/workspace-sessions.ini")).toUtf8());
#endif
    parser.addOption(styleOption);
    parser.addOption({QStringLiteral("no-ui-animations"), QStringLiteral("Disable Qlementine control animations.")});
    parser.process(a);
    const QString style = parser.value(styleOption);
    auto& theme = ApplicationThemeManager::instance();
    if ((style != QStringLiteral("classic") && style != QStringLiteral("qlementine"))
        || !theme.selectBackend(style == QStringLiteral("qlementine")
                                    ? UiStyleBackend::Qlementine : UiStyleBackend::Classic)) {
        QMessageBox::critical(nullptr, QStringLiteral("ZeroSlack"),
                             QStringLiteral("The requested UI style is unavailable in this build."));
        return 2;
    }
    theme.setAnimationsEnabled(!parser.isSet(QStringLiteral("no-ui-animations")));
    theme.applyToApplication();
    a.setWindowIcon(QIcon(QStringLiteral(":/images/zeroslack_app.png")));

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
#ifdef ZEROSLACK_PREVIEW_BUILD
    w.setWindowTitle(QStringLiteral("ZeroSlack Qlementine Preview — %1").arg(QString::fromLatin1(APP_VERSION)));
#endif
    w.show();
#ifdef ZEROSLACK_HAS_SUITEAPP
    ZeroSlackSuiteIntegration suiteIntegration(&w);
    QTimer::singleShot(0, &a, [&suiteIntegration]() {
        suiteIntegration.start();
    });
#endif
    return a.exec();
}
