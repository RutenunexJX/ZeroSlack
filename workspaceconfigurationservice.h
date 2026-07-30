#ifndef WORKSPACECONFIGURATIONSERVICE_H
#define WORKSPACECONFIGURATIONSERVICE_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

struct WorkspaceConfiguration {
    QString workspaceRoot;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QStringList ignoredDirs;
    QStringList fileExtensions;
    QString topModule;

    bool isValid() const {
        return !workspaceRoot.isEmpty();
    }
};

enum class WorkspaceConfigurationSource {
    Default,
    ProjectFile,
    LegacySession,
};

struct WorkspaceConfigurationLoadResult {
    bool loaded = false;
    WorkspaceConfigurationSource source =
        WorkspaceConfigurationSource::Default;
    WorkspaceConfiguration configuration;
    QString projectFilePath;
    QString legacyFilePath;
    QStringList externalPaths;
    QString message;
};

class WorkspaceConfigurationService
{
public:
    static constexpr int kVersion = 1;

    explicit WorkspaceConfigurationService(
        const QString& projectFilePathOverride =
            QString());

    static WorkspaceConfigurationService* getInstance();
    static QStringList defaultFileExtensions();
    static QString projectDirectoryPath(
        const QString& workspaceRoot);
    static QString projectFilePath(
        const QString& workspaceRoot);
    static QString legacyFilePath(
        const QString& workspaceRoot);

    WorkspaceConfiguration defaultConfiguration(
        const QString& workspaceRoot) const;
    WorkspaceConfigurationLoadResult loadWithResult(
        const QString& workspaceRoot) const;
    WorkspaceConfiguration load(
        const QString& workspaceRoot) const;
    bool save(
        const WorkspaceConfiguration& configuration) const;
    bool clear(const QString& workspaceRoot) const;

    WorkspaceConfiguration normalized(
        const WorkspaceConfiguration& configuration) const;

private:
    QString projectFilePathOverride;
    static std::unique_ptr<
        WorkspaceConfigurationService> instance;

    QString effectiveProjectFilePath(
        const QString& workspaceRoot) const;
};

#endif // WORKSPACECONFIGURATIONSERVICE_H
