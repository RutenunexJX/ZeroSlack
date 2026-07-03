# ZeroSlack Handoff

ZeroSlack is a SystemVerilog code editor and workspace browser. It should feel
fast on real RTL workspaces while keeping semantic behavior behind stable
service boundaries.

## Hard Rules

- Use Qt 6, CMake, and Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old
  Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship
  analysis, or long-lived scattered perflog probes.
- Do not revive `;:cmd` or use `;:` as an active command namespace.
- UI must not run Slang directly, scan workspace files directly, or own semantic
  policy.
- New semantic or RTL behavior must flow through:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

- Do not discard user changes. Do not touch dirty RTL fixtures unless the user
  asks for that exact change.
- Do not push unless explicitly asked. The active long-term goal mode is an
  explicit instruction that every completed milestone must be committed and
  pushed.
- Keep docs short, current, and useful for the next development turn.

## Current Product Baseline

- Current audit milestone: Signal Usage Hotspot v2 integration is complete.
  The service/report layer is wired into RTL Insights as a dual Track/Matrix
  panel, with editor source-symbol context entry and source reveal/flash from
  usage items.
- Corpus audit is retired as an acceptance signal. The current strategy is
  targeted regression fixtures plus GUI smoke and feature-specific tests:
  `completion_test`, `relationship_test`, `gui_smoke_test`, `jump_test`, and
  the lightweight `full_feature_audit_test` inventory.
- Current handoff state: no implementation milestone is active. The latest
  completed work is Signal Usage Hotspot v2 integration on top of the service
  and Insight UI v2 branches. The current baseline also includes import-aware
  SystemVerilog package symbol visibility, user template JSON usage, explicit
  header/include and package import commands, semantic `;m` module
  instantiation Slot Mode, Macro / Define semantics, Package Tools phase 1,
  References / Relationships workflow closure, and Fold Shelf
  persistence/restore/management.
- Insight UI v2 visual foundation is available through `InsightVisualStyle`.
  Signal Kernel Graph and Signal Usage Hotspot both use it. Hotspot opens from
  the editor source-symbol context action `Signal Usage Hotspot` or the RTL
  Insights `Usage Hotspot` action. Track mode groups usage blocks by module/file
  lane with role colors; Matrix mode summarizes module/file by role with heat
  cells and item drill-down. Search, role filters, selection inspector, empty
  and error states, and source reveal/flash are wired. Future polish can add
  deeper virtualization/asynchronous progress for very large reports.
- The app is a lightweight SystemVerilog editor/workspace browser with tabs,
  workspace file navigation, semantic indexing, diagnostics, completion,
  jump-to-definition, references/relationships, and focused RTL visual helpers.
- Tree-sitter owns low-latency editor syntax and live editor structure. Slang
  owns semantic facts. `SemanticIndexSnapshot` is the UI-facing query truth.
- COM Mode is an editor-local command layer, not a Vim clone. It has INSERT and
  COM states, toggles with `Ctrl+Shift+Alt+backtick` whenever an editor tab is open,
  exits with plain backtick while active, and shows an app-level command strip.
  Esc is cancellation-only: it clears/cancels the highest-priority temporary
  editor state or COM buffer, but no longer enters COM Mode. Current g-domain
  commands are `gm`,
  `g<num><Enter>`, `gp`, `gpk`, `gpa`, `gpo`, `gsi`, `gsd`, `gii`, `gac`,
  `gpi`, `ge`, and `gef`; current c-domain commands are `cn` for the Column
  Number Tool and `cr` for clearing assignment RHS fill points; the current
  select-domain command is `si` for selecting complete lines inside the nearest
  `begin ... end`. Existing fixed commands, prefixes, and the
  module-relative line command are described by shared `commodecommandregistry`
  metadata. The command strip renders registry-backed hints for prefixes and
  module-relative line buffers, with a normal dark palette and a distinct dark
  alert palette for command failures. Registry validation owns duplicate,
  executable prefix conflict, and malformed prefix reasons, and COM command
  failures use centralized registry-backed messages where practical. Active COM
  editors also paint a small `COM` badge and highlighted bottom edge.
- Ctrl+Space opens Global Control as a domain-first surface. Current root
  domains are `ow` and `fd`; displayed commands are `ow <num>`, `ow r`,
  `fd r`, and `fd s`.
- Workspace Configuration is available from the app-level Workspace menu. It is
  scoped per workspace and persists include dirs, defines, ignored dirs, file
  extensions, and optional top module / active top. Applying configuration
  updates `ProjectModel`, refreshes workspace file filtering, and queues
  analysis through the existing project-change path.
- Macro / define semantics are now first-class editor workflow data. Static
  `define` records appear in outline and semantic lookup, `` `MACRO`` uses can
  jump to current-file or workspace/include-visible definitions, hover shows
  macro signature/body text, Find References returns the `define` row and all
  indexed uses, undefined macro diagnostics are supplemented under the
  Semantic index owner, and conservatively evaluated inactive `ifdef` /
  `ifndef` branches are grayed using configured defines plus static
  current-file `define` / `undef` state. This is not a full SystemVerilog
  preprocessor and does not add Vivado project parsing or new define UI.
- Header include insertion is explicit through `;h`: `;h <query>` inserts a
  SystemVerilog `` `include "..."`` from existing header candidates, and
  `;h -n <name>` creates a `.svh` header by default before inserting the
  include. The old hidden `` `include `` + space trigger is no longer active,
  and `;;h` is intentionally unassigned.
- Package imports are explicit through `;pk`: `;pk <query>` searches indexed
  package symbols and inserts `import pkg_name::*;`. `;p` remains parameter
  semantic completion, COM `gpk` remains package navigation, and `;;pk` is
  intentionally unassigned.
- Package members are import-aware for ordinary unqualified lookup. Package
  parameters, localparams, typedefs, enums, and structs marked
  `PackageVisible` participate in completion, goto, and hover only when the
  current file/scope has an active `import pkg::*;`. Local/module definitions
  win over imported package members, and conflicting same-name members from
  multiple imported packages do not produce a random jump. Explicit
  `pkg::symbol` completion remains outside the current baseline.
- User `;;cmd` templates can now be maintained in JSON files. ZeroSlack reads
  a global `user_templates.json` plus an optional workspace
  `.zeroslack/user_templates.json`; workspace templates override global
  templates with the same command token, while built-in templates and the
  reserved `;;h` / `;;pk` slots cannot be overridden. Valid user templates
  enter the existing `;;cmd` completion and Slot Mode insertion path. The
  Tools / User Templates menu opens or creates the global/workspace JSON files
  with an empty `{"templates":[]}` skeleton and reloads templates with a
  status/report summary for invalid JSON, invalid commands, invalid slots, and
  conflicts.
- Problems/Diagnostics now expose owner and status data in the existing
  Problems panel: diagnostic rows show owner (`Slang`, `Semantic index`),
  current-file error/warning/info summary follows the active tab, and status
  reports current/stale/analyzing/background. F8 / Shift+F8 navigate
  next/previous diagnostics using the current Problems scope and severity
  filters, then reveal and flash the target editor line.
- `;cmd` remains semantic command completion. `;;cmd` remains template
  expansion. COM Mode is for editor-local command actions. Slot Mode is the
  post-template editor-local fill flow; it is active for `;;p` / `;;lp`
  parameter templates and `;;l` / `;;w` / `;;r` signal declaration templates,
  for `;m` semantic module instantiation completions, and for user templates
  that provide slot metadata. `;;m` remains the module definition skeleton path.
- Package Tools phase 1 is available when the active editor cursor is inside a
  parseable SystemVerilog package. The lightweight editor bar inserts
  package-scoped `parameter`, `localparam`, `typedef enum`,
  `typedef struct`, `typedef struct packed`, and `function` templates at
  syntax-derived positions inside the current package, then starts Slot Mode
  on all editable slots. It does not change `;;cmd` template behavior.
- Verification Baseline Repair after Package Tools phase 1 is baseline-only,
  not Package Tools phase 2. Release `relationship_test` rebuilds now work
  from a plain `cmake --build` because generated Windows compile/link rules
  inject the detected MinGW compiler bin into `PATH`. Package Tools regression
  coverage now checks all six first-phase templates/buttons while preserving
  the same feature boundaries.
- Batch RTL editing has a first editor-local action:
  `MyCodeEditor::clearSelectedAssignmentRhs` clears RHS expressions in the
  complete line range touched by the current selection, or the current
  assignment when there is no selection, for supported assignment statements,
  then starts Slot Mode on the cleared RHS fill points. COM Mode command `cr`
  invokes this editor-local action. The obsolete named-action entry layer has
  been removed.
- Column Selection is visual-column based. Internal column state converts
  through the editor tab width so selections, copy/cut/paste, delete/backspace,
  text input, mouse adjustment, and Tab / Shift+Tab stay visually rectangular
  on lines that contain `\t`. In column mode, Tab inserts spaces to the next
  tab stop and Shift+Tab performs visual outdent instead of leaking to normal
  editor indentation.
- COM Mode command `cn` opens the Column Number Tool when a column selection is
  active. The tool previews and applies Dec/Hex/Bin numbering with Plain,
  C-like, SystemVerilog unsized, and SystemVerilog sized styles, plus step,
  repeat, direction, digit width, padding, hex case, and replace/insert modes.
  Number formatting and first-row inference live in `columnnumbertool`, while
  the COM popup only consumes that model and applies one undoable column edit.
- Fold Region and Fold Shelf are available through Global Control. Fold Shelf
  now has a service/model-owned persistence baseline and explicit cross-file
  restore flow plus basic rename, search, and stale/consumed cleanup
  management.
- Signal Kernel Graph exists as a signal-centric exploration graph.
  Service/report-layer high-fanout grouping metadata is available for dense
  input/output sides, and the panel can render those groups as collapsible
  summaries with report-data filters and graph search highlighting/focus.
- Wave Preview exists as a code-understanding sketch, not a simulator. The
  current entry points are scoped to the selected/current `always` block or,
  when not in an `always` block, the selected/current module. The panel renders
  scope and legend/readability cues from the report data so users can interpret
  the sketch without implying simulation accuracy.
- RTL Insights `FSM Graph` renders `FsmGraphService` reports as an interactive
  graph, not a tree table. FSM candidates must have states and parsed
  case-derived transitions, so ordinary enum/register declarations such as
  non-case bookkeeping registers are not promoted to FSM graphs.
- State Transition Graph entry gating exists for editor source-symbol actions:
  `StateTransitionTriggerService` now asks structural FSM discovery for the
  selected symbol's role. A discovered next-state role can open the graph, a
  discovered current-state role is rejected with a next-state prompt, and names
  such as `ns`, `next_state`, `cs`, or `current_state` are examples/display
  hints rather than gates. `StateTransitionGraphService` shapes accepted
  requests into selected next-state graph reports by filtering existing
  `FsmGraphService` data, and RTL Insights renders those reports as an
  interactive graph. State/register/signal nodes and transition edges are
  selectable, and double-clicking a graph element follows its carried source
  link.
- Module Block Diagram renders in RTL Insights from the service-owned
  `ModuleBlockDiagramReport`. The current UI entry points are the RTL Insights
  `Module Block Diagram` action for the active module and the editor source
  action for selected module names. Rendering is module/interface-only and does
  not show signals. The graph uses a grid canvas with the selected module as a
  large container and child module/instance nodes arranged inside it. Child
  nodes show both the module type and instance name, unresolved module types
  remain visible as blackbox nodes with an explicit reason, and containment
  edges carry instance metadata. Module and instance graph elements are
  selectable; mouse wheel and `-` / `Fit` / `+` controls zoom the canvas.
  Double-clicking a root module follows the module definition link; child
  modules jump to the instance declaration and then refresh the diagram around
  the jumped module so its children remain visible. Unresolved blackbox nodes
  jump to the instance declaration without drilling into a missing definition.
- Formatter support exists as conservative editor formatting. Current daily
  editor action inventory: `Ctrl+F` opens Find; formatter document/selection
  actions live in the editor context menu; line comment actions are available
  through `Ctrl+/`, `Ctrl+Shift+/`, and editor context-menu actions; line
  indent actions are available through `Ctrl+]`, `Ctrl+[`, and editor
  context-menu actions; replace is available through `Ctrl+H` and editor
  context-menu action; goto line is available through `Ctrl+G` and editor
  context-menu action. The first-pass daily editor operation entry points are
  complete.

## Huge Workspace Status

Huge Workspace work is in status-audit mode only. Do not add new Huge Workspace
UX features as part of the current long-term plan.

HWA.1 owner inventory:

- `WorkspaceAnalysisPlanService` owns current/dirty-open/clean-open/background
  ordering, protected dirty-open files, band summaries, band metadata, and
  priority publication checkpoints.
- `WorkspaceAnalysisRequestQueue` owns active/pending workspace requests,
  latest-request coalescing, cancellation telemetry, and stale pending request
  replacement.
- `WorkspaceSymbolAnalysisController` owns plan application to
  `SymbolAnalyzer`, cached-project reuse, workspace expiration, cancellation,
  and the handoff from completed symbol analysis to relationship analysis.
- `SymbolAnalyzer` owns async workspace symbol/diagnostic extraction, generation
  expiration, protected-file preservation, chunked/staged publication, and
  publication telemetry.
- `SemanticIndex` / `SemanticIndexSnapshot` own analysis-band metadata storage
  and priority-aware query ordering exposed to completion, definition, and
  semantic search paths.
- `RelationshipAnalysisController`, `RelationshipAnalysisWorker`, and
  `RelationshipResultPublisher` own workspace relationship cancellation and
  stale/cancelled result rejection.
- `AnalysisProgressCoordinator` and `ActivityLogService` own visible Activity
  telemetry for planning, progress, queued/restarted requests, cancellation,
  symbol timing, diagnostics, and relationship timing.

Existing verification and harness anchors:

- `completion_test` covers workspace plan priority/protected files,
  publication checkpoints, analysis-band propagation and query preference,
  request queue coalescing, cancellation telemetry, staged publication, and
  final background publication.
- `relationship_test` covers stale snapshot rejection, stale async file
  analysis, relationship request coalescing, workspace relationship
  cancellation, cancelled worker telemetry, and publisher rejection of
  cancelled workspace results.
- `large_file_perf_test` guards large-file/workspace responsiveness and verifies
  whitespace edits do not queue semantic or relationship reanalysis.
- `relationship_perf_test` is the focused `huge_prj` performance harness for
  symbol publication, relationship extraction/compute/publish timing, and
  design hierarchy metrics.

Known reference points:

- `test_sv/huge_prj`: 428 HDL files, about 18.59 MiB.
- Release relationship analysis reference: about 3.891s in the focused harness.
- Release async symbol publication reference: about 5.096s in the focused
  harness after the first-publication native-store scan fix.
- HWA.2 safe verification: Release build of `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`
  passed. No test executable was launched in this milestone; `ctest` was not
  run, so the external Windows error-dialog check was not exercised.
- HWA.3 audit conclusion: the current codebase has identifiable owners and
  coverage anchors for the low-level huge-workspace strategies, but this audit
  did not refresh runtime `huge_prj` performance numbers, did not run full
  `ctest`, and did not validate GUI-runtime behavior in this environment.
  Those remain audit gaps, not permission to add new Huge Workspace UX.

## Workspace Workflow Status

The workspace workflow scope now has two explicit lanes: the earlier limited
workspace navigation lane (`ow r`, ignored directories) and the workspace
engineering configuration / diagnostics lane.

- `WorkspaceManager` owns open, close, switch, alias rename, file scanning,
  cached scanned-file restoration, and the recent-workspace list.
- Multiple workspaces can be open. Each `WorkspaceEntry` carries alias, path,
  scanned files, and `scanComplete`; switching back to a scanned entry restores
  cached files without starting a new directory scan.
- Recent workspaces are already persisted through `QSettings` under the
  `ZeroSlack` / `ZeroSlack` application settings. Entries store alias and path,
  are normalized, deduplicated, capped at 20, and exposed through
  `WorkspaceManager::recentWorkspaceEntries()`.
- Workspace alias rename updates matching recent-workspace metadata.
- Global Control root shows only the `ow` and `fd` domains. The `ow` domain
  displays `ow 1`, `ow 2`, `ow r`, and accepts numeric `ow <num>` queries.
- `ow r` is a displayed Global Control child command. It routes through the
  existing `MainWindow` recent-workspaces dialog and uses
  `WorkspaceManager::recentWorkspaceEntries()`.
- `ProjectModel` already has `ignoredPaths` and filters raw scanned files
  through `setIgnoredPaths()`.
- `WorkspaceIgnoreService` normalizes and validates ignored-directory requests
  for the active workspace. It rejects the workspace root, paths outside the
  workspace, and existing non-directory paths while allowing not-yet-created
  directories under the workspace.
- `WorkspaceManager::setIgnoredDirectories()` applies ignored directories to
  the active `ProjectModel`, updates current file lists from the model, and
  stores ignored-directory state per open `WorkspaceEntry`. Switching back to a
  cached workspace restores raw scanned files first, then reapplies its ignored
  directories so clearing ignores can reveal previously hidden files.
- `WorkspaceConfigurationService` persists per-workspace engineering settings
  in `QSettings`: include dirs, defines, ignored dirs, file extensions, and
  optional top module / active top.
- `WorkspaceConfigurationDialog` is the first workspace-focused configuration
  UI. It edits include dirs, defines as key/value pairs, ignored dirs, file
  extensions, and top module without running Slang or scanning files from UI.
- `ProjectModel` now carries file extensions alongside include dirs, defines,
  ignored paths, and top module. Workspace analysis keys include include dirs,
  defines, file extensions, and top module so configuration changes invalidate
  the previous full-workspace analysis key.

## Current Architecture

- `ProjectModel` owns workspace root, file list, include dirs, defines, file
  extensions, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions,
  dirty/saved state, cursor, live module names, and registry-backed text
  queries.
- `AnalysisScheduler` owns timing, debounce, cancellation, refresh requests,
  relationship work routing, and lifecycle. It does not own feature policy.
- `SemanticIndex` / `SemanticIndexSnapshot` own semantic facts, cached content,
  symbols, diagnostics, relationships, and UI-facing query data.
- Query services and feature services own semantic reads, feature policy, report
  shaping, failure reasons, and navigation payloads.
- UI panels and editor widgets render models, route commands, and apply returned
  edit operations. They do not scan workspaces or run Slang.
- `ComModeCoordinator` owns COM state, command strip, and picker presentation.
  `ComModeService` owns COM picker query/report shaping.
- `GlobalControlService` owns Global Control command/domain shaping.
- Slot Mode baseline: `CodeTemplateService` produces template
  text plus relative editable slot metadata; `EditorCompletionWorkflow` applies
  the insertion and starts an editor-local slot session; `MyCodeEditor` state
  owns active slot ranges, highlighting, navigation, and invalidation.
  Slot state must not live in UI panels or semantic services. COM Mode toggle
  does not clear an active Slot Mode session; explicit Esc and stale-session
  events own slot exit.
- Package Tools baseline: `PackageToolService` owns the package-only template
  text and relative slot metadata. `TSDocument` owns current-package detection
  and syntax-derived insertion targets, including same-kind append anchors and
  the default `endpackage` insertion point. `MyCodeEditor` applies the returned
  text in one undoable edit and starts Slot Mode. `MainWindow` only renders the
  lightweight button bar and dispatches selected tools. Baseline coverage
  verifies non-empty text and slot metadata for all six first-phase templates,
  exact `typedef struct packed` template text, six stable GUI button object
  names with exactly six package-tool buttons, and one GUI insertion path into
  Slot Mode.
- Batch RTL edit baseline: `RtlBatchEditService` owns selected-text RTL batch
  edit planning and returns reports with replacement text, failure reasons, and
  template-slot metadata. It does not mutate editor text, scan workspaces, or
  run Slang.
- `WorkspaceAnalysisPlanService`, `WorkspaceAnalysisRequestQueue`,
  `WorkspaceSymbolAnalysisController`, `RelationshipAnalysisController`, and
  `AnalysisProgressCoordinator` own Huge Workspace planning, request lifecycle,
  staged publication, cancellation, and visibility.
- `WorkspaceIgnoreService` owns ignored-directory request validation and
  normalization. `WorkspaceManager` is the workspace-level model entry point;
  UI should call it rather than mutating `ProjectModel::ignoredPaths` directly.
- `WorkspaceConfigurationService` owns persistent per-workspace engineering
  configuration. `WorkspaceConfigurationDialog` is only a form consumer;
  applying settings goes through `WorkspaceManager::setWorkspaceConfiguration`.
- `DiagnosticNavigationService` owns next/previous diagnostic selection using
  existing `DiagnosticService` filters. Problems panel and main-window actions
  consume its result and navigate through `NavigationCommandCoordinator`.
- `SvMacroSemantics` owns the conservative macro scanner used by symbol
  extraction, hover/reference helpers, undefined-macro diagnostics, and
  inactive-branch decorations. UI code consumes records/reports/decorations
  only; it does not run its own preprocessor.
- Fold Shelf baseline: `FoldBlockShelfModel` owns the shelf item list and
  mutation lifecycle including rename, query/filter, and stale/consumed
  cleanup, `FoldShelfPersistenceService` owns versioned QSettings-backed
  load/save scoped by workspace root,
  `FoldShelfRestoreService` owns explicit restore reports for active-editor
  relocation, `FoldBlockShelfPanel` renders the list and handles
  preview/delete/drag/restore UI requests, `EditorFoldingController` creates
  and reinserts custom fold block text, and `MainWindow` wires the dock/model
  plus restore command flow. Durable shelf persistence and restore policy live
  in the model/service layer, not in the panel.
- Signal Kernel Graph baseline: `SignalKernelGraphService` builds graph
  reports from `SignalJourneyService` and `SemanticIndexSnapshot` data. The
  report preserves raw nodes/edges while adding high-fanout grouping metadata
  for dense input/output sides. `SignalKernelGraphPanelCoordinator` owns only
  panel-local collapse/expand, filter, and graph-search UI state while
  rendering group summary/header items from that metadata; grouping policy
  remains in the service/report layer.
- Wave Preview baseline: `TSDocument` owns current/selected `always` and
  module scope detection, `MyCodeEditor` exposes editor-local scope targets,
  `MainWindow` passes those targets into `WavePreviewPanelCoordinator` with
  `always` scope taking priority over module scope, and `WavePreviewService`
  shapes the scoped report. `WavePreviewPanelCoordinator` renders scope,
  legend, and readability overview rows from that report. UI code remains a
  consumer and does not scan workspace files or run Slang.
- State Transition Graph baseline: `StateTransitionTriggerService` owns the
  first entry-gating policy for selected source symbols. `EditorSourceNavigationQuery`
  asks that service before enabling or dispatching `Show State Transition Graph`.
  `StateTransitionGraphService` then owns accepted-request report shaping and
  exact next-state graph filtering on top of `FsmGraphService`. `EditorCoordinator`
  and `SemanticPanelRefreshCoordinator` only route accepted requests; RTL
  Insights consumes the service report, renders an interactive graph scene,
  and keeps navigation wired through source links carried by graph elements.
- FSM Graph baseline: `FsmGraphService` owns candidate selection and transition
  extraction. A report graph requires both state values and parsed case-derived
  transition evidence before the UI renders it. `RtlInsightsPanelCoordinator`
  consumes that report as selectable graph nodes/edges and does not scan
  workspace files or run Slang.
- Module Block Diagram baseline: `ModuleBlockDiagramService` owns selected
  module/interface containment report shaping from indexed symbols plus
  `INSTANTIATES` relationships. The report carries root module definitions,
  child module type names, instance names, module-definition links, instance
  declaration links, instantiation edges, and unresolved/blackbox reasons.
  Signal and non-instance relationships stay filtered in the service/report
  layer. `RtlInsightsPanelCoordinator` renders that report as a module-only
  interactive graph with a root-module container, child module/instance blocks,
  visible zoom controls, graph-element drill-down for resolved modules, and
  instance-declaration navigation for child nodes and blackboxes. Source symbol
  requests still route through `SemanticPanelRefreshCoordinator`; UI code does
  not scan workspaces or run Slang. Future Module Block Diagram regressions
  should be captured as compact fixtures rather than broad corpus sweeps.

## Command Responsibility Map

Current command surfaces are intentionally separate:

- `;cmd` is inline semantic command completion. `InlineCommandMode` owns the
  built-in prefix descriptors and parsing guardrails, including safe-prefix and
  comment/string rejection. `CompletionCommandMode` and `CompletionService`
  shape command state, help, symbol presentation, activation state, and
  `CompletionSemanticQuery` requests against `SemanticIndexSnapshot` data.
- `;;cmd` is inline code template expansion. Its built-in command prefixes are
  derived from the inline descriptor set, while built-in template text and
  relative slot metadata come from `CodeTemplateService`.
  `EditorCompletionWorkflow` applies the selected template and starts Slot Mode
  through `MyCodeEditor` when slot metadata exists.
- Package Tools is a package-scoped button surface, not a `;;cmd` alias. It
  reuses the Slot Mode editor machinery after insertion, but package templates,
  current-package validation, and insertion targets stay in the package tool
  and Tree-sitter layers. The first phase intentionally excludes package/import
  semantic interpretation, cross-file package management, automatic
  package-wide sorting, new graphs, and insight panels; Track 15 owns
  macro/define semantics.
- User template storage/query is service-owned by `UserTemplateService`. It
  reads validated compact `;;` template records from global and workspace JSON
  files, exposes `CodeTemplateItem`-compatible catalog and exact-token query
  results, and is consumed by `CompletionService` for inline `;;cmd` template
  completion. Workspace JSON has priority over global JSON for user-defined
  command tokens; built-in `;;cmd` templates and reserved `;;h` / `;;pk` slots
  are reported and ignored instead of overridden. Activation preserves template
  slot metadata and starts Slot Mode through the existing
  `EditorCompletionWorkflow` path when a user template carries slots.
- Custom abbreviation resolution is service-owned by
  `CustomAbbreviationService`. It persists validated compact abbreviations in
  `QSettings`, resolves them to existing `;cmd` semantic command tokens or
  `;;cmd` template command tokens, exposes prefix and intent-scoped exact
  queries, and does not own UI rendering or semantic lookup policy.
- The obsolete named-action layer has been removed. Editor actions stay
  available through normal shortcuts, context menus, COM Mode where explicitly
  registered, or direct editor APIs; no legacy alternate-action service or
  dispatcher remains.
- COM Mode is editor-local and registry-backed. `commodecommandregistry` owns
  command metadata and conflict validation; `ComModeService` owns picker/query
  reports; `ComModeCoordinator` owns mode state, strip text, picker UI, and
  navigation dispatch. Column-number formatting and inference are owned by
  `columnnumbertool`; the `cn` popup is only a COM/editor-local UI consumer.
- Global Control is app/workspace/global command discovery. `GlobalControlService`
  owns root domains and domain child command shaping; `MainWindow` dispatches
  selected items through existing coordinators and dialogs.

Conflict boundaries:

- Do not revive `;:cmd`. `InlineCommandMode::actionDescriptors()` is currently
  empty, and `;:` remains reserved/inactive.
- `;cmd` must stay semantic completion; it must not open workspace UI, run
  Slang from UI, or perform editor-local structural edits.
- `;;cmd` must stay template insertion; template storage/query work belongs in
  a service layer, not in UI widgets.
- User template records must use compact `;;` command tokens. `;:` remains
  reserved/inactive, and user template storage must not alter `;cmd`, COM Mode,
  or Global Control namespaces.
- Custom abbreviations may only resolve to existing compact `;cmd` or `;;cmd`
  tokens. They must not revive `;:cmd`, define COM Mode commands, or define
  Global Control commands.
- COM Mode must not become a Vim clone or share syntax with `;cmd` / `;;cmd`.
- Global Control must stay Ctrl+Space app/workspace control and must not become
  editor-local COM Mode.

## Slot Mode Contract

Slot Mode is the `;;cmd` follow-up state for filling editable points in an
inserted template. It currently applies to `;;p` / `;;lp` parameter declaration
templates, `;;l` / `;;w` / `;;r` signal declaration templates, and user
templates that provide `CodeTemplateSlotList` metadata. Other template
families keep current insertion behavior until an implementation milestone
explicitly enables slots for that family.

- Slot data is an ordered set of relative ranges inside inserted template text.
  A range may be empty, preselected text, or a named placeholder. Existing
  `selectionStart` / `selectionLength` fields map to the first primary slot.
- Entry happens only after a template activation inserts text through the
  existing completion workflow. The insertion remains one undoable edit block.
- Tab moves to the next slot; Shift+Tab moves to the previous slot. Both wrap
  around, so Tab on the last slot returns to the first slot and Shift+Tab on
  the first slot returns to the last slot. Tab navigation never exits Slot Mode.
- Esc cancels Slot Mode only: inserted text and user edits stay in the document,
  slot highlights clear, and normal editor input resumes.
- Cursor movement outside the active slot/session, document edits that make slot
  ranges stale, tab switch/close, or Global Control entry exits Slot Mode
  without rollback. COM Mode toggle does not clear Slot Mode by itself.
- While active, Slot Mode paints all slots with a weak blinking highlight,
  emphasizes the active slot, and reports status as `SLOT n/m`.
- Slot Mode is editor-local text state. It must not run Slang, scan the
  workspace, or add semantic policy.
- Parameter template slots are ordered as name then value. Editing the name
  shifts the value slot; Tab reaches the value and wraps from the value slot
  back to the name slot.
- Signal declaration templates use a single name slot. Editing the signal name
  keeps packed/unpacked dimensions intact; Tab keeps cycling that slot until
  Esc exits Slot Mode.

## Fold Shelf Persistence Contract

Fold Shelf stores custom fold blocks that users move or copy out of editors.
The runtime path is model/service-owned: editor shelf mode creates a
`FoldShelfItem`, the panel drops it into `FoldBlockShelfModel`, the model
persists mutations through `FoldShelfPersistenceService`, and restore uses
`MainWindow` plus the current editor insertion path.

G7 persistence uses a versioned service/model schema:

- Storage root: `foldShelf/v1/items`, scoped by normalized workspace root when
  a workspace exists.
- Required item fields: `id`, `alias`, `text`, `sourceFile`, `sourceModule`,
  `sourceStartLine`, `sourceEndLine`, `lineCount`, `originKind`, `consumed`,
  and `stale`.
- `originKind` stays the existing `moved` / `copied` value set.
- `text` is the full custom fold block text including fold markers.
- `sourceFile` is stored normalized; relative display paths are derived by UI
  code when needed.
- The existing MIME JSON helpers remain drag/drop transport. They are allowed
  to share fields with the durable schema, but durable persistence owns
  validation, migration, and write timing separately.

Ownership rules:

- `FoldShelfPersistenceService` owns load/save normalization and the versioned
  storage layout; `FoldBlockShelfModel` owns when mutations are persisted.
- `FoldBlockShelfPanel` must remain a model consumer. It must not read/write
  `QSettings`, scan workspaces, or decide persistence policy.
- `MainWindow` may wire the service/model and route restore actions, but should
  not own serialization details.
- `EditorFoldingController` keeps owning text extraction/insertion and shelf
  mode behavior.

G7.2 same-file restore reloads persisted shelf items for the current workspace
and restores them to their recorded source file through the existing editor
insertion path. Items are removed or consumed only after successful insertion;
missing or invalid restore targets are marked stale.

G7.3 cross-file restore adds `FoldShelfRestoreService` as the explicit
active-editor restore path. The panel emits a restore request for the selected
item; `MainWindow` supplies the active editor and cursor line; the service
inserts through the existing editor fold-shelf insertion API, then consumes or
removes the persisted item only after successful insertion. Missing active
editors or failed insertions mark the item stale with a failure reason.

G7.4 management actions keep shelf policy in `FoldBlockShelfModel`: renaming
trims and persists aliases, search/filtering reads only shelf item fields, and
cleanup removes stale or consumed items as an explicit model mutation. The panel
provides the search box and management buttons but does not own persistence or
workspace scanning.

## Batch RTL Edit Contract

The first batch RTL edit target is clearing RHS expressions from selected
assignment statements and producing fill slots for the future Slot Mode flow.

- `RtlBatchEditService::planClearAssignmentRhs` accepts selected text and
  returns a `RtlClearAssignmentRhsReport`.
- A ready report contains replacement text, per-assignment edit metadata, and
  `rhs1`, `rhs2`, ... zero-length template slots placed before each semicolon.
- Supported first-pass forms are complete selected blocking assignments,
  nonblocking assignments, and continuous `assign` statements. Whitespace and
  comments around selected statements are preserved.
- Unsupported cases fail without text mutation: empty selection, incomplete
  statement, declaration initializer, control-flow statement, macro statement,
  ambiguous top-level assignment, unmatched delimiter, unterminated string, or
  unterminated comment.
- Editor command wiring is available through
  `MyCodeEditor::clearSelectedAssignmentRhs`. The action applies the report in
  one undoable edit block and preserves failure reasons in editor status
  feedback.
- COM Mode command `cr` invokes the same editor-local clear-RHS path. It expands
  any text selection to the touched complete line range, clears assignments
  found on those lines, or clears the current assignment when there is no
  selection. It enters Slot Mode on the cleared RHS positions and reports `No
  assignment RHS found` in the command strip/status path without opening a
  modal dialog.
- COM Mode command `si` selects the complete interior lines of the nearest
  Tree-sitter `seq_block` (`begin ... end`) and stays in COM Mode so users can
  immediately run `cr`.
- Slot Mode entry is active for this report. The first RHS slot is selected
  after replacement; Tab and Shift+Tab wrap between slots, while Esc exits Slot
  Mode.

## Long-Term Goal Scope

Only these long-term goals are active:

1. Editor daily operations completion.
2. Slot mode after template insertion.
3. Batch RTL edit actions.
4. COM Mode framework completion.
5. Limited workspace workflow additions.
6. Completion / `;cmd` / `;;cmd` cleanup and user customization.
7. Fold / Fold Shelf persistence and management.
8. Signal Kernel Graph fanout/search/filter improvements.
9. Wave Preview as selected-block or selected-module waveform sketch only.
10. State transition graph for selected structural next-state roles only.
11. Module block diagram for selected module names only.
12. Workspace project configuration and diagnostics workflow.
13. References / Relationships workflow closure.
14. Package Tools.
15. Macro / Define semantic workflow.

Do not add unlisted long-term goals without explicit user approval.

## Verification Baseline

- Narrow documentation-only changes: inspect docs and run `git diff --check`.
- Narrow compile-safe implementation changes: build affected targets first.
- Shared semantic, scheduler, editor, project, snapshot, or relationship
  boundary changes: run full Ninja and full `ctest --output-on-failure` unless
  the user has blocked executable test runs.
- If `ctest` appears to hang or runs far longer than expected, check for an
  external Windows application-error or memory-read dialog before assuming the
  test process is still making progress.
- Avoid launching GUI smoke executables in this environment when they are known
  to produce external Windows error dialogs; compile/link targets instead unless
  the user explicitly asks to run them.
- Latest Module Block Diagram real-usability pass: Debug targets
  `completion_test`, `relationship_test`, `gui_smoke_test`,
  `full_feature_audit_test`, and `jump_test` compile/link; focused CTest runs
  passed. Regression coverage should stay in compact fixtures and GUI smoke,
  not in a broad corpus audit.
- Latest acceptance baseline repair: `completion_test` source-symbol context
  menu assertions now cover all five actions including `ShowModuleBlockDiagram`.
  Release `ctest -R "^completion_test$" --output-on-failure` and
  `ctest -R "^relationship_test$" --output-on-failure` both passed.
- Latest COM clear-RHS command repair: Release `completion_test` and
  `relationship_test` passed; Release `completion_test` and `gui_smoke_test`
  targets compile/link. `gui_smoke_test` was launched with Windows fault-dialog
  suppression and its new COM `cr` checks passed, but the monolithic smoke test
  still fails on existing non-`cr` baseline checks.
- Latest COM/Slot interaction repair: Release `completion_test` and
  `relationship_test` passed; Release `completion_test` and `gui_smoke_test`
  targets compile/link. `gui_smoke_test` was launched with Windows fault-dialog
  suppression and all new `si`, partial-selection `cr`, and Slot Mode cycling
  checks passed; the monolithic smoke test still fails on existing non-current
  baseline checks.
- Latest COM/Column Selection repair: Release `completion_test` passed through
  `ctest`; Release `gui_smoke_test` target compile/link passed. The launched
  smoke output shows all new COM toggle, visual-column column selection,
  column-mode Tab/Shift+Tab, `cn`, Column Number Tool, and number inference
  checks passing. The full monolithic GUI smoke baseline still fails on
  existing non-current checks: one VENDOR ctrl-click fixture check and the Wave
  Preview rendering group.
- Latest workspace configuration / diagnostics workflow: Release
  `completion_test` passed through `ctest`; Release `completion_test` and
  `gui_smoke_test` targets compile/link. A Release `relationship_test` run was
  attempted and failed in existing RTL Insights FSM graph/panel checks, not in
  the workspace configuration or diagnostics workflow covered by this
  milestone.
- Latest workspace cache repair: cached workspace activation now restores
  workspace configuration silently and no longer emits `filesScanned` /
  `workspaceListChanged`, so Navigation design hierarchy caches survive cached
  workspace switch/close flows. Explicit Workspace Configuration edits still
  notify and refresh. Release `completion_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$" --output-on-failure` passed.
  The required workspace/cache regression checks pass.
- Latest GUI smoke baseline repair: VENDOR ctrl-click now resolves the
  `huge_prj` fixture from either source or build execution roots; Wave Preview
  scope lookup accepts safe Tree-sitter module/always ranges even when a
  contained parse node has localized errors; RTL Insights synthetic fixture
  snapshots are reinstalled before each panel action so staged workspace
  publication cannot overwrite the test input; and the Problems preservation
  check now uses an unopened external diagnostic probe. Release verification
  passed: `ctest -R "^completion_test$" --output-on-failure` and
  `ctest -R "^gui_smoke_test$" --output-on-failure`; the GUI smoke test was
  repeated successfully.
- Latest References / Relationships workflow closure stage: source-symbol
  right-click actions now present a stable six-action menu: Go to Definition,
  Find References, Show Relationships, Signal Kernel Graph, State Transition
  Graph, and Module Block Diagram. Disabled actions remain visible with
  tooltip/status reasons. Module Block Diagram is gated to existing
  module/interface definition records, ordinary signals remain disabled, and
  State Transition Graph gating still rejects discovered current-state roles.
  This stage is workflow/UI closure only; it does not add semantic analysis, package
  tools, Wave Preview behavior, or new graph algorithms; Track 15 owns
  macro/define semantics.
- Latest References / Relationships panel closure stage: References and
  Relationships now keep visible query context, render explicit empty-state
  reasons, support path and `file:line` copy from result rows, and use one
  validated jump/flash path. Relationship rows carry existing-data graph
  entries for Signal Kernel Graph, Module Block Diagram, and State Transition
  Graph only. Instance relationships prefer existing module/interface
  definition links when available. Navigation history entries are keyed by
  workspace so Back/Forward does not cross workspace boundaries. Workspace
  mismatch feedback is limited to targets that belong to a different open
  workspace; ordinary external files from current panel results may still jump
  through the same validated path. This remains workflow/UI closure only, not a
  new semantic-analysis pass.
- Latest References / Relationships workflow verification: Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link. Release `ctest -R "^completion_test$" --output-on-failure`
  passed. The first Release `ctest -R "^gui_smoke_test$"
  --output-on-failure` run and the required rerun both failed on RTL Insights
  FSM graph navigation because workspace-mismatch validation rejected an
  external panel-result fixture; after narrowing mismatch detection to
  different open workspaces, the same GUI smoke CTest passed.
- Latest References / Relationships acceptance repair: RTL Insights FSM Graph
  and State Transition Graph panels now render the service report's
  state-register and next-state signal nodes alongside state nodes, keep the
  existing transition edges, add the existing-data signal-flow edge, and let
  next-state signal nodes trigger the panel navigation handler. This repairs
  the relationship-test panel closure without adding FSM extraction semantics
  or new graph algorithms. Release verification passed:
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.
- Latest Macro / Define semantic workflow stage: static `define` records now
  enter the semantic index as macros, outline shows object-like and
  function-like macro names, goto/hover/references work for `` `MACRO`` uses,
  undefined macro diagnostics are supplemented without replacing Slang
  diagnostics, and inactive preprocessor branches are grayed using configured
  defines plus static current-file `define` / `undef` handling. Boundaries:
  no Vivado `.xpr` / Tcl parsing, no define configuration UI, and no complete
  macro expansion engine. Verification passed: `cmake --build . --target
  completion_test relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest Macro / Define acceptance stabilization: the GUI smoke
  reference/relationship dock regression now uses an isolated local semantic
  snapshot injected into `ReferenceService`, `RelationshipService`, and
  `HierarchyService`, with dock filters reset at fixture entry and exit. This
  repairs CTest-order state leakage without weakening reference/relationship
  assertions or expanding macro scope. Release verification passed:
  `git diff --check -- . ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`;
  `cmake --build . --target completion_test relationship_test gui_smoke_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` twice consecutively.
- Latest `;m` module instantiation completion: semantic module command
  completion now expands selected modules into full named instantiation
  templates using indexed module parameter and port records when available.
  Parameter blocks are emitted only when parameters exist; all ports become
  named connections. Activation starts Slot Mode with instance name first,
  followed by parameter value slots and port connection slots in module
  definition order. If semantic parameter/port records are insufficient, `;m`
  falls back to the previous simple `module u_module (\n);` text. `;;m` still
  owns module definition skeletons, and this does not add Package Tools phase
  2, include/package/import workflows, new commands, or COM Mode behavior.
  Release verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.
- Latest header/include command convergence: the hidden `` `include `` +
  space completion entry has been removed. Use `;h <query>` to search the
  existing workspace/include header candidates and insert `` `include
  "path.svh"``. Use `;h -n <name>` to create a header, defaulting to `.svh`
  when no suffix is supplied and preferring the current file directory; existing
  files are not overwritten and report an explicit already-exists failure.
  `;;h` remains intentionally absent, and this does not add package/import
  completion, COM Mode commands, or Global Control entries. Release
  verification passed: `cmake --build . --target completion_test
  gui_smoke_test`; `cmake --build . --target relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest package import explicit entry: use `;pk <query>` to search existing
  SystemVerilog package records from the semantic index and insert
  `import pkg_name::*;`. This is import insertion only, not jump/navigation:
  COM `gpk` still owns package picker navigation, `;p` still owns parameter
  completion, Package Tools still only edit package files, and `;;pk` remains
  intentionally absent. No `pkg::symbol` completion, cross-file package
  management, COM Mode command, or Global Control entry was added. Release
  verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest package/import semantic repair: unqualified package member visibility
  is driven by active `import pkg::*;` context. Completion, definition, and
  hover hide package members before import, expose them after import, prefer
  local/module symbols over imports, and reject ambiguous same-name imported
  package members instead of jumping to an arbitrary definition. `;pk` remains
  an insertion command only, and Package Tools behavior is unchanged. Debug
  verification passed in the current build: `cmake --build . --target
  completion_test relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest user template JSON phase 1: users can maintain compact `;;cmd`
  templates in JSON files without adding a GUI editor. The service reads a
  global `user_templates.json` and an active-workspace
  `.zeroslack/user_templates.json`; each record uses `command`, `description`,
  `body` or `insertText`, and optional relative `slots`. Workspace templates
  override global user templates with the same token, but built-in templates
  and reserved `;;h` / `;;pk` holes are reported and ignored instead of being
  overridden. Valid templates use the existing `;;cmd` completion and Slot Mode
  insertion path; this adds no `;cmd`, COM Mode, Global Control, Package Tools,
  import/export, macro recorder, or variable system. Release verification
  passed: `cmake --build . --target completion_test gui_smoke_test
  relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest user template JSON usage entry: Tools / User Templates now exposes
  Open Global User Templates, Open Workspace User Templates, and Reload User
  Templates. Opening a missing file creates only the minimal legal skeleton
  `{"templates":[]}` before using the existing tab/open-file workflow.
  Workspace opening requires an active workspace and creates the `.zeroslack`
  directory as needed. Reload reports loaded and ignored counts in the status
  bar; invalid JSON, invalid commands, invalid slots, reserved tokens, and
  conflicts are listed in a lightweight warning report with file, command,
  field, and reason. This is still direct JSON editing only: no template GUI
  editor, import/export, variable system, macro recorder, `;cmd`, COM Mode,
  Global Control, or Package Tools feature was added. Release verification
  passed: `cmake --build . --target completion_test gui_smoke_test
  relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Latest Verification Baseline Repair: this is a baseline repair after Package
  Tools phase 1, not new Package Tools functionality. The blank-diagnostic
  Release rebuild failure was traced to generated MinGW build rules that did
  not make the compiler bin available on `PATH`; CMake now wraps generated
  compile and link rules with `cmake -E env PATH=...` using the detected
  compiler directory, so `relationship_test.cpp` can rebuild from a plain
  `cmake --build`. Package Tools coverage was reinforced in `completion_test`
  for all six templates' non-empty text/slots and exact packed-struct text,
  and in `gui_smoke_test` for the six stable package-tool buttons while still
  only exercising one GUI click/Slot Mode path; both service and GUI coverage
  assert the phase remains exactly six tools. This did not change `;;cmd`
  behavior and did not add package/import semantics, cross-file package
  management, automatic package sorting, or Package Tools phase 2; Track 15
  owns macro/define semantics. Release verification passed: `cmake --build . --target
  completion_test relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$" --output-on-failure`;
  `git diff --check -- . ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Functional Corpus Audit: retired. `corpus_audit_test` and
  `test_sv/corpus_audit_report.*` are no longer maintained because the broad
  sweep was expensive and gave weak acceptance signals. New GUI-discovered
  issues should be reduced into small fixtures and added to
  `relationship_test`, `completion_test`, `gui_smoke_test`, `jump_test`, or a
  feature-specific regression target.
- Full Feature Audit: `full_feature_audit_test` now inventories the
  broader product surface and writes `test_sv/full_feature_audit_report.json`
  plus `test_sv/full_feature_audit_report.md`. It is a lightweight feature
  inventory and ownership check, not a corpus acceptance gate. Verification
  should pair it with the focused regression targets and GUI smoke.
- Full Feature Audit acceptance repair: fast regression failures after
  `fbf7273` were traced to stale test context, not new product behavior. The
  FSM assertions now use structural current<=next fixtures instead of
  name-gated `ns`/`*_ns` assumptions; the GUI FSM graph fixture includes the
  clocked `state_q <= state_d` update required by structural discovery; and
  `jump_test` now resolves `test_sv/new` from the source tree when run from the
  build directory while package-member definition assertions provide an
  explicit `import snap_pkg::*;` context. Verification passed individually:
  `ctest -R "^completion_test$" --output-on-failure`; `ctest -R
  "^jump_test$" --output-on-failure`; `ctest -R "^gui_smoke_test$"
  --output-on-failure`; `ctest -R "^full_feature_audit_test$"
  --output-on-failure`.
