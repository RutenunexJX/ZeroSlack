#include "simulationcommandservice.h"
#include "../integrations/native/nativecontextview.h"
#include "../integrations/xips/xipscontextprovider.h"
#include "../simulation/simdock/ui/workbench.h"
#include "editorfileidentity.h"
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
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

SimulationCommandService::SimulationCommandService(TabManager *t, WorkspaceManager *w, simdock::Workbench *b,
                                                   QWidget *parent)
    : QObject(parent), tabs(t), workspaces(w), buildInputs(b), owner(parent)
{
    const auto changed = [this]
    {
        cancel();
        if (nativeView && !nativeView->property("nativeComponentRetired").toBool())
            nativeView->setWorkspace(workspaces->getWorkspacePath());
    };
    connect(w, &WorkspaceManager::workspaceActivated, this, changed);
    connect(w, &WorkspaceManager::workspaceClosed, this, changed);
}
SimulationCommandService::~SimulationCommandService()
{
    cancel();
}
void SimulationCommandService::cancel()
{
    if (pending)
        pending->store(true);
    pending.reset();
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
void SimulationCommandService::prepare()
{
    cancel();
    if (!workspaces || workspaces->getWorkspacePath().isEmpty())
        return;
    const auto entries = XipsHostBridge::workspaceReferences(workspaces->getWorkspacePath());
    for (const auto &value : entries)
    {
        const auto entry = value.toMap();
        const auto root =
            QDir(workspaces->getWorkspacePath()).absoluteFilePath(entry.value("path").toString());
        const auto metadata = QDir(root).filePath("xips-commands.json");
        if (!QFileInfo::exists(metadata))
            continue;
        const auto result = UserTemplateService::getInstance()->migrateXipsCommands(metadata, entry);
        if (!result.valid)
            emit message(result.failureReason.isEmpty()
                             ? QStringLiteral("Legacy command mappings conflict with existing Templates; "
                                              "both originals were retained.")
                             : result.failureReason);
    }
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
    context.readOnly = editor->isReadOnly();
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
    if (buildInputs)
        request.orderedFiles = buildInputs->activeCompilationInputs();
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
    request.roles = UserTemplateService::getInstance()->signalRoles();
    return request;
}
bool SimulationCommandService::current(const GlobalControlItem &item, MyCodeEditor *editor) const
{
    if (!tabs || !editor || editor->isReadOnly() || tabs->getCurrentEditor() != editor)
        return false;
    const auto *document = tabs->sharedDocumentForEditor(editor);
    return document && document->instanceSerial() == item.sourceDocumentInstance &&
           (EditorFileIdentity::same(editor->documentFileName(), item.sourceFileName) ||
            (editor->documentFileName().isEmpty() && item.sourceFileName.isEmpty())) &&
           document->textRevision() == item.sourceDocumentTextRevision &&
           editor->textCursor().position() == item.sourceCursorPosition &&
           UserTemplateService::getInstance()->catalogRevision() == item.sourceTemplateCatalogRevision;
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
void SimulationCommandService::activate(const GlobalControlItem &selected)
{
    QPointer<MyCodeEditor> editor = tabs ? tabs->getCurrentEditor() : nullptr;
    if (editor && editor->isReadOnly())
    {
        emit message(QStringLiteral("This document is read-only."));
        return;
    }
    if (!current(selected, editor))
    {
        emit message(QStringLiteral(
            "The document, cursor or template catalog changed. Reopen the insertion palette."));
        return;
    }
    if (pending)
    {
        emit message(QStringLiteral("A command is being prepared. Reopen the palette to cancel it."));
        return;
    }
    auto request = currentRequest();
    request.item = selected;
    request.context.replacementStart = selected.replacementStart;
    request.context.replacementLength = selected.replacementLength;
    const auto workspace = workspaces ? workspaces->getWorkspacePath() : QString();
    const auto project = buildInputs ? buildInputs->activeProjectId() : QString();
    const auto compilation = buildInputs ? buildInputs->activeCompilationInputs() : QStringList();
    QStringList dependencies = selected.parameters.value("compileOrder").toStringList();
    const auto kind = selected.parameters.value("kind").toString();
    if ((kind == "driver" || kind == "print") && dependencies.isEmpty())
    {
        QStringList support;
        const auto error = materializeSupport(workspace, kind, &support);
        if (!error.isEmpty())
        {
            emit message(error);
            return;
        }
        dependencies = support + dependencies;
        dependencies.removeDuplicates();
    }
    for (const auto &file : std::as_const(dependencies))
        if (!QFileInfo(file).isFile())
        {
            emit message(QStringLiteral("A package input is missing: %1").arg(file));
            return;
        }
    request.orderedFiles = dependencies + request.orderedFiles;
    request.orderedFiles.removeDuplicates();
    const auto projectIncludes = std::make_shared<QStringList>(request.includeDirectories);
    const auto packageIncludes = selected.parameters.value("includeDirectories").toStringList();
    request.includeDirectories = packageIncludes + request.includeDirectories;
    request.includeDirectories.removeDuplicates();
    pending = request.cancelled = std::make_shared<std::atomic_bool>(false);
    auto *watcher = new QFutureWatcher<SimulationCommandPlan>(this);
    connect(watcher, &QFutureWatcher<SimulationCommandPlan>::finished, this,
            [=, this]
            {
                auto result = watcher->result();
                watcher->deleteLater();
                if (pending != request.cancelled || request.cancelled->load())
                    return;
                cancel();
                const auto unchanged = [=, this]() -> QString
                {
                    if (!editor || editor->isReadOnly() || !tabs || tabs->getCurrentEditor() != editor ||
                        !workspaces || workspaces->getWorkspacePath() != workspace ||
                        (buildInputs && buildInputs->activeProjectId() != project))
                        return QStringLiteral("The active document or project changed.");
                    const auto latest = workspaces->projectSnapshot();
                    if (latest.includeDirs != *projectIncludes || latest.defines != request.defines)
                        return QStringLiteral("The compilation settings changed.");
                    if (UserTemplateService::getInstance()->catalogRevision() !=
                        selected.sourceTemplateCatalogRevision)
                        return QStringLiteral("The template catalog changed.");
                    if (UserTemplateService::getInstance()->signalRoles() != request.roles)
                        return QStringLiteral("The signal roles changed.");
                    QSet<QString> openFiles;
                    for (auto *open : tabs->openEditors())
                    {
                        openFiles.insert(open->documentFileName());
                        if (open != editor && !open->documentFileName().isEmpty() &&
                            (!request.contents.contains(open->documentFileName()) ||
                             request.contents.value(open->documentFileName()) != open->cachedDocumentText()))
                            return QStringLiteral("An open source dependency changed.");
                    }
                    for (auto it = request.contents.cbegin(); it != request.contents.cend(); ++it)
                    {
                        if (openFiles.contains(it.key()) ||
                            EditorFileIdentity::same(it.key(), request.context.fileName))
                            continue;
                        QFile file(it.key());
                        if (!file.open(QIODevice::ReadOnly) ||
                            QString::fromUtf8(file.readAll()) != it.value())
                            return QStringLiteral("A closed source dependency changed.");
                    }
                    return {};
                };
                if (!result.error.isEmpty())
                {
                    emit message(result.error);
                    return;
                }
                const auto contextChange = unchanged();
                const auto issue = !current(selected, editor)
                                       ? QStringLiteral("The document, cursor or template catalog changed.")
                                   : !contextChange.isEmpty() ? contextChange
                                   : buildInputs && buildInputs->activeCompilationInputs() != compilation
                                       ? QStringLiteral("The compilation input order changed.")
                                   : !SimulationCommandPlanner::sourcesCurrent(result.captured)
                                       ? QStringLiteral("A source or include changed on disk.")
                                       : QString();
                if (!issue.isEmpty())
                {
                    emit message(issue + QStringLiteral(" Reopen the insertion palette."));
                    return;
                }
                if (!dependencies.isEmpty() && buildInputs)
                {
                    const auto error = buildInputs->adoptHdlInputs(project, dependencies, packageIncludes);
                    if (!error.isEmpty())
                    {
                        emit message(error);
                        return;
                    }
                }
                *projectIncludes = workspaces->projectSnapshot().includeDirs;
                const auto expectedCompilation =
                    buildInputs ? buildInputs->activeCompilationInputs() : QStringList();
                const QPointer<SimulationCommandService> self(this);
                const auto captured = result.captured;
                result.inputs->current = [self, unchanged, captured, expectedCompilation]
                {
                    return self && unchanged().isEmpty() &&
                           (!self->buildInputs ||
                            self->buildInputs->activeCompilationInputs() == expectedCompilation) &&
                           SimulationCommandPlanner::sourcesCurrent(captured);
                };
                QTextCursor cursor(editor->document());
                cursor.setPosition(result.start);
                cursor.setPosition(result.start + result.length, QTextCursor::KeepAnchor);
                editor->setTextCursor(cursor);
                QString error;
                if (!editor->insertCompletionText(result.text, -1, 0, result.placeholders, &error))
                    emit message(error);
                else
                {
                    editor->setFocus();
                    emit message(result.placeholders.isEmpty()
                                     ? QStringLiteral("Inserted template.")
                                     : QStringLiteral("Use Tab to move between inputs; Up/Down selects "
                                                      "candidates. Value inputs are typed directly."));
                }
            });
    watcher->setFuture(QtConcurrent::run([request] { return SimulationCommandPlanner::prepare(request); }));
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
    connect(dialog, &QDialog::finished, this, [this] { emit catalogChanged(); });
    dialog->show();
}
