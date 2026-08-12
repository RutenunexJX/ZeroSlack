#include "actionregistry.h"
#include "codetemplateservice.h"
#include "commandlayercommandregistry.h"
#include "globalcontrolservice.h"
#include "inlinecommandmode.h"
#include "packagetoolservice.h"

#include <QList>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

class RecordingActionHost final : public ActionExecutionHost
{
public:
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override
    {
        ++calls;
        actionId = descriptor.id;
        route = descriptor.executionRoute;
        lastInvocation = invocation;

        ActionExecutionResult result;
        result.handled = true;
        result.succeeded = true;
        result.message = QStringLiteral("executed");
        if (returnResolvedParameters) {
            result.hasResolvedParameters = true;
            result.resolvedParameters =
                resolvedParameters;
        }
        return result;
    }

    int calls = 0;
    QString actionId;
    QString route;
    ActionInvocation lastInvocation;
    bool returnResolvedParameters = false;
    QVariantMap resolvedParameters;
};

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

bool descriptorHasAlias(const ActionDescriptor& descriptor,
                        ActionSurface surface,
                        const QString& token)
{
    const ActionAliasDescriptor* alias =
        findActionAlias(descriptor, surface, token);
    return alias && alias->token == token;
}

bool firstCommandMatchIs(const QString& query, const QString& expected)
{
    const QList<CommandLayerCommandMatch> matches =
        commandLayerCommandMatches(query);
    if (!matches.isEmpty() && matches.first().command.name == expected)
        return true;

    const QByteArray queryText = query.toUtf8();
    const QByteArray expectedText = expected.toUtf8();
    std::fprintf(stderr,
                 "Command query '%s' expected '%s'; matches:",
                 queryText.constData(),
                 expectedText.constData());
    for (const CommandLayerCommandMatch& match : matches) {
        const QByteArray name = match.command.name.toUtf8();
        const QByteArray rank = commandLayerMatchRankName(match.rank).toUtf8();
        std::fprintf(stderr,
                     " [%s,%s,%d]",
                     name.constData(),
                     rank.constData(),
                     match.skippedCharacters);
    }
    std::fprintf(stderr, "\n");
    return false;
}
}

int main()
{
    resetApplicationActionExecutionHistory();
    expect("application action history can reset before registry initialization",
           applicationActionExecutionHistory()
               .lastActionId().isEmpty());

    QString registryReason;
    expect("unified action registry validates",
           actionRegistryIsValid(&registryReason));

    const QList<QPair<QString, QString>> requiredCommandAbbreviations = {
        {QStringLiteral("gm"), QStringLiteral("go module")},
        {QStringLiteral("gpk"), QStringLiteral("go package")},
        {QStringLiteral("gopack"), QStringLiteral("go package")},
        {QStringLiteral("goendm"), QStringLiteral("go endmodule")},
        {QStringLiteral("as"), QStringLiteral("add signal")},
        {QStringLiteral("addsig"), QStringLiteral("add signal")},
        {QStringLiteral("apar"), QStringLiteral("add parameter")},
        {QStringLiteral("addparam"), QStringLiteral("add parameter")},
        {QStringLiteral("aport"), QStringLiteral("add port")},
        {QStringLiteral("addport"), QStringLiteral("add port")},
        {QStringLiteral("cr"), QStringLiteral("clear right")},
        {QStringLiteral("clearr"), QStringLiteral("clear right")},
        {QStringLiteral("sbe"), QStringLiteral("select begin end")},
        {QStringLiteral("selectbe"), QStringLiteral("select begin end")},
        {QStringLiteral("ss"), QStringLiteral("select signals")},
        {QStringLiteral("selectsig"), QStringLiteral("select signals")},
    };
    bool requiredCommandAbbreviationsMatch = true;
    for (const auto& abbreviation : requiredCommandAbbreviations) {
        requiredCommandAbbreviationsMatch =
            firstCommandMatchIs(abbreviation.first, abbreviation.second)
            && requiredCommandAbbreviationsMatch;
    }
    expect("Command Layer required abbreviations stay registry-derived",
           requiredCommandAbbreviationsMatch);

    const QList<CommandLayerCommandMatch> ambiguousGo =
        commandLayerCommandMatches(QStringLiteral("go"));
    expect("Command Layer keeps only the four strongest stable go matches",
           ambiguousGo.size() == 4
               && ambiguousGo.at(0).command.name
                      == QStringLiteral("go <number>")
               && ambiguousGo.at(1).command.name
                      == QStringLiteral("go module"));
    bool invalidIdsRejected = true;
    const QStringList invalidIds = {
        QStringLiteral("Navigation.goLine"),
        QStringLiteral("navigation"),
        QStringLiteral("navigation."),
        QStringLiteral("navigation..goLine"),
        QStringLiteral("navigation.go_line"),
        QStringLiteral("navigation.göLine"),
    };
    for (const QString& invalidId : invalidIds) {
        QList<ActionDescriptor> invalidRegistry = actionRegistry();
        invalidRegistry.front().id = invalidId;
        invalidIdsRejected =
            invalidIdsRejected
            && !validateActionRegistry(invalidRegistry);
    }
    expect("action ids retain the ASCII dotted identifier policy",
           invalidIdsRejected);

    QList<ActionDescriptor> duplicateIdRegistry =
        actionRegistry();
    duplicateIdRegistry[1].id =
        duplicateIdRegistry.constFirst().id;
    QString duplicateIdReason;
    expect("registry rejects duplicate canonical ids",
           !validateActionRegistry(
               duplicateIdRegistry,
               &duplicateIdReason)
               && duplicateIdReason.contains(
                   QStringLiteral("Duplicate action id")));

    QList<ActionDescriptor> duplicateShortcutRegistry =
        actionRegistry();
    for (ActionDescriptor& descriptor :
         duplicateShortcutRegistry) {
        if (descriptor.id
            == QStringLiteral("navigation.goModule")) {
            descriptor.defaultShortcut =
                QStringLiteral("Ctrl+G");
            break;
        }
    }
    QString duplicateShortcutReason;
    expect("registry rejects conflicting default shortcuts",
           !validateActionRegistry(
               duplicateShortcutRegistry,
               &duplicateShortcutReason)
               && duplicateShortcutReason.contains(
                   QStringLiteral("Shortcut")));

    QList<ActionDescriptor> duplicateRouteRegistry =
        actionRegistry();
    for (ActionDescriptor& descriptor :
         duplicateRouteRegistry) {
        if (descriptor.id
            == QStringLiteral("navigation.goModule")) {
            descriptor.executionRoute =
                QStringLiteral(
                    "editor.navigation.goLine");
            break;
        }
    }
    QString duplicateRouteReason;
    expect("registry rejects conflicting execution routes",
           !validateActionRegistry(
               duplicateRouteRegistry,
               &duplicateRouteReason)
               && duplicateRouteReason.contains(
                   QStringLiteral(
                       "Execution route")));

    QHash<QString, QList<const ActionDescriptor*>>
        descriptorsByRoute;
    for (const ActionDescriptor& descriptor :
         actionRegistry()) {
        descriptorsByRoute[
            descriptor.executionRoute.toCaseFolded()]
            .append(&descriptor);
    }
    bool sharedRoutesAreExplicit = true;
    int sharedRouteGroups = 0;
    for (auto it = descriptorsByRoute.cbegin();
         it != descriptorsByRoute.cend();
         ++it) {
        if (it.value().size() < 2)
            continue;
        ++sharedRouteGroups;
        for (const ActionDescriptor* descriptor :
             it.value()) {
            sharedRoutesAreExplicit =
                sharedRoutesAreExplicit
                && descriptor
                && descriptor->sharedExecutionRoute;
        }
    }
    expect("parameterized adapters explicitly declare shared routes",
           sharedRouteGroups == 3
               && sharedRoutesAreExplicit);

    expect("unified catalog covers all trigger families",
           actionRegistry().size() >= 50
               && !actionDescriptorsForSurface(
                       ActionSurface::CommandLayer).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::InlineSemantic).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::InlineTemplate).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::GlobalControl).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::PackageTools).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::ContextMenu).isEmpty()
               && actionDescriptorsForSurface(
                      ActionSurface::GraphPanel).size()
                      == 13);
    const QStringList graphViewActionIds = {
        QString::fromLatin1(ActionIds::GraphViewFit),
        QString::fromLatin1(ActionIds::GraphViewZoomIn),
        QString::fromLatin1(ActionIds::GraphViewZoomOut),
        QString::fromLatin1(
            ActionIds::GraphViewCenterCurrent),
        QString::fromLatin1(
            ActionIds::GraphViewResetLayout),
    };
    bool graphViewDescriptorsComplete = true;
    for (const QString& id : graphViewActionIds) {
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor graphAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::GraphPanel)
            : ActionAliasDescriptor();
        graphViewDescriptorsComplete =
            graphViewDescriptorsComplete
            && descriptor
            && descriptor->id == id
            && descriptor->executionRoute.startsWith(
                   QStringLiteral(
                       "insight.graphView."))
            && descriptor->category
                   == ActionCategory::Inspect
            && descriptor->parameterModel.kind
                   == ActionParameterKind::None
            && (descriptor->requirementMask
                & ActionRequirements::GraphContent)
            && !descriptor->repeatable
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && !graphAlias.label.isEmpty()
            && !graphAlias.adapterKey.isEmpty();
    }
    expect("graph view descriptors own ids, routes, labels and availability",
           graphViewDescriptorsComplete);
    struct GraphSelectionExpectation {
        const char* id;
        const char* route;
        const char* label;
        const char* objectName;
    };
    const GraphSelectionExpectation
        graphSelectionExpectations[] = {
            {ActionIds::GraphJumpSelected,
             "insight.graph.jumpSelected",
             "Jump",
             "rtlGraphJumpAction"},
            {ActionIds::GraphFocusSelected,
             "insight.graph.focusSelected",
             "Focus",
             "rtlGraphFocusAction"},
            {ActionIds::GraphSetTopSelected,
             "insight.graph.setTopSelected",
             "Set Top",
             "rtlGraphSetTopAction"},
        };
    bool graphSelectionDescriptorsComplete = true;
    for (const GraphSelectionExpectation& expected :
         graphSelectionExpectations) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(expected.id));
        const ActionAliasDescriptor graphAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::GraphPanel)
            : ActionAliasDescriptor();
        graphSelectionDescriptorsComplete =
            graphSelectionDescriptorsComplete
            && descriptor
            && descriptor->executionRoute
                   == QString::fromLatin1(expected.route)
            && descriptor->parameterModel.kind
                   == ActionParameterKind::None
            && (descriptor->requirementMask
                & ActionRequirements::GraphContent)
            && !descriptor->repeatable
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && graphAlias.label
                   == QString::fromLatin1(expected.label)
            && graphAlias.adapterKey
                   == QString::fromLatin1(
                          expected.objectName);
    }
    expect("graph selection descriptors own ids, routes, labels and availability",
           graphSelectionDescriptorsComplete);
    const QStringList graphExportActionIds = {
        QString::fromLatin1(
            ActionIds::GraphExportRtlInsights),
        QString::fromLatin1(
            ActionIds::GraphExportSignalKernel),
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotTrack),
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotMatrix),
        QString::fromLatin1(
            ActionIds::GraphExportWavePreview),
    };
    bool graphExportDescriptorsComplete = true;
    for (const QString& id : graphExportActionIds) {
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor graphAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::GraphPanel)
            : ActionAliasDescriptor();
        graphExportDescriptorsComplete =
            graphExportDescriptorsComplete
            && descriptor
            && descriptor->id == id
            && descriptor->executionRoute.startsWith(
                   QStringLiteral("graphExport."))
            && descriptor->parameterModel.kind
                   == ActionParameterKind::FilePath
            && descriptor->parameterModel.name
                   == QStringLiteral("outputPath")
            && (descriptor->requirementMask
                & ActionRequirements::GraphContent)
            && !descriptor->unavailableReason.isEmpty()
            && !graphAlias.label.isEmpty()
            && !graphAlias.defaultValue.isEmpty()
            && !graphAlias.adapterKey.isEmpty();
    }
    expect("graph export descriptors own ids, labels, routes and failures",
           graphExportDescriptorsComplete);
    const QList<ActionCatalogEntry> catalog =
        unifiedActionCatalog();
    bool catalogHasHelpAliases = false;
    bool catalogHasGlobalSession = false;
    bool catalogHasExpose = false;
    for (const ActionCatalogEntry& entry : catalog) {
        if (entry.actionId == QStringLiteral("help.actionCatalog")) {
            catalogHasHelpAliases =
                entry.displayText.contains(
                    QStringLiteral("Command Mode: help"))
                && entry.displayText.contains(
                    QStringLiteral("Inline semantic command: ;?"))
                && entry.displayText.contains(
                    QStringLiteral("Inline template command: ;;?"));
        } else if (entry.actionId
                   == QStringLiteral("workspace.session.save")) {
            catalogHasGlobalSession =
                entry.displayText.contains(
                    QStringLiteral("Global Control: ow s save"));
        } else if (entry.actionId
                   == QStringLiteral("refactor.exposeSignalToTop")) {
            catalogHasExpose =
                entry.displayText.contains(
                    QStringLiteral("Context menu"));
        }
    }
    expect("unified user-visible catalog exposes every action and alias",
           catalog.size() == actionRegistry().size()
               && catalogHasHelpAliases
               && catalogHasGlobalSession
               && catalogHasExpose);

    bool everyDescriptorComplete = true;
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        everyDescriptorComplete =
            everyDescriptorComplete
            && !descriptor.id.trimmed().isEmpty()
            && !descriptor.canonicalName.trimmed().isEmpty()
            && !descriptor.description.trimmed().isEmpty()
            && descriptor.category != ActionCategory::Unknown
            && descriptor.scope != ActionScope::Unknown
            && !descriptor.executionRoute.trimmed().isEmpty()
            && static_cast<bool>(descriptor.execute)
            && !descriptor.aliases.isEmpty();
        if (descriptor.requirementMask != 0) {
            everyDescriptorComplete =
                everyDescriptorComplete
                && !descriptor.unavailableReason.trimmed().isEmpty();
        }
    }
    expect("every descriptor has complete canonical metadata",
           everyDescriptorComplete);

    const ActionDescriptor* undoAction =
        findActionById(QStringLiteral("edit.undo"));
    const ActionDescriptor* lineAction =
        findActionById(QStringLiteral("navigation.goLine"));
    const ActionDescriptor* nextAssignmentAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationNextAssignment));
    const ActionDescriptor* previousAssignmentAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationPreviousAssignment));
    const ActionDescriptor* nextConditionalAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationNextConditionalBranch));
    const ActionDescriptor* previousConditionalAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationPreviousConditionalBranch));
    const ActionDescriptor* settingsAction =
        findActionById(QString::fromLatin1(
            ActionIds::ViewSettingsCenter));
    const ActionDescriptor* crashRecoveryAction =
        findActionById(QString::fromLatin1(
            ActionIds::ReviewCrashRecovery));
    const ActionDescriptor* repeatAction =
        findActionById(QString::fromLatin1(
            ActionIds::RepeatLastAction));
    struct FileActionExpectation {
        const char* id;
        const char* command;
        const char* route;
        const char* shortcut;
        const char* adapterKey;
        quint32 requirements;
    };
    const FileActionExpectation fileActions[] = {
        {ActionIds::FileNew,
         "new file",
         "ui.file.new",
         "Ctrl+N",
         "new_file",
         0},
        {ActionIds::FileOpen,
         "open file",
         "ui.file.open",
         "Ctrl+O",
         "open_file",
         0},
        {ActionIds::FileSave,
         "save file",
         "ui.file.save",
         "Ctrl+S",
         "save_file",
         ActionRequirements::Editor},
        {ActionIds::FileSaveAs,
         "save file as",
         "ui.file.saveAs",
         "Ctrl+Shift+S",
         "save_as",
         ActionRequirements::Editor},
        {ActionIds::WorkspaceOpen,
         "open workspace",
         "ui.workspace.open",
         "Ctrl+K, Ctrl+O",
         "open_direction_as_workspace",
         0},
    };
    bool fileActionsComplete = true;
    for (const FileActionExpectation& expected :
         fileActions) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(expected.id));
        const ActionAliasDescriptor shortcutAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::Shortcut)
            : ActionAliasDescriptor();
        const CommandLayerCommandMetadata* command =
            findCommandLayerCommand(
                QString::fromLatin1(expected.command));
        fileActionsComplete =
            fileActionsComplete
            && descriptor
            && descriptor->executionRoute
                   == QString::fromLatin1(expected.route)
            && descriptor->defaultShortcut
                   == QString::fromLatin1(expected.shortcut)
            && descriptor->requirementMask
                   == expected.requirements
            && shortcutAlias.adapterKey
                   == QString::fromLatin1(
                       expected.adapterKey)
            && descriptor->hasSurface(
                   ActionSurface::CommandLayer)
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && command
            && command->actionId == descriptor->id
            && !descriptor->repeatable;
    }
    expect("file and workspace commands are canonical registry actions",
           fileActionsComplete);
    expect("registry owns shortcuts and invocation policy",
           undoAction
               && undoAction->defaultShortcut
                      == QStringLiteral("Ctrl+Z")
               && undoAction->repeatable
               && lineAction
               && lineAction->rememberParameters
               && lineAction->repeatable);
    expect("registry owns structural navigation routes and shortcuts",
           nextAssignmentAction
               && previousAssignmentAction
               && nextConditionalAction
               && previousConditionalAction
               && nextAssignmentAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.nextAssignment")
               && previousAssignmentAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.previousAssignment")
               && nextConditionalAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.nextConditionalBranch")
               && previousConditionalAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.previousConditionalBranch")
               && nextAssignmentAction->defaultShortcut
                      == QStringLiteral("Alt+F7")
               && previousAssignmentAction->defaultShortcut
                      == QStringLiteral("Shift+Alt+F7")
               && nextConditionalAction->defaultShortcut
                      == QStringLiteral("Alt+F8")
               && previousConditionalAction->defaultShortcut
                      == QStringLiteral("Shift+Alt+F8")
               && nextAssignmentAction->hasSurface(
                      ActionSurface::CommandLayer)
               && nextAssignmentAction->hasSurface(
                      ActionSurface::Shortcut)
               && nextConditionalAction->hasSurface(
                      ActionSurface::CommandLayer)
               && nextConditionalAction->hasSurface(
                      ActionSurface::Shortcut));
    expect("registry owns Settings and crash-recovery menu actions",
           settingsAction
               && crashRecoveryAction
               && settingsAction->executionRoute
                      == QStringLiteral(
                          "ui.settingsCenter.show")
               && crashRecoveryAction->executionRoute
                      == QStringLiteral(
                          "ui.crashRecovery.review")
               && settingsAction->hasSurface(
                      ActionSurface::Menu)
               && crashRecoveryAction->hasSurface(
                      ActionSurface::Menu)
               && settingsAction
                      ->aliasForSurface(
                          ActionSurface::Menu)
                      .adapterKey
                      == QStringLiteral(
                          "viewSettingsCenterAction")
               && crashRecoveryAction
                      ->aliasForSurface(
                          ActionSurface::Menu)
                      .adapterKey
                      == QStringLiteral(
                          "reviewCrashRecoveryAction"));

    struct MenuActionExpectation {
        const char* id;
        const char* adapterKey;
        const char* route;
        quint32 requirements;
        const char* shortcut;
    };
    const MenuActionExpectation stableMenuActions[] = {
        {ActionIds::ViewNavigation,
         "viewNavigationAction",
         "ui.panel.navigation.toggle",
         0,
         "Ctrl+1"},
        {ActionIds::ViewScopedSearch,
         "viewScopedSearchAction",
         "ui.panel.scopedSearch.show",
         ActionRequirements::Workspace,
         "Ctrl+Shift+F"},
        {ActionIds::ViewFocusMode,
         "toggleFocusModeAction",
         "ui.panelLayout.focusMode.toggle",
         0,
         "Ctrl+K, Z"},
        {ActionIds::ViewEditorSplitLeft,
         "splitEditorLeftAction",
         "ui.editorLayout.split.left",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitRight,
         "splitEditorRightAction",
         "ui.editorLayout.split.right",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitAbove,
         "splitEditorAboveAction",
         "ui.editorLayout.split.above",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitBelow,
         "splitEditorBelowAction",
         "ui.editorLayout.split.below",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitMaximize,
         "maximizeEditorSplitAction",
         "ui.editorLayout.split.toggleMaximized",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitsEqualize,
         "equalizeEditorSplitsAction",
         "ui.editorLayout.split.equalize",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewEditorSplitMerge,
         "mergeEditorSplitAction",
         "ui.editorLayout.split.merge",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewReopenClosedTab,
         "reopenClosedTabAction",
         "ui.editorTabs.reopenClosed",
         0,
         "Ctrl+Shift+T"},
        {ActionIds::ViewGroupTabsNone,
         "groupTabsNoneAction",
         "ui.editorTabs.group.none",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewGroupTabsModule,
         "groupTabsModuleAction",
         "ui.editorTabs.group.module",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewGroupTabsWorkspace,
         "groupTabsWorkspaceAction",
         "ui.editorTabs.group.workspace",
         ActionRequirements::Editor,
         ""},
        {ActionIds::ViewProblems,
         "viewProblemsAction",
         "ui.panel.problems.toggle",
         0,
         ""},
        {ActionIds::ViewActivity,
         "viewActivityAction",
         "ui.panel.activity.toggle",
         0,
         ""},
        {ActionIds::ViewRtlInsights,
         "viewRtlInsightsAction",
         "ui.panel.rtlInsights.toggle",
         0,
         ""},
        {ActionIds::ViewSignalKernelGraph,
         "viewSignalKernelGraphAction",
         "ui.panel.signalKernelGraph.toggle",
         0,
         ""},
        {ActionIds::ViewWavePreview,
         "viewWavePreviewAction",
         "ui.panel.wavePreview.toggle",
         0,
         ""},
        {ActionIds::ViewBottomPanelCollapsed,
         "toggleBottomPanelCollapsedAction",
         "ui.bottomPanel.collapsed.toggle",
         0,
         "Ctrl+J"},
        {ActionIds::ViewBottomPanelPinned,
         "pinActiveBottomPanelAction",
         "ui.bottomPanel.pinned.toggle",
         0,
         ""},
        {ActionIds::ViewBottomPanelClose,
         "closeActiveBottomPanelAction",
         "ui.bottomPanel.closeActive",
         0,
         ""},
        {ActionIds::ViewFocusRtlInsights,
         "focusRtlInsightsAction",
         "ui.insightFocus.rtlInsights.enter",
         0,
         ""},
        {ActionIds::ViewFocusSignalKernelGraph,
         "focusSignalKernelGraphAction",
         "ui.insightFocus.signalKernelGraph.enter",
         0,
         ""},
        {ActionIds::ViewFocusWavePreview,
         "focusWavePreviewAction",
         "ui.insightFocus.wavePreview.enter",
         0,
         ""},
        {ActionIds::ViewLeaveInsightFocus,
         "leaveInsightFocusAction",
         "ui.insightFocus.leave",
         0,
         ""},
        {ActionIds::ViewFoldShelf,
         "viewFoldShelfAction",
         "ui.panel.foldShelf.toggle",
         0,
         ""},
        {ActionIds::ViewSettingsCenter,
         "viewSettingsCenterAction",
         "ui.settingsCenter.show",
         0,
         ""},
        {ActionIds::ViewResetPanelLayout,
         "resetPanelLayoutAction",
         "ui.panelLayout.reset",
         0,
         ""},
        {ActionIds::WorkspaceCloseActive,
         "closeActiveWorkspaceAction",
         "ui.workspace.closeActive",
         ActionRequirements::Workspace,
         ""},
        {ActionIds::WorkspaceConfigure,
         "workspaceConfigurationAction",
         "ui.workspace.configure",
         ActionRequirements::Workspace,
         ""},
        {ActionIds::WorkspaceNextDiagnostic,
         "nextDiagnosticAction",
         "ui.diagnostics.next",
         ActionRequirements::Workspace,
         "F8"},
        {ActionIds::WorkspacePreviousDiagnostic,
         "previousDiagnosticAction",
         "ui.diagnostics.previous",
         ActionRequirements::Workspace,
         "Shift+F8"},
        {ActionIds::UserTemplatesOpenGlobal,
         "openGlobalUserTemplatesAction",
         "ui.userTemplates.openGlobal",
         0,
         ""},
        {ActionIds::UserTemplatesOpenWorkspace,
         "openWorkspaceUserTemplatesAction",
         "ui.userTemplates.openWorkspace",
         ActionRequirements::Workspace,
         ""},
        {ActionIds::UserTemplatesReload,
         "reloadUserTemplatesAction",
         "ui.userTemplates.reload",
         0,
         ""},
        {ActionIds::ReviewCrashRecovery,
         "reviewCrashRecoveryAction",
         "ui.crashRecovery.review",
         0,
         ""},
        {ActionIds::RtlRename,
         "rtlRenameAction",
         "rtledit.rename",
         ActionRequirements::Editor
             | ActionRequirements::Workspace
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol,
         "Ctrl+R"},
        {ActionIds::RtlConnectionTransform,
         "rtlConnectionTransformAction",
         "rtledit.connection.transform",
         ActionRequirements::Editor
             | ActionRequirements::Workspace
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol
             | ActionRequirements::Hierarchy,
         ""},
        {ActionIds::RtlConnectInstancePair,
         "connectInstancePairAction",
         "rtledit.instancePair.connect",
         ActionRequirements::Editor
             | ActionRequirements::Workspace
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol
             | ActionRequirements::Hierarchy,
         ""},
        {ActionIds::RtlPropagateMultipleSignals,
         "propagateMultipleSignalsAction",
         "rtledit.signal.propagateBatch",
         ActionRequirements::Editor
             | ActionRequirements::Workspace
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol
             | ActionRequirements::Hierarchy,
         ""},
    };
    bool stableMenuCatalogComplete = true;
    for (const MenuActionExpectation& expected :
         stableMenuActions) {
        const QString id =
            QString::fromLatin1(expected.id);
        const QString shortcut =
            QString::fromLatin1(expected.shortcut);
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor menuAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::Menu)
            : ActionAliasDescriptor();
        const ActionAliasDescriptor catalogAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ActionCatalog)
            : ActionAliasDescriptor();
        stableMenuCatalogComplete =
            stableMenuCatalogComplete
            && descriptor
            && descriptor->canonicalName
                   .trimmed().size() > 0
            && descriptor->description
                   .trimmed().size() > 0
            && descriptor->executionRoute
                   == QString::fromLatin1(
                       expected.route)
            && descriptor->requirementMask
                   == expected.requirements
            && !descriptor->unavailableReason
                    .trimmed().isEmpty()
            && descriptor->defaultShortcut
                   == shortcut
            && descriptor->hasSurface(
                   ActionSurface::Menu)
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && menuAlias.token == id
            && menuAlias.adapterKey
                   == QString::fromLatin1(
                       expected.adapterKey)
            && !menuAlias.label.isEmpty()
            && catalogAlias.token == id
            && catalogAlias.catalogued
            && findActionByAlias(
                   ActionSurface::Menu,
                   id) == descriptor
            && findActionByAlias(
                   ActionSurface::ActionCatalog,
                   id) == descriptor
            && (shortcut.isEmpty()
                    ? !descriptor->hasSurface(
                          ActionSurface::Shortcut)
                    : descriptor->hasSurface(
                          ActionSurface::Shortcut)
                          && descriptor
                                 ->aliasForSurface(
                                     ActionSurface::Shortcut)
                                 .token
                                 == shortcut);
    }
    const qsizetype stableMenuActionCount =
        static_cast<qsizetype>(
            sizeof(stableMenuActions)
            / sizeof(stableMenuActions[0]));
    expect("registry owns the complete stable View Workspace Tools menu catalog",
           stableMenuCatalogComplete
               && actionDescriptorsForSurface(
                      ActionSurface::Menu)
                      .size()
                      == stableMenuActionCount
               && actionDescriptorsForSurface(
                      ActionSurface::ActionCatalog)
                      .size()
                      >= stableMenuActionCount);
    const ActionDescriptor* globalControlAction =
        findActionById(QString::fromLatin1(
            ActionIds::ViewGlobalControl));
    expect("Global Control shortcut is registry-owned without a menu entry",
           globalControlAction
               && globalControlAction->executionRoute
                      == QStringLiteral(
                          "ui.globalControl.show")
               && globalControlAction->defaultShortcut
                      == QStringLiteral("Ctrl+Space")
               && globalControlAction->hasSurface(
                      ActionSurface::Shortcut)
               && globalControlAction->hasSurface(
                      ActionSurface::ActionCatalog)
               && !globalControlAction->hasSurface(
                      ActionSurface::Menu));
    const ActionDescriptor* commandModeAction =
        findActionById(QString::fromLatin1(
            ActionIds::ViewCommandMode));
    expect("Command Mode hold shortcut is registry-owned and non-repeatable",
           commandModeAction
               && commandModeAction->executionRoute
                      == QStringLiteral(
                          "ui.commandMode.show")
               && commandModeAction->defaultShortcut
                      == QStringLiteral("F24")
               && commandModeAction->hasSurface(
                      ActionSurface::Shortcut)
               && commandModeAction->hasSurface(
                      ActionSurface::ActionCatalog)
               && !commandModeAction->hasSurface(
                      ActionSurface::Menu)
               && !commandModeAction->repeatable);
    QVariantMap invalidCommandModeShortcut;
    invalidCommandModeShortcut.insert(
        QString::fromLatin1(
            ActionIds::ViewCommandMode),
        QStringLiteral("Ctrl+K, F24"));
    QStringList invalidCommandModeIssues;
    expect("Command Mode rejects multi-stroke hold shortcuts",
           !configureActionShortcutOverrides(
               invalidCommandModeShortcut,
               &invalidCommandModeIssues)
               && !invalidCommandModeIssues.isEmpty());
    configureActionShortcutOverrides({});

    const ActionDescriptor* columnNumbersAction =
        findActionById(QString::fromLatin1(
            ActionIds::InsertColumnNumbers));
    expect("Column Number Tool is a Registry-owned interactive Action",
           columnNumbersAction
               && columnNumbersAction->executionRoute
                      == QStringLiteral(
                          "editor.columnNumbers.show")
               && columnNumbersAction->defaultShortcut
                      == QStringLiteral("Alt+C")
               && columnNumbersAction->hasSurface(
                      ActionSurface::Shortcut)
               && columnNumbersAction->hasSurface(
                      ActionSurface::CommandLayer)
               && columnNumbersAction->hasSurface(
                      ActionSurface::ActionCatalog)
               && !columnNumbersAction->hasSurface(
                      ActionSurface::Menu)
               && !columnNumbersAction->repeatable
               && findActionByAlias(
                      ActionSurface::CommandLayer,
                      QStringLiteral("column number"))
                      == columnNumbersAction);

    const ActionDescriptor* foldShelfDeleteAction =
        findActionById(QString::fromLatin1(
            ActionIds::FoldShelfDeleteSelected));
    expect("Fold Shelf Delete is a Registry-owned contextual Action",
           foldShelfDeleteAction
               && foldShelfDeleteAction->executionRoute
                      == QStringLiteral(
                          "ui.foldShelf.deleteSelected")
               && foldShelfDeleteAction->defaultShortcut
                      == QStringLiteral("Delete")
               && foldShelfDeleteAction->hasSurface(
                      ActionSurface::Shortcut)
               && foldShelfDeleteAction->hasSurface(
                      ActionSurface::CommandLayer)
               && foldShelfDeleteAction->hasSurface(
                      ActionSurface::ActionCatalog)
               && !foldShelfDeleteAction->hasSurface(
                      ActionSurface::Menu)
               && !foldShelfDeleteAction->repeatable
               && findActionByAlias(
                      ActionSurface::CommandLayer,
                      QStringLiteral(
                          "fold shelf delete"))
                      == foldShelfDeleteAction);

    struct TabContextExpectation {
        const char* id;
        const char* route;
        const char* label;
    };
    const TabContextExpectation tabContextActions[] = {
        {ActionIds::ViewEditorTabClose,
         "ui.editorTabs.close", "Close"},
        {ActionIds::ViewEditorTabCloseOthers,
         "ui.editorTabs.closeOthers", "Close Others"},
        {ActionIds::ViewEditorTabCloseRight,
         "ui.editorTabs.closeRight", "Close to the Right"},
        {ActionIds::ViewEditorTabCloseAll,
         "ui.editorTabs.closeAll", "Close All"},
        {ActionIds::ViewReopenClosedTab,
         "ui.editorTabs.reopenClosed", "Reopen Closed Tab"},
        {ActionIds::ViewEditorTabDuplicate,
         "ui.editorTabs.duplicateView", "Duplicate View"},
        {ActionIds::ViewTemporaryEditorOpen,
         "ui.temporaryEditor.open", "Open in Temporary Editor"},
        {ActionIds::ViewEditorSplitLeft,
         "ui.editorLayout.split.left", "Split Left"},
        {ActionIds::ViewEditorSplitRight,
         "ui.editorLayout.split.right", "Split Right"},
        {ActionIds::ViewEditorSplitAbove,
         "ui.editorLayout.split.above", "Split Above"},
        {ActionIds::ViewEditorSplitBelow,
         "ui.editorLayout.split.below", "Split Below"},
        {ActionIds::ViewEditorSplitMerge,
         "ui.editorLayout.split.merge", "Merge Group"},
        {ActionIds::ViewEditorTabToggleLocked,
         "ui.editorTabs.toggleLocked", "Lock Tab"},
    };
    bool tabContextCatalogComplete = true;
    for (const TabContextExpectation& expected :
         tabContextActions) {
        const QString id = QString::fromLatin1(expected.id);
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor contextAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::TabContextMenu)
            : ActionAliasDescriptor();
        tabContextCatalogComplete =
            tabContextCatalogComplete
            && descriptor
            && descriptor->executionRoute
                   == QString::fromLatin1(expected.route)
            && descriptor->hasSurface(
                   ActionSurface::TabContextMenu)
            && descriptor->hasSurface(
                   ActionSurface::CommandLayer)
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && contextAlias.token == id
            && contextAlias.label
                   == QString::fromLatin1(expected.label)
            && findActionByAlias(
                   ActionSurface::TabContextMenu,
                   id) == descriptor;
    }
    expect("Registry owns the complete Tab context Action catalog",
           tabContextCatalogComplete
               && actionDescriptorsForSurface(
                      ActionSurface::TabContextMenu)
                      .size()
                      == static_cast<qsizetype>(
                          sizeof(tabContextActions)
                          / sizeof(tabContextActions[0]))
               && actionSurfaceText(
                      ActionSurface::TabContextMenu)
                      == QStringLiteral("Tab context menu"));

    struct HierarchyContextExpectation {
        const char* id;
        const char* route;
        const char* label;
        ActionScope scope;
        quint32 requirements;
        bool commandLayer;
    };
    const HierarchyContextExpectation hierarchyContextActions[] = {
        {ActionIds::NavigationDesignGoInstantiation,
         "navigation.design.goInstantiation",
         "Go to Instantiation",
         ActionScope::Hierarchy,
         ActionRequirements::Workspace
             | ActionRequirements::Hierarchy,
         false},
        {ActionIds::NavigationDesignGoDefinition,
         "navigation.design.goDefinition",
         "Go to Module Definition",
         ActionScope::Hierarchy,
         ActionRequirements::Workspace
             | ActionRequirements::Hierarchy,
         false},
        {ActionIds::ViewTemporaryEditorOpen,
         "ui.temporaryEditor.open",
         "Open in Temporary Editor",
         ActionScope::Editor,
         0,
         true},
        {ActionIds::NavigationDesignSetTop,
         "navigation.design.setTop",
         "Set as Design Top",
         ActionScope::Hierarchy,
         ActionRequirements::Workspace
             | ActionRequirements::Hierarchy,
         false},
    };
    bool hierarchyContextCatalogComplete = true;
    for (const HierarchyContextExpectation& expected :
         hierarchyContextActions) {
        const QString id = QString::fromLatin1(expected.id);
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor contextAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ContextMenu)
            : ActionAliasDescriptor();
        hierarchyContextCatalogComplete =
            hierarchyContextCatalogComplete
            && descriptor
            && descriptor->executionRoute
                   == QString::fromLatin1(expected.route)
            && descriptor->scope
                   == expected.scope
            && descriptor->requirementMask
                   == expected.requirements
            && descriptor->hasSurface(
                   ActionSurface::ContextMenu)
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && descriptor->hasSurface(
                   ActionSurface::CommandLayer)
                   == expected.commandLayer
            && !descriptor->repeatable
            && !descriptor->rememberParameters
            && contextAlias.token == id
            && contextAlias.label
                   == QString::fromLatin1(expected.label)
            && findActionByAlias(
                   ActionSurface::ContextMenu,
                   id) == descriptor;
    }
    expect("Registry owns design-hierarchy context Actions",
           hierarchyContextCatalogComplete);

    const ActionDescriptor* temporaryEditorAction =
        findActionById(QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen));
    expect("temporary editor uses one navigation Action on all entry surfaces",
           temporaryEditorAction
               && temporaryEditorAction->category
                      == ActionCategory::Navigate
               && temporaryEditorAction->scope
                      == ActionScope::Editor
               && temporaryEditorAction->requirementMask == 0
               && temporaryEditorAction->executionRoute
                      == QStringLiteral("ui.temporaryEditor.open")
               && temporaryEditorAction->hasSurface(
                      ActionSurface::ContextMenu)
               && temporaryEditorAction->hasSurface(
                      ActionSurface::TabContextMenu)
               && temporaryEditorAction->hasSurface(
                      ActionSurface::CommandLayer)
               && temporaryEditorAction->hasSurface(
                      ActionSurface::ActionCatalog));

    struct PanelContextExpectation {
        const char* id;
        const char* route;
        const char* label;
        const char* command;
    };
    const PanelContextExpectation panelContextActions[] = {
        {ActionIds::ViewBottomPanelPinned,
         "ui.bottomPanel.pinned.toggle",
         "Pin Page",
         "toggle bottom page pin"},
        {ActionIds::ViewBottomPanelClose,
         "ui.bottomPanel.closeActive",
         "Close Page",
         "close bottom page"},
    };
    bool panelContextCatalogComplete = true;
    for (const PanelContextExpectation& expected :
         panelContextActions) {
        const QString id = QString::fromLatin1(expected.id);
        const ActionDescriptor* descriptor =
            findActionById(id);
        const ActionAliasDescriptor panelAlias =
            descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::PanelContextMenu)
            : ActionAliasDescriptor();
        panelContextCatalogComplete =
            panelContextCatalogComplete
            && descriptor
            && descriptor->executionRoute
                   == QString::fromLatin1(expected.route)
            && descriptor->hasSurface(
                   ActionSurface::PanelContextMenu)
            && descriptor->hasSurface(
                   ActionSurface::CommandLayer)
            && descriptor->hasSurface(
                   ActionSurface::ActionCatalog)
            && descriptor->hasSurface(
                   ActionSurface::Menu)
            && !descriptor->repeatable
            && panelAlias.token == id
            && panelAlias.label
                   == QString::fromLatin1(expected.label)
            && findActionByAlias(
                   ActionSurface::PanelContextMenu,
                   id) == descriptor
            && findActionByAlias(
                   ActionSurface::CommandLayer,
                   QString::fromLatin1(
                       expected.command))
                   == descriptor;
    }
    expect("Registry owns bottom-panel context Actions",
           panelContextCatalogComplete
               && actionDescriptorsForSurface(
                      ActionSurface::PanelContextMenu)
                      .size()
                      == static_cast<qsizetype>(
                          sizeof(panelContextActions)
                          / sizeof(panelContextActions[0]))
               && actionSurfaceText(
                      ActionSurface::PanelContextMenu)
                      == QStringLiteral(
                          "Panel context menu"));

    const ActionDescriptor* splitLeftAction =
        findActionById(QString::fromLatin1(
            ActionIds::ViewEditorSplitLeft));
    const ActionDescriptor* workspaceTemplatesAction =
        findActionById(QString::fromLatin1(
            ActionIds::UserTemplatesOpenWorkspace));
    ActionAvailabilityContext unavailableMenuContext;
    const ActionAvailabilityState splitUnavailable =
        splitLeftAction
        ? evaluateActionAvailability(
              *splitLeftAction,
              unavailableMenuContext)
        : ActionAvailabilityState();
    const ActionAvailabilityState templatesUnavailable =
        workspaceTemplatesAction
        ? evaluateActionAvailability(
              *workspaceTemplatesAction,
              unavailableMenuContext)
        : ActionAvailabilityState();
    ActionAvailabilityContext availableMenuContext;
    availableMenuContext.editorAvailable = true;
    availableMenuContext.workspaceAvailable = true;
    const ActionAvailabilityState splitAvailable =
        splitLeftAction
        ? evaluateActionAvailability(
              *splitLeftAction,
              availableMenuContext)
        : ActionAvailabilityState();
    const ActionAvailabilityState templatesAvailable =
        workspaceTemplatesAction
        ? evaluateActionAvailability(
              *workspaceTemplatesAction,
              availableMenuContext)
        : ActionAvailabilityState();
    expect("menu requirements expose resolvable editor and workspace failures",
           splitLeftAction
               && workspaceTemplatesAction
               && !splitUnavailable.executable
               && splitUnavailable.enterable()
               && !splitUnavailable.reason.isEmpty()
               && !templatesUnavailable.executable
               && templatesUnavailable.enterable()
               && !templatesUnavailable.reason.isEmpty()
               && splitAvailable.executable
               && templatesAvailable.executable);

    expect("registry owns the non-recursive repeat Action",
           repeatAction
               && repeatAction->executionRoute
                      == QStringLiteral(
                          "action.repeatLast")
               && repeatAction->hasSurface(
                      ActionSurface::CommandLayer)
               && !repeatAction->hasSurface(
                      ActionSurface::Shortcut)
               && !repeatAction->repeatable);
    QVariantMap shortcutOverrides;
    shortcutOverrides.insert(
        QString::fromLatin1(
            ActionIds::NavigationNextAssignment),
        QStringLiteral("Ctrl+Alt+N"));
    QStringList shortcutIssues;
    expect("shortcut overrides apply by canonical action id",
           configureActionShortcutOverrides(
               shortcutOverrides,
               &shortcutIssues)
               && shortcutIssues.isEmpty()
               && effectiveActionShortcut(
                      QString::fromLatin1(
                          ActionIds::
                              NavigationNextAssignment))
                      == QStringLiteral("Ctrl+Alt+N")
               && effectiveActionShortcut(
                      QString::fromLatin1(
                          ActionIds::
                              NavigationPreviousAssignment))
                      == QStringLiteral("Shift+Alt+F7"));
    bool catalogShowsEffectiveShortcut = false;
    for (const ActionCatalogEntry& entry :
         unifiedActionCatalog()) {
        if (entry.actionId
                == QString::fromLatin1(
                    ActionIds::
                        NavigationNextAssignment)
            && entry.displayText.contains(
                QStringLiteral(
                    "Shortcut: Ctrl+Alt+N"))) {
            catalogShowsEffectiveShortcut = true;
            break;
        }
    }
    expect("action help displays the effective shortcut",
           catalogShowsEffectiveShortcut);
    QVariantMap conflictingShortcuts =
        shortcutOverrides;
    conflictingShortcuts.insert(
        QString::fromLatin1(
            ActionIds::NavigationPreviousAssignment),
        QStringLiteral("Ctrl+Alt+N"));
    expect("conflicting shortcut overrides are rejected atomically",
           !configureActionShortcutOverrides(
               conflictingShortcuts,
               &shortcutIssues)
               && !shortcutIssues.isEmpty()
               && effectiveActionShortcut(
                      QString::fromLatin1(
                          ActionIds::
                              NavigationNextAssignment))
                      == QStringLiteral("Ctrl+Alt+N")
               && effectiveActionShortcut(
                      QString::fromLatin1(
                          ActionIds::
                              NavigationPreviousAssignment))
                      == QStringLiteral("Shift+Alt+F7"));
    shortcutOverrides.insert(
        QString::fromLatin1(
            ActionIds::NavigationNextAssignment),
        QString());
    expect("empty shortcut override disables the binding",
           configureActionShortcutOverrides(
               shortcutOverrides,
               &shortcutIssues)
               && effectiveActionShortcut(
                      QString::fromLatin1(
                          ActionIds::
                              NavigationNextAssignment))
                      .isEmpty());
    configureActionShortcutOverrides({});
    const ActionDescriptor* deleteLinesAction =
        findActionById(
            QStringLiteral("edit.deleteLines"));
    const ActionDescriptor* joinLinesAction =
        findActionById(
            QStringLiteral("edit.joinLines"));
    const ActionDescriptor* moveLinesUpAction =
        findActionById(QString::fromLatin1(
            ActionIds::EditMoveLinesUp));
    const ActionDescriptor* moveLinesDownAction =
        findActionById(QString::fromLatin1(
            ActionIds::EditMoveLinesDown));
    expect("registry owns line-operation routes and shortcuts",
           deleteLinesAction
               && joinLinesAction
               && moveLinesUpAction
               && moveLinesDownAction
               && deleteLinesAction->executionRoute
                      == QStringLiteral(
                          "editor.lines.delete")
               && joinLinesAction->executionRoute
                      == QStringLiteral(
                          "editor.lines.join")
               && moveLinesUpAction->executionRoute
                      == QStringLiteral(
                          "editor.lines.moveUp")
               && moveLinesDownAction->executionRoute
                      == QStringLiteral(
                          "editor.lines.moveDown")
               && deleteLinesAction->defaultShortcut
                      == QStringLiteral(
                          "Ctrl+Shift+K")
               && joinLinesAction->defaultShortcut
                      == QStringLiteral(
                          "Ctrl+Shift+J")
               && moveLinesUpAction->defaultShortcut
                      == QStringLiteral("Alt+Up")
               && moveLinesDownAction->defaultShortcut
                      == QStringLiteral("Alt+Down")
               && deleteLinesAction->hasSurface(
                      ActionSurface::Shortcut)
               && joinLinesAction->hasSurface(
                      ActionSurface::Shortcut)
               && moveLinesUpAction->hasSurface(
                      ActionSurface::Shortcut)
               && moveLinesDownAction->hasSurface(
                      ActionSurface::Shortcut));
    const CommandLayerCommandMetadata* moveLinesUpCommand =
        findCommandLayerCommand(
            QStringLiteral("move lines up"));
    const CommandLayerCommandMetadata* moveLinesDownCommand =
        findCommandLayerCommand(
            QStringLiteral("move lines down"));
    expect("line movement Command Mode aliases stay canonical",
           moveLinesUpCommand
               && moveLinesDownCommand
               && moveLinesUpCommand->actionId
                      == QString::fromLatin1(
                          ActionIds::EditMoveLinesUp)
               && moveLinesDownCommand->actionId
                      == QString::fromLatin1(
                          ActionIds::EditMoveLinesDown));
    const ActionDescriptor* findAction =
        findActionById(QStringLiteral("edit.find"));
    const ActionDescriptor* replaceAction =
        findActionById(QStringLiteral("edit.replace"));
    expect("registry owns find and replace shortcuts",
           findAction
               && replaceAction
               && findAction->executionRoute
                      == QStringLiteral("editor.edit.find")
               && replaceAction->executionRoute
                      == QStringLiteral("editor.edit.replace")
               && findAction->defaultShortcut
                      == QStringLiteral("Ctrl+F")
               && replaceAction->defaultShortcut
                      == QStringLiteral("Ctrl+H")
               && findAction->hasSurface(
                      ActionSurface::Shortcut)
               && replaceAction->hasSurface(
                      ActionSurface::Shortcut));
    const ActionDescriptor* nextOccurrenceAction =
        findActionById(
            QStringLiteral(
                "select.nextSymbolOccurrence"));
    const ActionDescriptor* allOccurrencesAction =
        findActionById(
            QStringLiteral(
                "select.allSymbolOccurrences"));
    expect("registry owns multi-cursor occurrence routes and shortcuts",
           nextOccurrenceAction
               && allOccurrencesAction
               && nextOccurrenceAction->executionRoute
                      == QStringLiteral(
                          "editor.multicursor.addNextOccurrence")
               && allOccurrencesAction->executionRoute
                      == QStringLiteral(
                          "editor.multicursor.selectScopeOccurrences")
               && nextOccurrenceAction->defaultShortcut
                      == QStringLiteral("Ctrl+D")
               && allOccurrencesAction->defaultShortcut
                      == QStringLiteral("Ctrl+Shift+L")
               && nextOccurrenceAction->hasSurface(
                      ActionSurface::Shortcut)
               && allOccurrencesAction->hasSurface(
                      ActionSurface::Shortcut));
    const CommandLayerCommandMetadata* nextOccurrenceCommand =
        findCommandLayerCommand(
            QStringLiteral("select next occurrence"));
    const CommandLayerCommandMetadata* allOccurrencesCommand =
        findCommandLayerCommand(
            QStringLiteral("select scope occurrences"));
    expect("F24 commands use canonical action ids",
           nextOccurrenceCommand
               && allOccurrencesCommand
               && nextOccurrenceCommand->actionId
                      == QStringLiteral(
                          "select.nextSymbolOccurrence")
               && allOccurrencesCommand->actionId
                      == QStringLiteral(
                          "select.allSymbolOccurrences"));
    const ActionDescriptor* expandSelectionAction =
        findActionById(QString::fromLatin1(
            ActionIds::SelectExpandSmart));
    const ActionDescriptor* nextSelectedOccurrenceAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationNextSelectedSymbolOccurrence));
    const ActionDescriptor* previousSelectedOccurrenceAction =
        findActionById(QString::fromLatin1(
            ActionIds::NavigationPreviousSelectedSymbolOccurrence));
    expect("registry owns structural selection navigation shortcuts",
           expandSelectionAction
               && nextSelectedOccurrenceAction
               && previousSelectedOccurrenceAction
               && expandSelectionAction->executionRoute
                      == QStringLiteral(
                          "editor.selection.expandSmart")
               && nextSelectedOccurrenceAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.nextSelectedSymbolOccurrence")
               && previousSelectedOccurrenceAction->executionRoute
                      == QStringLiteral(
                          "editor.navigation.previousSelectedSymbolOccurrence")
               && expandSelectionAction->defaultShortcut
                      == QStringLiteral("Ctrl+W")
               && nextSelectedOccurrenceAction->defaultShortcut
                      == QStringLiteral("Ctrl+E")
               && previousSelectedOccurrenceAction->defaultShortcut
                      == QStringLiteral("Ctrl+Q")
               && expandSelectionAction->hasSurface(
                      ActionSurface::Shortcut)
               && nextSelectedOccurrenceAction->hasSurface(
                      ActionSurface::Shortcut)
               && previousSelectedOccurrenceAction->hasSurface(
                      ActionSurface::Shortcut));
    const CommandLayerCommandMetadata* expandSelectionCommand =
        findCommandLayerCommand(
            QStringLiteral("expand selection"));
    const CommandLayerCommandMetadata* nextSelectedOccurrenceCommand =
        findCommandLayerCommand(
            QStringLiteral("next selected occurrence"));
    const CommandLayerCommandMetadata* previousSelectedOccurrenceCommand =
        findCommandLayerCommand(
            QStringLiteral("previous selected occurrence"));
    expect("selection navigation Command Mode aliases stay canonical",
           expandSelectionCommand
               && nextSelectedOccurrenceCommand
               && previousSelectedOccurrenceCommand
               && expandSelectionCommand->actionId
                      == QString::fromLatin1(
                          ActionIds::SelectExpandSmart)
               && nextSelectedOccurrenceCommand->actionId
                      == QString::fromLatin1(
                          ActionIds::NavigationNextSelectedSymbolOccurrence)
               && previousSelectedOccurrenceCommand->actionId
                      == QString::fromLatin1(
                          ActionIds::NavigationPreviousSelectedSymbolOccurrence));

    RecordingActionHost actionHost;
    ActionExecutionHistory& applicationHistory =
        applicationActionExecutionHistory();
    resetApplicationActionExecutionHistory();
    const ActionDescriptor* contextHistoryAction =
        findActionById(QStringLiteral("format.document"));
    ActionInvocation surfaceInvocation;
    surfaceInvocation.workspaceId =
        QStringLiteral("workspace-a");
    const ActionExecutionResult menuExecution =
        settingsAction
        ? executeAction(
              *settingsAction,
              actionHost,
              surfaceInvocation)
        : ActionExecutionResult();
    const bool menuRecorded =
        menuExecution.succeeded
        && settingsAction
        && applicationHistory.lastActionId()
               == settingsAction->id;
    const ActionExecutionResult shortcutExecution =
        deleteLinesAction
        ? executeAction(
              *deleteLinesAction,
              actionHost,
              surfaceInvocation)
        : ActionExecutionResult();
    const bool shortcutRecorded =
        shortcutExecution.succeeded
        && deleteLinesAction
        && applicationHistory.lastActionId()
               == deleteLinesAction->id;
    const ActionExecutionResult contextExecution =
        contextHistoryAction
        ? executeAction(
              *contextHistoryAction,
              actionHost,
              surfaceInvocation)
        : ActionExecutionResult();
    const bool contextRecorded =
        contextExecution.succeeded
        && contextHistoryAction
        && contextHistoryAction->hasSurface(
               ActionSurface::ContextMenu)
        && applicationHistory.lastActionId()
               == contextHistoryAction->id;

    ActionInvocation lineInvocation;
    lineInvocation.workspaceId = QStringLiteral("workspace-a");
    lineInvocation.parameters.insert(QStringLiteral("line"), 37);
    const int callsBeforeLine = actionHost.calls;
    const ActionExecutionResult lineExecution =
        lineAction
        ? executeAction(*lineAction, actionHost, lineInvocation)
        : ActionExecutionResult();
    expect("descriptor execution function dispatches its canonical route",
           lineExecution.succeeded
               && actionHost.calls == callsBeforeLine + 1
               && actionHost.actionId == lineAction->id
               && actionHost.route == lineAction->executionRoute
               && actionHost.lastInvocation.parameters
                      .value(QStringLiteral("line")).toInt() == 37);
    expect("menu, shortcut, context, and Command Mode share application history",
           menuRecorded
               && shortcutRecorded
               && contextRecorded
               && lineAction
               && lineAction->hasSurface(
                      ActionSurface::CommandLayer)
               && applicationHistory.lastActionId()
                      == lineAction->id);

    lineInvocation.parameters.insert(QStringLiteral("line"), 88);
    const ActionExecutionResult repeated =
        applicationHistory.repeatLast(actionHost);
    ActionInvocation secondWorkspaceInvocation;
    secondWorkspaceInvocation.workspaceId =
        QStringLiteral("workspace-b");
    secondWorkspaceInvocation.parameters.insert(
        QStringLiteral("line"), 73);
    const ActionExecutionResult secondWorkspaceExecution =
        lineAction
        ? executeAction(
              *lineAction,
              actionHost,
              secondWorkspaceInvocation)
        : ActionExecutionResult();
    secondWorkspaceInvocation.parameters.insert(
        QStringLiteral("line"), 91);
    const ActionExecutionResult repeatCommandExecution =
        repeatAction
        ? executeAction(
              *repeatAction,
              actionHost,
              secondWorkspaceInvocation)
        : ActionExecutionResult();
    const bool repeatCommandPreservedHistory =
        repeatCommandExecution.succeeded
        && lineAction
        && applicationHistory.lastActionId()
               == lineAction->id
        && applicationHistory.rememberedParameters(
               QStringLiteral("workspace-b"),
               lineAction->id)
               .value(QStringLiteral("line")).toInt() == 73;
    const ActionExecutionResult repeatedSecondWorkspace =
        applicationHistory.repeatLast(actionHost);
    expect("application history repeats with workspace-scoped parameters",
           repeated.succeeded
               && secondWorkspaceExecution.succeeded
               && repeatCommandPreservedHistory
               && repeatedSecondWorkspace.succeeded
               && applicationHistory.lastActionId()
                      == lineAction->id
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-a"),
                      lineAction->id)
                      .value(QStringLiteral("line")).toInt() == 37
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-b"),
                      lineAction->id)
                      .value(QStringLiteral("line")).toInt() == 73
               && actionHost.lastInvocation.parameters
                      .value(QStringLiteral("line")).toInt() == 73);

    const ActionDescriptor* saveLocalSession =
        findActionById(
            QStringLiteral("workspace.session.save"));
    const ActionDescriptor* restoreLocalSession =
        findActionById(
            QStringLiteral("workspace.session.restore"));
    const ActionDescriptor* clearLocalSession =
        findActionById(
            QStringLiteral("workspace.session.clean"));
    expect("ow s names local state and preserves portable project config",
           saveLocalSession
               && restoreLocalSession
               && clearLocalSession
               && saveLocalSession->canonicalName
                      == QStringLiteral(
                          "Save Local Workspace Session")
               && saveLocalSession->description.contains(
                      QStringLiteral("local AppData"))
               && saveLocalSession->description.contains(
                      QStringLiteral(
                          "project configuration is unchanged"))
               && restoreLocalSession->description.contains(
                      QStringLiteral("read-only"))
               && clearLocalSession->canonicalName
                      == QStringLiteral(
                          "Clear Local Workspace Session")
               && clearLocalSession->description.contains(
                      QStringLiteral("project.json"))
               && clearLocalSession->description.contains(
                      QStringLiteral("legacy .zs")));

    bool commandLayerAdapterUsesRegistry = true;
    for (const CommandLayerCommandMetadata& command :
         commandLayerCommandRegistry()) {
        const ActionDescriptor* descriptor =
            findActionById(command.actionId);
        commandLayerAdapterUsesRegistry =
            commandLayerAdapterUsesRegistry
            && descriptor
            && command.name == descriptor
                                   ->aliasForSurface(
                                       ActionSurface::CommandLayer)
                                   .token
            && command.description == descriptor->description
            && command.executionRoute == descriptor->executionRoute;
    }
    QString commandLayerValidationFailure;
    expect("all F24 commands are registry adapters",
           commandLayerCommandRegistryIsValid(
               &commandLayerValidationFailure)
               && commandLayerValidationFailure.isEmpty()
               && commandLayerCommandRegistry().size()
                   == actionDescriptorsForSurface(
                          ActionSurface::CommandLayer)
                          .size()
               && commandLayerAdapterUsesRegistry);

    RecordingActionHost f24Host;
    bool everyF24ActionExecutedThroughRegistry = true;
    for (const CommandLayerCommandMetadata& command :
         commandLayerCommandRegistry()) {
        ActionInvocation invocation;
        if (command.inputKind
            == CommandLayerCommandInputKind::PositiveInteger) {
            invocation.parameters.insert(
                QStringLiteral("line"), 19);
        }
        const ActionExecutionResult result =
            executeCommandLayerCommand(
                command, f24Host, invocation);
        everyF24ActionExecutedThroughRegistry =
            everyF24ActionExecutedThroughRegistry
            && result.handled
            && result.succeeded
            && f24Host.actionId == command.actionId
            && f24Host.route == command.executionRoute;
    }
    expect("every F24 action executes through ActionDescriptor::execute",
           everyF24ActionExecutedThroughRegistry
               && f24Host.calls
                      == commandLayerCommandRegistry().size());

    const CommandLayerCommandMetadata* goLineCommand =
        findCommandLayerCommand(
            QStringLiteral("go <number>"));
    RecordingActionHost goLineHost;
    ActionInvocation goLineInvocation;
    goLineInvocation.parameters.insert(
        QStringLiteral("line"), 23);
    const ActionExecutionResult goLineResult =
        goLineCommand
        ? executeCommandLayerCommand(
              *goLineCommand,
              goLineHost,
              goLineInvocation)
        : ActionExecutionResult();
    expect("F24 go-line parameters reach the registered action host",
           goLineResult.succeeded
               && goLineHost.actionId
                      == QStringLiteral("navigation.goLine")
               && goLineHost.lastInvocation.parameters
                      .value(QStringLiteral("line"))
                      .toInt() == 23
               && applicationHistory.lastActionId()
                      == QStringLiteral("navigation.goLine")
               && applicationHistory.rememberedParameters(
                      QString(),
                      QStringLiteral("navigation.goLine"))
                      .value(QStringLiteral("line"))
                      .toInt() == 23);

    CommandLayerCommandMetadata unmappedCommand;
    unmappedCommand.name = QStringLiteral("unmapped");
    unmappedCommand.actionId =
        QStringLiteral("missing.commandLayerAction");
    unmappedCommand.executionRoute =
        QStringLiteral("ui.actionCatalog");
    const int callsBeforeUnmapped = f24Host.calls;
    const ActionExecutionResult unmappedResult =
        executeCommandLayerCommand(
            unmappedCommand, f24Host);
    expect("unmapped F24 actions fail instead of falling back to help",
           unmappedResult.handled
               && !unmappedResult.succeeded
               && unmappedResult.failureReason.contains(
                      QStringLiteral("not registered"))
               && f24Host.calls == callsBeforeUnmapped);

    CommandLayerActionExecutionHost routeHost;
    ActionDescriptor unknownRouteDescriptor =
        lineAction ? *lineAction : ActionDescriptor();
    unknownRouteDescriptor.id =
        QStringLiteral("test.failedAction");
    unknownRouteDescriptor.executionRoute =
        QStringLiteral("editor.unknown.route");
    const ActionExecutionResult unknownRouteResult =
        lineAction
        ? executeAction(unknownRouteDescriptor, routeHost)
        : ActionExecutionResult();
    expect("unknown production action routes fail explicitly",
           unknownRouteResult.handled
               && !unknownRouteResult.succeeded
               && unknownRouteResult.failureReason.contains(
                      QStringLiteral("editor.unknown.route"))
               && applicationHistory.lastActionId()
                      == QStringLiteral("navigation.goLine"));

    const CommandLayerCommandMetadata* deleteLinesCommand =
        findCommandLayerCommand(
            QStringLiteral("delete lines"));
    ActionInvocation f24DryRun;
    f24DryRun.mode = ActionExecutionMode::DryRun;
    const int callsBeforeF24DryRun = f24Host.calls;
    const ActionExecutionResult f24DryRunResult =
        deleteLinesCommand
        ? executeCommandLayerCommand(
              *deleteLinesCommand, f24Host, f24DryRun)
        : ActionExecutionResult();
    expect("F24 dispatch cannot bypass registry dry-run policy",
           f24DryRunResult.handled
               && !f24DryRunResult.succeeded
               && !f24DryRunResult.failureReason.isEmpty()
               && f24Host.calls == callsBeforeF24DryRun);

    bool inlineAdaptersUseRegistry = true;
    for (const InlineCommandDescriptor& inlineDescriptor :
         InlineCommandMode::descriptors()) {
        const ActionSurface surface =
            inlineDescriptor.intent == InlineCommandIntent::CodeTemplate
            ? ActionSurface::InlineTemplate
            : ActionSurface::InlineSemantic;
        const ActionDescriptor* descriptor =
            findActionById(inlineDescriptor.actionId);
        inlineAdaptersUseRegistry =
            inlineAdaptersUseRegistry
            && descriptor
            && inlineDescriptor.executionRoute
                   == descriptor->executionRoute
            && descriptorHasAlias(*descriptor,
                                  surface,
                                  inlineDescriptor.label);
    }
    expect("all built-in inline commands are registry adapters",
           InlineCommandMode::descriptors().size() >= 44
               && inlineAdaptersUseRegistry);

    const ActionDescriptor* semanticParameter =
        findActionByAlias(ActionSurface::InlineSemantic,
                          QStringLiteral(";p"));
    const ActionDescriptor* templateParameter =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;p"));
    expect("semantic search and template insertion remain distinct intents",
           semanticParameter
               && templateParameter
               && semanticParameter->id != templateParameter->id
               && semanticParameter->executionRoute
                      != templateParameter->executionRoute);

    const ActionDescriptor* visibleSymbols =
        findActionByAlias(ActionSurface::InlineSemantic,
                          QStringLiteral(";v"));
    const ActionDescriptor* visibleSymbolsTemplate =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;v"));
    expect("visible-symbol token is a unique semantic-only action",
           visibleSymbols
               && visibleSymbols->id
                      == QStringLiteral("completion.visibleSymbols")
               && visibleSymbols->canonicalName
                      == QStringLiteral("Search Visible Symbols")
               && visibleSymbols->executionRoute
                      == QStringLiteral("completion.semantic")
               && !visibleSymbolsTemplate);

    bool templatesUseRegistry = true;
    for (const CodeTemplateItem& item :
         CodeTemplateService::getInstance()->catalog()) {
        const ActionDescriptor* descriptor =
            findActionById(item.actionId);
        templatesUseRegistry =
            templatesUseRegistry
            && descriptor
            && item.executionRoute == descriptor->executionRoute
            && descriptorHasAlias(*descriptor,
                                  ActionSurface::InlineTemplate,
                                  item.commandToken);
    }
    expect("built-in template catalog uses registry metadata",
           CodeTemplateService::getInstance()->catalog().size() == 17
               && templatesUseRegistry);

    const ActionDescriptor* genericAlways =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;a"));
    const ActionDescriptor* combinationalAlways =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;ac"));
    const ActionDescriptor* flipFlopAlways =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;af"));
    expect("registry owns the three distinct always template commands",
           genericAlways
               && combinationalAlways
               && flipFlopAlways
               && genericAlways->id != combinationalAlways->id
               && genericAlways->id != flipFlopAlways->id
               && combinationalAlways->id != flipFlopAlways->id
               && !findActionByAlias(ActionSurface::InlineSemantic,
                                     QStringLiteral(";ac"))
               && !findActionByAlias(ActionSurface::InlineSemantic,
                                     QStringLiteral(";af")));

    const CodeTemplateItem genericAlwaysTemplate =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;a"));
    expect("generic always template exposes sensitivity and empty body slots",
           genericAlwaysTemplate.insertText
                   == QStringLiteral("always @(*) begin\n"
                                     "    \n"
                                     "end")
               && genericAlwaysTemplate.templateSlots.size() == 2
               && genericAlwaysTemplate.templateSlots.at(0).name
                      == QStringLiteral("sensitivity")
               && genericAlwaysTemplate.insertText.mid(
                      genericAlwaysTemplate.templateSlots.at(0).start,
                      genericAlwaysTemplate.templateSlots.at(0).length)
                      == QStringLiteral("*")
               && genericAlwaysTemplate.templateSlots.at(1).name
                      == QStringLiteral("body")
               && genericAlwaysTemplate.templateSlots.at(1).length == 0
               && genericAlwaysTemplate.templateSlots.at(1).visibleWhenEmpty);

    const CodeTemplateItem combinationalAlwaysTemplate =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;ac"));
    expect("always_comb template exposes a visible empty body slot",
           combinationalAlwaysTemplate.insertText
                   == QStringLiteral("always_comb begin\n"
                                     "    \n"
                                     "end")
               && combinationalAlwaysTemplate.templateSlots.size() == 1
               && combinationalAlwaysTemplate.templateSlots.first().name
                      == QStringLiteral("body")
               && combinationalAlwaysTemplate.templateSlots.first().length == 0
               && combinationalAlwaysTemplate.templateSlots.first()
                      .visibleWhenEmpty);

    const PackageToolService packageTools;
    bool packageToolsUseRegistry = true;
    for (const PackageToolKind kind : PackageToolService::toolOrder()) {
        const QString actionId =
            PackageToolService::actionIdForKind(kind);
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        const CodeTemplateItem item =
            packageTools.templateForKind(kind);
        packageToolsUseRegistry =
            packageToolsUseRegistry
            && descriptor
            && item.actionId == actionId
            && item.executionRoute == descriptor->executionRoute
            && descriptorHasAlias(
                *descriptor,
                ActionSurface::PackageTools,
                PackageToolService::idForKind(kind));
    }
    expect("all Package Tools are registry adapters",
           PackageToolService::toolOrder().size() == 6
               && packageToolsUseRegistry);

    const QStringList sourceActionIds = {
        QStringLiteral("insight.signalKernelGraph"),
        QStringLiteral("insight.signalUsageHotspot"),
        QStringLiteral("insight.stateTransitionGraph"),
        QStringLiteral("insight.moduleBlockDiagram"),
        QStringLiteral("refactor.exposeSignalToTop"),
    };
    bool sourceActionsComplete = true;
    for (const QString& id : sourceActionIds) {
        const ActionDescriptor* descriptor = findActionById(id);
        sourceActionsComplete =
            sourceActionsComplete
            && descriptor
            && descriptor->hasSurface(ActionSurface::ContextMenu);
    }
    const ActionDescriptor* expose =
        findActionById(QStringLiteral("refactor.exposeSignalToTop"));
    ActionAvailabilityContext exposeContext;
    exposeContext.editorAvailable = true;
    exposeContext.workspaceAvailable = true;
    exposeContext.semanticCurrent = true;
    exposeContext.symbolAvailable = true;
    const ActionAvailabilityState exposeAvailability =
        expose ? evaluateActionAvailability(*expose, exposeContext)
               : ActionAvailabilityState();
    expect("source actions and Expose have canonical context-menu descriptors",
           sourceActionsComplete
               && expose
               && expose->executionRoute
                      == QStringLiteral(
                          "rtledit.signal.exposeToTop")
               && expose->riskLevel == ActionRiskLevel::High
               && expose->supportsDryRun
               && !exposeAvailability.executable
               && exposeAvailability.enterable()
               && !exposeAvailability.reason.isEmpty());
    const QHash<QString, QString> formatShortcuts = {
        {QStringLiteral("format.commentLines"),
         QStringLiteral("Ctrl+/")},
        {QStringLiteral("format.uncommentLines"),
         QStringLiteral("Ctrl+Shift+/")},
        {QStringLiteral("format.indentLines"),
         QStringLiteral("Ctrl+]")},
        {QStringLiteral("format.unindentLines"),
         QStringLiteral("Ctrl+[")},
    };
    bool formatShortcutsCanonical = true;
    for (auto shortcut = formatShortcuts.constBegin();
         shortcut != formatShortcuts.constEnd();
         ++shortcut) {
        const ActionDescriptor* descriptor =
            findActionById(shortcut.key());
        formatShortcutsCanonical =
            formatShortcutsCanonical
            && descriptor
            && descriptor->defaultShortcut
                   == shortcut.value()
            && descriptor->hasSurface(
                ActionSurface::Shortcut)
            && descriptor
                   ->aliasForSurface(
                       ActionSurface::Shortcut)
                   .token
                   == shortcut.value();
    }
    expect("line format shortcuts are canonical Registry surfaces",
           formatShortcutsCanonical);
    const ActionDescriptor* instancePairAction =
        findActionById(QString::fromLatin1(
            ActionIds::RtlConnectInstancePair));
    const ActionDescriptor* multiSignalAction =
        findActionById(QString::fromLatin1(
            ActionIds::RtlPropagateMultipleSignals));
    const ActionDescriptor* rtlRenameAction =
        findActionById(QString::fromLatin1(
            ActionIds::RtlRename));
    const ActionDescriptor*
        rtlConnectionTransformAction =
            findActionById(QString::fromLatin1(
                ActionIds::
                    RtlConnectionTransform));
    ActionAvailabilityContext rtlEditContext;
    rtlEditContext.editorAvailable = true;
    rtlEditContext.workspaceAvailable = true;
    rtlEditContext.semanticCurrent = true;
    rtlEditContext.symbolAvailable = true;
    rtlEditContext.hierarchyBound = true;
    expect("instance-pair and batch propagation are stable High+Diff actions",
           instancePairAction
               && multiSignalAction
               && instancePairAction->riskLevel
                      == ActionRiskLevel::High
               && multiSignalAction->riskLevel
                      == ActionRiskLevel::High
               && instancePairAction->supportsDryRun
               && multiSignalAction->supportsDryRun
               && instancePairAction->rememberParameters
               && multiSignalAction->rememberParameters
               && instancePairAction->parameterModel.kind
                      == ActionParameterKind::HierarchySelection
                && multiSignalAction->parameterModel.kind
                       == ActionParameterKind::HierarchySelection
                && instancePairAction->hasSurface(
                       ActionSurface::CommandLayer)
                && multiSignalAction->hasSurface(
                       ActionSurface::CommandLayer)
                && evaluateActionAvailability(
                       *instancePairAction,
                       rtlEditContext)
                      .executable
               && evaluateActionAvailability(
                      *multiSignalAction,
                      rtlEditContext)
                      .executable);
    ActionAvailabilityContext noRtlSymbol =
        rtlEditContext;
    noRtlSymbol.symbolAvailable = false;
    ActionAvailabilityContext noRtlHierarchy =
        rtlEditContext;
    noRtlHierarchy.hierarchyBound = false;
    const ActionAvailabilityState
        renameUnavailable =
            rtlRenameAction
            ? evaluateActionAvailability(
                  *rtlRenameAction,
                  noRtlSymbol)
            : ActionAvailabilityState();
    const ActionAvailabilityState
        transformUnavailable =
            rtlConnectionTransformAction
            ? evaluateActionAvailability(
                  *rtlConnectionTransformAction,
                  noRtlHierarchy)
            : ActionAvailabilityState();
    expect("unified RTL rename and connection transform are registry-owned",
           rtlRenameAction
               && rtlConnectionTransformAction
               && rtlRenameAction->id
                      == QStringLiteral("rtl.rename")
               && rtlConnectionTransformAction->id
                      == QStringLiteral(
                          "rtl.connection.transform")
               && rtlRenameAction->riskLevel
                      == ActionRiskLevel::High
               && rtlConnectionTransformAction
                      ->riskLevel
                      == ActionRiskLevel::High
               && rtlRenameAction->supportsDryRun
               && rtlConnectionTransformAction
                      ->supportsDryRun
               && rtlRenameAction->rememberParameters
               && rtlConnectionTransformAction
                      ->rememberParameters
               && rtlRenameAction->defaultShortcut
                      == QStringLiteral("Ctrl+R")
               && rtlRenameAction->hasSurface(
                      ActionSurface::Shortcut)
               && rtlRenameAction->hasSurface(
                      ActionSurface::Menu)
               && rtlRenameAction->hasSurface(
                      ActionSurface::CommandLayer)
               && rtlRenameAction->hasSurface(
                      ActionSurface::ActionCatalog)
               && rtlConnectionTransformAction
                      ->hasSurface(
                          ActionSurface::Menu)
               && rtlConnectionTransformAction
                      ->hasSurface(
                          ActionSurface::
                              CommandLayer)
               && rtlConnectionTransformAction
                      ->hasSurface(
                          ActionSurface::
                              ActionCatalog)
               && findActionByAlias(
                      ActionSurface::CommandLayer,
                      QStringLiteral(
                          "rename rtl symbol"))
                      == rtlRenameAction
               && findActionByAlias(
                      ActionSurface::CommandLayer,
                      QStringLiteral(
                          "transform instance connections"))
                      == rtlConnectionTransformAction
               && evaluateActionAvailability(
                      *rtlRenameAction,
                      rtlEditContext)
                      .executable
               && evaluateActionAvailability(
                      *rtlConnectionTransformAction,
                      rtlEditContext)
                      .executable
               && !renameUnavailable.executable
               && !renameUnavailable.reason.isEmpty()
               && !transformUnavailable.executable
               && !transformUnavailable.reason.isEmpty());
    const ActionDescriptor* definitionAction =
        findActionById(QStringLiteral("source.goToDefinition"));
    expect("definition remains shortcut-only",
           definitionAction
               && definitionAction->defaultShortcut == QStringLiteral("F12")
               && !definitionAction->hasSurface(
                      ActionSurface::ContextMenu));
    expect("standalone references and relationships actions are removed",
           findActionById(QStringLiteral("source.findReferences")) == nullptr
               && findActionById(
                      QStringLiteral("source.showRelationships")) == nullptr);

    ActionInvocation exposePreview;
    exposePreview.mode = ActionExecutionMode::DryRun;
    const ActionExecutionResult exposePreviewResult =
        expose
        ? executeAction(*expose, actionHost, exposePreview)
        : ActionExecutionResult();
    ActionInvocation undoPreview;
    undoPreview.mode = ActionExecutionMode::DryRun;
    const int hostCallsBeforeRejectedPreview = actionHost.calls;
    const ActionExecutionResult rejectedPreview =
        undoAction
        ? executeAction(*undoAction, actionHost, undoPreview)
        : ActionExecutionResult();
    expect("high-risk dry-run is supported and safe actions reject it",
           exposePreviewResult.succeeded
               && exposePreviewResult.dryRun
               && rejectedPreview.handled
               && !rejectedPreview.succeeded
               && !rejectedPreview.failureReason.isEmpty()
               && actionHost.calls == hostCallsBeforeRejectedPreview
               && applicationHistory.lastActionId()
                      == QStringLiteral("navigation.goLine"));

    bool globalControlUsesRegistry = true;
    const GlobalControlService globalControl;
    for (const QString& query :
         {QStringLiteral("fd"),
          QStringLiteral("ow"),
          QStringLiteral("ow s")}) {
        for (const GlobalControlItem& item :
             globalControl.query(query)) {
            if (item.kind != GlobalControlItemKind::Command)
                continue;
            const ActionDescriptor* descriptor =
                findActionById(item.actionId);
            globalControlUsesRegistry =
                globalControlUsesRegistry
                && descriptor
                && item.executionRoute
                       == descriptor->executionRoute
                && descriptorHasAlias(*descriptor,
                                      ActionSurface::GlobalControl,
                                      item.id);
        }
    }
    expect("static Global Control commands use registry metadata",
           globalControlUsesRegistry);

    CommandLayerActionExecutionHost repeatRoutingHost;
    repeatRoutingHost.setFallbackHost(&actionHost);
    int knownRouteCalls = 0;
    QString bindFailure;
    const bool knownRouteBound =
        lineAction
        && repeatRoutingHost.bindRoute(
            lineAction->executionRoute,
            [&knownRouteCalls](
                const ActionDescriptor&,
                const ActionInvocation&) {
                ++knownRouteCalls;
                ActionExecutionResult result;
                result.handled = true;
                result.succeeded = true;
                return result;
            },
            &bindFailure);
    const int fallbackCallsBeforeKnown =
        actionHost.calls;
    const ActionExecutionResult knownRouteResult =
        lineAction
        ? executeAction(
              *lineAction,
              repeatRoutingHost,
              lineInvocation)
        : ActionExecutionResult();
    expect("Command Layer routes stay local when a MainWindow fallback exists",
           repeatRoutingHost.hasFallbackHost()
               && knownRouteBound
               && bindFailure.isEmpty()
               && knownRouteResult.succeeded
               && knownRouteCalls == 1
               && actionHost.calls
                      == fallbackCallsBeforeKnown);

    actionHost.returnResolvedParameters = true;
    actionHost.resolvedParameters = {
        {QStringLiteral("leftInstancePath"),
         QStringLiteral("top.u_left")},
        {QStringLiteral("rightInstancePath"),
         QStringLiteral("top.u_right")},
        {QStringLiteral("connectionName"),
         QStringLiteral("payload_link")},
    };
    ActionInvocation interactiveInvocation;
    interactiveInvocation.workspaceId =
        QStringLiteral("workspace-rtl");
    const ActionExecutionResult interactiveResult =
        instancePairAction
        ? executeAction(
              *instancePairAction,
              repeatRoutingHost,
              interactiveInvocation)
        : ActionExecutionResult();
    const int fallbackCallsAfterInteractive =
        actionHost.calls;
    const ActionExecutionResult repeatedInteractive =
        applicationHistory.repeatLast(
            repeatRoutingHost);
    expect("interactive Action history records resolved workspace parameters",
           interactiveResult.succeeded
               && repeatedInteractive.succeeded
               && actionHost.calls
                      == fallbackCallsAfterInteractive + 1
                && actionHost.route
                       == QStringLiteral(
                           "rtledit.instancePair.connect")
                && actionHost.lastInvocation.mode
                       == ActionExecutionMode::DryRun
                && actionHost.lastInvocation.parameters
                       == actionHost.resolvedParameters
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-rtl"),
                      QString::fromLatin1(
                          ActionIds::RtlConnectInstancePair))
                      == actionHost.resolvedParameters);
    actionHost.returnResolvedParameters = false;
    actionHost.resolvedParameters.clear();

    resetApplicationActionExecutionHistory();
    expect("application action history reset clears action and parameter state",
           applicationHistory.lastActionId().isEmpty()
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-a"),
                      QStringLiteral("navigation.goLine"))
                      .isEmpty()
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-b"),
                      QStringLiteral("navigation.goLine"))
                      .isEmpty()
               && applicationHistory.rememberedParameters(
                      QStringLiteral("workspace-rtl"),
                      QString::fromLatin1(
                          ActionIds::RtlConnectInstancePair))
                      .isEmpty());

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
