# ZeroSlack Long-Term Goal Model

ZeroSlack is a SystemVerilog code editor and workspace browser. Long-term work
must improve daily RTL editing and understanding while preserving the service
boundary:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

## Goal Mode Rules

- Work only on the goal tracks listed in this file.
- Each milestone must be small, verifiable, and deliverable.
- Each completed milestone must update `readme.md`, `plan.md`, and `goal.md`.
- Each completed milestone must run verification appropriate to the changed
  scope.
- Each completed milestone must be committed and pushed.
- Do not add unlisted features to a milestone.
- Do not put semantic policy, workspace scans, or Slang execution in UI code.
- Do not restore qmake/pro/pri, SVLexer, old Tree-sitter symbol parsing, regex
  relationship analysis, or active `;:` commands.
- Huge Workspace work is status audit only unless the user explicitly changes
  the scope.

## Current Goal State

Current milestone: State Transition Graph real usability repair is complete.
The next milestone should wait for a new explicit scoped request.

Current baseline highlights:

- Most recent completed milestone: State Transition Graph real usability
  repair. FSM discovery now handles structural `current <= #delay next` update
  pairs, including `#TP`, `# TP`, and `#(...)` controls common in the real
  corpus, while State Transition Graph remains gated to discovered next-state
  roles only. Current-state roles still reject with an explicit next-state
  selection reason.
- Verification for State Transition Graph real usability repair passed in the
  Debug build: `completion_test`, `gui_smoke_test`, `corpus_audit_test`,
  `full_feature_audit_test`, `jump_test`, and `relationship_test`.
  `corpus_audit_test` regenerated `test_sv/corpus_audit_report.json` and `.md`;
  `full_feature_audit_test` regenerated
  `test_sv/full_feature_audit_report.json` and `.md`.
- Current bounded corpus result: State Transition Graph is 68 pass / 0 fail /
  398 skipped, Signal Kernel Graph is 32 pass / 0 fail / 1960 skipped, and Wave
  Preview is 1047 pass / 0 fail / 2957 skipped / 4 empty-valid. Full-feature is
  now 23 pass / 0 fail / 0 skipped / 0 known-issue.
- Most recent completed milestone: import-aware SystemVerilog package symbol
  visibility. Unqualified package members are available to completion, goto,
  and hover only from active `import pkg::*;` context; local/module symbols win
  over imports; ambiguous same-name imported package members do not produce a
  random jump.
- Recently completed workflow baselines also include user template JSON usage,
  user template JSON phase 1, explicit header/include and package import
  commands, semantic `;m` module instantiation Slot Mode, Macro / Define
  first-class semantics, Package Tools phase 1, References / Relationships
  workflow closure, and Fold Shelf persistence/restore/management.
- `;pk` remains the explicit package import insertion entry. Package Tools
  remain package-file editing tools, and `pkg::symbol` completion was not added.

Status history:

- Package/import semantic visibility repair is complete. `PackageVisible`
  package parameters, localparams, typedefs, enums, and structs are no longer
  treated as globally visible for unqualified lookup. Completion, definition,
  and hover use the current file/scope import context, including
  `import pkg::*;`; local/module declarations take priority over imported
  package members; and conflicting same-name members from multiple imported
  packages are reported as ambiguous rather than resolved randomly. `;pk`
  remains an import insertion command only, Package Tools behavior is
  unchanged, and `pkg::symbol` completion was not added.
- Verification for package/import semantic visibility repair passed in the
  Debug build: `cmake --build . --target completion_test relationship_test
  gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Macro / Define first-class semantics are complete. Static `define` records
  enter the semantic index as Macro definitions; outline shows object-like and
  function-like macro names while keeping `ifdef` / `ifndef` / `else` /
  `endif` out of the main symbol list; `` `MACRO`` goto prefers current-file
  definitions and then workspace/include-visible definitions; hover shows
  definition location plus signature/body text; Find References returns the
  `define` row and all indexed backtick uses; undefined macro diagnostics are
  supplemented under the Semantic index owner; and inactive preprocessor
  branches are grayed conservatively from configured defines plus static
  current-file `define` / `undef` state.
- Macro / Define boundaries: no Vivado `.xpr` / Tcl parsing, no new define
  configuration UI, no complete SystemVerilog preprocessor or argument
  substitution engine, no formatter text mutation, and no ownership changes to
  Package Tools, COM Mode, Slot Mode, References, Relationships, or Problems
  workflows.
- Verification for Macro / Define first-class semantics passed: `cmake
  --build . --target completion_test relationship_test gui_smoke_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Macro / Define acceptance stabilization is complete. The GUI smoke
  reference/relationship dock regression now uses an isolated local semantic
  snapshot injected into `ReferenceService`, `RelationshipService`, and
  `HierarchyService`, resets dock filters at fixture entry/exit, and restores
  services to the global semantic index. This fixes CTest-order state leakage
  without weakening reference/relationship assertions or expanding the Macro /
  Define feature scope.
- Verification for the acceptance stabilization passed: `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`; `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed twice consecutively.
- `;m` module instantiation semantic completion is complete. Selecting a
  module through existing `;m` now emits a full named instantiation from
  semantic parameter and port records when available, omits `#(...)` for
  modules without parameters, falls back to the previous simple instantiation
  when parameter/port records are insufficient, and starts Slot Mode with
  instance, parameter value, then port connection slots. `;;m` remains the
  module definition skeleton path; no Package Tools phase 2, include/package/
  import workflow, new command, or COM Mode behavior was added.
- Verification for `;m` semantic instantiation passed: `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed.
- Header/include command convergence is complete. The old hidden `` `include
  `` + space entry is removed. `;h <query>` searches existing header/include
  candidates through the existing service-owned candidate/filter path and
  inserts a full SystemVerilog `` `include "..."`` statement. `;h -n <name>`
  creates a header before insertion, defaults suffix-less names to `.svh`,
  accepts `.vh` / `.svh`, prefers the current file directory, and refuses to
  overwrite existing files. `;;h` remains absent; no package/import completion,
  COM Mode command, or Global Control command was added.
- Verification for header/include convergence passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test`; `cmake --build .
  --target relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Package import explicit entry is complete. `;pk <query>` searches existing
  package semantic records through the completion service and inserts
  `import pkg_name::*;`. `;p` remains parameter completion, COM `gpk` remains
  package picker / package navigation, Package Tools remain package-file
  editing tools, and `;;pk` remains absent. No `pkg::symbol` completion,
  cross-file package management, COM Mode command, or Global Control command
  was added.
- Verification for package import explicit entry passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- User template JSON phase 1 is complete. `UserTemplateService` reads compact
  `;;` user templates from global `user_templates.json` and active-workspace
  `.zeroslack/user_templates.json`, with workspace records overriding global
  records by command token. Built-in templates and reserved `;;h` / `;;pk`
  holes cannot be overridden; invalid JSON, invalid commands, invalid slots,
  and conflicts are reported and ignored. Valid user templates enter the
  existing `;;cmd` completion path and start Slot Mode when slot metadata is
  present. No template GUI editor, import/export, macro recorder, new `;cmd`,
  COM Mode command, Global Control command, or Package Tools change was added.
- Verification for user template JSON phase 1 passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- User template JSON usage entry is complete. Tools / User Templates now has
  actions to open or create the global `user_templates.json`, open or create
  the active workspace `.zeroslack/user_templates.json`, and reload templates.
  Missing files are created with the empty legal skeleton only. Reload reports
  loaded/ignored counts in the status bar and lists invalid JSON, invalid
  commands, invalid slots, reserved tokens, and conflicts with file, command,
  field, and reason. No GUI template editor, import/export, variable system,
  macro recorder, new `;cmd`, new `;;cmd`, COM Mode, Global Control, or
  Package Tools feature was added.
- Verification for user template JSON usage entry passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Verification Baseline Repair after Package Tools phase 1 is complete. This
  was baseline repair only, not Package Tools phase 2: Release
  `relationship_test` rebuild failures were traced to generated MinGW
  compile/link rules missing the compiler bin on `PATH`, and CMake now injects
  the detected compiler directory through `cmake -E env PATH=...` for generated
  compile and link rules. No `relationship_test` target was skipped and no
  assertions were weakened.
- Package Tools coverage is reinforced: `completion_test` verifies non-empty
  insert text and slot metadata for `parameter`, `localparam`, `typedef enum`,
  `typedef struct`, `typedef struct packed`, and `function`, plus exact
  `typedef struct packed` text and an exact six-tool count; `gui_smoke_test`
  verifies all six stable package-tool buttons exist, exactly six Package
  Tools buttons are present, and still covers one clicked insertion into Slot
  Mode. Existing `;;cmd` template behavior is unchanged.
- Verification for this repair passed: Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'` passed.
- Package Tools phase 1 is complete: when the active cursor is inside a
  parseable SystemVerilog package, the editor shows a lightweight Package Tools
  bar with `parameter`, `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function` insertion buttons. Insert positions
  are derived from Tree-sitter package structure, stay inside the current
  package, default before `endpackage`, append near same-kind direct package
  items when present, and reject module/interface/program scope with clear
  failure messages. Inserted templates start Slot Mode on all editable slots.
  Existing `;;cmd` behavior is unchanged; package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Focused verification for Package Tools phase 1 passed: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "completion_test|gui_smoke_test" --output-on-failure` passed.
- References / Relationships workflow closure is complete: source-symbol
  context actions are stable, References and Relationships panels keep query
  context and explicit empty reasons, result rows jump/copy through one
  validated path, graph entry points call only existing graphs, and navigation
  history is isolated per workspace.
- Acceptance repair for the workflow closure is complete: RTL Insights FSM
  Graph and State Transition Graph panels render the service report's
  state-register and next-state signal nodes, preserve existing transition
  edges, add the existing-data signal-flow edge, and let next-state signal
  nodes invoke the existing navigation handler. No FSM extraction semantics,
  State Transition trigger rules, package tools, Wave Preview behavior, or
  graph algorithms were added; Track 15 owns macro/define semantics.
- Final verification for the workflow closure passed: Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; Release `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed.
- Scoped named-action cleanup is complete: the obsolete named-action service,
  editor mode path, shortcut hooks, dispatcher path, tests, and user-facing
  docs were removed while preserving normal editor shortcuts, context menus,
  COM Mode, `;cmd`, and `;;cmd` boundaries.
- Focused verification for the cleanup: Release `completion_test` and
  `relationship_test` passed through `ctest`; Release `gui_smoke_test` target
  compiled and linked without launching the executable.
- Interactive RTL Insights graph rendering is complete: State Transition Graph
  and Module Block Diagram now render as selectable QGraphics nodes/edges with
  graph-element navigation sourced from service reports.
- Focused verification for the interactive graph rendering: Release
  `completion_test` and `relationship_test` passed through `ctest`; Release
  `gui_smoke_test` target compiled and linked without launching the executable.
- RTL Insights FSM Graph rendering/candidate repair is complete: the direct
  `FSM Graph` entry renders selectable graph nodes/edges, and
  `FsmGraphService` filters out state-register candidates that lack parsed
  case-derived transitions. The regression target keeps the real
  `phy_pass_thrg_cfg_cs` / `phy_pass_thrg_cfg_ns` FSM present while excluding
  non-case enum/register noise.
- Focused verification for the FSM Graph repair: Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.
- COM Mode strip alert styling is complete: command failure messages now use a
  dark alert background, red alert text, and red border while normal command
  entry and prefix hints keep the cold dark strip. Parsing and dispatch are
  unchanged.
- Focused verification for the COM strip styling repair: Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^(completion_test|relationship_test)$"
  --output-on-failure` passed. The GUI smoke executable was not launched to
  avoid another modal Windows crash dialog during this repair loop.
- Module Block Diagram container rendering and drill-down is complete: RTL
  Insights now renders the selected module as a large container on a grid-backed
  canvas, arranges child modules inside it, exposes `-` / `Fit` / `+` zoom
  controls alongside mouse-wheel zoom, and re-renders the diagram around a
  double-clicked module after navigation.
- Focused verification for the Module Block Diagram repair: Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` and `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.
- COM clear-RHS command integration is complete: COM Mode now registers `cr`
  as the clear-RHS editor-local command, dispatches it through
  `MyCodeEditor::clearSelectedAssignmentRhs`, supports selected assignments and
  current-assignment no-selection cleanup, starts Slot Mode on cleared RHS fill
  points, and keeps failure feedback non-modal in the command strip/status path.
- Focused verification for the COM clear-RHS command: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` and
  `ctest -R "^relationship_test$" --output-on-failure` passed. Release
  `gui_smoke_test` was launched with Windows fault-dialog suppression; all new
  COM `cr` checks passed, but the full smoke baseline still fails on existing
  non-`cr` checks.
- COM select-inside and Slot Mode interaction repair is complete: `cr` now
  expands text selections to complete touched lines, `si` selects complete
  interior lines of the nearest Tree-sitter `begin ... end` block and stays in
  COM Mode, active COM editors paint a visible badge/bottom-edge indicator, and
  Slot Mode highlights all slots with a weak blink while Tab / Shift+Tab cycle
  without implicit exit.
- Focused verification for the COM select-inside and Slot Mode repair: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` and
  `ctest -R "^relationship_test$" --output-on-failure` passed. Release
  `gui_smoke_test` was launched with Windows fault-dialog suppression; all new
  `si`, partial-selection `cr`, and Slot Mode cycling checks passed, while the
  full smoke baseline still fails on existing non-current checks.
- COM toggle, visual-column column selection, and Column Number Tool repair is
  complete: `Ctrl+Shift+Alt+backtick` toggles COM Mode from editor or
  non-editor focus whenever an editor tab is open; Esc no longer enters COM
  Mode and remains cancellation-only. Column Selection now stores visual
  columns, converts through editor tab width, and captures Tab / Shift+Tab for
  visual alignment edits. COM command `cn` opens a Column Number Tool popup
  backed by `columnnumbertool` formatting/inference and applies one undoable
  column edit.
- Focused verification for the COM/Column Selection repair: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` passed. Release
  `gui_smoke_test` was launched; all new COM toggle, visual-column column
  mode, column-mode Tab/Shift+Tab, `cn`, Column Number Tool, and inference
  checks passed. The full smoke baseline still fails on existing non-current
  checks: `VENDOR ctrl-click fixture opens` and the Wave Preview rendering group.
- Workspace Project Configuration And Diagnostics Workflow is complete:
  `WorkspaceConfigurationService` persists per-workspace include dirs,
  defines, ignored dirs, file extensions, and optional top module;
  `WorkspaceConfigurationDialog` provides the workspace-focused UI;
  `WorkspaceManager::setWorkspaceConfiguration()` applies settings through
  `ProjectModel` and existing ignored-directory validation; and workspace
  analysis keys include the configuration fields that affect Slang analysis.
- Diagnostics workflow completion: Problems rows show diagnostic owner,
  current-file diagnostic summary follows the active tab, diagnostics state
  displays current/stale/analyzing/background, Problems jumps validate target
  files/locations and flash the revealed line, and F8 / Shift+F8 use
  `DiagnosticNavigationService` to move next/previous by the Problems panel's
  scope and severity filters.
- Focused verification for the workspace configuration / diagnostics workflow:
  Release `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` passed. Release
  `ctest -R "^relationship_test$" --output-on-failure` was attempted and
  failed in existing RTL Insights FSM graph/panel checks, not in this
  milestone's workspace configuration or diagnostics workflow.
- Workspace cache repair after the workflow milestone is complete:
  `WorkspaceManager` now applies workspace configuration silently during
  workspace activation/cached restore, so cached workspace switching no longer
  emits `filesScanned` / `workspaceListChanged` or invalidates the Navigation
  design hierarchy cache. Explicit Workspace Configuration edits still notify
  and refresh file filtering. The Defines table no longer shows misleading
  Up/Down buttons because define order is not persisted.
- Focused verification for the cache repair: Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `ctest -R
  "^completion_test$" --output-on-failure` passed. Release `ctest -R
  "^gui_smoke_test$" --output-on-failure` initially still failed on existing
  VENDOR ctrl-click and Wave Preview checks, but the four workspace/navigation
  cache regression checks passed.
- GUI smoke baseline repair is complete: VENDOR ctrl-click, Wave Preview,
  RTL Insights, and Problems diagnostics failures were confirmed as current
  regression coverage rather than obsolete features. The fix stabilized fixture
  path lookup, Tree-sitter scope fallback for safe Wave Preview ranges, RTL
  Insights synthetic snapshot isolation, and the external-diagnostic
  preservation probe.
- Focused verification for the GUI smoke baseline repair: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` passed; Release
  `ctest -R "^gui_smoke_test$" --output-on-failure` passed twice.
- G0 Documentation And Goal Reset is complete and pushed in commit `bc2c059`.
- G4.1 Registry Metadata For Existing COM Commands is complete:
  `commodecommandregistry` now owns metadata for fixed executable commands,
  non-executable prefixes, and the module-relative line command.
- Existing COM command behavior is preserved; editor runtime parsing now reads
  executable commands, prefixes, and `g<num>` checks from the shared registry
  API.
- G4.2 Help And Hint Rendering For COM Commands is complete:
  `commodecommandregistry` exposes hint text for executable commands,
  non-executable prefixes, and module-relative line buffers; the app-level
  command strip renders those hints.
- G4.3 Centralized Conflict Validation And Failure Reason Display is complete:
  registry validation can be tested against injected metadata, reports duplicate
  command, executable prefix conflict, and malformed prefix reasons, and editor
  COM failures use registry-backed messages where practical.
- Focused verification for G4.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.1 Editor Daily Action Inventory is complete: the baseline captured
  `Ctrl+F` Find, formatter context-menu actions, shortcut/context-menu
  metadata, and the missing daily action entry points.
- G1.2 Comment And Uncomment Entry Points is complete:
  active-line and selected-line line comments are available through `Ctrl+/`,
  `Ctrl+Shift+/`, and editor context-menu actions, without reviving active
  `;:` commands.
- Focused verification for G1.2: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.3 Indent And Unindent Entry Points is complete:
  active-line and selected-line indentation is available through `Ctrl+]`,
  `Ctrl+[`, and editor context-menu actions, without reviving active `;:`
  commands.
- Focused verification for G1.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.4 Replace And Goto Line Entry Points is complete:
  replace is available through `Ctrl+H` and editor context-menu action; goto
  line is available through `Ctrl+G` and editor context-menu action;
  non-dialog editor API coverage checks valid/invalid goto line, replace-next,
  selected replacement, replace-all, and replace-all undo behavior.
- Focused verification for G1.4: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G2.1 Slot Mode Model And Editor State is complete: Slot Mode ownership is
  documented as template-relative slot metadata from `CodeTemplateService`,
  insertion/start routing through `EditorCompletionWorkflow`, and editor-local
  session ownership in `MyCodeEditor` state; entry, Tab/Shift+Tab navigation,
  completion, Esc cancel, natural exit, stale-session invalidation, and future
  verification cases are defined without changing template behavior.
- Focused verification for G2.1: documentation inspection plus `git diff --check`.
- G2.2 Parameter Template Slot Mode is complete: `;;p` and `;;lp` produce
  ordered name/value slot metadata; parameter template activation starts Slot
  Mode through the existing completion workflow; `MyCodeEditor` state owns
  active slot ranges, highlighting, Tab/Shift+Tab navigation, Esc cancel,
  edit-driven range shifts, and cursor-outside stale exit; non-parameter
  template behavior is preserved. Current COM Mode toggle behavior does not
  clear Slot Mode by itself.
- Focused verification for G2.2: Release `completion_test` passed with 625
  checks and 0 failures; Release `completion_test` and `gui_smoke_test` targets
  compile/link, with `gui_smoke_test` not launched.
- G2.3 Signal Template Slot Mode is complete: `;;l`, `;;w`, and `;;r` produce
  a signal-name slot; signal template activation reuses the existing Slot Mode
  session path; signal-name editing preserves packed/unpacked dimensions and
  Tab / Shift+Tab keep cycling until Esc exits Slot Mode. Parameter template
  behavior is preserved.
- Focused verification for G2.3: Release `completion_test` passed with 630
  checks and 0 failures; Release `completion_test` and `gui_smoke_test` targets
  compile/link, with `gui_smoke_test` not launched.
- Verification flow now includes checking for external Windows application-error
  or memory-read dialogs when CTest appears stalled.
- G3.1 Clear-RHS Report/Service Design is complete:
  `RtlBatchEditService::planClearAssignmentRhs` now returns a non-mutating
  selected-text report with replacement text, per-assignment edit metadata,
  document-relative RHS offsets, zero-length `rhsN` template slots, and failure
  reasons. Supported first-pass forms are complete selected blocking,
  nonblocking, and continuous `assign` statements. Unsupported selections fail
  without mutation for empty selection, incomplete statement, declaration
  initializer, control-flow statement, macro statement, ambiguous top-level
  assignment, unmatched delimiter, unterminated string, or unterminated comment.
- G3.2 Clear-RHS Editor Command is complete:
  `MyCodeEditor::clearSelectedAssignmentRhs` routes selected text through
  `RtlBatchEditService`, applies successful reports in one undoable edit block,
  leaves the replacement range selected, and reports validation failures
  without text mutation. The obsolete named-action layer has been removed.
- G3.3 Clear-RHS Slot Mode Integration is complete:
  successful clear-RHS execution starts Slot Mode from the
  `RtlClearAssignmentRhsReport` `rhsN` template-slot metadata while preserving
  one undoable text replacement. G3.2 failure behavior is unchanged; Slot Mode
  start, slot editing, Tab / Shift+Tab cycling, Esc exit, undo restore, and
  declaration rejection are covered by focused tests.
- G5.1 Workspace Open/Recent Behavior Audit is complete:
  `WorkspaceManager` owns workspace open/close/switch, alias rename, cached
  scanned-file restoration, directory scan lifecycle, file watching, and recent
  workspace persistence. Recent entries are stored through `QSettings`,
  normalized, deduplicated, capped at 20, and exposed through
  `recentWorkspaceEntries()`. Global Control currently displays `ow 1`, `ow 2`,
  and numeric `ow <num>` only; `ow r` is hidden by `GlobalControlService` but
  still handled internally by `MainWindow` through the existing recent
  workspaces dialog. `ProjectModel` already supports `ignoredPaths` filtering,
  but there is no workspace-owned service/model path to set ignored directories.
- G5.2 Ignored-Directory Model/Service Path is complete:
  `WorkspaceIgnoreService` validates and normalizes ignored-directory requests,
  while `WorkspaceManager::setIgnoredDirectories()` applies them through
  `ProjectModel`, refreshes current file lists, updates workspace entry state,
  and preserves per-workspace ignored directories across cached switches.
  Cached restore keeps raw scanned files so clearing ignores can reveal
  previously hidden files again. No ignored-directory UI or `ow r` work was
  added in this milestone.
- Focused verification for G5.2: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `completion_test` passed directly with 669 checks and 0 failures; `ctest -R
  "^completion_test$"` passed.
- G5.3 Restore ow r Global Control Command is complete:
  `GlobalControlService` displays `ow r` as an `ow` domain child command, exact
  `ow r` queries return the recent-workspaces command instead of the numeric
  count hint, and dispatch continues through the existing `MainWindow` recent
  workspaces dialog path. No session restore, include dirs/defines UI, recent
  files, or broader workspace UX was added.
- Focused verification for G5.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `completion_test` passed directly with 672 checks and 0 failures; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G6.1 Completion Command Responsibility Audit is complete:
  `;cmd` is documented as inline semantic command completion owned by
  `InlineCommandMode`, `CompletionCommandMode`, `CompletionService`, and
  `CompletionSemanticQuery`; `;;cmd` is documented as template expansion owned
  by `CodeTemplateService` and applied through `EditorCompletionWorkflow` /
  Slot Mode; the obsolete named-action layer is documented as removed;
  COM Mode and Global Control are documented as separate command surfaces with
  separate registries, services, and coordinators.
- G6.1 conflict boundaries are documented: do not revive `;:cmd`, keep `;cmd`
  semantic, keep `;;cmd` template-only, keep COM editor-local, and keep Global
  Control app/workspace/global.
- Focused verification for G6.1: documentation inspection plus
  `git diff --check`.
- G6.2 User Template Storage/Query Model is complete:
  `UserTemplateService` now owns validation, persistence, reload, exact-token
  query, add/update, remove, and clear operations for compact `;;` user
  template records. Records are maintained in JSON files: a global
  `user_templates.json` and an optional workspace
  `.zeroslack/user_templates.json`. The service preserves
  `CodeTemplateSlotList` metadata, exposes `CodeTemplateItem`-compatible query
  results, and reports invalid JSON, invalid commands, invalid slots, and
  conflicts without mutating built-in templates.
- Focused verification for G6.2: Release `completion_test` and
  `gui_smoke_test` passed through `ctest -R
  "^(completion_test|gui_smoke_test)$" --output-on-failure`.
- G6.3 Custom Abbreviation Resolution is complete:
  `CustomAbbreviationService` now owns validation, persistence, reload, prefix
  query, intent-scoped query, exact resolution, add/update, remove, and clear
  operations for compact custom abbreviations. Records persist under
  `QSettings` `customAbbreviations/items`, reject duplicate aliases, invalid
  command tokens, command-surface marker aliases, and reserved `;:` action
  tokens, and resolve only to existing `;cmd` semantic command tokens or
  `;;cmd` template command tokens. Built-in `;cmd` / `;;cmd` behavior remains
  unchanged; custom abbreviations are not COM Mode or Global Control commands.
- Focused verification for G6.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G6.4 Integrate User Templates With Slot Mode is complete:
  `CompletionService` now consumes service-owned `UserTemplateService` records
  in the inline `;;cmd` template path. Normal use falls back to the singleton
  user-template service; tests can inject global/workspace JSON paths. Compact user
  `;;token ` commands are recognized as `CodeTemplate` intent without changing
  `;cmd`, COM Mode, Global Control, or custom abbreviation namespaces. Built-in
  templates keep priority, workspace user templates override global user
  templates with the same command token, and reserved `;;h` / `;;pk` holes stay
  unavailable in this phase. Activation preserves insert text, primary
  selection, and `CodeTemplateSlotList` metadata through the existing
  `EditorCompletionWorkflow` / `MyCodeEditor` Slot Mode path.
- Focused verification for G6.4: Release `completion_test` and
  `gui_smoke_test` passed through `ctest -R
  "^(completion_test|gui_smoke_test)$" --output-on-failure`.
- G7.1 Fold Shelf Persistence Schema And Ownership is complete:
  existing owners are documented. `FoldBlockShelfModel` owns the in-memory
  shelf item list and add/consume/remove/clear lifecycle; `FoldShelfItem`
  carries id, alias, text, source file/module, source line range, line count,
  origin kind, consumed, and stale; MIME helpers are drag/drop transport only;
  `FoldBlockShelfPanel` renders and manages preview/delete/drop/drag UI;
  `EditorFoldingController` owns shelf mode, custom fold extraction, source
  deletion, and insertion; `MainWindow` wires dock/model visibility and restore
  dispatch. The durable schema is versioned under `foldShelf/v1/items`, scoped
  by normalized workspace root, and persists the current item fields with
  normalized `sourceFile`. Persistence policy belongs in the model/service
  layer, not the panel. G7.2 same-file restore requirements are documented
  without implementing restore behavior in G7.1.
- Focused verification for G7.1: source inspection of
  `foldblockshelfmodel`, `foldblockshelfpanel`, `editorfolding`,
  `mainwindow`, and existing Fold Shelf tests, plus `git diff --check`.
- G7.2 Persist And Restore Same-File Fold Shelf Items is complete:
  `FoldShelfPersistenceService` owns versioned QSettings-backed storage under
  `foldShelf/v1/workspaces/<scope>/items`, scoped by normalized workspace root
  and normalized source paths. `FoldBlockShelfModel` attaches the persistence
  service and workspace root, reloads persisted items on root changes, and
  persists add, consume, stale mark, remove, and clear mutations.
  `MainWindow` wires the singleton service into the model, updates model scope
  on workspace activation/close, and marks items stale when same-file restore
  fails because the source file is unavailable, cannot be opened, or cannot be
  inserted at the recorded location. Successful restore removes the item only
  after insertion. `FoldBlockShelfPanel` remains storage-policy free.
- Focused verification for G7.2: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G7.3 Restore Fold Shelf Items Across Files is complete:
  `FoldShelfRestoreService` owns explicit active-editor restore reports,
  validates model/item/editor availability, restores selected persisted shelf
  items through the existing editor fold-shelf insertion API, and consumes or
  removes items only after successful insertion. Missing active editor or failed
  insertion marks the item stale with a clear failure reason. `FoldBlockShelfPanel`
  stays storage-policy free and only emits restore requests; `MainWindow` wires
  the selected shelf item to the active editor/cursor line.
- Focused verification for G7.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G7.4 Add Fold Shelf Rename/Search/Clean Management Actions is complete:
  `FoldBlockShelfModel` owns rename, query/filter, and stale/consumed cleanup
  behavior. Rename trims aliases, rejects blank aliases, persists successful
  changes, and emits the existing change signal. Filtering reads only shelf
  item data, and clean management removes stale or consumed items through an
  explicit model mutation. `FoldBlockShelfPanel` provides the search field and
  Rename / Clean Stale/Consumed buttons while remaining a model consumer.
- Focused verification for G7.4: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G8.1 Add Signal Kernel Graph Fanout Grouping Reports is complete:
  `SignalKernelGraphReport` now carries service-owned high-fanout grouping
  metadata for dense input/output sides while preserving raw inputs, outputs,
  edges, and module groups. `SignalKernelGraphFanoutGroup` records group id,
  role, input lane, module, display name, grouped node ids, per-group count,
  total side count, cross-module state, and high-fanout state. Grouping is
  emitted only when a graph side reaches the service-owned threshold; UI
  collapse/expand, filtering UI, and graph search were not added.
- Focused verification for G8.1: `git diff --check`; Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G8.2 Render Collapsible Signal Kernel Graph Fanout Groups is complete:
  `SignalKernelGraphPanelCoordinator` renders G8.1 fanout groups as
  collapsible summaries. New group keys default to collapsed; collapsed groups
  hide raw nodes, render a summary item, and route grouped edges through that
  summary while deduplicating only collapsed group edges. Expanded groups show
  raw nodes with their existing preview, navigation, and rebase handlers plus a
  clickable header to collapse again. Collapse/expand state stays in the panel.
- Focused verification for G8.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G8.3 Add Signal Kernel Graph Filtering And Search is complete:
  `SignalKernelGraphPanelCoordinator` now exposes graph search plus
  input/output/cross-module filters above the graph. Filtering rebuilds the
  visible graph from existing `SignalKernelGraphReport` node data, preserves
  fanout collapse/expand state, hides groups with no visible members, and keeps
  raw visible-node preview, navigation, and rebase behavior. Graph search
  highlights/focuses visible node matches and collapsed fanout-group matches
  without expanding groups implicitly. No workspace scan or UI-side Slang work
  was added.
- Focused verification for G8.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.1 Selected-Always Wave Preview Entry And Scoped Report is complete:
  `TSDocument` now exposes current/selected `always_construct` scope targets and
  rejects selections spanning multiple `always` blocks. `MyCodeEditor` exposes
  that editor-local target to `MainWindow`, and the Wave Preview dock refreshes
  from the selected/current `always` scope only. The report still flows through
  existing `WavePreviewQuery` / `WavePreviewService` scoped fields; no simulator
  behavior, selected-module entry, workspace scan, or UI-side Slang work was
  added.
- Focused verification for G9.1: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.2 Selected-Module Wave Preview Entry And Scoped Report is complete:
  `TSDocument` now exposes current/selected module/interface/program scope
  targets and rejects selections spanning multiple RTL containers. `MyCodeEditor`
  exposes that editor-local target to `MainWindow`; Wave Preview uses selected
  `always` scope first and falls back to selected/current module scope only when
  no `always` scope is active. The report still flows through existing
  `WavePreviewQuery` / `WavePreviewService` scoped fields; no simulator
  behavior, workspace scan, UI-side Slang work, or sketch readability polish was
  added.
- Focused verification for G9.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.3 Wave Preview Sketch Readability Polish is complete:
  `WavePreviewPanelCoordinator` renders compact canvas legends for trace
  sketches and assign/blocking/nonblocking code sketches. The Wave Preview tree
  adds Scope and Legend overview rows ahead of detailed trace, warning,
  activity, lane, and event rows. The selected-`always` and selected/current
  module entry behavior is preserved, and the UI remains a scoped report
  consumer with no workspace scan, UI-side Slang work, or simulator behavior.
- Focused verification for G9.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G10.1 State Transition Trigger Gating is complete and now structural:
  `StateTransitionTriggerService` asks `FsmGraphService` for the selected
  symbol's discovered FSM role. `EditorSourceNavigationQuery` enables and
  dispatches `Show State Transition Graph` only for selected next-state roles;
  selected current-state roles, ordinary symbols, and missing module context do
  not dispatch. Names such as `ns`, `next_state`, `cs`, and `current_state` are
  examples/display hints rather than gates. Accepted requests route through
  `EditorCoordinator` and `SemanticPanelRefreshCoordinator` to the existing RTL
  Insights FSM graph path.
- Focused verification for G10.1: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G10.2 State Transition Service-Owned Report is complete:
  `StateTransitionGraphService` validates accepted requests through
  `StateTransitionTriggerService`, builds module FSM graph data through
  `FsmGraphService`, and filters the result to the selected next-state signal.
  Accepted structural next-state role reports expose selected signal, module,
  state count, transition count, and the matching `FsmGraph`; rejected
  current-state roles, no-FSM modules, and symbols without a matching
  next-state graph return service-owned failure reasons. The existing
  state-transition UI route consumes the new service report through RTL
  Insights without moving extraction or filtering into UI. No workspace scan or
  UI-side Slang work was added.
- Focused verification for G10.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` and `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G10.3 State Transition Graph UI And Navigation Evidence is complete:
  RTL Insights renders the service-owned `StateTransitionGraphReport` as an
  interactive graph scene with selectable state/register/signal nodes and
  transition edges. Panel-level coverage verifies that selecting a next-state
  role renders only the matching next-state graph and excludes sibling FSM
  graphs in the same module. The same coverage invokes graph-element navigation
  for transition edges and next-state signal nodes and verifies that source
  links from the report reach the navigation handler. G10.1 gating and G10.2
  selected next-state filtering are preserved; extraction and report shaping
  remain outside UI code.
- Focused verification for G10.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` and `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G11.1 Module Block Diagram Service Report is complete:
  `ModuleBlockDiagramService` owns selected-module containment report shaping
  from indexed module/instance symbols plus `INSTANTIATES` relationships. The
  report returns module/interface definition nodes, child module type names,
  instance names, instantiation edges, module definition links, instance
  declaration links, and unresolved/blackbox reasons; signal and non-instance
  relationships are filtered before UI consumption.
- Focused verification for G11.1: `git diff --check`; Release
  `relationship_test` target compile/link; `ctest -R "^relationship_test$"`
  passed.

- G11.2 Module Block Diagram Rendering is complete:
  RTL Insights renders `ModuleBlockDiagramReport` as a module-only interactive
  graph scene. The panel action renders the active module, and editor
  source-symbol routing can render selected module names as the diagram root.
  The rendered nodes and edges come from the service report, show
  module/interface containment only, include module type plus instance names,
  keep unresolved module types visible as blackbox nodes with reasons, and do
  not show signal nodes. No UI workspace scan, UI-side Slang work, or new
  relationship extraction was added.
- Focused verification for G11.2: `git diff --check`; Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G11.3 Module Block Diagram Navigation Evidence is complete:
  rendered top-module and child-module graph nodes carry report-provided module
  definition links into the existing RTL Insights navigation path; instantiation
  edges carry child module definition links. Focused panel coverage invokes
  graph-element navigation for root and child module nodes and verifies that
  the navigation handler receives the
  corresponding module definition file, line, and column. G11.1 report
  ownership and G11.2 module-only rendering are preserved; no signal rendering,
  UI workspace scan, UI-side Slang work, or new relationship extraction was
  added.
- Focused verification for G11.3: `git diff --check`; Release
  `relationship_test` target compile/link; `ctest -R "^relationship_test$"`
  passed.

- HWA.1 Huge Workspace Owner And Verification Inventory is complete:
  `WorkspaceAnalysisPlanService` owns current/dirty-open/clean-open/background
  priority planning, protected files, band metadata, and staged publication
  checkpoints; `WorkspaceAnalysisRequestQueue` owns active/pending request
  coalescing, stale pending replacement, cancellation state, and telemetry;
  `WorkspaceSymbolAnalysisController` plus `SymbolAnalyzer` own workspace
  expiration, cancellation, async extraction, and chunked/staged publication;
  `SemanticIndex` / `SemanticIndexSnapshot` own band metadata and
  priority-aware query ordering; `RelationshipAnalysisController`,
  `RelationshipAnalysisWorker`, and `RelationshipResultPublisher` own
  relationship cancellation and stale/cancelled result rejection; and
  `AnalysisProgressCoordinator` plus `ActivityLogService` own visible Activity
  telemetry.
- Existing HWA verification anchors are documented as `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`.
  No Huge Workspace UX feature was added in HWA.1.
- Focused verification for HWA.1: documentation inspection plus
  `git diff --check`.

- HWA.2 Huge Workspace Safe Verification is complete:
  the safe verification path was Release compile/link of the HWA-related
  test/harness targets rather than launching GUI executables in an environment
  that has recently shown external Windows application-error dialogs.
  `completion_test`, `relationship_test`, `large_file_perf_test`, and
  `relationship_perf_test` built successfully in the Release CMake build
  directory, and artifact inspection confirmed all four executables exist.
  `ctest` was not run, so no stalled-CTest or external memory-read dialog check
  was exercised in this milestone.
- Focused verification for HWA.2: Release build of `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`;
  documentation inspection plus `git diff --check`.

- HWA.3 Huge Workspace Confirmed Status And Gaps is complete:
  the Huge Workspace docs now state that owner boundaries exist for planning,
  request coalescing, symbol cancellation/expiration, staged publication,
  relationship cancellation, Activity telemetry, and semantic snapshot query
  exposure. They also state the remaining audit gaps: HWA.2 did not run test
  executables, full `ctest` was not run in the current dialog-prone
  environment, `relationship_perf_test` was not freshly executed against
  `test_sv/huge_prj`, Activity telemetry was not revalidated in a live GUI
  session, and the prior Release `huge_prj` timings remain reference points
  rather than refreshed HWA.3 measurements. No Huge Workspace UX or behavior
  change was added.
- Focused verification for HWA.3: documentation inspection plus
  `git diff --check`.

- Post-HWA acceptance baseline repair is complete:
  `completion_test` source-symbol context menu assertions now match the current
  five-action menu after Module Block Diagram was added. The repaired baseline
  verifies ordinary signals keep Module Block Diagram disabled, selected module
  names enable it and carry the expected action/file/module payload, `ns` and
  `next_state` still enable State Transition Graph, and `cs` /
  `current_state` still reject it. No product feature behavior was changed.
- Focused verification for the baseline repair: Release `ctest -R
  "^completion_test$" --output-on-failure` and Release `ctest -R
  "^relationship_test$" --output-on-failure` both passed.

## Track 1: Editor Daily Operations Completion

Goal: provide practical entry points for daily editing actions.

Allowed actions:

- replace
- goto
- comment
- uncomment
- indent
- unindent

Milestones:

- G1.1 Inventory current commands/actions and document missing entry points.
- G1.2 Deliver one coherent action family with tests.
- G1.3 Repeat until all listed daily actions have usable entry points.
- G1.4 Replace and goto line entry points.

## Track 2: Slot Mode After Template Insertion

Goal: after `;;cmd` insertion, users can jump through editable slots instead of
manually moving the cursor.

Milestones:

- G2.1 Define slot model, editor state, cancel/exit behavior, and verification
  cases.
- G2.2 Enable slot mode for the parameter declaration template family.
- G2.3 Expand slot mode to signal declaration template families.

## Track 3: Batch RTL Edit Actions

Goal: add focused RTL batch edits that feed slot mode.

First target:

- clear assignment RHS expressions and create fill points
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`

Milestones:

- G3.1 Design the clear-RHS report/service path.
  (complete: non-mutating `RtlBatchEditService` report, failure reasons, and
  fill-slot metadata)
- G3.2 Implement the editor-local clear-RHS command for selected assignments.
  (complete: `MyCodeEditor::clearSelectedAssignmentRhs` applies the service
  report in one undoable edit block and preserves failure reasons)
- G3.3 Connect clear-RHS output to slot mode.
  (complete: successful clear-RHS execution starts Slot Mode on `rhsN` fill
  slots)
- G3.4 Expose clear-RHS as COM command `cr`.
  (complete: `cr` dispatches through the editor-local clear-RHS path, supports
  selected/current assignment cleanup, starts Slot Mode, and keeps failures
  non-modal)
- G3.5 Make clear-RHS line-oriented for partial selections.
  (complete: selected text expands to touched complete lines before RHS
  cleanup, preserving source-order slot flow)

## Track 4: COM Mode Framework Completion

Goal: improve COM Mode framework quality before adding more commands.

Allowed work:

- unified command registry
- help/hints
- conflict handling
- failure reason display
- metadata for existing commands and prefixes

Milestones:

- G4.1 Registry metadata for existing commands and prefixes.
- G4.2 Help/hint rendering for commands and prefixes.
- G4.3 Centralized conflict validation and failure reason display.
- G4.4 Register the first non-g editor-local editing command.
  (complete: `cr` uses the registry/dispatcher/hint/failure-message path)
- G4.5 Select-inside begin-end COM command.
  (complete: `si` uses editor Tree-sitter structure, stays in COM Mode, and
  feeds line-oriented `cr`)
- G4.6 Explicit COM toggle and cancellation-only Esc.
  (complete: `Ctrl+Shift+Alt+backtick` toggles COM Mode without clearing
  column selection or Slot Mode; Esc no longer enters COM Mode)
- G4.7 Column Number Tool COM command.
  (complete: `cn` opens the column-number popup for active column selections,
  backed by `columnnumbertool` formatting and inference)

## Track 5: Limited Workspace Workflow Additions

Goal: add only the explicitly allowed workspace workflow pieces.

Allowed work:

- ignored directories
- Global Control `ow r` recent workspace command

Not allowed in this track:

- session restore
- include dirs/defines configuration UI
- recent files
- broad workspace UX expansion

Milestones:

- G5.1 Audit current workspace open/recent behavior.
  (complete: owners, current `ow` behavior, hidden `ow r` compatibility, recent
  persistence, and ignored-path baseline documented)
- G5.2 Add ignored-directory model/service support.
  (complete: service validation plus workspace manager state path)
- G5.3 Restore `ow r` as a displayed Global Control child command.
  (complete: visible `ow r` child command plus existing recent dialog routing)

## Track 6: Completion / `;cmd` / `;;cmd`

Goal: support user customization and clarify command responsibilities.

Allowed work:

- user templates
- custom abbreviations
- slot mode integration
- clear boundary between `;cmd`, `;;cmd`, and COM Mode

Milestones:

- G6.1 Document current responsibilities and conflict boundaries.
  (complete: command surface ownership and conflict boundaries documented)
- G6.2 Add user-template storage/query model.
  (complete: `UserTemplateService` storage/query model)
- G6.3 Add custom abbreviation resolution.
- G6.4 Integrate user templates with slot mode.

## Track 7: Fold / Fold Shelf

Goal: make Fold Shelf durable across work sessions and files.

Allowed work:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

Milestones:

- G7.1 Define persistence schema and ownership.
- G7.2 Persist/restore same-file shelf items.
- G7.3 Restore shelf items across files.
- G7.4 Add rename/search/clean management actions.

## Track 8: Signal Kernel Graph

Goal: make dense graphs readable.

Allowed work:

- high fanout collapse/grouping
- filtering
- graph search

Milestones:

- G8.1 Add service/report grouping for high fanout.
  (complete: service/report metadata)
- G8.2 Render collapsible fanout groups.
  (complete: panel collapse/expand)
- G8.3 Add filter and in-graph search.
  (complete: panel filters and graph search)

## Track 9: Wave Preview

Goal: show waveform sketches for selected RTL context only.

Allowed work:

- selected `always` block sketch
- selected module sketch
- readability polish for the sketch

Not allowed:

- simulator behavior
- timing-accurate verification
- waveform database import
- testbench execution

Milestones:

- G9.1 Selected-`always` entry and scoped report.
  (complete: editor Tree-sitter scope target plus scoped service report)
- G9.2 Selected-module entry and scoped report.
  (complete: module fallback scope target plus scoped service report)
- G9.3 Sketch readability polish.
  (complete: canvas legend plus Scope/Legend overview rows)

## Track 10: State Transition Graph

Goal: show a state transition graph only when the selected symbol is the
discovered next-state role of a structural FSM pair.

Rules:

- Structural FSM discovery owns current/next role classification.
- Selected discovered next-state roles trigger.
- Selected discovered current-state roles do not trigger and should prompt for
  the next-state role.
- Names such as `ns`, `next_state`, `cs`, and `current_state` are examples or
  display hints only.

Milestones:

- G10.1 Trigger gating and tests for structural FSM roles.
  (complete: service-owned structural role gate plus editor source-symbol
  action)
- G10.2 Service-owned transition extraction/report.
  (complete: selected next-state report service and filtered FSM graph data)
- G10.3 Graph UI rendering and navigation evidence.
  (complete: RTL Insights interactive graph rendering and graph-element
  source-link navigation evidence)

## Track 11: Module Block Diagram

Goal: from a selected module name, show a module-only block diagram rooted at
that module.

Rules:

- show module/interface instance and wrapping relationships only
- do not show signals
- clicking the root module block jumps to the module definition
- clicking a child module/instance block jumps to the instance declaration and
  drills into the child module when its definition is resolved
- unresolved/blackbox instances remain visible with an explicit reason and jump
  to their instance declaration

Milestones:

- G11.1 Service report for module containment from selected module.
  (complete: `ModuleBlockDiagramService` owns the module-only containment
  report, definition links, instance links, instance names, and blackbox
  reasons)
- G11.2 Module-only block diagram rendering.
  (complete: RTL Insights renders `ModuleBlockDiagramReport` for active or
  selected modules, including blackbox instance nodes)
- G11.3 Click navigation to module definitions.
  (complete: focused tests verify root definition navigation, child instance
  declaration navigation, resolved child drill-down, and unresolved blackbox
  instance navigation)
- G11.4 Real corpus usability audit.
  (complete: 2026-07-02 corpus audit over 424 modules reports
  `module_block_diagram` pass=209, fail=0, skipped=0, timeout=0,
  empty-valid=215, with 8644 resolved diagram instances and 7 unresolved
  blackbox instances)

## Track 12: Workspace Project Configuration And Diagnostics Workflow

Goal: make workspace-level project configuration explicit and make diagnostics
usable for day-to-day navigation.

Allowed work:

- include dirs
- defines
- ignored dirs
- file extensions
- optional top module / active top
- next/previous diagnostic navigation
- Problems jump stability, flash, owner, status, and current-file summary

Not allowed in this track:

- session restore
- recent files
- new diagnostic engines beyond the existing Slang and Semantic index paths
- UI-side Slang execution
- UI-side workspace scanning
- broad workspace UX expansion

Milestones:

- G12.1 Workspace Configuration Service And Dialog.
  (complete: per-workspace persisted include dirs, defines, ignored dirs, file
  extensions, and top module with a focused dialog)
- G12.2 Diagnostics Navigation And Problems Status.
  (complete: next/previous diagnostic navigation, owner display, current-file
  summary, diagnostics state, validated jumps, and flash)
- G12.3 Verification And Documentation.
  (complete: docs updated and focused Release verification recorded)

## Track 13: References / Relationships Workflow Closure

Goal: make source-symbol navigation, references, relationship rows, and graph
result jumps easy to find, jump from, and return from.

Allowed work:

- source-symbol right-click workflow organization
- References result grouping, query context, empty-state reasons, copying, and
  jumps
- Relationships result source jumps and existing graph entry points
- unified navigation path with Back/Forward history isolation per workspace
- status-bar or panel feedback for jump failures

Not allowed:

- package tools
- macro/define semantics, which is owned by Track 15
- Wave Preview expansion
- new graph types
- complex graph algorithms
- expanded State Transition Graph behavior beyond structural FSM role gating

Milestones:

- G13.1 Unified source-symbol context menu.
  (complete: Go to Definition, Find References, Show Relationships, Signal
  Kernel Graph, State Transition Graph, and Module Block Diagram are always
  present; disabled entries expose reasons; Module Block Diagram is limited to
  existing module/interface definitions; discovered current-state roles remain
  rejected)
- G13.2 References panel workflow closure.
  (complete: query context, empty reasons, click/activated jump with flash,
  and copy path or file:line)
- G13.3 Relationships panel workflow closure.
  (complete: source-code jumps for every result row, instance/module-definition
  jumps where existing data supports them, signal role jumps from existing
  relationship data, and only existing graph entry points)
- G13.4 Unified navigation failure feedback and history isolation.
  (complete: References, Relationships, and graph jumps share one validated
  navigation path; Back/Forward history is isolated by workspace; failures are
  visible)
- G13.5 Verification and documentation.
  (complete: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release acceptance CTest
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed)

## Track 14: Package Tools

Goal: give SystemVerilog package scopes a lightweight package-only insertion
surface for common definitions.

Allowed work:

- package-scope detection from Tree-sitter editor structure
- package-only insertion buttons for common definition templates
- same-kind direct package item append anchors
- default insertion before `endpackage`
- Slot Mode after insertion
- clear failure reasons when the current package cannot be determined

Not allowed:

- package/import semantic interpretation
- macro/define handling, which is owned by Track 15
- cross-file package management
- automatic package-wide sorting
- new graphs or insight panels
- changing existing `;;cmd` template behavior

Milestones:

- G14.1 Package Tools first phase.
  (complete: `parameter`, `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function` buttons insert package-only
  templates inside the current package and start Slot Mode)
- G14.1a Verification Baseline Repair.
  (complete: Release `relationship_test` rebuild baseline repaired and all six
  Package Tools phase-1 templates/buttons covered without Package Tools phase
  2, package/import semantics, cross-file package management, package sorting,
  graph work, or `;;cmd` changes; Track 15 owns macro/define semantics)
- G14.2 Future Package Tools expansion.
  (not started: additional package templates, semantic package handling,
  package sorting, or cross-file workflows require a new scoped request)

## Track 15: Macro / Define Semantic Workflow

Goal: make common SystemVerilog macro and preprocessor-branch workflows usable
without turning ZeroSlack into a full preprocessor.

Allowed work:

- static `define` symbol records and outline rows
- goto definition, hover, and Find References for `` `MACRO`` uses
- clearer undefined macro diagnostics as Semantic index supplements to Slang
- conservative inactive `ifdef` / `ifndef` / `elsif` / `else` / `endif`
  branch decorations using workspace configured defines and static current-file
  `define` / `undef` state

Not allowed:

- Vivado `.xpr` / Tcl parsing
- new define configuration UI
- complete SystemVerilog macro expansion or argument substitution
- formatter-visible text mutation
- changing Package Tools, COM Mode, Slot Mode, diagnostics, references, or
  relationship ownership boundaries

Milestones:

- G15.1 Macro / Define First-Class Semantics.
  (complete: outline, goto definition, hover, references, supplemental
  undefined diagnostics, and inactive branch gray decorations are available
  with conservative static behavior)
- G15.1a Acceptance Stabilization.
  (complete: GUI smoke reference/relationship dock regression now isolates
  semantic services and panel filters; Release acceptance CTest passed twice
  consecutively)

## Huge Workspace Status Audit

Huge Workspace is not an active feature expansion track in this goal model.
Only audit and document existing strategy status.

Audit topics:

- current/open/dirty-open priority
- analysis bands and query ordering
- stale request coalescing and expiration
- cancellation
- staged publication
- Activity telemetry
- Release `huge_prj` performance references

Milestones:

- HWA.1 Inventory owner classes, current behavior, and existing verification.
  (complete: owners and existing harnesses documented)
- HWA.2 Run safe verification or compile relevant targets.
  (complete: Release build of related targets passed)
- HWA.3 Update docs with confirmed status and open gaps.
  (complete: confirmed status and open audit gaps documented)

## Functional Corpus Audit

Goal: audit real corpus behavior across core semantic/RTL insight features
without mutating `test_sv/new` or `test_sv/huge_prj` source files.

Milestones:

- FCA.1 Add repeatable corpus audit target.
  (complete: `corpus_audit_test` recursively opens all 454 `.sv` / `.svh` /
  `.v` corpus files read-only and writes JSON + Markdown reports)
- FCA.2 Run four priority feature sweeps.
  (complete: current report covers State Transition Graph, Signal Kernel Graph,
  Module Block Diagram, and Wave Preview through service/report-layer paths)
- FCA.3 Document known issues and residual risk.
  (complete: current report generated 2026-07-02T10:46:26Z UTC and covered 454
  files, 424 modules, 3942 always/process records, 49,345 signal/port
  candidates, 117,069 semantic records, 82,636 audit relationships, and 704
  diagnostics. State Transition Graph is 68 pass / 0 fail / 398 skipped under a
  deterministic module sample cap. The audit counts discovered clocked FSM
  pairs, including delayed current<=next assignments, not `ns` / `next_state`
  name candidates; each discovered pair contributes a next-state positive case
  and a current-state negative trigger case. Signal
  Kernel Graph reports 32 pass / 0 fail / 1,960 skipped, with
  skipped cases carrying deterministic no-candidate or sample-cap reasons.
  Module Block Diagram reports 209 pass / 215 empty-valid. Wave Preview uses source-discovered always/process
  records and reports 1047 pass / 0 fail / 2957 skipped / 4 empty-valid under
  a deterministic process preview cap; no-lane/no-warning cases are repaired
  into lane-producing previews or explicit unsupported reasons. Semantic baseline is 874 pass / 0 fail / 5
  empty-but-valid after classifying empty outlines as comment/preprocessor-only
  or header-like empty.)

## Full Feature Audit

Goal: inventory the current implemented feature surface, connect entries to
services/tests, and fold the full recursive corpus audit into a durable feature
health report without mutating real corpus files.

Milestones:

- FFA.1 Feature inventory and matrix.
  (complete: `full_feature_audit_test` writes
  `test_sv/full_feature_audit_report.json` and
  `test_sv/full_feature_audit_report.md` with 23 feature rows, 93 user entry
  points, 75 service/test touchpoints, automation method, corpus coverage,
  pollution risk, status, reason, and next action.)
- FFA.2 Full recursive corpus integration.
  (complete: current run consumes the refreshed `corpus_audit_test` report for
  all 454 `.sv` / `.svh` / `.v` files under `test_sv/new` and
  `test_sv/huge_prj`; semantic totals are 117,069 records, 82,636 audit
  relationships, and 704 diagnostics.)
- FFA.3 Known issue triage without broad refactor.
  (complete: current matrix generated 2026-07-01T18:25:42Z UTC is 23 pass / 0
  fail / 0 skipped / 0 known-issue. Signal Kernel Graph budget limits are now
  expressed as deterministic case-level skipped reasons; other expensive RTL
  sweeps are likewise bounded and remain pass.)
- FFA.4 Verification and documentation.
  (complete: `ctest -R "^corpus_audit_test$" --output-on-failure` and
  `ctest -R
  "^(completion_test|jump_test|relationship_test|gui_smoke_test|full_feature_audit_test)$"
  --output-on-failure` passed.)
- FFA.5 Acceptance repair after independent rerun.
  (complete: stale fast-regression fixtures were repaired instead of
  converting failures to known issues. FSM assertions now use structural
  current<=next fixtures, GUI FSM graph smoke includes the clocked
  `state_q <= state_d` update required by structural discovery, `jump_test`
  resolves `test_sv/new` from the source tree when launched from the build
  directory, and package member definition checks provide explicit
  `import snap_pkg::*;` context. Individual `completion_test`, `jump_test`,
  `gui_smoke_test`, and `full_feature_audit_test` CTest runs pass.)

## Completion Order

Preferred starting order:

1. G0 Documentation And Goal Reset.
2. G4 COM Mode Framework Completion.
3. G1 Editor Daily Operations Completion.
4. G2 Slot Mode After Template Insertion.
5. G3 Batch RTL Edit Actions.

The remaining tracks may be pulled forward only when the user explicitly asks or
when a milestone naturally depends on them. Do not combine unrelated tracks in
one milestone.
