#include "navigationmanager.h"

#include "editorhoverpopup.h"
#include "editorfileidentity.h"
#include "navigationservice.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacefileoperationservice.h"
#include "workspacemanager.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {
const QString kPlanRevisionParameter =
    QStringLiteral("workspacePlanRevision");
const QString kPlanRevisionOutput =
    QStringLiteral("revisionToken");

QAction* addRegistryMenuAction(
    QMenu* menu,
    const QString& actionId,
    bool enabled,
    const std::function<void()>& trigger)
{
    if (!menu)
        return nullptr;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor)
        return nullptr;
    const ActionAliasDescriptor alias =
        descriptor->aliasForSurface(
            ActionSurface::ContextMenu);
    if (alias.label.isEmpty())
        return nullptr;

    QAction* action =
        menu->addAction(alias.label);
    action->setObjectName(alias.adapterKey);
    action->setProperty("actionId", descriptor->id);
    action->setProperty(
        "executionRoute",
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    action->setStatusTip(descriptor->description);
    action->setEnabled(enabled);
    QObject::connect(action,
                     &QAction::triggered,
                     menu,
                     trigger);
    return action;
}

Qt::CaseSensitivity fileSystemCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool pathAtOrBelow(
    const QString& candidatePath,
    const QString& sourcePath)
{
    const QString candidate =
        EditorFileIdentity::normalized(candidatePath);
    const QString source =
        EditorFileIdentity::normalized(sourcePath);
    if (candidate.isEmpty() || source.isEmpty())
        return false;
    if (candidate.compare(
            source,
            fileSystemCaseSensitivity()) == 0) {
        return true;
    }
    const QString prefix =
        source.endsWith(QLatin1Char('/'))
        ? source
        : source + QLatin1Char('/');
    return candidate.startsWith(
        prefix,
        fileSystemCaseSensitivity());
}

QString remappedChildPath(
    const QString& childPath,
    const QString& sourcePath,
    const QString& targetPath)
{
    const QString child =
        EditorFileIdentity::normalized(childPath);
    const QString source =
        EditorFileIdentity::normalized(sourcePath);
    const QString target =
        EditorFileIdentity::normalized(targetPath);
    if (!pathAtOrBelow(child, source))
        return child;
    if (child.compare(
            source,
            fileSystemCaseSensitivity()) == 0) {
        return target;
    }
    return target + child.mid(source.size());
}

void showFileOperationFailure(
    QWidget* parent,
    const ActionDescriptor& descriptor,
    const QString& reason)
{
    QMessageBox::warning(
        parent,
        descriptor.canonicalName,
        reason.isEmpty()
            ? QStringLiteral(
                  "The file operation could not be completed.")
            : reason);
}
}

WorkspaceFileOperationService*
NavigationManager::fileOperationServiceForTesting() const
{
    return fileOperationService.get();
}

void NavigationManager::onFileTreeNodeContextMenuRequested(
    const QString& requestedPath,
    bool directory,
    const QPoint& globalPos)
{
    if (!connectedWorkspaceManager
        || !connectedWorkspaceManager
                ->isWorkspaceOpen()) {
        return;
    }
    const QString workspaceRoot =
        connectedWorkspaceManager
            ->getWorkspacePath();
    const QString path =
        requestedPath.isEmpty()
        ? workspaceRoot
        : requestedPath;
    const bool rootSelected =
        EditorFileIdentity::same(
            workspaceRoot, path);

    QMenu menu(navigationWidget);
    const auto addFileAction =
        [this, &menu, path, directory](
            const QString& actionId,
            bool enabled = true) {
            return addRegistryMenuAction(
                &menu,
                actionId,
                enabled,
                [this, actionId, path, directory]() {
                    executeFileTreeAction(
                        actionId,
                        path,
                        directory);
                });
        };

    if (!directory) {
        addFileAction(
            QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen));
        menu.addSeparator();
    }
    addFileAction(
        QString::fromLatin1(
            ActionIds::WorkspaceFileCreate));
    addFileAction(
        QString::fromLatin1(
            ActionIds::WorkspaceDirectoryCreate));
    menu.addSeparator();
    if (!rootSelected) {
        addFileAction(
            QString::fromLatin1(
                ActionIds::WorkspacePathRename));
        addFileAction(
            QString::fromLatin1(
                ActionIds::WorkspacePathDelete));
        menu.addSeparator();
    }
    addFileAction(
        QString::fromLatin1(
            ActionIds::WorkspacePathCopy));
    addFileAction(
        QString::fromLatin1(
            ActionIds::WorkspacePathReveal));

    if (!directory && navigationService) {
        const QStringList modules =
            navigationService
                ->modulesDefinedInFile(path);
        if (!modules.isEmpty()) {
            menu.addSeparator();
            const QString setTopActionId =
                QString::fromLatin1(
                    ActionIds::NavigationDesignSetTop);
            if (modules.size() == 1) {
                const QString moduleName =
                    modules.first();
                addRegistryMenuAction(
                    &menu,
                    setTopActionId,
                    true,
                    [this,
                     setTopActionId,
                     moduleName]() {
                        DesignHierarchyNode node;
                        node.moduleType = moduleName;
                        requestDesignNodeAction(
                            setTopActionId, node);
                    });
            } else {
                const ActionDescriptor* descriptor =
                    findActionById(setTopActionId);
                const ActionAliasDescriptor contextAlias =
                    descriptor
                    ? descriptor->aliasForSurface(
                          ActionSurface::ContextMenu)
                    : ActionAliasDescriptor();
                if (descriptor
                    && !contextAlias.label.isEmpty()) {
                    QMenu* topMenu =
                        menu.addMenu(
                            contextAlias.label);
                    topMenu->menuAction()->setProperty(
                        "actionId", descriptor->id);
                    topMenu->menuAction()->setProperty(
                        "executionRoute",
                        descriptor->executionRoute);
                    for (const QString& moduleName :
                         modules) {
                        QAction* action =
                            topMenu->addAction(
                                moduleName);
                        action->setProperty(
                            "actionId",
                            descriptor->id);
                        action->setProperty(
                            "executionRoute",
                            descriptor->executionRoute);
                        action->setProperty(
                            "moduleType",
                            moduleName);
                        connect(
                            action,
                            &QAction::triggered,
                            this,
                            [this,
                             setTopActionId,
                             moduleName]() {
                                DesignHierarchyNode node;
                                node.moduleType =
                                    moduleName;
                                requestDesignNodeAction(
                                    setTopActionId,
                                    node);
                            });
                    }
                }
            }
        }
    }
    menu.exec(globalPos);
}

void NavigationManager::executeFileTreeAction(
    const QString& actionId,
    const QString& path,
    bool directory)
{
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor || !fileOperationService)
        return;

    ActionAvailabilityContext availabilityContext;
    availabilityContext.workspaceAvailable =
        connectedWorkspaceManager
        && connectedWorkspaceManager
               ->isWorkspaceOpen();
    const ActionAvailabilityState availability =
        evaluateActionAvailability(
            *descriptor,
            availabilityContext);
    if (!availability.executable) {
        showFileOperationFailure(
            navigationWidget,
            *descriptor,
            availability.reason);
        return;
    }

    ActionInvocation invocation;
    invocation.workspaceId =
        connectedWorkspaceManager
            ->getWorkspacePath();
    invocation.parameters.insert(
        QStringLiteral("path"), path);
    invocation.parameters.insert(
        QStringLiteral("directory"), directory);
    invocation.parameters.insert(
        QStringLiteral("parentDirectory"),
        directory
            ? path
            : QFileInfo(path)
                  .dir()
                  .absolutePath());

    if (actionId
            == QString::fromLatin1(
                ActionIds::WorkspaceFileCreate)
        || actionId
               == QString::fromLatin1(
                   ActionIds::
                       WorkspaceDirectoryCreate)
        || actionId
               == QString::fromLatin1(
                   ActionIds::WorkspacePathRename)) {
        const QString suggested =
            actionId
                    == QString::fromLatin1(
                        ActionIds::
                            WorkspacePathRename)
            ? QFileInfo(path).fileName()
            : QString();
        bool accepted = false;
        const QString name =
            QInputDialog::getText(
                navigationWidget,
                descriptor->canonicalName,
                QStringLiteral("Name:"),
                QLineEdit::Normal,
                suggested,
                &accepted);
        if (!accepted)
            return;
        invocation.parameters.insert(
            QStringLiteral("name"), name);
    }

    if (descriptor->supportsDryRun) {
        ActionInvocation previewInvocation =
            invocation;
        previewInvocation.mode =
            ActionExecutionMode::DryRun;
        const ActionExecutionResult preview =
            executeAction(
                *descriptor,
                *this,
                previewInvocation);
        if (!preview.succeeded) {
            showFileOperationFailure(
                navigationWidget,
                *descriptor,
                preview.failureReason);
            emit workspaceFileOperationFailed(
                actionId,
                path,
                preview.failureReason);
            return;
        }

        const ActionAliasDescriptor alias =
            descriptor->aliasForSurface(
                ActionSurface::ContextMenu);
        PeekContentModel content;
        content.kind = PeekContentKind::ActionPlanPreview;
        content.title = descriptor->canonicalName;
        content.rows.append(
            {QStringLiteral(
                 "Review the planned file-system change."),
             PeekContentRowRole::Warning,
             true});
        content.readOnlyText.enabled = true;
        content.readOnlyText.text =
            preview.output
                .value(QStringLiteral("preview"))
                .toString();
        content.readOnlyText.objectName =
            QStringLiteral("workspaceFileOperationPreviewText");
        content.readOnlyText.minimumSize = QSize(360, 140);
        content.readOnlyText.wordWrap = true;
        content.maximumSize = QSize(620, 360);

        PeekContentAction applyAction;
        applyAction.id = QStringLiteral("apply");
        applyAction.label = alias.label.isEmpty()
            ? descriptor->canonicalName
            : alias.label;
        applyAction.role =
            actionId
                    == QString::fromLatin1(
                        ActionIds::WorkspacePathDelete)
            ? PeekContentActionRole::Destructive
            : PeekContentActionRole::Primary;
        content.actions.append(applyAction);

        PeekContentAction cancelAction;
        cancelAction.id = QStringLiteral("cancel");
        cancelAction.label = QStringLiteral("Cancel");
        cancelAction.defaultAction = true;
        content.actions.append(cancelAction);

        const QRect anchor(
            navigationWidget->mapToGlobal(
                navigationWidget->rect().center()),
            QSize(1, 1));
        if (execPeekActionPrompt(
                navigationWidget,
                content,
                anchor,
                navigationWidget->font())
            != QStringLiteral("apply")) {
            return;
        }
        const QString revisionToken =
            preview.output
                .value(kPlanRevisionOutput)
                .toString();
        if (revisionToken.isEmpty()) {
            const QString reason =
                QStringLiteral(
                    "The preview did not provide a revision token. "
                    "No file-system change was applied.");
            showFileOperationFailure(
                navigationWidget,
                *descriptor,
                reason);
            emit workspaceFileOperationFailed(
                actionId, path, reason);
            return;
        }
        invocation.parameters.insert(
            kPlanRevisionParameter,
            revisionToken);
    }

    const ActionExecutionResult result =
        executeAction(
            *descriptor, *this, invocation);
    if (!result.succeeded) {
        showFileOperationFailure(
            navigationWidget,
            *descriptor,
            result.failureReason);
        if (actionId
            != QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen)) {
            emit workspaceFileOperationFailed(
                actionId,
                path,
                result.failureReason);
        }
        return;
    }
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen)) {
        return;
    }
    emit workspaceFileOperationCompleted(
        actionId,
        result.output
            .value(QStringLiteral("path"))
            .toString());
}

ActionExecutionResult
NavigationManager::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    ActionExecutionResult actionResult;
    actionResult.handled = true;
    if (!connectedWorkspaceManager
        || !connectedWorkspaceManager
                ->isWorkspaceOpen()) {
        actionResult.failureReason =
            QStringLiteral(
                "Open a workspace first.");
        return actionResult;
    }

    const QString workspaceRoot =
        connectedWorkspaceManager
            ->getWorkspacePath();
    if (!invocation.workspaceId.isEmpty()
        && !EditorFileIdentity::same(
            invocation.workspaceId,
            workspaceRoot)) {
        actionResult.failureReason =
            QStringLiteral(
                "The active workspace changed before "
                "the action was applied.");
        return actionResult;
    }

    const QString& route =
        descriptor.executionRoute;
    if (route
        == QStringLiteral("ui.temporaryEditor.open")) {
        const EditorLocation location =
            editorLocationFromActionParameters(
                invocation.parameters);
        if (!location.isValid()) {
            actionResult.failureReason = QStringLiteral(
                "The selected temporary-editor target is unavailable.");
            return actionResult;
        }
        emit temporaryEditorOpenRequested(location);
        if (!temporaryEditorOpenHandler) {
            actionResult.failureReason = QStringLiteral(
                "The temporary editor is unavailable.");
            emit temporaryEditorOpenFinished(
                location,
                false,
                actionResult.failureReason);
            return actionResult;
        }
        if (!temporaryEditorOpenHandler(location)) {
            actionResult.failureReason = QStringLiteral(
                "The temporary editor could not open the selected target.");
            emit temporaryEditorOpenFinished(
                location,
                false,
                actionResult.failureReason);
            return actionResult;
        }
        actionResult.output.insert(
            QStringLiteral("path"), location.filePath);
        actionResult.output.insert(
            QStringLiteral("line"), location.line);
        actionResult.succeeded = true;
        emit temporaryEditorOpenFinished(
            location, true, QString());
        return actionResult;
    }
    if (route
        == QStringLiteral(
            "waveSimulation.runDesignInstance")) {
        const QString path = invocation.parameters
                                 .value(QStringLiteral("path"))
                                 .toString();
        const QString moduleName = invocation.parameters
                                       .value(QStringLiteral("moduleType"))
                                       .toString()
                                       .trimmed();
        const QString instancePath = invocation.parameters
                                         .value(QStringLiteral("instancePath"))
                                         .toString()
                                         .trimmed();
        if (path.isEmpty() || moduleName.isEmpty()
            || instancePath.isEmpty()) {
            actionResult.failureReason = QStringLiteral(
                "The selected hierarchy instance is incomplete.");
            return actionResult;
        }
        emit waveSimulationRequested(
            path, moduleName, instancePath);
        actionResult.output.insert(
            QStringLiteral("path"), path);
        actionResult.output.insert(
            QStringLiteral("moduleName"), moduleName);
        actionResult.output.insert(
            QStringLiteral("instancePath"), instancePath);
        actionResult.succeeded = true;
        return actionResult;
    }
    if (!fileOperationService) {
        actionResult.failureReason = QStringLiteral(
            "The workspace file operation service is unavailable.");
        return actionResult;
    }
    if (route
            == QStringLiteral(
                "navigation.design.goInstantiation")
        || route
               == QStringLiteral(
                   "navigation.design.goDefinition")) {
        const QString path =
            invocation.parameters
                .value(QStringLiteral("path"))
                .toString();
        if (path.isEmpty()) {
            actionResult.failureReason =
                QStringLiteral(
                    "The selected hierarchy location is unavailable.");
            return actionResult;
        }
        DesignHierarchyNode node;
        node.id = invocation.parameters
                      .value(QStringLiteral("nodeId"))
                      .toString();
        node.rootModule =
            invocation.parameters
                .value(QStringLiteral("rootModule"))
                .toString();
        node.instancePath =
            invocation.parameters
                .value(QStringLiteral("instancePath"))
                .toString();
        node.moduleType =
            invocation.parameters
                .value(QStringLiteral("moduleType"))
                .toString();
        const int line =
            invocation.parameters
                .value(QStringLiteral("line"), -1)
                .toInt();
        navigateToDesignNodeFile(
            path, line, node);
        actionResult.output.insert(
            QStringLiteral("path"), path);
        actionResult.output.insert(
            QStringLiteral("line"), line);
        actionResult.succeeded = true;
        return actionResult;
    }
    if (route
        == QStringLiteral(
            "navigation.design.setTop")) {
        const QString moduleType =
            invocation.parameters
                .value(QStringLiteral("moduleType"))
                .toString()
                .trimmed();
        if (moduleType.isEmpty()) {
            actionResult.failureReason =
                QStringLiteral(
                    "The selected hierarchy node has no module type.");
            return actionResult;
        }
        setDesignTop(moduleType);
        actionResult.output.insert(
            QStringLiteral("moduleType"),
            moduleType);
        actionResult.succeeded = true;
        return actionResult;
    }

    const QString path =
        invocation.parameters
            .value(QStringLiteral("path"))
            .toString();
    const QString parentDirectory =
        invocation.parameters
            .value(
                QStringLiteral(
                    "parentDirectory"))
            .toString();
    const QString name =
        invocation.parameters
            .value(QStringLiteral("name"))
            .toString();

    const auto makePlan =
        [&]() {
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.file.create")) {
                return fileOperationService
                    ->planCreateFile(
                        workspaceRoot,
                        parentDirectory,
                        name);
            }
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.directory.create")) {
                return fileOperationService
                    ->planCreateDirectory(
                        workspaceRoot,
                        parentDirectory,
                        name);
            }
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.path.rename")) {
                return fileOperationService
                    ->planRename(
                        workspaceRoot,
                        path,
                        name);
            }
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.path.delete")) {
                return fileOperationService
                    ->planDelete(
                        workspaceRoot,
                        path);
            }
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.path.copy")) {
                return fileOperationService
                    ->planCopyFullPath(
                        workspaceRoot,
                        path);
            }
            if (descriptor.executionRoute
                == QStringLiteral(
                    "workspace.path.reveal")) {
                return fileOperationService
                    ->planReveal(
                        workspaceRoot,
                        path);
            }
            WorkspaceFileOperationPlan invalid;
            invalid.failureReason =
                QStringLiteral(
                    "Unknown workspace file action route: %1")
                    .arg(
                        descriptor.executionRoute);
            return invalid;
        };

    WorkspaceFileOperationPlan plan =
        makePlan();
    if (!plan.valid) {
        actionResult.failureReason =
            plan.failureReason;
        return actionResult;
    }
    const QString expectedRevision =
        invocation.parameters
            .value(kPlanRevisionParameter)
            .toString();
    if (invocation.mode
            == ActionExecutionMode::Execute
        && !expectedRevision.isEmpty()
        && expectedRevision
               != plan.revisionToken) {
        actionResult.failureReason =
            QStringLiteral(
                "The selected path changed after preview. "
                "Review a new plan before applying it.");
        return actionResult;
    }
    actionResult.output.insert(
        QStringLiteral("preview"),
        plan.preview);
    actionResult.output.insert(
        kPlanRevisionOutput,
        plan.revisionToken);
    actionResult.output.insert(
        QStringLiteral("path"),
        plan.targetPath.isEmpty()
            ? plan.sourcePath
            : plan.targetPath);
    if (invocation.mode
        == ActionExecutionMode::DryRun) {
        actionResult.succeeded = true;
        actionResult.dryRun = true;
        actionResult.message = plan.preview;
        return actionResult;
    }

    const bool pathMutation =
        plan.kind
            == WorkspaceFileOperationKind::Rename
        || plan.kind
               == WorkspaceFileOperationKind::Delete;
    if (pathMutation && connectedTabManager) {
        QString pendingFailure;
        if (!connectedTabManager
                 ->prepareWorkspacePathMutation(
                     plan.sourcePath,
                     plan.sourceDirectory,
                     navigationWidget,
                     &pendingFailure)) {
            actionResult.failureReason =
                pendingFailure;
            return actionResult;
        }
        // Saving a pending document legitimately changes the source
        // snapshot. Re-analyze after the unified pending-document
        // decision, immediately before atomic application.
        plan = makePlan();
        if (!plan.valid) {
            actionResult.failureReason =
                plan.failureReason;
            return actionResult;
        }
    }

    const WorkspaceFileOperationResult result =
        fileOperationService->apply(plan);
    if (!result.succeeded) {
        actionResult.failureReason =
            result.failureReason;
        return actionResult;
    }

    QString closeFailure;
    if (pathMutation && connectedTabManager) {
        connectedTabManager
            ->finalizeWorkspacePathMutation(
                plan.sourcePath,
                plan.sourceDirectory,
                &closeFailure);
    }

    if (plan.kind
            == WorkspaceFileOperationKind::
                CopyFullPath) {
        QClipboard* clipboard =
            QApplication::clipboard();
        if (!clipboard) {
            actionResult.failureReason =
                QStringLiteral(
                    "The system clipboard is unavailable.");
            return actionResult;
        }
        clipboard->setText(result.path);
    } else if (
        plan.kind
        == WorkspaceFileOperationKind::
            RevealInFileManager) {
        bool opened = false;
#ifdef Q_OS_WIN
        if (!plan.sourceDirectory) {
            opened = QProcess::startDetached(
                QStringLiteral("explorer.exe"),
                {QStringLiteral("/select,"),
                 QDir::toNativeSeparators(
                     result.path)});
        } else {
            opened =
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(
                        result.path));
        }
#else
        opened = QDesktopServices::openUrl(
            QUrl::fromLocalFile(
                plan.sourceDirectory
                    ? result.path
                    : QFileInfo(result.path)
                          .absolutePath()));
#endif
        if (!opened) {
            actionResult.failureReason =
                QStringLiteral(
                    "The system file manager could not be opened.");
            return actionResult;
        }
    } else {
        if (navigationWidget) {
            if (plan.kind
                == WorkspaceFileOperationKind::
                    CreateFile) {
                navigationWidget
                    ->registerWorkspacePath(
                        plan.targetPath,
                        false);
            } else if (
                plan.kind
                == WorkspaceFileOperationKind::
                    CreateDirectory) {
                navigationWidget
                    ->registerWorkspacePath(
                        plan.targetPath,
                        true);
            } else if (
                plan.kind
                == WorkspaceFileOperationKind::
                    Rename) {
                navigationWidget
                    ->renameWorkspacePath(
                        plan.sourcePath,
                        plan.targetPath,
                        plan.sourceDirectory);
            } else if (
                plan.kind
                == WorkspaceFileOperationKind::
                    Delete) {
                navigationWidget
                    ->unregisterWorkspacePath(
                        plan.sourcePath,
                        plan.sourceDirectory);
            }
        }
        refreshAfterFileOperation(plan);
        if (plan.kind
            == WorkspaceFileOperationKind::
                CreateFile) {
            navigateToFile(plan.targetPath);
        }
    }

    if (!closeFailure.isEmpty()) {
        actionResult.failureReason =
            closeFailure;
        actionResult.output.insert(
            QStringLiteral("path"),
            result.path);
        return actionResult;
    }

    actionResult.succeeded = true;
    actionResult.message =
        plan.preview;
    actionResult.output.insert(
        QStringLiteral("path"),
        result.path);
    if (!result.recoveredPath.isEmpty()) {
        actionResult.output.insert(
            QStringLiteral("recoveredPath"),
            result.recoveredPath);
    }
    return actionResult;
}

void NavigationManager::refreshAfterFileOperation(
    const WorkspaceFileOperationPlan& plan)
{
    updateFileCacheAfterOperation(plan);
    caches.fileHierarchyValid = false;
    if (currentView == FileHierarchyView)
        refreshFileHierarchy();
    if (connectedWorkspaceManager)
        connectedWorkspaceManager
            ->refreshWorkspaceFiles();
}

void NavigationManager::updateFileCacheAfterOperation(
    const WorkspaceFileOperationPlan& plan)
{
    QStringList updated;
    updated.reserve(caches.fileList.size() + 1);
    for (const QString& filePath :
         std::as_const(caches.fileList)) {
        if (plan.kind
                == WorkspaceFileOperationKind::
                    Delete
            && pathAtOrBelow(
                filePath, plan.sourcePath)) {
            continue;
        }
        if (plan.kind
                == WorkspaceFileOperationKind::
                    Rename
            && pathAtOrBelow(
                filePath, plan.sourcePath)) {
            updated.append(
                remappedChildPath(
                    filePath,
                    plan.sourcePath,
                    plan.targetPath));
            continue;
        }
        updated.append(filePath);
    }
    if (plan.kind
        == WorkspaceFileOperationKind::CreateFile) {
        updated.append(plan.targetPath);
    }
    updated.removeDuplicates();
    std::sort(
        updated.begin(),
        updated.end(),
        [](const QString& left,
           const QString& right) {
            return left.compare(
                       right,
                       Qt::CaseInsensitive)
                < 0;
        });
    caches.fileList = updated;
    caches.fileListValid = true;
}
