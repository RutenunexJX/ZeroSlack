#include "actionregistry.h"
#include "editorhoverpopup.h"
#include "editorfileidentity.h"
#include "navigationmanager.h"
#include "navigationwidget.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "workspacefileoperationservice.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                name);
}

bool writeText(const QString& path,
               const QByteArray& text)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(text) == text.size()
        && file.commit();
}

QTreeWidgetItem* findPathItem(
    QTreeWidgetItem* item,
    const QString& path)
{
    if (!item)
        return nullptr;
    if (EditorFileIdentity::same(
            item->data(0, Qt::UserRole)
                .toString(),
            path)) {
        return item;
    }
    for (int index = 0;
         index < item->childCount();
         ++index) {
        if (QTreeWidgetItem* found =
                findPathItem(
                    item->child(index),
                    path)) {
            return found;
        }
    }
    return nullptr;
}

QTreeWidgetItem* findPathItem(
    QTreeWidget* tree,
    const QString& path)
{
    if (!tree)
        return nullptr;
    for (int index = 0;
         index < tree->topLevelItemCount();
         ++index) {
        if (QTreeWidgetItem* found =
                findPathItem(
                    tree->topLevelItem(index),
                    path)) {
            return found;
        }
    }
    return nullptr;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    QWidget previewHost;
    previewHost.resize(760, 440);
    previewHost.show();
    QApplication::processEvents();

    PeekContentModel previewContent;
    previewContent.kind =
        PeekContentKind::ActionPlanPreview;
    previewContent.title =
        QStringLiteral("Rename Workspace Path");
    previewContent.rows.append(
        {QStringLiteral(
             "Review the planned file-system change."),
         PeekContentRowRole::Warning,
         true});
    previewContent.readOnlyText.enabled = true;
    previewContent.readOnlyText.text =
        QStringLiteral("Rename rtl/old.sv to rtl/new.sv");
    previewContent.readOnlyText.objectName =
        QStringLiteral("workspaceFileOperationPreviewText");
    PeekContentAction applyPreview;
    applyPreview.id = QStringLiteral("apply");
    applyPreview.label = QStringLiteral("Rename");
    applyPreview.role = PeekContentActionRole::Primary;
    previewContent.actions.append(applyPreview);
    PeekContentAction cancelPreview;
    cancelPreview.id = QStringLiteral("cancel");
    cancelPreview.label = QStringLiteral("Cancel");
    cancelPreview.defaultAction = true;
    previewContent.actions.append(cancelPreview);

    bool embeddedPreviewObserved = false;
    bool completePlanObserved = false;
    QTimer::singleShot(0, &previewHost, [&]() {
        EditorHoverPopup* peek =
            previewHost.findChild<EditorHoverPopup*>(
                QString(), Qt::FindDirectChildrenOnly);
        QPlainTextEdit* plan = peek
            ? peek->findChild<QPlainTextEdit*>(
                  QStringLiteral(
                      "workspaceFileOperationPreviewText"))
            : nullptr;
        QPushButton* apply = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.apply"))
            : nullptr;
        embeddedPreviewObserved =
            peek
            && peek->isVisible()
            && !peek->isWindow()
            && QApplication::activeModalWidget() == nullptr;
        completePlanObserved =
            plan
            && plan->isReadOnly()
            && plan->toPlainText()
                   == previewContent.readOnlyText.text;
        if (apply)
            apply->click();
    });
    QTimer::singleShot(1000, &previewHost, [&]() {
        if (EditorHoverPopup* peek =
                previewHost.findChild<EditorHoverPopup*>(
                    QString(), Qt::FindDirectChildrenOnly)) {
            peek->closePopup();
        }
    });
    const QString appliedPreviewAction =
        execPeekActionPrompt(
            &previewHost,
            previewContent,
            QRect(previewHost.mapToGlobal(
                      previewHost.rect().center()),
                  QSize(1, 1)),
            previewHost.font());
    expect("file-operation plan uses a non-modal embedded Peek",
           embeddedPreviewObserved
               && completePlanObserved
               && appliedPreviewAction
                      == QStringLiteral("apply"));

    bool cancelActionObserved = false;
    QTimer::singleShot(0, &previewHost, [&]() {
        EditorHoverPopup* peek =
            previewHost.findChild<EditorHoverPopup*>(
                QString(), Qt::FindDirectChildrenOnly);
        QPushButton* cancel = peek
            ? peek->findChild<QPushButton*>(
                  QStringLiteral("peekAction.cancel"))
            : nullptr;
        cancelActionObserved = cancel && cancel->isDefault();
        QKeyEvent enter(
            QEvent::KeyPress,
            Qt::Key_Return,
            Qt::NoModifier);
        QApplication::sendEvent(&previewHost, &enter);
    });
    QTimer::singleShot(1000, &previewHost, [&]() {
        if (EditorHoverPopup* peek =
                previewHost.findChild<EditorHoverPopup*>(
                    QString(), Qt::FindDirectChildrenOnly)) {
            peek->closePopup();
        }
    });
    const QString cancelledPreviewAction =
        execPeekActionPrompt(
            &previewHost,
            previewContent,
            QRect(previewHost.mapToGlobal(
                      previewHost.rect().center()),
                  QSize(1, 1)),
            previewHost.font());
    expect("file-operation Peek keeps Cancel as the safe Enter default",
           cancelActionObserved
               && cancelledPreviewAction
                      == QStringLiteral("cancel"));

    QTemporaryDir sandbox;
    expect("temporary sandbox is available",
           sandbox.isValid());
    const QString workspace =
        QDir(sandbox.path())
            .absoluteFilePath(
                QStringLiteral("workspace"));
    const QString outside =
        QDir(sandbox.path())
            .absoluteFilePath(
                QStringLiteral("outside"));
    expect("test directories are created",
           QDir().mkpath(workspace)
               && QDir().mkpath(outside));

    WorkspaceFileOperationService service;
    const WorkspaceFileOperationPlan escapePlan =
        service.planCreateFile(
            workspace,
            outside,
            QStringLiteral("escape.sv"));
    expect("create rejects a parent outside workspace identity",
           !escapePlan.valid
               && !escapePlan.failureReason.isEmpty());

    const WorkspaceFileOperationPlan createPlan =
        service.planCreateFile(
            workspace,
            workspace,
            QStringLiteral("created.sv"));
    expect("new-file plan is structured and previewable",
           createPlan.valid
               && createPlan.preview.contains(
                   QStringLiteral("created.sv"))
               && !createPlan.targetPath.isEmpty());
    const WorkspaceFileOperationResult createResult =
        service.apply(createPlan);
    expect("new file is atomically created",
           createResult.succeeded
               && QFileInfo::exists(
                   createPlan.targetPath));

    const WorkspaceFileOperationPlan directoryPlan =
        service.planCreateDirectory(
            workspace,
            workspace,
            QStringLiteral("rtl"));
    const WorkspaceFileOperationResult directoryResult =
        service.apply(directoryPlan);
    expect("new directory is created inside workspace",
           directoryResult.succeeded
               && QFileInfo(
                      directoryPlan.targetPath)
                      .isDir());
    const QString directoryChild =
        QDir(directoryPlan.targetPath)
            .absoluteFilePath(
                QStringLiteral("child.sv"));
    expect("directory rename fixture is written",
           writeText(
               directoryChild,
               "module child; endmodule\n"));
    const WorkspaceFileOperationPlan
        directoryRenamePlan =
            service.planRename(
                workspace,
                directoryPlan.targetPath,
                QStringLiteral("source"));
    const WorkspaceFileOperationResult
        directoryRenameResult =
            service.apply(
                directoryRenamePlan);
    expect("directory rename preserves nested content",
           directoryRenameResult.succeeded
               && QFileInfo::exists(
                   QDir(
                       directoryRenamePlan
                           .targetPath)
                       .absoluteFilePath(
                           QStringLiteral(
                               "child.sv")))
               && !QFileInfo::exists(
                   directoryRenamePlan
                       .sourcePath));

    const WorkspaceFileOperationPlan renamePlan =
        service.planRename(
            workspace,
            createPlan.targetPath,
            QStringLiteral("renamed.sv"));
    const WorkspaceFileOperationResult renameResult =
        service.apply(renamePlan);
    expect("file rename applies one sibling operation",
           renameResult.succeeded
               && QFileInfo::exists(
                   renamePlan.targetPath)
               && !QFileInfo::exists(
                   renamePlan.sourcePath));

    const QString changingPath =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("changing.sv"));
    expect("stale-plan fixture is written",
           writeText(changingPath, "a"));
    const WorkspaceFileOperationPlan stalePlan =
        service.planRename(
            workspace,
            changingPath,
            QStringLiteral("stale_target.sv"));
    expect("mutating plan captures an exact content revision",
           stalePlan.sourceSnapshot.contentDigest.size()
               == 32
               && !stalePlan.revisionToken.isEmpty());
    expect("stale-plan source is changed without changing its size",
           writeText(changingPath, "b"));
    QFile changingFile(changingPath);
    const bool changingFileOpened =
        changingFile.open(QIODevice::ReadWrite);
    const bool timestampRestored =
        changingFileOpened
        && changingFile.setFileTime(
            stalePlan.sourceSnapshot.modifiedUtc,
            QFileDevice::FileModificationTime);
    changingFile.close();
    const WorkspaceFileSnapshot changedSnapshot =
        WorkspaceFileOperationService::snapshot(
            changingPath);
    expect("content digest detects same-size same-timestamp revisions",
           timestampRestored
               && changedSnapshot.size
                      == stalePlan.sourceSnapshot.size
               && changedSnapshot.modifiedUtc
                      == stalePlan.sourceSnapshot.modifiedUtc
               && changedSnapshot.contentDigest
                      != stalePlan.sourceSnapshot.contentDigest);
    const WorkspaceFileOperationResult staleResult =
        service.apply(stalePlan);
    expect("apply rejects source changed after preview",
           !staleResult.succeeded
               && QFileInfo::exists(changingPath)
               && !QFileInfo::exists(
                   stalePlan.targetPath));
    const WorkspaceFileOperationPlan copyPathPlan =
        service.planCopyFullPath(
            workspace,
            changingPath);
    expect("read-only path actions avoid full-file hashing",
           copyPathPlan.valid
               && copyPathPlan.sourceSnapshot
                      .contentDigest.isEmpty()
               && !copyPathPlan.revisionToken.isEmpty());

    const QString routeSource =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("route_source.sv"));
    const QString routeTarget =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("route_target.sv"));
    expect("action-route revision fixture is written",
           writeText(routeSource, "abcd"));
    WorkspaceManager workspaceManager;
    workspaceManager
        .setRecentWorkspacePersistenceEnabledForTesting(
            false);
    expect("action-route workspace opens",
           workspaceManager.openWorkspace(workspace));
    NavigationManager navigationManager;
    navigationManager.connectToWorkspaceManager(
        &workspaceManager);
    DesignHierarchyNode hierarchyNode;
    hierarchyNode.id = QStringLiteral("top/u_child");
    hierarchyNode.rootModule = QStringLiteral("top");
    hierarchyNode.instanceName = QStringLiteral("u_child");
    hierarchyNode.instancePath =
        QStringLiteral("top/u_child");
    hierarchyNode.moduleType =
        QStringLiteral("child");
    hierarchyNode.instanceFile = routeSource;
    hierarchyNode.instanceLine = 17;
    hierarchyNode.definitionFile = routeTarget;
    hierarchyNode.definitionLine = 4;

    const QList<DesignHierarchyContextAction>
        hierarchyActions =
            navigationManager
                .designNodeContextActions(
                    hierarchyNode);
    const QStringList expectedHierarchyActionIds = {
        QString::fromLatin1(
            ActionIds::NavigationDesignGoInstantiation),
        QString::fromLatin1(
            ActionIds::NavigationDesignGoDefinition),
        QString::fromLatin1(
            ActionIds::NavigationDesignSetTop),
    };
    QStringList actualHierarchyActionIds;
    bool hierarchyActionMetadataMatches =
        hierarchyActions.size()
        == expectedHierarchyActionIds.size();
    for (const DesignHierarchyContextAction& action :
         hierarchyActions) {
        actualHierarchyActionIds.append(
            action.actionId);
        const ActionDescriptor* descriptor =
            findActionById(action.actionId);
        hierarchyActionMetadataMatches =
            hierarchyActionMetadataMatches
            && descriptor
            && action.label
                   == descriptor
                          ->aliasForSurface(
                              ActionSurface::ContextMenu)
                          .label
            && action.executionRoute
                   == descriptor->executionRoute
            && action.enabled;
    }
    expect("design hierarchy context model consumes Registry metadata",
           actualHierarchyActionIds
                   == expectedHierarchyActionIds
               && hierarchyActionMetadataMatches
               && hierarchyActions.value(2)
                      .separatorBefore);

    QString navigatedHierarchyFile;
    int navigatedHierarchyLine = -1;
    HierarchyInstanceContext navigatedHierarchyContext;
    QObject::connect(
        &navigationManager,
        &NavigationManager::instanceNavigationRequested,
        &navigationManager,
        [&](const QString& filePath,
            int line,
            const HierarchyInstanceContext& instanceContext) {
            navigatedHierarchyFile = filePath;
            navigatedHierarchyLine = line;
            navigatedHierarchyContext = instanceContext;
        });
    const ActionExecutionResult instantiationNavigation =
        navigationManager.requestDesignNodeAction(
            QString::fromLatin1(
                ActionIds::
                    NavigationDesignGoInstantiation),
            hierarchyNode);
    expect("hierarchy instantiation Action preserves instance context",
           instantiationNavigation.succeeded
               && navigatedHierarchyFile
                      == hierarchyNode.instanceFile
               && navigatedHierarchyLine
                      == hierarchyNode.instanceLine
               && navigatedHierarchyContext.workspacePath
                      == workspaceManager.getWorkspacePath()
               && navigatedHierarchyContext.activeTopModule
                      == hierarchyNode.rootModule
               && navigatedHierarchyContext.instancePath
                      == hierarchyNode.instancePath);

    const ActionExecutionResult definitionNavigation =
        navigationManager.requestDesignNodeAction(
            QString::fromLatin1(
                ActionIds::
                    NavigationDesignGoDefinition),
            hierarchyNode);
    expect("hierarchy definition Action uses the registered route",
           definitionNavigation.succeeded
               && navigatedHierarchyFile
                      == hierarchyNode.definitionFile
               && navigatedHierarchyLine
                      == hierarchyNode.definitionLine);

    const ActionExecutionResult setTopResult =
        navigationManager.requestDesignNodeAction(
            QString::fromLatin1(
                ActionIds::NavigationDesignSetTop),
            hierarchyNode);
    DesignHierarchyNode topNode = hierarchyNode;
    topNode.isTop = true;
    const QList<DesignHierarchyContextAction>
        topNodeActions =
            navigationManager
                .designNodeContextActions(topNode);
    expect("Set Design Top Action updates selection and root availability",
           setTopResult.succeeded
               && navigationManager
                      .selectedDesignTopModule()
                      == hierarchyNode.moduleType
               && !topNodeActions.isEmpty()
               && !topNodeActions.first().enabled
               && topNodeActions.value(1).enabled
               && topNodeActions.value(2).enabled);

    const ActionDescriptor* routeRenameDescriptor =
        findActionById(
            QString::fromLatin1(
                ActionIds::WorkspacePathRename));
    ActionInvocation routePreviewInvocation;
    routePreviewInvocation.workspaceId = workspace;
    routePreviewInvocation.mode =
        ActionExecutionMode::DryRun;
    routePreviewInvocation.parameters.insert(
        QStringLiteral("path"), routeSource);
    routePreviewInvocation.parameters.insert(
        QStringLiteral("name"),
        QFileInfo(routeTarget).fileName());
    const ActionExecutionResult routePreview =
        routeRenameDescriptor
        ? executeAction(
              *routeRenameDescriptor,
              navigationManager,
              routePreviewInvocation)
        : ActionExecutionResult();
    const QString previewRevision =
        routePreview.output
            .value(QStringLiteral("revisionToken"))
            .toString();
    expect("dry-run exposes the reviewed plan revision",
           routePreview.succeeded
               && routePreview.dryRun
               && !previewRevision.isEmpty());
    expect("action-route source changes during preview",
           writeText(routeSource, "wxyz"));
    ActionInvocation routeApplyInvocation =
        routePreviewInvocation;
    routeApplyInvocation.mode =
        ActionExecutionMode::Execute;
    routeApplyInvocation.parameters.insert(
        QStringLiteral("workspacePlanRevision"),
        previewRevision);
    const ActionExecutionResult staleRouteResult =
        routeRenameDescriptor
        ? executeAction(
              *routeRenameDescriptor,
              navigationManager,
              routeApplyInvocation)
        : ActionExecutionResult();
    expect("action route rejects a source changed after preview",
           !staleRouteResult.succeeded
               && staleRouteResult.failureReason.contains(
                   QStringLiteral("changed after preview"))
               && QFileInfo::exists(routeSource)
               && !QFileInfo::exists(routeTarget));

    const WorkspaceFileOperationPlan rootDelete =
        service.planDelete(workspace, workspace);
    expect("workspace root deletion is rejected",
           !rootDelete.valid
               && rootDelete.failureReason.contains(
                   QStringLiteral("workspace root"),
                   Qt::CaseInsensitive));

    const QString fakeTrash =
        QDir(sandbox.path()).absoluteFilePath(
            QStringLiteral("fake-trash"));
    expect("fake recoverable trash exists",
           QDir().mkpath(fakeTrash));
    service.setTrashMoverForTesting(
        [fakeTrash](const QString& source,
                    QString* recovered,
                    QString* failure) {
            const QString destination =
                QDir(fakeTrash).absoluteFilePath(
                    QFileInfo(source).fileName());
            if (!QFile::rename(source, destination)) {
                if (failure) {
                    *failure =
                        QStringLiteral(
                            "fake trash move failed");
                }
                return false;
            }
            if (recovered)
                *recovered = destination;
            if (failure)
                failure->clear();
            return true;
        });
    const WorkspaceFileOperationPlan deletePlan =
        service.planDelete(
            workspace,
            renamePlan.targetPath);
    const WorkspaceFileOperationResult deleteResult =
        service.apply(deletePlan);
    expect("delete uses injected recoverable-trash operation",
           deleteResult.succeeded
               && !deleteResult.recoveredPath.isEmpty()
               && !QFileInfo::exists(
                   deletePlan.sourcePath)
               && QFileInfo::exists(
                   deleteResult.recoveredPath));

    const QString retainedPath =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("retained.sv"));
    expect("non-destructive failure fixture is written",
           writeText(retainedPath, "module retained; endmodule\n"));
    service.setTrashMoverForTesting(
        [](const QString&,
           QString*,
           QString* failure) {
            if (failure) {
                *failure = QStringLiteral(
                    "recoverable deletion unavailable");
            }
            return false;
        });
    const WorkspaceFileOperationResult retainedResult =
        service.apply(
            service.planDelete(
                workspace, retainedPath));
    expect("trash failure never falls back to permanent deletion",
           !retainedResult.succeeded
               && QFileInfo::exists(retainedPath)
               && retainedResult.failureReason.contains(
                   QStringLiteral("recoverable")));

    const QString emptyDirectory =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("empty"));
    expect("empty directory fixture is created",
           QDir().mkpath(emptyDirectory));
    NavigationWidget navigation;
    navigation.setWorkspaceRoot(workspace);
    navigation.updateFileHierarchy({retainedPath});
    navigation.registerWorkspacePath(
        emptyDirectory, true);
    QTreeWidget* fileTree =
        navigation.findChild<QTreeWidget*>(
            QStringLiteral(
                "navigationFileTree"));
    QTreeWidgetItem* directoryItem =
        findPathItem(fileTree, emptyDirectory);
    expect("file tree gives directory nodes normalized identities",
           directoryItem
               && directoryItem
                      ->data(
                          0,
                          NavigationWidget::
                              FileTreeKindRole)
                      .toInt()
                      == NavigationWidget::
                          DirectoryItem
               && EditorFileIdentity::same(
                   directoryItem
                       ->data(
                           0, Qt::UserRole)
                       .toString(),
                   emptyDirectory));

    const QStringList fileActionIds = {
        QString::fromLatin1(
            ActionIds::WorkspaceFileCreate),
        QString::fromLatin1(
            ActionIds::WorkspaceDirectoryCreate),
        QString::fromLatin1(
            ActionIds::WorkspacePathRename),
        QString::fromLatin1(
            ActionIds::WorkspacePathDelete),
        QString::fromLatin1(
            ActionIds::WorkspacePathCopy),
        QString::fromLatin1(
            ActionIds::WorkspacePathReveal),
    };
    bool descriptorsComplete = true;
    for (const QString& actionId :
         fileActionIds) {
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        const ActionAliasDescriptor alias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ContextMenu)
            : ActionAliasDescriptor();
        descriptorsComplete =
            descriptorsComplete
            && descriptor
            && !descriptor
                    ->canonicalName.isEmpty()
            && !descriptor
                    ->executionRoute.isEmpty()
            && !descriptor
                    ->unavailableReason.isEmpty()
            && !alias.label.isEmpty()
            && !alias.adapterKey.isEmpty();
    }
    const ActionDescriptor* renameDescriptor =
        findActionById(
            QString::fromLatin1(
                ActionIds::
                    WorkspacePathRename));
    const ActionDescriptor* deleteDescriptor =
        findActionById(
            QString::fromLatin1(
                ActionIds::
                    WorkspacePathDelete));
    expect("file-tree actions use complete registry metadata",
           descriptorsComplete
               && renameDescriptor
               && deleteDescriptor
               && renameDescriptor->supportsDryRun
               && deleteDescriptor->supportsDryRun);

    const QString pendingDirectory =
        QDir(workspace).absoluteFilePath(
            QStringLiteral("open-directory"));
    expect("pending-document directory is created",
           QDir().mkpath(pendingDirectory));
    const QString pendingPath =
        QDir(pendingDirectory).absoluteFilePath(
            QStringLiteral("pending.sv"));
    expect("pending-document fixture is written",
           writeText(
               pendingPath,
               "module pending; endmodule\n"));
    QTabWidget tabs;
    TabManager tabManager(&tabs);
    tabManager.setCrashRecoveryService(
        std::make_unique<
            CrashRecoveryService>(
            QDir(sandbox.path())
                .absoluteFilePath(
                    QStringLiteral(
                        "recovery"))));
    expect("pending document opens",
           tabManager.openFileInTab(
               pendingPath));
    MyCodeEditor* pendingEditor =
        tabManager.getCurrentEditor();
    expect("pending editor is available",
           pendingEditor != nullptr);
    if (pendingEditor)
        pendingEditor->insertPlainText(
            QStringLiteral("// local\n"));
    SharedDocument* pendingDocument =
        tabManager.sharedDocumentForEditor(
            pendingEditor);
    if (pendingDocument) {
        pendingDocument->setExternalState(
            SharedDocumentExternalState::
                Conflict);
    }

    QString mutationFailure;
    expect("locked affected tab blocks path mutation",
           pendingEditor
               && tabManager.setTabLocked(
                   pendingEditor, true)
               && !tabManager
                       .prepareWorkspacePathMutation(
                           pendingDirectory,
                           true,
                           nullptr,
                           &mutationFailure)
               && mutationFailure.contains(
                   QStringLiteral("Unlock")));
    if (pendingEditor)
        tabManager.setTabLocked(
            pendingEditor, false);

    bool sawConflict = false;
    tabManager
        .unsavedDocumentManagerForTesting()
        ->setDecisionProvider(
            [&sawConflict](
                const QList<
                    PendingDocumentChange>&
                    changes,
                QWidget*) {
                sawConflict =
                    !changes.isEmpty()
                    && changes.first().kind
                           == PendingDocumentChangeKind::
                               Conflict;
                return UnsavedDocumentBatchDecision::
                    Cancel;
            });
    mutationFailure.clear();
    expect("cancelled unified conflict flow prevents mutation",
           !tabManager
                .prepareWorkspacePathMutation(
                    pendingDirectory,
                    true,
                    nullptr,
                    &mutationFailure)
               && sawConflict
               && tabManager.editorCount() == 1);

    tabManager
        .unsavedDocumentManagerForTesting()
        ->setDecisionProvider(
            [](const QList<
                   PendingDocumentChange>&,
               QWidget*) {
                return UnsavedDocumentBatchDecision::
                    DiscardAll;
            });
    mutationFailure.clear();
    expect("resolved pending document permits mutation preflight",
           tabManager
               .prepareWorkspacePathMutation(
                   pendingDirectory,
                   true,
                   nullptr,
                   &mutationFailure));
    expect("successful mutation finalization closes affected views once",
           tabManager
                   .finalizeWorkspacePathMutation(
                       pendingDirectory,
                       true,
                       &mutationFailure)
               && tabManager.editorCount() == 0);

    std::printf("\n%d checks, %d failed\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
