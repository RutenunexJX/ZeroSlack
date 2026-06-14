#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include "symboltaxonomy.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

struct ProjectSnapshot {
    QString workspaceRoot;
    QStringList allFiles;
    QStringList systemVerilogFiles;
    QStringList includeDirs;
    QHash<QString, QString> defines;
    QHash<QString, SymbolTaxonomy::SourceRole> sourceRoles;
    QString filelistPath;
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
    void closeProject();

    void setScannedFiles(const QStringList& files);
    void setIncludeDirs(const QStringList& dirs);
    void setDefines(const QHash<QString, QString>& newDefines);
    void setFilelistPath(const QString& path);
    void setTopModule(const QString& moduleName);
    void setIgnoredPaths(const QStringList& paths);

    ProjectSnapshot snapshot() const;
    QString workspaceRoot() const;
    QStringList allFiles() const;
    QStringList systemVerilogFiles() const;
    QStringList includeDirs() const;
    QHash<QString, QString> defines() const;
    QHash<QString, SymbolTaxonomy::SourceRole> sourceRoles() const;
    SymbolTaxonomy::SourceRole sourceRoleForFile(const QString& filePath) const;
    QStringList filesForSourceRole(SymbolTaxonomy::SourceRole role) const;
    QStringList designSourceFiles() const;
    QStringList headerSourceFiles() const;
    QString filelistPath() const;
    QString topModule() const;
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
        QStringList defaultIncludeDirsForFiles(
            const QString& workspaceRoot,
            const QStringList& files) const;
        bool isIgnored(const QString& filePath,
                       const QStringList& ignoredPaths) const;
        bool isSystemVerilogFile(const QString& filePath) const;
        SymbolTaxonomy::SourceRole sourceRoleForFile(
            const QString& filePath) const;
    };

    ProjectSnapshot current;
    QStringList rawScannedFiles;
    bool includeDirsExplicit = false;
    ProjectPathRules pathRules;

    void publishChanged();
};

Q_DECLARE_METATYPE(ProjectSnapshot)

#endif // PROJECTMODEL_H
