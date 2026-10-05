#ifndef WORKSPACECONFIGURATIONSERVICE_H
#define WORKSPACECONFIGURATIONSERVICE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

struct WorkspaceVirtualSourceGroup {
    QString name;
    QStringList files;

    bool operator==(const WorkspaceVirtualSourceGroup& other) const {
        return name == other.name && files == other.files;
    }
};

struct WorkspaceConfiguration {
    // Editing baseline, never serialized as product configuration. New drafts
    // can create a missing file; loaded drafts carry the exact input revision.
    QString storageRevision = QStringLiteral("missing");
    QString workspaceRoot;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QStringList ignoredDirs;
    QStringList fileExtensions;
    QString topModule;
    QList<WorkspaceVirtualSourceGroup> virtualSourceGroups;

    bool isValid() const {
        return !workspaceRoot.isEmpty();
    }
};

struct WorkspaceConfigurationSaveResult {
    bool saved = false;
    bool conflict = false;
    QString revision;
    QString message;
};

enum class WorkspaceConfigurationSource {
    Default,
    ProjectFile,
    LegacySession,
};

enum class WorkspaceConfigurationLoadState { Missing, Loaded, Invalid, Unsupported, ReadError };

struct WorkspaceConfigurationLoadResult {
    WorkspaceConfigurationLoadState state = WorkspaceConfigurationLoadState::Missing;
    bool loaded = false;
    WorkspaceConfigurationSource source =
        WorkspaceConfigurationSource::Default;
    WorkspaceConfiguration configuration;
    QString projectFilePath;
    QString legacyFilePath;
    QStringList externalPaths;
    QString message;
    bool usable() const {
        return (state == WorkspaceConfigurationLoadState::Missing
                || state == WorkspaceConfigurationLoadState::Loaded)
            && configuration.isValid();
    }
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
    WorkspaceConfigurationSaveResult saveWithResult(const WorkspaceConfiguration& configuration) const;
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
