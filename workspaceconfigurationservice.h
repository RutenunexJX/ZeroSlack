#ifndef WORKSPACECONFIGURATIONSERVICE_H
#define WORKSPACECONFIGURATIONSERVICE_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

class QSettings;

struct WorkspaceConfiguration {
    QString workspaceRoot;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QStringList ignoredDirs;
    QStringList fileExtensions;
    QString topModule;

    bool isValid() const { return !workspaceRoot.isEmpty(); }
};

class WorkspaceConfigurationService
{
public:
    explicit WorkspaceConfigurationService(
        const QString& settingsFilePath = QString());

    static WorkspaceConfigurationService* getInstance();
    static QStringList defaultFileExtensions();

    WorkspaceConfiguration defaultConfiguration(
        const QString& workspaceRoot) const;
    WorkspaceConfiguration load(const QString& workspaceRoot) const;
    bool save(const WorkspaceConfiguration& configuration) const;
    bool clear(const QString& workspaceRoot) const;
    QString storageDescription() const;

    WorkspaceConfiguration normalized(
        const WorkspaceConfiguration& configuration) const;

private:
    QString settingsFilePath;
    static std::unique_ptr<WorkspaceConfigurationService> instance;

    std::unique_ptr<QSettings> makeSettings() const;
};

#endif // WORKSPACECONFIGURATIONSERVICE_H
