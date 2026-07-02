# ZeroSlack Full Feature Audit

- Generated: 2026-07-02T12:44:37Z UTC
- Workspace: E:\ZeroSlack\ZeroSlack
- Roots: test_sv/new, test_sv/huge_prj
- SV context files: 454
- Features inventoried: 23
- User entry points counted: 93
- Service/test touchpoints counted: 75

## Status Summary

| Status | Count |
| --- | ---: |
| `pass` | 23 |
| `fail` | 0 |
| `skipped` | 0 |
| `empty-valid` | 0 |
| `known-issue` | 0 |

## Feature Matrix

| Feature | Entries | Services | Automation | Coverage | Status | Reason | Pollution | Next |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `global_control` Global command palette / Ctrl+Space domains | Ctrl+Space<br>ow<br>fd<br>RTL Insights | GlobalControlService<br>GlobalControlCoordinator<br>CodeTemplateService | service inventory plus GUI smoke | offscreen GUI smoke plus 454-file read-only context | `pass` | global commands=7 rtl insight entries=5 templates=15 | no | Keep adding domains through GlobalControlService::commandItems. |
| `com_mode_lifecycle` COM Mode enter/exit lifecycle | backtick<br>Esc<br>status mode chip | ModeManager<br>ComModeCoordinator<br>ComModeService | registry validation plus offscreen key workflow | targeted regression fixtures plus 454-file read-only context | `pass` | registry validates; Esc/backtick lifecycle covered by GUI smoke | no | Add lifecycle-only unit coverage if COM mode grows more states. |
| `com_mode_commands` COM Mode fixed and g-family commands | cr<br>cn<br>si<br>g<num><br>gm<br>ga*<br>ge*<br>gp*<br>gi*<br>gs* | ComModeCommandRegistry<br>ComModeCoordinator<br>EditorSourceNavigation | registry validation and GUI command smoke | targeted regression fixtures plus 454-file read-only context | `pass` | registered COM commands=20 | no | Promote each new executable command into registry validation. |
| `inline_semantic_commands` ;cmd semantic completion commands | ;r<br>;w<br>;l<br>;m<br>;t<br>;f<br>;i<br>;d<br>;p<br>;a<br>;c | InlineCommandMode<br>CompletionCommandMode<br>CompletionService<br>CompletionSemanticQuery | descriptor inventory, service completion tests, GUI activation | targeted regression fixtures plus 454-file read-only context | `pass` | semantic command descriptors=21 | no | Add command-kind distribution checks through focused fixtures. |
| `inline_template_commands` ;;cmd built-in template commands | ;;r<br>;;w<br>;;l<br>;;m<br>;;? | InlineCommandMode<br>CompletionService<br>CodeTemplateService | descriptor inventory and activation smoke | targeted regression fixtures plus 454-file read-only context | `pass` | all inline descriptors=44 built-in templates=15 | no | Keep templates fixture-backed to avoid editing real corpus files. |
| `user_template_commands` ;;cmd user template commands | ;;<user-template> | UserTemplateService<br>CompletionService | temporary JSON fixture plus GUI activation | fixture only; no real corpus mutation | `pass` | GUI smoke writes a temporary user_templates.json and activates ;;guiut | no | Add schema validation report for malformed user templates. |
| `slot_mode` Slot Mode traversal and template slot editing | Tab/Enter activation<br>Esc exit<br>template slot selection | MyCodeEditor<br>CompletionActivationState<br>CodeTemplateSlotList | temporary editor buffers | fixture only; corpus read-only | `pass` | module instantiation and user template GUI paths enter Slot Mode | no | Extend with multi-slot navigation edge cases. |
| `column_mode` Column Mode / column number tool | COM cn<br>column number dialog/tool | ColumnNumberTool<br>ComModeCoordinator | offscreen GUI smoke | fixture only; corpus read-only | `pass` | COM cn and ColumnNumberTool are covered by GUI smoke | no | Add service-only assertions for large column ranges. |
| `editor_basic_actions` Editor base actions and shortcuts | new/open/save<br>keyboard navigation<br>selection<br>hover<br>completion keys | TabManager<br>FileCommandCoordinator<br>EditorCoordinator<br>MyCodeEditor | temporary workspace plus fixtures | fixture only; corpus read-only | `pass` | open/new/save/selection/navigation paths covered by existing GUI smoke | no | Keep destructive editor commands on temp files only. |
| `menus_context_sidebars` Menus, right-click menus, sidebar buttons, panel actions | Workspace menu<br>View menu<br>Tools menu<br>workspace tab context menu<br>dock toggle actions<br>package tool buttons | MainWindow<br>NavigationPaneCoordinator<br>SemanticDockCoordinator<br>PackageToolService | offscreen GUI smoke plus static inventory | offscreen GUI smoke plus 454-file read-only context | `pass` | MainWindow setup creates workspace/view/tools menus, docks, package buttons, and panel actions | no | Add QAction object-name inventory if menu churn increases. |
| `navigation_hierarchy` Navigation and design hierarchy | navigation pane<br>module hierarchy<br>file hierarchy<br>design hierarchy | NavigationService<br>HierarchyService<br>NavigationManager<br>ModuleHierarchyModel | targeted service regressions plus GUI smoke | feature-specific regression tests and GUI smoke | `pass` | jump, relationship, and GUI smoke tests cover focused hierarchy fixtures | no | Promote future hierarchy UI failures into small cross-file fixtures. |
| `outline_goto_back_forward` Outline, goto, and back/forward navigation | outline tree<br>goto symbol<br>back<br>forward | NavigationService<br>SourceNavigationService<br>DefinitionNavigationService | focused jump tests plus GUI smoke | feature-specific regression tests and GUI smoke | `pass` | jump and GUI smoke tests cover focused outline/navigation flows | no | Add back-forward stack depth fixtures when navigation bugs are found. |
| `definition_references_relationships` Definition, References, Relationships | go to definition<br>references panel<br>relationships panel<br>relationship graph | DefinitionService<br>ReferenceService<br>RelationshipService<br>SmartRelationshipBuilder | service regressions plus GUI smoke | feature-specific regression tests and GUI smoke | `pass` | relationship and jump tests cover fixture-based semantic navigation | no | Add precision/recall fixture cases for cross-file relationships. |
| `diagnostics_workspace_config` Diagnostics, Problems, workspace config, includes, defines, ignored dirs | Problems panel<br>workspace config dialog<br>include dirs<br>defines<br>ignored dirs | DiagnosticService<br>DiagnosticNavigationService<br>ProblemsPanelCoordinator<br>WorkspaceConfigurationService<br>WorkspaceIgnoreService | GUI panel smoke plus service inventory | offscreen GUI smoke plus 454-file read-only context | `pass` | workspace diagnostic/config defaults=4; GUI smoke owns panel workflow | no | Add targeted diagnostic fixtures for each config/include regression. |
| `formatter_fold_shelf` Formatter, fold, and fold shelf | format action<br>editor fold<br>fd r<br>fd s<br>fold shelf dock | FormatterService<br>EditorFolding<br>FoldBlockShelfModel<br>FoldShelfPersistenceService<br>FoldShelfRestoreService | temporary editor buffers and temporary shelf settings | fixture only; corpus read-only | `pass` | formatter/fold/fold shelf paths are covered through GUI and temp-buffer tests | no | Add direct formatter idempotence sweep on copied corpus files. |
| `package_macro_include` Package tools, macro/define semantics, include/header/package shortcuts | Package Tools bar<br>;h<br>;pk<br>;d<br>Go package gpk | PackageToolService<br>SvMacroSemantics<br>CompletionSemanticQuery<br>InlineCommandMode | descriptor inventory, GUI activation, semantic fixtures | feature-specific regression tests and GUI smoke | `pass` | package tool buttons=6; ;h and ;pk descriptors present in inline command inventory | no | Add include resolution success/failure fixtures. |
| `rtl_fsm` RTL Insights: FSM / State Transition Graph | RTL Insights FSM Graph<br>state transition graph panel/action | FsmGraphService<br>StateTransitionGraphService<br>StateTransitionTriggerService<br>RtlInsightsPanelCoordinator | GUI smoke and feature-specific regression fixtures | feature-specific regression tests and GUI smoke | `pass` | RTL insight action inventory is present; GUI-found FSM regressions should become focused fixtures | no | Add compact state-transition fixtures for each observed FSM parsing bug. |
| `rtl_signal_journey_clock_reset` RTL Insights: Signal Journey and Clock/Reset | Signal Journey<br>Clock/Reset Domain Map | SignalJourneyService<br>ClockResetDomainService<br>RelationshipService | panel smoke plus targeted relationship fixtures | feature-specific regression tests and GUI smoke | `pass` | RTL insight actions are inventoried; relationship behavior is covered by focused tests | no | Add compact signal-path fixtures for each journey/domain regression. |
| `rtl_semantic_diff` RTL Insights: Semantic Diff | RTL Insights Semantic Diff<br>compare workflow | SemanticDiffService<br>RtlInsightsPanelCoordinator | temporary fixture compare | fixture only; corpus read-only | `pass` | GUI smoke covers semantic diff panel/action on temporary fixtures | no | Add copied-corpus pair diff sweep if needed. |
| `rtl_signal_kernel_graph` RTL Insights: Signal Kernel Graph | Signal Kernel Graph panel<br>signal graph action | SignalKernelGraphService<br>SignalKernelGraphPanelCoordinator | GUI smoke and feature-specific regression fixtures | feature-specific regression tests and GUI smoke | `pass` | Signal Kernel Graph action is inventoried; focused fixtures own graph correctness | no | Add compact signal-kernel fixtures for each graph rendering or data bug. |
| `rtl_module_block_diagram` RTL Insights: Module Block Diagram | Module Block Diagram<br>RTL Insights diagram action | ModuleBlockDiagramService<br>RtlInsightsPanelCoordinator | GUI smoke and feature-specific regression fixtures | feature-specific regression tests and GUI smoke | `pass` | Module Block Diagram action is inventoried; focused fixtures own diagram correctness | no | Add compact block-diagram fixtures for each connection/rendering bug. |
| `rtl_wave_preview` RTL Insights: Wave Preview | Wave Preview dock<br>active-editor wave refresh | WavePreviewService<br>WavePreviewPanelCoordinator | GUI smoke and feature-specific regression fixtures | feature-specific regression tests and GUI smoke | `pass` | Wave Preview action is inventoried; focused fixtures own waveform extraction behavior | no | Add compact wave-preview fixtures for each no-lane/no-warning regression. |
| `search_rename_workspace_workflow` Search, rename, semantic diff, and workspace workflow | search<br>safe rename<br>workspace open/close/recent<br>semantic diff | SearchService<br>SafeRenameService<br>WorkspaceManager<br>SemanticDiffService | temporary workspace plus service-level smoke | fixture only; corpus read-only | `pass` | service and GUI smoke cover search/rename/workspace open-close paths on fixtures | no | Add rename collision/cross-file fixture matrix. |

## Known Issues

- None recorded by this audit.

## Notes

- Real corpus files were read only. Editing features are represented by service or GUI tests that use temporary buffers/files.
- `known-issue` marks a real feature surface with bounded failures or incomplete coverage that should not block the inventory.
- Broad corpus sweeps have been retired; GUI-discovered defects should become small regression fixtures in the focused test targets.
