#include "xipscontextprovider.h"
#include "tabmanager.h"
#include "uicontrols.h"
#include "workspacemanager.h"
#include "../native/nativecontextview.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

namespace
{
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
    const QString root = receipt.value(QStringLiteral("workspace")).toString();
    const QString path = receipt.value(QStringLiteral("path")).toString();
    if (root.isEmpty() || !QFileInfo(root).isDir() || !within(path, root) || linked(root) ||
        linked(path))
        return tr("Files were exported, but their workspace reference could not be recorded.");
    const QString folder = QDir(root).filePath(QStringLiteral(".zeroslack"));
    const QString target = QDir(folder).filePath(QStringLiteral("xips-references.json"));
    if (linked(target) || !QDir().mkpath(folder))
        return tr("Files were exported; the reference directory is unavailable.");
    QLockFile lock(target + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return tr("Files were exported; another operation is updating the asset references.");
    QJsonObject document{{QStringLiteral("schema"), QStringLiteral("zeroslack.xips-references/v1")},
                         {QStringLiteral("assets"), QJsonArray{}}};
    if (QFileInfo::exists(target))
    {
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly))
            return tr("Cannot read existing asset references.");
        QJsonParseError error;
        const auto stored = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !stored.isObject() ||
            stored.object().value(QStringLiteral("schema")).toString() !=
                QStringLiteral("zeroslack.xips-references/v1") ||
            !stored.object().value(QStringLiteral("assets")).isArray())
            return tr("Existing asset references are invalid; they were preserved.");
        document = stored.object();
    }
    auto entry = QJsonObject::fromVariantMap(receipt);
    entry.remove(QStringLiteral("workspace"));
    entry.remove(QStringLiteral("schema"));
    entry.insert(QStringLiteral("path"), QDir(root).relativeFilePath(path));
    auto assets = document.value(QStringLiteral("assets")).toArray();
    assets.append(entry);
    document.insert(QStringLiteral("assets"), assets);
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Files were exported; their origin could not be saved.");
    const auto data = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
        return tr("Files were exported; their origin could not be saved.");
    if (workspaces && workspaces->getWorkspacePath() == root)
        workspaces->refreshWorkspaceFiles();
    if (tabs && workspaces && workspaces->getWorkspacePath() == root && QFileInfo(path).isFile() &&
        QStringList{QStringLiteral("v"), QStringLiteral("sv"), QStringLiteral("svh"),
                    QStringLiteral("vh")}
            .contains(QFileInfo(path).suffix().toLower()))
        tabs->openFileInTab(path);
    return {};
}
XipsContextProvider::XipsContextProvider(TabManager *tabs, WorkspaceManager *workspaces)
    : tabs(tabs), workspaces(workspaces)
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
    auto *host = new NativeContextView(NativeContextView::Kind::Xips, parent);
    host->setHostBridge(new XipsHostBridge(tabs, workspaces, host));
    if (workspaces) {
        const QPointer<WorkspaceManager> manager = workspaces;
        const auto update = [host, manager] {
            if (host->property("nativeComponentRetired").toBool()) return;
            host->setWorkspace(manager && manager->isWorkspaceOpen() ? manager->getWorkspacePath() : QString());
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
    if (auto *host = qobject_cast<NativeContextView *>(view)) host->retire();
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
    if (auto *host = qobject_cast<NativeContextView *>(view)) host->restoreState(state);
}
