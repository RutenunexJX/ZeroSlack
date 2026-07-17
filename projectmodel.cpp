#include "projectmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

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
    if (current.fileExtensions.isEmpty())
        current.fileExtensions = pathRules.defaultFileExtensions();
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
    current.fileExtensions = pathRules.defaultFileExtensions();
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
        if (pathRules.isSystemVerilogFile(filePath, current.fileExtensions))
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

void ProjectModel::setIgnoredPaths(const QStringList& paths)
{
    current.ignoredPaths =
        pathRules.uniqueSorted(pathRules.normalizePathList(paths));
    setScannedFiles(rawScannedFiles);
}

void ProjectModel::setWorkspaceConfiguration(
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    const QStringList& fileExtensions,
    const QString& topModule,
    const QStringList& ignoredPaths)
{
    includeDirsExplicit = true;
    current.includeDirs =
        pathRules.uniquePreservingOrder(pathRules.normalizePathList(includeDirs));
    current.defines = pathRules.normalizeDefines(defines);
    current.fileExtensions = pathRules.normalizeFileExtensions(fileExtensions);
    current.topModule = topModule.trimmed();
    current.ignoredPaths =
        pathRules.uniqueSorted(pathRules.normalizePathList(ignoredPaths));
    applyScannedFiles(rawScannedFiles);
    publishChanged();
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

QStringList ProjectModel::fileExtensions() const
{
    return current.fileExtensions;
}

SymbolTaxonomy::SourceRole ProjectModel::sourceRoleForFile(
    const QString& filePath) const
{
    return current.sourceRoles.value(
        pathRules.normalizePath(filePath),
        SymbolTaxonomy::SourceRole::Unknown);
}

QStringList ProjectModel::designSourceFiles() const
{
    return current.designSourceFiles();
}

QStringList ProjectModel::headerSourceFiles() const
{
    return current.headerSourceFiles();
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

QStringList ProjectModel::ProjectPathRules::uniquePreservingOrder(
    const QStringList& values) const
{
    QStringList result;
    QSet<QString> seen;
    result.reserve(values.size());
    for (const QString& value : values) {
        const QString key = value.toCaseFolded();
        if (value.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        result.append(value);
    }
    return result;
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

QStringList ProjectModel::ProjectPathRules::defaultFileExtensions() const
{
    return {QStringLiteral(".sv"),
            QStringLiteral(".svh"),
            QStringLiteral(".v"),
            QStringLiteral(".vh")};
}

QStringList ProjectModel::ProjectPathRules::normalizeFileExtensions(
    const QStringList& extensions) const
{
    QStringList normalized;
    normalized.reserve(extensions.size());
    for (QString extension : extensions) {
        extension = extension.trimmed().toLower();
        if (extension.isEmpty())
            continue;
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        normalized.append(extension);
    }
    normalized = uniquePreservingOrder(normalized);
    return normalized.isEmpty() ? defaultFileExtensions() : normalized;
}

QHash<QString, QString> ProjectModel::ProjectPathRules::normalizeDefines(
    const QHash<QString, QString>& defines) const
{
    QHash<QString, QString> normalized;
    for (auto it = defines.cbegin(); it != defines.cend(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty())
            continue;
        normalized.insert(key, it.value().trimmed());
    }
    return normalized;
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
    const QString& filePath,
    const QStringList& fileExtensions) const
{
    if (filePath.isEmpty())
        return false;

    const QString extension =
        QStringLiteral(".%1").arg(QFileInfo(filePath).suffix().toLower());
    return fileExtensions.contains(extension, Qt::CaseInsensitive);
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
