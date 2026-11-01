#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <cstdio>
#include <cstdlib>

namespace {
void initializeManagedTestSettings()
{
    const QString root = qEnvironmentVariable("ZEROSLACK_MANAGED_SETTINGS_ROOT");
    if (root.isEmpty()) return;
    const auto reject = [] {
        std::fputs("Managed test settings isolation is unavailable\n", stderr);
        std::_Exit(2);
    };
    if (!QDir::isAbsolutePath(root) || !QDir(root).exists()) reject();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root + "/system");
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "ZeroSlack", "ZeroSlack");
    const QJsonObject record{{"processId", double(QCoreApplication::applicationPid())},
                             {"application", QCoreApplication::applicationName()},
                             {"settingsRoot", root}, {"format", "ini"},
                             {"settingsFile", settings.fileName()}};
    QFile evidence(root + "/bootstrap-" + QString::number(QCoreApplication::applicationPid()) + ".json");
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (!evidence.open(QIODevice::WriteOnly) || evidence.write(bytes) != bytes.size()) reject();
}
}
Q_COREAPP_STARTUP_FUNCTION(initializeManagedTestSettings)
