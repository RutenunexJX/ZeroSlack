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

Current milestone: `G10.2 State Transition Service-Owned Report`.

Status:

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
  `Ctrl+F` Find, formatter context-menu actions, alternate command metadata,
  and the missing daily action entry points.
- G1.2 Comment And Uncomment Entry Points is complete:
  active-line and selected-line line comments are available through `Ctrl+/`,
  `Ctrl+Shift+/`, editor context-menu actions, and alternate commands
  `comment` / `uncomment`, without reviving active `;:` commands.
- Focused verification for G1.2: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.3 Indent And Unindent Entry Points is complete:
  active-line and selected-line indentation is available through `Ctrl+]`,
  `Ctrl+[`, editor context-menu actions, and alternate commands `indent` /
  `unindent`, without reviving active `;:` commands.
- Focused verification for G1.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.4 Replace And Goto Line Entry Points is complete:
  replace is available through `Ctrl+H`, editor context-menu action, and
  alternate command `replace`; goto line is available through `Ctrl+G`, editor
  context-menu action, and alternate command `goto_line`; non-dialog editor API
  coverage checks valid/invalid goto line, replace-next, selected replacement,
  replace-all, and replace-all undo behavior.
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
  active slot ranges, highlighting, Tab/Shift+Tab navigation, final Tab
  completion, Esc cancel, edit-driven range shifts, cursor-outside stale exit,
  and COM Mode entry cleanup; non-parameter template behavior is preserved.
- Focused verification for G2.2: Release `completion_test` passed with 625
  checks and 0 failures; Release `completion_test` and `gui_smoke_test` targets
  compile/link, with `gui_smoke_test` not launched.
- G2.3 Signal Template Slot Mode is complete: `;;l`, `;;w`, and `;;r` produce
  a signal-name slot; signal template activation reuses the existing Slot Mode
  session path; signal-name editing preserves packed/unpacked dimensions and
  final Tab exits before the semicolon; parameter template behavior is
  preserved.
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
  leaves the replacement range selected, and reports validation failures without
  text mutation. Alternate command `clear_rhs` is registered through the
  existing alternate-command service and dispatcher.
- G3.3 Clear-RHS Slot Mode Integration is complete:
  successful `clear_rhs` execution starts Slot Mode from the
  `RtlClearAssignmentRhsReport` `rhsN` template-slot metadata while preserving
  one undoable text replacement. G3.2 failure behavior is unchanged; Slot Mode
  start, slot editing, Tab advance, final Tab exit, undo restore, and
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
  Slot Mode; alternate commands are documented as `AlternateCommandService`
  names dispatched through existing file/editor APIs; COM Mode and Global
  Control are documented as separate command surfaces with separate registries,
  services, and coordinators.
- G6.1 conflict boundaries are documented: do not revive `;:cmd`, keep `;cmd`
  semantic, keep `;;cmd` template-only, keep COM editor-local, and keep Global
  Control app/workspace/global.
- Focused verification for G6.1: documentation inspection plus
  `git diff --check`.
- G6.2 User Template Storage/Query Model is complete:
  `UserTemplateService` now owns validation, persistence, reload, exact-token
  query, add/update, remove, and clear operations for compact `;;` user
  template records. Records persist under `QSettings` `userTemplates/items`,
  preserve selection and `CodeTemplateSlotList` metadata, and expose
  `CodeTemplateItem`-compatible query results. Built-in `CodeTemplateService`
  behavior remains separate and unchanged; arbitrary user template tokens are
  not wired into the inline `;;cmd` popup in this milestone.
- Focused verification for G6.2: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `completion_test` passed directly with 680 checks and 0 failures; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
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
  user-template service; tests can inject an ini-backed service. Compact user
  `;;token ` commands are recognized as `CodeTemplate` intent without changing
  `;cmd`, COM Mode, Global Control, or custom abbreviation namespaces. Built-in
  templates keep priority, template completion results merge built-in and exact
  user-template records, and activation preserves insert text, primary
  selection, and `CodeTemplateSlotList` metadata through the existing
  `EditorCompletionWorkflow` / `MyCodeEditor` Slot Mode path.
- Focused verification for G6.4: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
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

- G10.1 State Transition Trigger Gating For ns/next_state is complete:
  `StateTransitionTriggerService` owns the first exact-name trigger policy for
  editor source-symbol actions. `EditorSourceNavigationQuery` enables and
  dispatches `Show State Transition Graph` only for selected `ns` and
  `next_state`; selected `cs`, selected `current_state`, ordinary symbols, and
  missing module context do not dispatch. Accepted requests route through
  `EditorCoordinator` and `SemanticPanelRefreshCoordinator` to the existing RTL
  Insights FSM graph path. No new transition extraction, report shaping, graph
  rendering, workspace scan, or UI-side Slang work was added.
- Focused verification for G10.1: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

Completion criteria for G10.2:

- add service-owned State Transition Graph report shaping for accepted
  next-state trigger requests
- preserve G10.1 gating: only selected `ns` and `next_state` can request the
  graph entry
- keep extraction/report logic out of UI code; UI must consume report data only
- use existing semantic/index or Tree-sitter service capabilities, not UI
  workspace scans or UI-side Slang execution
- do not add broad graph UI rendering beyond the report milestone
- `readme.md`, `plan.md`, and `goal.md` are updated
- appropriate focused verification passes
- milestone commit is pushed

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
  (complete: `clear_rhs` alternate command applies the service report in one
  undoable edit block and preserves failure reasons)
- G3.3 Connect clear-RHS output to slot mode.
  (complete: successful `clear_rhs` starts Slot Mode on `rhsN` fill slots)

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

Goal: show a state transition graph only when the selected variable is a
next-state variable.

Rules:

- Selected `ns` triggers.
- Selected `next_state` triggers.
- Selected `cs` does not trigger.
- Selected `current_state` does not trigger.

Milestones:

- G10.1 Trigger gating and tests for allowed/disallowed names.
  (complete: service-owned exact-name gate plus editor source-symbol action)
- G10.2 Service-owned transition extraction/report.
- G10.3 Graph UI rendering and navigation evidence.

## Track 11: Module Block Diagram

Goal: from a selected module name, show a module-only block diagram rooted at
that module.

Rules:

- show module/interface instance and wrapping relationships only
- do not show signals
- clicking a module block jumps to the module definition

Milestones:

- G11.1 Service report for module containment from selected module.
- G11.2 Module-only block diagram rendering.
- G11.3 Click navigation to module definitions.

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
- HWA.2 Run safe verification or compile relevant targets.
- HWA.3 Update docs with confirmed status and open gaps.

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
