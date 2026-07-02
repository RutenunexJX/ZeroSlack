# ZeroSlack Full Feature Audit

- Generated: 2026-07-02T07:33:45Z UTC
- Workspace: E:\ZeroSlack\ZeroSlack
- Roots: test_sv/new, test_sv/huge_prj
- Recursive SV files: 454
- Corpus report files: 454
- Semantic records: 117069
- Relationships: 82636
- Diagnostics: 704
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

| Feature | Entries | Services | Automation | Corpus coverage | Status | Reason | Pollution | Next |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `global_control` Global command palette / Ctrl+Space domains | Ctrl+Space<br>ow<br>fd<br>RTL Insights | GlobalControlService<br>GlobalControlCoordinator<br>CodeTemplateService | service inventory plus GUI smoke | offscreen GUI smoke plus 454-file corpus context | `pass` | global commands=7 rtl insight entries=5 templates=15 | no | Keep adding domains through GlobalControlService::commandItems. |
| `com_mode_lifecycle` COM Mode enter/exit lifecycle | backtick<br>Esc<br>status mode chip | ModeManager<br>ComModeCoordinator<br>ComModeService | registry validation plus offscreen key workflow | focused fixtures plus 454-file corpus context | `pass` | registry validates; Esc/backtick lifecycle covered by GUI smoke | no | Add lifecycle-only unit coverage if COM mode grows more states. |
| `com_mode_commands` COM Mode fixed and g-family commands | cr<br>cn<br>si<br>g<num><br>gm<br>ga*<br>ge*<br>gp*<br>gi*<br>gs* | ComModeCommandRegistry<br>ComModeCoordinator<br>EditorSourceNavigation | registry validation and GUI command smoke | focused fixtures plus 454-file corpus context | `pass` | registered COM commands=20 | no | Promote each new executable command into registry validation. |
| `inline_semantic_commands` ;cmd semantic completion commands | ;r<br>;w<br>;l<br>;m<br>;t<br>;f<br>;i<br>;d<br>;p<br>;a<br>;c | InlineCommandMode<br>CompletionCommandMode<br>CompletionService<br>CompletionSemanticQuery | descriptor inventory, service completion tests, GUI activation | focused fixtures plus 454-file corpus context | `pass` | semantic command descriptors=21 | no | Add corpus-distribution metrics by command kind. |
| `inline_template_commands` ;;cmd built-in template commands | ;;r<br>;;w<br>;;l<br>;;m<br>;;? | InlineCommandMode<br>CompletionService<br>CodeTemplateService | descriptor inventory and activation smoke | focused fixtures plus 454-file corpus context | `pass` | all inline descriptors=44 built-in templates=15 | no | Keep templates fixture-backed to avoid editing real corpus files. |
| `user_template_commands` ;;cmd user template commands | ;;<user-template> | UserTemplateService<br>CompletionService | temporary JSON fixture plus GUI activation | fixture only; no real corpus mutation | `pass` | GUI smoke writes a temporary user_templates.json and activates ;;guiut | no | Add schema validation report for malformed user templates. |
| `slot_mode` Slot Mode traversal and template slot editing | Tab/Enter activation<br>Esc exit<br>template slot selection | MyCodeEditor<br>CompletionActivationState<br>CodeTemplateSlotList | temporary editor buffers | fixture only; corpus read-only | `pass` | module instantiation and user template GUI paths enter Slot Mode | no | Extend with multi-slot navigation edge cases. |
| `column_mode` Column Mode / column number tool | COM cn<br>column number dialog/tool | ColumnNumberTool<br>ComModeCoordinator | offscreen GUI smoke | fixture only; corpus read-only | `pass` | COM cn and ColumnNumberTool are covered by GUI smoke | no | Add service-only assertions for large column ranges. |
| `editor_basic_actions` Editor base actions and shortcuts | new/open/save<br>keyboard navigation<br>selection<br>hover<br>completion keys | TabManager<br>FileCommandCoordinator<br>EditorCoordinator<br>MyCodeEditor | temporary workspace plus fixtures | fixture only; corpus read-only | `pass` | open/new/save/selection/navigation paths covered by existing GUI smoke | no | Keep destructive editor commands on temp files only. |
| `menus_context_sidebars` Menus, right-click menus, sidebar buttons, panel actions | Workspace menu<br>View menu<br>Tools menu<br>workspace tab context menu<br>dock toggle actions<br>package tool buttons | MainWindow<br>NavigationPaneCoordinator<br>SemanticDockCoordinator<br>PackageToolService | offscreen GUI smoke plus static inventory | offscreen GUI smoke plus 454-file corpus context | `pass` | MainWindow setup creates workspace/view/tools menus, docks, package buttons, and panel actions | no | Add QAction object-name inventory if menu churn increases. |
| `navigation_hierarchy` Navigation and design hierarchy | navigation pane<br>module hierarchy<br>file hierarchy<br>design hierarchy | NavigationService<br>HierarchyService<br>NavigationManager<br>ModuleHierarchyModel | service tests, GUI smoke, corpus outline sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=874 fail=0 skipped=0 timeout=0 empty-valid=5 | no | Split hierarchy failures by file/module when corpus grows. |
| `outline_goto_back_forward` Outline, goto, and back/forward navigation | outline tree<br>goto symbol<br>back<br>forward | NavigationService<br>SourceNavigationService<br>DefinitionNavigationService | focused jump tests plus corpus outline sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=874 fail=0 skipped=0 timeout=0 empty-valid=5 | no | Track back-forward stack depth in next audit. |
| `definition_references_relationships` Definition, References, Relationships | go to definition<br>references panel<br>relationships panel<br>relationship graph | DefinitionService<br>ReferenceService<br>RelationshipService<br>SmartRelationshipBuilder | service tests plus full corpus relationship extraction | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=874 fail=0 skipped=0 timeout=0 empty-valid=5 | no | Add precision/recall fixture cases for cross-file relationships. |
| `diagnostics_workspace_config` Diagnostics, Problems, workspace config, includes, defines, ignored dirs | Problems panel<br>workspace config dialog<br>include dirs<br>defines<br>ignored dirs | DiagnosticService<br>DiagnosticNavigationService<br>ProblemsPanelCoordinator<br>WorkspaceConfigurationService<br>WorkspaceIgnoreService | GUI panel smoke plus full corpus diagnostic extraction | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | diagnostics=704 default extensions=4 | no | Report diagnostics by severity and config source. |
| `formatter_fold_shelf` Formatter, fold, and fold shelf | format action<br>editor fold<br>fd r<br>fd s<br>fold shelf dock | FormatterService<br>EditorFolding<br>FoldBlockShelfModel<br>FoldShelfPersistenceService<br>FoldShelfRestoreService | temporary editor buffers and temporary shelf settings | fixture only; corpus read-only | `pass` | formatter/fold/fold shelf paths are covered through GUI and temp-buffer tests | no | Add direct formatter idempotence sweep on copied corpus files. |
| `package_macro_include` Package tools, macro/define semantics, include/header/package shortcuts | Package Tools bar<br>;h<br>;pk<br>;d<br>Go package gpk | PackageToolService<br>SvMacroSemantics<br>CompletionSemanticQuery<br>InlineCommandMode | descriptor inventory, GUI activation, semantic corpus baseline | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | package tool buttons=6; ;h and ;pk descriptors present in inline command inventory | no | Add include resolution success/failure buckets. |
| `rtl_fsm` RTL Insights: FSM / State Transition Graph | RTL Insights FSM Graph<br>state transition graph panel/action | FsmGraphService<br>StateTransitionGraphService<br>StateTransitionTriggerService<br>RtlInsightsPanelCoordinator | deterministic bounded structural FSM sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=68 fail=0 skipped=398 timeout=0 empty-valid=0 | no | Skipped modules lack current<=next FSM shape or are beyond the deterministic audit sample cap. |
| `rtl_signal_journey_clock_reset` RTL Insights: Signal Journey and Clock/Reset | Signal Journey<br>Clock/Reset Domain Map | SignalJourneyService<br>ClockResetDomainService<br>RelationshipService | panel smoke plus full corpus relationship context | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | relationships=82636 diagnostics=704 | no | Add corpus-level journey path length distribution. |
| `rtl_semantic_diff` RTL Insights: Semantic Diff | RTL Insights Semantic Diff<br>compare workflow | SemanticDiffService<br>RtlInsightsPanelCoordinator | temporary fixture compare | fixture only; corpus read-only | `pass` | GUI smoke covers semantic diff panel/action on temporary fixtures | no | Add copied-corpus pair diff sweep if needed. |
| `rtl_signal_kernel_graph` RTL Insights: Signal Kernel Graph | Signal Kernel Graph panel<br>signal graph action | SignalKernelGraphService<br>SignalKernelGraphPanelCoordinator | deterministic bounded corpus graph sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=32 fail=0 skipped=1960 timeout=0 empty-valid=0 | no | Keep case-level skipped reasons explicit; expand graph sampling only in a dedicated performance pass. |
| `rtl_module_block_diagram` RTL Insights: Module Block Diagram | Module Block Diagram<br>RTL Insights diagram action | ModuleBlockDiagramService<br>RtlInsightsPanelCoordinator | full corpus module diagram sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=208 fail=0 skipped=0 timeout=0 empty-valid=216 | no | Empty-valid modules should remain explicit in reports. |
| `rtl_wave_preview` RTL Insights: Wave Preview | Wave Preview dock<br>active-editor wave refresh | WavePreviewService<br>WavePreviewPanelCoordinator | deterministic bounded always/process preview sweep | recursive test_sv/new + test_sv/huge_prj (454 files) | `pass` | corpus_audit: pass=1047 fail=0 skipped=2957 timeout=0 empty-valid=4 | no | Keep no-lane/no-warning regression coverage and review bounded skipped reasons periodically. |
| `search_rename_workspace_workflow` Search, rename, semantic diff, and workspace workflow | search<br>safe rename<br>workspace open/close/recent<br>semantic diff | SearchService<br>SafeRenameService<br>WorkspaceManager<br>SemanticDiffService | temporary workspace plus service-level smoke | fixture only; corpus read-only | `pass` | service and GUI smoke cover search/rename/workspace open-close paths on fixtures | no | Add rename collision/cross-file fixture matrix. |

## Known Issues

- None recorded by this audit.

## Notes

- Real corpus files were read only. Editing features are represented by service or GUI tests that use temporary buffers/files.
- `known-issue` marks a real feature surface with bounded failures or incomplete coverage that should not block the inventory.
- Deep RTL corpus behavior is delegated to `corpus_audit_test`; this report links that sweep into the broader feature matrix.
