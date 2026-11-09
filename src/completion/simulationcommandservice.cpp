#include "simulationcommandservice.h"
#include "../integrations/native/nativecontextview.h"
#include "../integrations/xips/xipscontextprovider.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "templatemanagerdialog.h"
#include "usertemplateservice.h"
#include "workspacemanager.h"
#include "xipsassetsyncdialog.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>

SimulationCommandService::SimulationCommandService(TabManager *t, WorkspaceManager *w, QWidget *parent)
    : QObject(parent), tabs(t), workspaces(w), owner(parent)
{
    const auto changed = [this]
    {
        if (nativeView && !nativeView->property("nativeComponentRetired").toBool())
            nativeView->setWorkspace(workspaces->getWorkspacePath());
    };
    connect(w, &WorkspaceManager::workspaceActivated, this, changed);
    connect(w, &WorkspaceManager::workspaceClosed, this, changed);
}
NativeContextView *SimulationCommandService::view(QWidget *parent)
{
    if (!nativeView || nativeView->property("nativeComponentRetired").toBool())
    {
        nativeView = new NativeContextView(owner);
        nativeView->hide();
        auto *bridge = new XipsHostBridge(tabs, workspaces, nativeView);
        nativeView->setHostBridge(bridge);
        XipsContextProvider provider(tabs, workspaces);
        nativeView->activate(
            provider.activationResource(workspaces ? workspaces->getWorkspacePath() : QString()));
    }
    if (parent && nativeView->parentWidget() != parent)
        nativeView->setParent(parent);
    return nativeView;
}
bool SimulationCommandService::canClose(QString *error) const
{
    if (owner)
        for (auto *dialog : owner->findChildren<XipsAssetSyncDialog *>())
            if (dialog->isBusy())
            {
                if (error)
                    *error = "Wait for the asset operation, or request cancellation in Update in xIPs.";
                return false;
            }
    return !nativeView || nativeView->canClose(error);
}
SimulationCommandRequest SimulationCommandService::currentRequest() const
{
    SimulationCommandRequest request;
    auto *editor = tabs ? tabs->getCurrentEditor() : nullptr;
    if (!editor)
        return request;
    auto &context = request.context;
    context.editorAvailable = true;
    context.fileName = editor->documentFileName();
    context.documentText = editor->cachedDocumentText();
    context.cursorPosition = editor->textCursor().position();
    context.readOnly = editor->documentReadOnly();
    context.documentRevision = editor->document()->revision();
    const auto semantic = editor->editorSemanticContextForPosition(-1, true);
    context.moduleName = semantic.moduleName;
    context.packageName = semantic.packageName;
    context.cursorLine = semantic.cursorLine;
    if (const auto *shared = tabs->sharedDocumentForEditor(editor))
    {
        context.documentInstance = shared->instanceSerial();
        context.documentTextRevision = shared->textRevision();
    }
    if (workspaces)
        request.orderedFiles = workspaces->projectSnapshot().designSourceFiles();
    if (!request.orderedFiles.contains(context.fileName))
        request.orderedFiles << context.fileName;
    if (workspaces)
    {
        const auto project = workspaces->projectSnapshot();
        request.includeDirectories = project.includeDirs;
        request.defines = project.defines;
    }
    for (const auto *open : tabs->openEditors())
        if (!open->documentFileName().isEmpty())
            request.contents.insert(open->documentFileName(), open->cachedDocumentText());
    request.roles = editor->templateSignalRoles();
    return request;
}
QString SimulationCommandService::materializeSupport(const QString &workspace, const QString &kind,
                                                     QStringList *files)
{
    if (workspace.isEmpty())
        return QStringLiteral("Open a workspace for the simulation support package.");
    const bool print = kind == "print";
    const QStringList names = print ? QStringList{"zeroslack_events_package.sv"}
                                    : QStringList{"tb_signal_package.sv",
                                                  "tb_reset_package.sv",
                                                  "tb_sequence_package.sv",
                                                  "tb_random_package.sv",
                                                  "xips-commands.json",
                                                  "LICENSE",
                                                  "NOTICE",
                                                  "THIRD-PARTY-NOTICES.md"};
    QMap<QString, QByteArray> contents;
    QCryptographicHash identity(QCryptographicHash::Sha256);
    for (const auto &name : names)
    {
        QFile resource(QStringLiteral(":/simulation/") +
                       (print ? QString() : QStringLiteral("tb-behaviors/")) + name);
        if (!resource.open(QIODevice::ReadOnly))
            return QStringLiteral("Bundled support source is unavailable: %1").arg(name);
        const auto data = resource.readAll();
        identity.addData(name.toUtf8());
        identity.addData(data);
        contents.insert(name, data);
    }
    const auto parent = QDir(workspace).filePath(".zeroslack/packages");
    const auto target = QDir(parent).filePath(
        (print ? QStringLiteral("simulation-events-") : QStringLiteral("tb-behaviors-")) +
        QString::fromLatin1(identity.result().toHex().left(16)));
    if (!QFileInfo::exists(target))
    {
        if (!QDir().mkpath(parent))
            return QStringLiteral("Cannot create the support package directory.");
        QTemporaryDir staging(QDir(parent).filePath("prepare-XXXXXX"));
        if (!staging.isValid())
            return QStringLiteral("Cannot stage the support package.");
        for (auto it = contents.cbegin(); it != contents.cend(); ++it)
        {
            QSaveFile file(QDir(staging.path()).filePath(it.key()));
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size() ||
                !file.commit())
                return QStringLiteral("Cannot save the support package; previous files were retained.");
        }
        if (!QDir().rename(staging.path(), target) && !QFileInfo(target).isDir())
            return QStringLiteral("The package destination changed. Retry without replacing existing files.");
    }
    for (const auto &name : names)
    {
        const auto path = QDir(target).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.readAll() != contents.value(name))
            return QStringLiteral("The bundled package copy was edited. Bind that copy as a user command or "
                                  "restore it explicitly; it was not overwritten.");
        if (name.endsWith(".sv"))
            files->append(path);
    }
    return {};
}
void SimulationCommandService::manage()
{
    if (auto *existing = owner ? owner->findChild<TemplateManagerDialog *>() : nullptr)
    {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dialog = new TemplateManagerDialog(UserTemplateService::getInstance(), currentRequest(),
                                             workspaces ? workspaces->getWorkspacePath() : QString(), owner);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->openAssets = [this]
    {
        if (showBrowser)
            showBrowser();
    };
    dialog->publishAssets =
        [this, dialog](const QString &kind, const QString &name, const QStringList &selected)
    {
        const auto workspace = workspaces ? workspaces->getWorkspacePath() : QString();
        if (workspace.isEmpty())
        {
            emit message("Open a workspace before saving an asset.");
            return;
        }
        auto files = selected;
        if (files.isEmpty() && (kind == "driver" || kind == "print"))
        {
            const auto error = materializeSupport(workspace, kind, &files);
            if (!error.isEmpty())
            {
                emit message(error);
                return;
            }
            dialog->setPackageInputs(files);
            if (kind == "driver" && !files.isEmpty())
                for (const auto *extra :
                     {"xips-commands.json", "LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.md"})
                    files << QDir(QFileInfo(files.first()).absolutePath()).filePath(extra);
        }
        if (files.isEmpty())
        {
            emit message("This template has no reusable package inputs.");
            return;
        }
        const auto library = nativeView ? nativeView->saveState().value("library").toString()
                                        : qEnvironmentVariable("XIPS_LIBRARY");
        auto *sync = new XipsAssetSyncDialog(workspace, files, name, library, dialog);
        sync->setAttribute(Qt::WA_DeleteOnClose);
        sync->currentContext = [this] { return currentRequest(); };
        if (workspaces)
        {
            connect(workspaces, &WorkspaceManager::workspaceActivated, sync, &XipsAssetSyncDialog::reject);
            connect(workspaces, &WorkspaceManager::workspaceClosed, sync, &XipsAssetSyncDialog::reject);
            connect(sync, &XipsAssetSyncDialog::referenceChanged, workspaces,
                    &WorkspaceManager::refreshWorkspaceFiles);
        }
        connect(sync, &QDialog::finished, dialog, [dialog] { dialog->refreshAssets(); });
        connect(sync, &XipsAssetSyncDialog::referenceChanged, this,
                [this]
                {
                    if (nativeView && nativeView->isReady())
                        QMetaObject::invokeMethod(nativeView->component(), "refresh");
                });
        sync->show();
    };
    if (workspaces)
    {
        connect(workspaces, &WorkspaceManager::workspaceActivated, dialog, &QDialog::reject);
        connect(workspaces, &WorkspaceManager::workspaceClosed, dialog, &QDialog::reject);
    }
    dialog->show();
}
