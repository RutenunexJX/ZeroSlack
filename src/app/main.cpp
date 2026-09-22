#include "mainwindow.h"
#include "tabmanager.h"
#include "applicationthememanager.h"
#include "symbolrelationshipengine.h"
#include "version.h"

#ifdef ZEROSLACK_HAS_SUITEAPP
#include "suiteappintegration.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#if defined(ZEROSLACK_ELA_BUILD)
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack-Ela"));
#elif defined(ZEROSLACK_PREVIEW_BUILD)
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack-Qlementine-Preview"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack"));
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"),
        QStringLiteral("Open source files without adding a workspace."), QStringLiteral("[files…]"));
    const QString defaultStyle = ApplicationThemeManager::elaAvailable() ? QStringLiteral("ela")
        : ApplicationThemeManager::qlementineAvailable() ? QStringLiteral("qlementine") : QStringLiteral("classic");
    const QCommandLineOption styleOption(QStringLiteral("ui-style"),
        QStringLiteral("Widget style: classic, qlementine or ela (default: %1).")
            .arg(defaultStyle),
        QStringLiteral("style"), defaultStyle);
#if defined(ZEROSLACK_PREVIEW_BUILD) || defined(ZEROSLACK_ELA_BUILD)
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
    if ((style != QStringLiteral("classic") && style != QStringLiteral("qlementine") && style != QStringLiteral("ela"))
        || !theme.selectBackend(style == QStringLiteral("qlementine")
                                    ? UiStyleBackend::Qlementine : style == QStringLiteral("ela")
                                    ? UiStyleBackend::Ela : UiStyleBackend::Classic)) {
        QMessageBox::critical(nullptr, QStringLiteral("ZeroSlack"),
                             QStringLiteral("The requested UI style is unavailable in this build."));
        return 2;
    }
    theme.setAnimationsEnabled(!parser.isSet(QStringLiteral("no-ui-animations")));
    theme.applyToApplication();
    a.setWindowIcon(QIcon(QStringLiteral(":/images/zeroslack_app.png")));

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
#if defined(ZEROSLACK_ELA_BUILD)
    w.setWindowTitle(QStringLiteral("ZeroSlack Ela — %1").arg(QString::fromLatin1(APP_VERSION)));
#elif defined(ZEROSLACK_PREVIEW_BUILD)
    w.setWindowTitle(QStringLiteral("ZeroSlack Qlementine Preview — %1").arg(QString::fromLatin1(APP_VERSION)));
#endif
    w.show();
    const QStringList files = parser.positionalArguments();
    QTimer::singleShot(0, &w, [&w, files] {
        for (const QString& file : files)
            w.tabManager->openFileInTab(QFileInfo(file).absoluteFilePath());
    });
#ifdef ZEROSLACK_HAS_SUITEAPP
    ZeroSlackSuiteIntegration suiteIntegration(&w);
    QTimer::singleShot(0, &a, [&suiteIntegration]() {
        suiteIntegration.start();
    });
#endif
    return a.exec();
}
