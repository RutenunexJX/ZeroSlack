#include "xipscontextprovider.h"
#include "../native/nativecontextview.h"
#include "simulationcommandservice.h"
#include "tabmanager.h"
#include "uicontrols.h"
#include "workspacemanager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace
{
// Windows QFile readers deny delete sharing. Close each snapshot before releasing
// this gate; source hashing and UI work must not hold a reference-file handle.
QMutex referenceFileMutex;

bool within(const QString &path, const QString &root)
{
    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString boundary = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    return normalized.compare(boundary, Qt::CaseInsensitive) == 0 ||
           normalized.startsWith(boundary + '/', Qt::CaseInsensitive);
}
bool linked(const QString &path)
{
    QString current = QFileInfo(path).absoluteFilePath();
    for (;;)
    {
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction())
            return true;
        const QString parent = info.absolutePath();
        if (parent == current)
            return false;
        current = parent;
    }
}
QJsonObject readReferences(const QString &target, QString *error)
{
    QJsonObject empty{{"schema", "zeroslack.xips-references/v1"}, {"assets", QJsonArray{}}};
    QByteArray bytes;
    {
        const QMutexLocker guard(&referenceFileMutex);
        if (!QFileInfo::exists(target))
            return empty;
        QFile input(target);
        if (!input.open(QIODevice::ReadOnly))
        {
            *error = QStringLiteral("Cannot read existing asset references: %1").arg(input.errorString());
            return {};
        }
        bytes = input.readAll();
        if (input.error() != QFileDevice::NoError)
        {
            *error = QStringLiteral("Cannot read existing asset references: %1").arg(input.errorString());
            return {};
        }
    }
    QJsonParseError parseError;
    const auto stored = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !stored.isObject() ||
        stored.object().value("schema").toString() != "zeroslack.xips-references/v1" ||
        !stored.object().value("assets").isArray())
    {
        *error = QStringLiteral("Existing asset references are invalid; they were preserved.");
        return {};
    }
    return stored.object();
}
QString writeReferences(const QString &target, const QJsonObject &document,
                        QVariantMap *saveFailure = nullptr)
{
    const QMutexLocker guard(&referenceFileMutex);
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    const auto failed = [&](const QString &stage)
    {
        const QVariantMap detail{{"stage", stage},
                                 {"qtFileError", int(output.error())},
                                 {"systemError", output.errorString()},
                                 {"path", target}};
        if (saveFailure)
            *saveFailure = detail;
        qWarning().noquote() << "Asset reference save failure:"
                             << QJsonDocument::fromVariant(detail).toJson(QJsonDocument::Compact);
        return QStringLiteral("The asset reference could not be saved at %1: %2 (file error %3). "
                              "The previous reference was retained.")
            .arg(stage, output.errorString())
            .arg(int(output.error()));
    };
    if (!output.open(QIODevice::WriteOnly))
        return failed("open");
    if (output.write(bytes) != bytes.size())
        return failed("write");
    if (!output.flush())
        return failed("flush");
    if (!output.commit())
        return failed("commit");
    return {};
}
QJsonObject storedReference(const QVariantMap &reference)
{
    auto object = QJsonObject::fromVariantMap(reference);
    object.remove("localState");
    return object;
}
} // namespace
XipsHostBridge::XipsHostBridge(TabManager *tabs, WorkspaceManager *workspaces, QObject *parent)
    : QObject(parent), tabs(tabs), workspaces(workspaces)
{
}
QString XipsHostBridge::destinationError(const QString &path)
{
    if (!workspaces || !workspaces->isWorkspaceOpen())
        return tr("Open a workspace before adding an asset.");
    if (!within(path, workspaces->getWorkspacePath()))
        return tr("Choose a destination inside the current workspace.");
    if (linked(path) || QFileInfo::exists(path))
        return tr("The destination already exists or contains a linked directory.");
    if (within(path, QDir(workspaces->getWorkspacePath()).filePath(QStringLiteral(".zeroslack"))) ||
        within(path, QDir(workspaces->getWorkspacePath()).filePath(QStringLiteral(".git"))))
        return tr("Choose a source or output directory, outside project metadata.");
    if (tabs)
        for (const QString &open : tabs->getAllOpenFileNames())
            if (within(open, path))
                return tr("The destination is already open in an editor. Choose another name.");
    return {};
}
QStringList XipsHostBridge::collectionSources()
{
    if (!tabs)
        return {};
    auto document = tabs->getCurrentDocumentMetadata();
    if (document.fileName.isEmpty())
        return {};
    if (document.dirty && !tabs->saveCurrentTab())
        return {};
    document = tabs->getCurrentDocumentMetadata();
    return QFileInfo(document.fileName).isFile() && !document.dirty ? QStringList{document.fileName}
                                                                    : QStringList{};
}
QString XipsHostBridge::exportCompleted(const QVariantMap &receipt)
{
    if (receipt.value(QStringLiteral("schema")).toString() != QStringLiteral("xips.use/v1") ||
        receipt.value(QStringLiteral("assetId")).toString().isEmpty() ||
        receipt.value(QStringLiteral("revision")).toString().isEmpty() ||
        receipt.value(QStringLiteral("contentHash")).toString().isEmpty())
        return tr("The asset receipt has no verifiable version identity; exported files were retained.");
    const QString root = receipt.value(QStringLiteral("workspace")).toString();
    const QString path = receipt.value(QStringLiteral("path")).toString();
    if (root.isEmpty() || !QFileInfo(root).isDir() || !within(path, root) || linked(root) || linked(path))
        return tr("Files were exported, but their workspace reference could not be recorded.");
    const QString folder = QDir(root).filePath(QStringLiteral(".zeroslack"));
    const QString target = QDir(folder).filePath(QStringLiteral("xips-references.json"));
    if (linked(target) || !QDir().mkpath(folder))
        return tr("Files were exported; the reference directory is unavailable.");
    QLockFile lock(target + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return tr("Files were exported; another operation is updating the asset references.");
    QString referenceError;
    auto document = readReferences(target, &referenceError);
    if (!referenceError.isEmpty())
        return referenceError;
    auto entry = QJsonObject::fromVariantMap(receipt);
    entry.remove(QStringLiteral("workspace"));
    entry.remove(QStringLiteral("schema"));
    entry.insert(QStringLiteral("path"), QDir(root).relativeFilePath(path));
    if (const auto *host = qobject_cast<NativeContextView *>(parent()))
        entry.insert("library", host->saveState().value("library").toString());
    else if (!qEnvironmentVariableIsEmpty("XIPS_LIBRARY"))
        entry.insert("library", qEnvironmentVariable("XIPS_LIBRARY"));
    entry.insert("singleFile", QFileInfo(path).isFile());
    auto assets = document.value(QStringLiteral("assets")).toArray();
    for (const auto &value : assets)
    {
        const auto previous = value.toObject();
        if (previous.value("assetId") == entry.value("assetId") &&
            previous.value("revision") == entry.value("revision") &&
            previous.value("path") == entry.value("path") &&
            (previous.value("library").isUndefined() || previous.value("library") == entry.value("library")))
        {
            if (previous.value("contentHash") != entry.value("contentHash"))
                return tr("The same asset version has a different content identity. Existing provenance was "
                          "preserved.");
            return {};
        }
    }
    QJsonObject hashes;
    const auto files = receipt.value("files").toStringList();
    if (files.isEmpty())
        return tr("The asset receipt contains no exported files.");
    for (const auto &relative : files)
    {
        if (QDir::isAbsolutePath(relative) || relative.isEmpty())
            return tr("The asset receipt contains an invalid relative path.");
        const auto exported =
            QFileInfo(path).isFile() && files.size() == 1 ? path : QDir(path).filePath(relative);
        if (!within(exported, path) || linked(exported))
            return tr("The asset receipt contains a path outside its export.");
        QFile source(exported);
        if (!source.open(QIODevice::ReadOnly))
            return tr("An exported dependency cannot be read; provenance was not changed.");
        hashes.insert(relative,
                      QString::fromLatin1(
                          QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex()));
    }
    if (!entry.value("singleFile").toBool() && files.contains("xips-asset-inputs.json"))
    {
        QFile metadata(QDir(path).filePath("xips-asset-inputs.json"));
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 16 * 1024 * 1024)
            return tr("Asset input metadata cannot be read; exported files were retained.");
        const auto inputs = QJsonDocument::fromJson(metadata.readAll()).object();
        if (inputs.value("schema").toString() != "xips.asset-inputs/v1" || !inputs.value("files").isArray())
            return tr("Asset input metadata is invalid; exported files were retained.");
        QStringList compilation, seen;
        for (const auto &value : inputs.value("files").toArray())
        {
            const auto file = value.toObject();
            const auto relative = file.value("path").toString();
            if (!hashes.contains(relative) || seen.contains(relative) ||
                relative == "xips-asset-inputs.json" || hashes.value(relative) != file.value("sha256"))
                return tr("Asset input metadata disagrees with the exported dependency set.");
            for (const auto &dependency : file.value("dependencies").toArray())
                if (!seen.contains(dependency.toString()))
                    return tr("Asset input metadata has an invalid dependency order.");
            seen << relative;
            if (QStringList{"sv", "v"}.contains(QFileInfo(relative).suffix().toLower()))
                compilation << relative;
        }
        if (seen.size() + 1 != files.size())
            return tr("Asset input metadata is incomplete.");
        entry.insert("compileOrder", QJsonArray::fromStringList(compilation));
    }
    entry.insert("baseFiles", hashes);
    assets.append(entry);
    document.insert(QStringLiteral("assets"), assets);
    const auto saveError = writeReferences(target, document);
    if (!saveError.isEmpty())
        return saveError;
    if (workspaces && workspaces->getWorkspacePath() == root)
        workspaces->refreshWorkspaceFiles();
    if (tabs && workspaces && workspaces->getWorkspacePath() == root && QFileInfo(path).isFile() &&
        QStringList{QStringLiteral("v"), QStringLiteral("sv"), QStringLiteral("svh"), QStringLiteral("vh")}
            .contains(QFileInfo(path).suffix().toLower()))
        tabs->openFileInTab(path);
    return {};
}
QString XipsHostBridge::recordPublishedVersion(const QString &workspace, const QVariantMap &expected,
                                               const QVariantMap &published, const QString &requestId,
                                               QVariantMap *saveFailure)
{
    if (saveFailure)
        saveFailure->clear();
    if (requestId.isEmpty() || published.value("library").toString().isEmpty() ||
        published.value("assetId").toString().isEmpty() || published.value("revision").toString().isEmpty() ||
        published.value("contentHash").toString().isEmpty())
        return QStringLiteral("A published version needs a request, library, asset and content identity.");
    const auto relativeRoot = published.value("path").toString();
    const auto root = QDir(workspace).filePath(relativeRoot);
    if (relativeRoot.isEmpty() || QDir::isAbsolutePath(relativeRoot) || !QFileInfo(workspace).isDir() ||
        !within(root, workspace) || linked(root) || linked(workspace))
        return QStringLiteral("The published version has an invalid workspace binding.");
    const auto files = published.value("files").toStringList();
    const auto hashes = published.value("baseFiles").toMap();
    const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    if (files.isEmpty() || hashes.size() != files.size() ||
        (published.value("singleFile").toBool() && files.size() != 1))
        return QStringLiteral("The published version has no complete file baseline.");
    QSet<QString> names;
    for (const auto &file : files)
    {
        const auto clean = QDir::cleanPath(QDir::fromNativeSeparators(file));
        if (file.isEmpty() || QDir::isAbsolutePath(file) || clean == "." || clean == ".." ||
            clean.startsWith("../") || names.contains(clean.toCaseFolded()) ||
            !sha256.match(hashes.value(file).toString()).hasMatch())
            return QStringLiteral("The published version contains an invalid file baseline.");
        names.insert(clean.toCaseFolded());
        const auto path = published.value("singleFile").toBool() ? root : QDir(root).filePath(file);
        if (!within(path, root) || linked(path))
            return QStringLiteral("The published version contains a linked or escaping file path.");
    }
    const auto target = QDir(workspace).filePath(".zeroslack/xips-references.json");
    if (linked(target) || !QDir().mkpath(QFileInfo(target).absolutePath()))
        return QStringLiteral("The workspace reference directory is unavailable.");
    QLockFile lock(target + ".lock");
    if (!lock.tryLock(0))
        return QStringLiteral(
            "Another operation is updating the asset references. Recover this update after it finishes.");
    QString error;
    auto document = readReferences(target, &error);
    if (!error.isEmpty())
        return error;
    auto assets = document.value("assets").toArray();
    const auto before = storedReference(expected);
    auto after = storedReference(published);
    after.insert("lastOperation", requestId);
    int replace = -1;
    for (int i = 0; i < assets.size(); ++i)
    {
        const auto candidate = assets[i].toObject();
        if (candidate.value("lastOperation").toString() == requestId)
        {
            if (candidate == after)
                return {};
            return QStringLiteral("This operation was already recorded with a different result.");
        }
        if (!before.isEmpty() && candidate == before)
            replace = i;
        if (before.isEmpty() && candidate.value("library") == after.value("library") &&
            candidate.value("assetId") == after.value("assetId") &&
            candidate.value("path") == after.value("path"))
            return QStringLiteral("The workspace binding changed. Its existing version was preserved.");
    }
    if (!before.isEmpty())
    {
        if (replace < 0)
            return QStringLiteral(
                "The workspace reference changed since preview. Both versions were retained.");
        if (before.value("library") != after.value("library") ||
            before.value("assetId") != after.value("assetId") || before.value("path") != after.value("path"))
            return QStringLiteral("A version update cannot change its library, asset or workspace binding.");
        assets[replace] = after;
    }
    else
        assets.append(after);
    document.insert("assets", assets);
    return writeReferences(target, document, saveFailure);
}
QVariantList XipsHostBridge::workspaceReferences(const QString &workspace, QString *error)
{
    QString issue;
    const auto document = readReferences(QDir(workspace).filePath(".zeroslack/xips-references.json"), &issue);
    if (error)
        *error = issue;
    return document.value("assets").toArray().toVariantList();
}
QVariantList XipsHostBridge::sourceBindings(const QString &workspace, const QStringList &sourceFiles)
{
    QVariantList result;
    for (const auto &value : workspaceReferences(workspace))
    {
        auto reference = value.toMap();
        const auto path = QDir(workspace).filePath(reference.value("path").toString());
        if (!within(path, workspace) || linked(path))
            continue;
        const auto files = reference.value("files").toStringList();
        const auto baseline = reference.value("baseFiles").toMap();
        bool matches = false, changed = false, missing = false;
        for (const auto &relative : files)
        {
            const auto file =
                (reference.value("singleFile").toBool() || QFileInfo(path).isFile()) && files.size() == 1
                    ? path
                    : QDir(path).filePath(relative);
            if (!within(file, path) || linked(file))
            {
                missing = true;
                continue;
            }
            for (const auto &source : sourceFiles)
                matches |= QFileInfo(source).absoluteFilePath().compare(QFileInfo(file).absoluteFilePath(),
                                                                        Qt::CaseInsensitive) == 0;
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly))
            {
                missing = true;
                continue;
            }
            if (baseline.contains(relative))
                changed |=
                    QString::fromLatin1(
                        QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex()) !=
                    baseline.value(relative).toString();
        }
        if (!matches)
            continue;
        reference.insert("localState", missing                           ? "missing"
                                       : baseline.size() != files.size() ? "unverified"
                                       : changed                         ? "modified"
                                                                         : "unchanged");
        result << reference;
    }
    return result;
}

XipsContextProvider::XipsContextProvider(TabManager *tabs, WorkspaceManager *workspaces,
                                         SimulationCommandService *commands)
    : tabs(tabs), workspaces(workspaces), commands(commands)
{
}
QString XipsContextProvider::providerId() const
{
    return QStringLiteral("xips");
}
QString XipsContextProvider::displayName() const
{
    return QStringLiteral("xIPs");
}
QString XipsContextProvider::iconKey() const
{
    return QStringLiteral(":/xips-brand/icon.png");
}
ContextResource XipsContextProvider::activationResource(const QString &workspaceId) const
{
    ContextResource resource;
    resource.providerId = providerId();
    resource.resourceId = QStringLiteral("library");
    resource.uri = QUrl(QStringLiteral("xips://show"));
    resource.title = displayName();
    resource.iconKey = iconKey();
    resource.workspaceId = workspaceId;
    return resource;
}
QWidget *XipsContextProvider::createView(const ContextResource &resource, QWidget *parent)
{
    if (commands)
        return commands->view(parent);
    auto *host = new NativeContextView(parent);
    host->setHostBridge(new XipsHostBridge(tabs, workspaces, host));
    if (workspaces)
    {
        const QPointer<WorkspaceManager> manager = workspaces;
        const auto update = [host, manager]
        {
            if (host->property("nativeComponentRetired").toBool())
                return;
            host->setWorkspace(manager && manager->isWorkspaceOpen() ? manager->getWorkspacePath()
                                                                     : QString());
        };
        QObject::connect(workspaces, &WorkspaceManager::workspaceActivated, host, update);
        QObject::connect(workspaces, &WorkspaceManager::workspaceClosed, host, update);
    }
    host->activate(resource);
    return host;
}
bool XipsContextProvider::activateView(QWidget *view, const ContextResource &resource)
{
    auto *host = qobject_cast<NativeContextView *>(view);
    return host && host->activate(resource);
}
bool XipsContextProvider::canCloseView(QWidget *view, QString *error) const
{
    auto *host = qobject_cast<NativeContextView *>(view);
    return !host || host->canClose(error);
}
void XipsContextProvider::deactivateView(QWidget *view)
{
    if (auto *host = qobject_cast<NativeContextView *>(view))
        host->retire();
}
ContextViewCapabilities XipsContextProvider::capabilities(const ContextResource &) const
{
    ContextViewCapabilities result;
    result.minimumWidth = 360;
    result.preferredWidth = 480;
    result.maximumWidth = 1000;
    result.minimumHeight = 380;
    result.preferredHeight = 620;
    return result;
}
QVariantMap XipsContextProvider::saveViewState(QWidget *view) const
{
    const auto *host = qobject_cast<NativeContextView *>(view);
    return host ? host->saveState() : QVariantMap{};
}
void XipsContextProvider::restoreViewState(QWidget *view, const QVariantMap &state)
{
    if (auto *host = qobject_cast<NativeContextView *>(view))
        host->restoreState(state);
}
