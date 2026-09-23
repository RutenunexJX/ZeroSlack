#include "xipscontextprovider.h"
#include "tabmanager.h"
#include "uicontrols.h"
#include "workspacemanager.h"
#include "xipsbrowserapi.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QVBoxLayout>

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
QWidget *browser(QWidget *view)
{
    return view ? view->findChild<QWidget *>(QStringLiteral("xipsBrowser")) : nullptr;
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
    library.setLoadHints(QLibrary::PreventUnloadHint);
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
bool XipsContextProvider::loadLibrary(QString *error)
{
    if (library.isLoaded())
    {
        *error = loadError;
        return compatible;
    }
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates{qEnvironmentVariable("XIPS_BROWSER_LIBRARY"),
                                 app.filePath(QStringLiteral("xips-browser.dll")),
                                 app.filePath(QStringLiteral("../xIPs/xips-browser.dll")),
                                 app.filePath(QStringLiteral("../xIPs/bin/xips-browser.dll")),
                                 QSettings(QStringLiteral("xIPs"), QStringLiteral("xIPs"))
                                     .value(QStringLiteral("runtime/browserLibrary"))
                                     .toString()};
    for (const auto &path : candidates)
    {
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        library.setFileName(path);
        if (!library.load())
        {
            *error = library.errorString();
            continue;
        }
        const auto abi = reinterpret_cast<XipsBrowserAbiV1>(library.resolve("xips_browser_abi_v1"));
        const auto create =
            reinterpret_cast<XipsCreateBrowserV1>(library.resolve("xips_create_browser_v1"));
        if (!abi || !create || QByteArray(abi()) != xipsExpectedBrowserAbi())
        {
            loadError = QStringLiteral("Install matching xIPs and ZeroSlack builds, then restart "
                                       "ZeroSlack. You can also open xIPs separately.");
            *error = loadError;
            return false;
        }
        compatible = true;
        return true;
    }
    return false;
}
QWidget *XipsContextProvider::createView(const ContextResource &resource, QWidget *parent)
{
    auto *host = new QWidget(parent);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    if (workspaces)
    {
        const auto updateContext = [this, host]
        {
            if (auto *panel = browser(host))
            {
                const QString root = workspaces && workspaces->isWorkspaceOpen()
                                         ? workspaces->getWorkspacePath()
                                         : QString();
                QMetaObject::invokeMethod(panel, "setContext", Q_ARG(QString, QString()),
                                          Q_ARG(QString, root));
            }
        };
        QObject::connect(workspaces, &WorkspaceManager::workspaceActivated, host, updateContext);
        QObject::connect(workspaces, &WorkspaceManager::workspaceClosed, host, updateContext);
    }
    QString error;
    if (loadLibrary(&error))
    {
        auto create =
            reinterpret_cast<XipsCreateBrowserV1>(library.resolve("xips_create_browser_v1"));
        auto *bridge = new XipsHostBridge(tabs, workspaces, host);
        layout->addWidget(create(host, bridge));
        activateView(host, resource);
    }
    else
    {
        auto *label = UiControls::label(host);
        label->setWordWrap(true);
        label->setText(
            QStringLiteral("Start the new xIPs application once, then reopen this panel.\n%1")
                .arg(error));
        layout->addWidget(label);
        auto *retry = UiControls::pushButton(QStringLiteral("Reload xIPs"), host);
        layout->addWidget(retry);
        auto *open = UiControls::pushButton(QStringLiteral("Open xIPs…"), host);
        layout->addWidget(open);
        layout->addStretch();
        QObject::connect(
            retry, &QPushButton::clicked, host,
            [this, host, layout, label, retry, open, resource]
            {
                QString error;
                if (!loadLibrary(&error))
                {
                    label->setText(
                        QStringLiteral("Launch xIPs once, then click Reload xIPs.\n%1").arg(error));
                    return;
                }
                auto create = reinterpret_cast<XipsCreateBrowserV1>(
                    library.resolve("xips_create_browser_v1"));
                auto *bridge = new XipsHostBridge(tabs, workspaces, host);
                auto *panel = create(host, bridge);
                if (!panel)
                {
                    label->setText(QStringLiteral("The xIPs panel could not be created."));
                    return;
                }
                label->hide();
                retry->hide();
                open->hide();
                delete layout->takeAt(layout->count() - 1);
                layout->addWidget(panel);
                activateView(host, resource);
            });
        QObject::connect(
            open, &QPushButton::clicked, host,
            [host, label]
            {
                const auto installed = QSettings(QStringLiteral("xIPs"), QStringLiteral("xIPs"))
                                           .value(QStringLiteral("runtime/browserLibrary"))
                                           .toString();
                QString executable =
                    QDir(QFileInfo(installed).absolutePath()).filePath(QStringLiteral("xips.exe"));
                if (installed.isEmpty() || !QFileInfo::exists(executable))
                    executable = QFileDialog::getOpenFileName(
                        host, QStringLiteral("Locate xIPs"), {}, QStringLiteral("xIPs (xips.exe)"));
                if (!executable.isEmpty() && !QProcess::startDetached(executable, {}))
                    label->setText(QStringLiteral("Could not launch xIPs."));
            });
    }
    return host;
}
bool XipsContextProvider::activateView(QWidget *view, const ContextResource &resource)
{
    auto *panel = browser(view);
    if (!panel)
        return false;
    const QString workspace =
        workspaces && workspaces->isWorkspaceOpen() ? workspaces->getWorkspacePath() : QString();
    QMetaObject::invokeMethod(panel, "setContext", Qt::DirectConnection, Q_ARG(QString, QString()),
                              Q_ARG(QString, workspace));
    restoreViewState(view, resource.state);
    return true;
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
    QVariantMap result;
    if (auto *panel = browser(view))
        QMetaObject::invokeMethod(panel, "saveState", Qt::DirectConnection,
                                  Q_RETURN_ARG(QVariantMap, result));
    return result;
}
void XipsContextProvider::restoreViewState(QWidget *view, const QVariantMap &state)
{
    if (auto *panel = browser(view))
        QMetaObject::invokeMethod(panel, "restoreState", Qt::DirectConnection,
                                  Q_ARG(QVariantMap, state));
}
