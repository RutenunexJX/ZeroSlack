#ifndef ACTIONREGISTRY_H
#define ACTIONREGISTRY_H

#include "zeroslackexport.h"

#include <QList>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

#include <functional>

enum class ActionCategory {
    Unknown,
    Navigate,
    Inspect,
    Refactor,
    Format,
    Insert,
    Select,
    Workspace,
    Fold,
    Help
};

enum class ActionScope {
    Unknown,
    Application,
    Workspace,
    Editor,
    Module,
    Package,
    Symbol,
    Hierarchy
};

enum class ActionParameterKind {
    None,
    PositiveInteger,
    TextQuery,
    TemplateSeed,
    HierarchySelection,
    PackageTool,
    FilePath
};

enum class ActionSurface {
    Unknown,
    CommandLayer,
    InlineSemantic,
    InlineTemplate,
    GlobalControl,
    PackageTools,
    ContextMenu,
    TabContextMenu,
    PanelContextMenu,
    Shortcut,
    ActionCatalog,
    GraphPanel,
    Menu
};

enum class ActionRecoveryPolicy {
    None,
    Explain,
    Analyze,
    SelectHierarchy
};

enum class ActionRiskLevel {
    Safe,
    Confirm,
    High
};

enum class ActionExecutionMode {
    Execute,
    DryRun
};

namespace ActionRequirements {
inline constexpr quint32 Editor = 1u << 0;
inline constexpr quint32 Workspace = 1u << 1;
inline constexpr quint32 SemanticCurrent = 1u << 2;
inline constexpr quint32 Symbol = 1u << 3;
inline constexpr quint32 Package = 1u << 4;
inline constexpr quint32 Hierarchy = 1u << 5;
inline constexpr quint32 GraphContent = 1u << 6;
}

namespace ActionIds {
inline constexpr const char FileNew[] =
    "file.new";
inline constexpr const char FileOpen[] =
    "file.open";
inline constexpr const char FileSave[] =
    "file.save";
inline constexpr const char FileSaveAs[] =
    "file.saveAs";
inline constexpr const char WorkspaceOpen[] =
    "workspace.open";
inline constexpr const char SelectExpandSmart[] =
    "select.expandSmart";
inline constexpr const char NavigationNextSelectedSymbolOccurrence[] =
    "navigation.nextSelectedSymbolOccurrence";
inline constexpr const char NavigationPreviousSelectedSymbolOccurrence[] =
    "navigation.previousSelectedSymbolOccurrence";
inline constexpr const char NavigationDesignGoInstantiation[] =
    "navigation.design.goInstantiation";
inline constexpr const char NavigationDesignGoDefinition[] =
    "navigation.design.goDefinition";
inline constexpr const char NavigationDesignSetTop[] =
    "navigation.design.setTop";
inline constexpr const char WaveSimulationRunCurrentContext[] =
    "waveSimulation.runCurrentContext";
inline constexpr const char WaveSimulationObserveSignal[] =
    "waveSimulation.observeSignal";
inline constexpr const char WaveSimulationRevealSignalInResult[] =
    "waveSimulation.revealSignalInResult";
inline constexpr const char WaveSimulationRunDesignInstance[] =
    "waveSimulation.runDesignInstance";
inline constexpr const char EditDuplicateLines[] =
    "edit.duplicateLines";
inline constexpr const char EditMoveLinesUp[] =
    "edit.moveLinesUp";
inline constexpr const char EditMoveLinesDown[] =
    "edit.moveLinesDown";
inline constexpr const char EditDeleteSelection[] =
    "edit.deleteSelection";
inline constexpr const char EditToggleSelectionCase[] =
    "edit.toggleSelectionCase";
inline constexpr const char EditReplaceSelectionWithSpaces[] =
    "edit.replaceSelectionWithSpaces";
inline constexpr const char RefactorOrganizeSignalDeclarations[] =
    "refactor.organizeSignalDeclarations";
inline constexpr const char PinloomLinkSelection[] =
    "pinloom.linkSelection";
inline constexpr const char PinloomOpenLinkedContent[] =
    "pinloom.openLinkedContent";
inline constexpr const char PinloomToggleBindingMarkers[] =
    "pinloom.toggleBindingMarkers";
inline constexpr const char InsertColumnNumbers[] =
    "insert.columnNumbers";
inline constexpr const char FoldShelfDeleteSelected[] =
    "fold.shelf.deleteSelected";
inline constexpr const char ViewSettingsCenter[] =
    "view.settingsCenter";
inline constexpr const char ViewNavigation[] =
    "view.navigation.toggle";
inline constexpr const char ViewGlobalControl[] =
    "view.globalControl";
inline constexpr const char ViewCommandMode[] =
    "view.commandMode";
inline constexpr const char ViewScopedSearch[] =
    "view.scopedSearch";
inline constexpr const char ViewFocusMode[] =
    "view.focusMode.toggle";
inline constexpr const char ViewEditorSplitLeft[] =
    "view.editorSplit.left";
inline constexpr const char ViewEditorSplitRight[] =
    "view.editorSplit.right";
inline constexpr const char ViewEditorSplitAbove[] =
    "view.editorSplit.above";
inline constexpr const char ViewEditorSplitBelow[] =
    "view.editorSplit.below";
inline constexpr const char ViewEditorSplitMaximize[] =
    "view.editorSplit.maximize";
inline constexpr const char ViewEditorSplitsEqualize[] =
    "view.editorSplit.equalize";
inline constexpr const char ViewEditorSplitMerge[] =
    "view.editorSplit.merge";
inline constexpr const char ViewReopenClosedTab[] =
    "view.editorTab.reopenClosed";
inline constexpr const char ViewEditorTabClose[] =
    "view.editorTab.close";
inline constexpr const char ViewEditorTabCloseOthers[] =
    "view.editorTab.closeOthers";
inline constexpr const char ViewEditorTabCloseRight[] =
    "view.editorTab.closeRight";
inline constexpr const char ViewEditorTabCloseAll[] =
    "view.editorTab.closeAll";
inline constexpr const char ViewEditorTabDuplicate[] =
    "view.editorTab.duplicateView";
inline constexpr const char ViewTemporaryEditorOpen[] =
    "view.temporaryEditor.open";
inline constexpr const char ViewEditorTabToggleLocked[] =
    "view.editorTab.toggleLocked";
inline constexpr const char ViewGroupTabsNone[] =
    "view.tabGrouping.none";
inline constexpr const char ViewGroupTabsModule[] =
    "view.tabGrouping.module";
inline constexpr const char ViewGroupTabsWorkspace[] =
    "view.tabGrouping.workspace";
inline constexpr const char ViewProblems[] =
    "view.problems.toggle";
inline constexpr const char ViewActivity[] =
    "view.activity.toggle";
inline constexpr const char ViewRtlInsights[] =
    "view.rtlInsights.toggle";
inline constexpr const char ViewSignalKernelGraph[] =
    "view.signalKernelGraph.toggle";
inline constexpr const char ViewWavePreview[] =
    "view.wavePreview.toggle";
inline constexpr const char ViewBottomPanelCollapsed[] =
    "view.bottomPanel.toggleCollapsed";
inline constexpr const char ViewBottomPanelPinned[] =
    "view.bottomPanel.togglePinned";
inline constexpr const char ViewBottomPanelClose[] =
    "view.bottomPanel.closeActive";
inline constexpr const char ViewFocusRtlInsights[] =
    "view.insightFocus.rtlInsights";
inline constexpr const char ViewFocusSignalKernelGraph[] =
    "view.insightFocus.signalKernelGraph";
inline constexpr const char ViewFocusWavePreview[] =
    "view.insightFocus.wavePreview";
inline constexpr const char ViewLeaveInsightFocus[] =
    "view.insightFocus.leave";
inline constexpr const char ViewFoldShelf[] =
    "view.foldShelf.toggle";
inline constexpr const char ViewResetPanelLayout[] =
    "view.panelLayout.reset";
inline constexpr const char WorkspaceCloseActive[] =
    "workspace.closeActive";
inline constexpr const char WorkspaceConfigure[] =
    "workspace.configure";
inline constexpr const char WorkspaceNextDiagnostic[] =
    "workspace.diagnostic.next";
inline constexpr const char WorkspacePreviousDiagnostic[] =
    "workspace.diagnostic.previous";
inline constexpr const char UserTemplatesOpenGlobal[] =
    "templates.user.openGlobal";
inline constexpr const char UserTemplatesOpenWorkspace[] =
    "templates.user.openWorkspace";
inline constexpr const char UserTemplatesReload[] =
    "templates.user.reload";
inline constexpr const char ReviewCrashRecovery[] =
    "workspace.crashRecovery.review";
inline constexpr const char RtlConnectInstancePair[] =
    "signal.connectInstancePair";
inline constexpr const char RtlPropagateMultipleSignals[] =
    "signal.propagateBatch";
inline constexpr const char RtlRename[] =
    "rtl.rename";
inline constexpr const char RtlConnectionTransform[] =
    "rtl.connection.transform";
inline constexpr const char RepeatLastAction[] =
    "action.repeatLast";
inline constexpr const char WorkspaceFileCreate[] =
    "workspace.file.create";
inline constexpr const char WorkspaceDirectoryCreate[] =
    "workspace.directory.create";
inline constexpr const char WorkspacePathRename[] =
    "workspace.path.rename";
inline constexpr const char WorkspacePathDelete[] =
    "workspace.path.delete";
inline constexpr const char WorkspacePathCopy[] =
    "workspace.path.copy";
inline constexpr const char WorkspacePathReveal[] =
    "workspace.path.reveal";
inline constexpr const char GraphJumpSelected[] =
    "graph.selection.jump";
inline constexpr const char GraphFocusSelected[] =
    "graph.selection.focus";
inline constexpr const char GraphSetTopSelected[] =
    "graph.moduleBlock.setTop";
inline constexpr const char GraphViewFit[] =
    "graph.view.fit";
inline constexpr const char GraphViewZoomIn[] =
    "graph.view.zoomIn";
inline constexpr const char GraphViewZoomOut[] =
    "graph.view.zoomOut";
inline constexpr const char GraphViewCenterCurrent[] =
    "graph.view.centerCurrent";
inline constexpr const char GraphViewResetLayout[] =
    "graph.view.resetLayout";
inline constexpr const char GraphExportRtlInsights[] =
    "graph.export.rtlInsights";
inline constexpr const char GraphExportSignalKernel[] =
    "graph.export.signalKernel";
inline constexpr const char GraphExportUsageHotspotTrack[] =
    "graph.export.usageHotspotTrack";
inline constexpr const char GraphExportUsageHotspotMatrix[] =
    "graph.export.usageHotspotMatrix";
inline constexpr const char GraphExportWavePreview[] =
    "graph.export.wavePreview";
}

struct ActionParameterModel {
    ActionParameterKind kind = ActionParameterKind::None;
    QString name;
    QString placeholder;
};

struct ActionAliasDescriptor {
    ActionSurface surface = ActionSurface::Unknown;
    QString token;
    QString label;
    QString description;
    QString defaultValue;
    QString adapterKey;
    QString intentKey;
    bool triggerAdapter = true;
    bool catalogued = false;
};

struct ActionInvocation {
    QString workspaceId;
    QVariantMap parameters;
    ActionExecutionMode mode = ActionExecutionMode::Execute;
};

struct ActionExecutionResult {
    bool handled = false;
    bool succeeded = false;
    bool dryRun = false;
    QString message;
    QString failureReason;
    // Interactive hosts can return the structured parameters that were
    // actually accepted. Action history records these values instead of the
    // incomplete invocation that opened the interaction.
    bool hasResolvedParameters = false;
    QVariantMap resolvedParameters;
    QVariantMap output;
};

using RegisteredActionRequestHandler =
    std::function<ActionExecutionResult(
        const QString&,
        const QVariantMap&)>;

struct ActionDescriptor;

class ZEROSLACK_API ActionExecutionHost
{
public:
    virtual ~ActionExecutionHost() = default;

    virtual ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) = 0;
};

using ActionExecutionFunction = std::function<ActionExecutionResult(
    const ActionDescriptor&,
    ActionExecutionHost&,
    const ActionInvocation&)>;

struct ActionDescriptor {
    QString id;
    QString canonicalName;
    QString description;
    ActionCategory category = ActionCategory::Unknown;
    ActionScope scope = ActionScope::Unknown;
    ActionParameterModel parameterModel;
    quint32 requirementMask = 0;
    ActionRecoveryPolicy recoveryPolicy = ActionRecoveryPolicy::None;
    QString unavailableReason;
    QString executionRoute;
    QString defaultShortcut;
    ActionRiskLevel riskLevel = ActionRiskLevel::Safe;
    bool supportsDryRun = false;
    bool sharedExecutionRoute = false;
    bool repeatable = false;
    bool rememberParameters = false;
    ActionExecutionFunction execute;
    QList<ActionAliasDescriptor> aliases;

    bool hasSurface(ActionSurface surface) const;
    ActionAliasDescriptor aliasForSurface(ActionSurface surface) const;
};

struct ActionAvailabilityContext {
    bool editorAvailable = false;
    bool workspaceAvailable = false;
    bool semanticCurrent = false;
    bool symbolAvailable = false;
    bool packageAvailable = false;
    bool hierarchyBound = false;
    bool graphContentAvailable = false;
};

struct ActionAvailabilityState {
    bool executable = false;
    bool resolvable = false;
    QString reason;

    bool enterable() const { return executable || resolvable; }
};

struct ActionCatalogEntry {
    QString actionId;
    QString canonicalName;
    ActionCategory category = ActionCategory::Unknown;
    ActionScope scope = ActionScope::Unknown;
    QString displayText;
};

class ActionExecutionHistory
{
public:
    void clear();
    void recordSuccessful(const ActionDescriptor& descriptor,
                          const ActionInvocation& invocation,
                          const ActionExecutionResult& result);
    QVariantMap rememberedParameters(const QString& workspaceId,
                                     const QString& actionId) const;
    QString lastActionId() const;
    bool hasRepeatableAction() const;
    ActionExecutionResult repeatLast(ActionExecutionHost& host);

private:
    struct RecordedInvocation {
        QString actionId;
        ActionInvocation invocation;
    };

    RecordedInvocation last;
    QHash<QString, QVariantMap> parametersByWorkspaceAndAction;
};

ActionExecutionHistory& applicationActionExecutionHistory();
void resetApplicationActionExecutionHistory();

const QList<ActionDescriptor>& actionRegistry();
const ActionDescriptor* findActionById(const QString& id);
const ActionDescriptor* findActionByAlias(ActionSurface surface,
                                          const QString& token);
const ActionAliasDescriptor* findActionAlias(
    const ActionDescriptor& descriptor,
    ActionSurface surface,
    const QString& token = QString());
QList<const ActionDescriptor*> actionDescriptorsForSurface(
    ActionSurface surface);
QList<ActionCatalogEntry> unifiedActionCatalog();
bool validateActionRegistry(const QList<ActionDescriptor>& registry,
                            QString* reason = nullptr);
bool actionRegistryIsValid(QString* reason = nullptr);
ActionAvailabilityState evaluateActionAvailability(
    const ActionDescriptor& descriptor,
    const ActionAvailabilityContext& context);
ActionExecutionResult executeAction(
    const ActionDescriptor& descriptor,
    ActionExecutionHost& host,
    const ActionInvocation& invocation = {});
QString actionCategoryText(ActionCategory category);
QString actionScopeText(ActionScope scope);
QString actionSurfaceText(ActionSurface surface);
bool configureActionShortcutOverrides(
    const QVariantMap& overrides,
    QStringList* issues = nullptr);
QVariantMap actionShortcutOverrides();
QString effectiveActionShortcut(const QString& actionId);

#endif // ACTIONREGISTRY_H
