#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include "symboltaxonomy.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <cstdint>

struct ProjectSnapshot {
    std::uint64_t revision = 0;
    QString workspaceRoot;
    QStringList allFiles;
    QStringList systemVerilogFiles;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QStringList fileExtensions;
    QHash<QString, SymbolTaxonomy::SourceRole> sourceRoles;
    QString topModule;
    QStringList ignoredPaths;

    bool isOpen() const { return !workspaceRoot.isEmpty(); }
    QStringList filesForSourceRole(SymbolTaxonomy::SourceRole role) const;
    QStringList designSourceFiles() const;
    QStringList headerSourceFiles() const;
};

class ProjectModel : public QObject
{
    Q_OBJECT

public:
    explicit ProjectModel(QObject* parent = nullptr);
    ~ProjectModel() override;

    void setWorkspaceRoot(const QString& rootPath);
    void setWorkspaceState(const QString& rootPath,
                           const QStringList& scannedFiles);
    void closeProject();

    void setScannedFiles(const QStringList& files);
    void setIgnoredPaths(const QStringList& paths);
    void setWorkspaceConfiguration(const QStringList& includeDirs,
                                   const QHash<QString, QString>& defines,
                                   const QStringList& fileExtensions,
                                   const QString& topModule,
                                   const QStringList& ignoredPaths);

    ProjectSnapshot snapshot() const;
    QString workspaceRoot() const;
    QStringList allFiles() const;
    QStringList systemVerilogFiles() const;
    QStringList fileExtensions() const;
    SymbolTaxonomy::SourceRole sourceRoleForFile(const QString& filePath) const;
    QStringList designSourceFiles() const;
    QStringList headerSourceFiles() const;
    QStringList ignoredPaths() const;

    bool isOpen() const;
    bool containsFile(const QString& filePath) const;

signals:
    void projectChanged(const ProjectSnapshot& snapshot);
    void projectClosed();

private:
    struct ProjectPathRules {
        QString normalizePath(const QString& path) const;
        QStringList normalizePathList(const QStringList& paths) const;
        QStringList uniqueSorted(QStringList values) const;
        QStringList uniquePreservingOrder(const QStringList& values) const;
        QStringList defaultIncludeDirsForFiles(
            const QString& workspaceRoot,
            const QStringList& files) const;
        QStringList defaultFileExtensions() const;
        QStringList normalizeFileExtensions(
            const QStringList& extensions) const;
        QHash<QString, QString> normalizeDefines(
            const QHash<QString, QString>& defines) const;
        bool isIgnored(const QString& filePath,
                       const QStringList& ignoredPaths) const;
        bool isSystemVerilogFile(const QString& filePath,
                                 const QStringList& fileExtensions) const;
        SymbolTaxonomy::SourceRole sourceRoleForFile(
            const QString& filePath) const;
    };

    ProjectSnapshot current;
    std::uint64_t revisionCounter = 0;
    QStringList rawScannedFiles;
    bool includeDirsExplicit = false;
    ProjectPathRules pathRules;

    void publishChanged();
    void applyScannedFiles(const QStringList& files);
};

Q_DECLARE_METATYPE(ProjectSnapshot)

#endif // PROJECTMODEL_H
