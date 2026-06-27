#include "projectmodel.h"

#include <QDir>
#include <QFileInfo>

QStringList ProjectSnapshot::filesForSourceRole(
    SymbolTaxonomy::SourceRole role) const
{
    QStringList files;
    for (auto it = sourceRoles.cbegin(); it != sourceRoles.cend(); ++it) {
        if (it.value() == role)
            files.append(it.key());
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

QStringList ProjectSnapshot::designSourceFiles() const
{
    return filesForSourceRole(SymbolTaxonomy::SourceRole::DesignSource);
}

QStringList ProjectSnapshot::headerSourceFiles() const
{
    QStringList files;
    for (auto it = sourceRoles.cbegin(); it != sourceRoles.cend(); ++it) {
        if (SymbolTaxonomy::isHeaderSourceRole(it.value()))
            files.append(it.key());
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ProjectSnapshot>("ProjectSnapshot");
}

ProjectModel::~ProjectModel() = default;

void ProjectModel::setWorkspaceRoot(const QString& rootPath)
{
    const QString normalized = pathRules.normalizePath(rootPath);
    if (current.workspaceRoot == normalized)
        return;

    current.workspaceRoot = normalized;
    if (!includeDirsExplicit)
        current.includeDirs = normalized.isEmpty() ? QStringList() : QStringList{normalized};
    publishChanged();
}

void ProjectModel::setWorkspaceState(const QString& rootPath,
                                     const QStringList& scannedFiles)
{
    const QString normalized = pathRules.normalizePath(rootPath);
    const QStringList normalizedFiles =
        pathRules.uniqueSorted(pathRules.normalizePathList(scannedFiles));
    if (current.workspaceRoot == normalized
        && rawScannedFiles == normalizedFiles) {
        return;
    }

    current = ProjectSnapshot();
    rawScannedFiles.clear();
    includeDirsExplicit = false;
    current.workspaceRoot = normalized;
    if (!includeDirsExplicit) {
        current.includeDirs =
            normalized.isEmpty() ? QStringList() : QStringList{normalized};
    }
    applyScannedFiles(normalizedFiles);
    publishChanged();
}

void ProjectModel::closeProject()
{
    if (!current.isOpen())
        return;

    current = ProjectSnapshot();
    rawScannedFiles.clear();
    includeDirsExplicit = false;
    emit projectClosed();
    publishChanged();
}

void ProjectModel::setScannedFiles(const QStringList& files)
{
    applyScannedFiles(files);
    publishChanged();
}

void ProjectModel::applyScannedFiles(const QStringList& files)
{
    rawScannedFiles = pathRules.uniqueSorted(pathRules.normalizePathList(files));
    QStringList acceptedFiles;
    acceptedFiles.reserve(rawScannedFiles.size());
    for (const QString& filePath : std::as_const(rawScannedFiles)) {
        if (!pathRules.isIgnored(filePath, current.ignoredPaths))
            acceptedFiles.append(filePath);
    }
    acceptedFiles = pathRules.uniqueSorted(acceptedFiles);

    QStringList svFiles;
    QHash<QString, SymbolTaxonomy::SourceRole> sourceRoles;
    svFiles.reserve(acceptedFiles.size());
    for (const QString& filePath : std::as_const(acceptedFiles)) {
        const SymbolTaxonomy::SourceRole sourceRole =
            pathRules.sourceRoleForFile(filePath);
        if (sourceRole != SymbolTaxonomy::SourceRole::Unknown)
            sourceRoles.insert(filePath, sourceRole);
        if (pathRules.isSystemVerilogFile(filePath))
            svFiles.append(filePath);
    }

    current.allFiles = acceptedFiles;
    current.systemVerilogFiles = svFiles;
    current.sourceRoles = sourceRoles;
    if (!includeDirsExplicit)
        current.includeDirs = pathRules.defaultIncludeDirsForFiles(
            current.workspaceRoot,
            acceptedFiles);
}

void ProjectModel::setIncludeDirs(const QStringList& dirs)
{
    includeDirsExplicit = true;
    current.includeDirs =
        pathRules.uniqueSorted(pathRules.normalizePathList(dirs));
    publishChanged();
}

void ProjectModel::setDefines(const QHash<QString, QString>& newDefines)
{
    current.defines = newDefines;
    publishChanged();
}

void ProjectModel::setFilelistPath(const QString& path)
{
    current.filelistPath = pathRules.normalizePath(path);
    publishChanged();
}

void ProjectModel::setTopModule(const QString& moduleName)
{
    current.topModule = moduleName.trimmed();
    publishChanged();
}

void ProjectModel::setIgnoredPaths(const QStringList& paths)
{
    current.ignoredPaths =
        pathRules.uniqueSorted(pathRules.normalizePathList(paths));
    setScannedFiles(rawScannedFiles);
}

ProjectSnapshot ProjectModel::snapshot() const
{
    return current;
}

QString ProjectModel::workspaceRoot() const
{
    return current.workspaceRoot;
}

QStringList ProjectModel::allFiles() const
{
    return current.allFiles;
}

QStringList ProjectModel::systemVerilogFiles() const
{
    return current.systemVerilogFiles;
}

QStringList ProjectModel::includeDirs() const
{
    return current.includeDirs;
}

QHash<QString, QString> ProjectModel::defines() const
{
    return current.defines;
}

QHash<QString, SymbolTaxonomy::SourceRole> ProjectModel::sourceRoles() const
{
    return current.sourceRoles;
}

SymbolTaxonomy::SourceRole ProjectModel::sourceRoleForFile(
    const QString& filePath) const
{
    return current.sourceRoles.value(
        pathRules.normalizePath(filePath),
        SymbolTaxonomy::SourceRole::Unknown);
}

QStringList ProjectModel::filesForSourceRole(
    SymbolTaxonomy::SourceRole role) const
{
    return current.filesForSourceRole(role);
}

QStringList ProjectModel::designSourceFiles() const
{
    return current.designSourceFiles();
}

QStringList ProjectModel::headerSourceFiles() const
{
    return current.headerSourceFiles();
}

QString ProjectModel::filelistPath() const
{
    return current.filelistPath;
}

QString ProjectModel::topModule() const
{
    return current.topModule;
}

QStringList ProjectModel::ignoredPaths() const
{
    return current.ignoredPaths;
}

bool ProjectModel::isOpen() const
{
    return current.isOpen();
}

bool ProjectModel::containsFile(const QString& filePath) const
{
    return current.allFiles.contains(pathRules.normalizePath(filePath));
}

QString ProjectModel::ProjectPathRules::normalizePath(const QString& path) const
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QStringList ProjectModel::ProjectPathRules::normalizePathList(
    const QStringList& paths) const
{
    QStringList normalized;
    normalized.reserve(paths.size());
    for (const QString& path : paths) {
        const QString clean = normalizePath(path);
        if (!clean.isEmpty())
            normalized.append(clean);
    }
    return normalized;
}

QStringList ProjectModel::ProjectPathRules::uniqueSorted(QStringList values) const
{
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList ProjectModel::ProjectPathRules::defaultIncludeDirsForFiles(
    const QString& workspaceRoot,
    const QStringList& files) const
{
    QStringList dirs;
    if (!workspaceRoot.isEmpty())
        dirs.append(workspaceRoot);

    for (const QString& filePath : files) {
        const QString suffix = QFileInfo(filePath).suffix().toLower();
        if (suffix == QLatin1String("svh") || suffix == QLatin1String("vh"))
            dirs.append(QFileInfo(filePath).absolutePath());
    }
    return uniqueSorted(dirs);
}

bool ProjectModel::ProjectPathRules::isIgnored(
    const QString& filePath,
    const QStringList& ignoredPaths) const
{
    const QString normalized = normalizePath(filePath);
    for (const QString& ignored : ignoredPaths) {
        if (normalized == ignored || normalized.startsWith(ignored + QLatin1Char('/')))
            return true;
    }
    return false;
}

bool ProjectModel::ProjectPathRules::isSystemVerilogFile(
    const QString& filePath) const
{
    if (filePath.isEmpty())
        return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    return svExtensions.contains(QFileInfo(filePath).suffix().toLower());
}

SymbolTaxonomy::SourceRole ProjectModel::ProjectPathRules::sourceRoleForFile(
    const QString& filePath) const
{
    return SymbolTaxonomy::sourceRoleForFileName(filePath);
}

void ProjectModel::publishChanged()
{
    emit projectChanged(current);
}
