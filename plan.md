# ZeroSlack Development Plan

Use `readme.md` for the current handoff baseline and `goal.md` for the active
long-term goal model. This file defines how to execute work.

## Current Identity

ZeroSlack is a SystemVerilog code editor and workspace browser. It should help
users read, navigate, edit, and understand RTL projects without turning UI code
into an analyzer.

## Non-Negotiable Architecture

- Use Qt 6, CMake, and Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old
  Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship
  analysis, or long-lived scattered perflog probes.
- Do not revive `;:cmd` or introduce active `;:` behavior.
- UI code must not run Slang, scan workspace files, or own semantic policy.
- New semantic/RTL behavior must flow through:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

- Tree-sitter may be used for editor-local syntax structure and low-latency
  insertion/navigation targets.
- Slang-backed semantic truth must enter UI features through semantic records,
  snapshots, query services, or feature services.
- Keep command routing separate:
  - Global Control: app/workspace/global domains.
  - COM Mode: current-editor local commands.
  - `;cmd`: semantic command completion.
  - `;;cmd`: code template expansion.

## Worktree Policy

- Start every turn by checking the current worktree state.
- Never revert user changes unless explicitly asked.
- If unrelated dirty files exist, leave them alone and stage only the files for
  the current milestone.
- Documentation cleanup may be committed separately from implementation changes.
- Do not push unless explicitly asked or unless the active goal milestone rules
  for the current milestone explicitly require it.

## Active Long-Term Scope

Only these long-term tracks are allowed. Do not add extra tracks without an
explicit user request.

### 1. Editor Daily Operations Completion

Goal: make common editing actions easy to reach and consistent.

Allowed scope:

- replace
- goto
- comment
- uncomment
- indent
- unindent
- clear entry points for existing partial command/action paths

First milestones:

- M1.1 inventory existing actions and shortcuts, document gaps, no behavior
  change
  (complete: baseline captured for find, formatter, alternate commands, and
  missing daily action entry points)
- M1.2 add reliable entry points for one action family, with focused editor
  tests
  (complete: comment/uncomment are available through `Ctrl+/`,
  `Ctrl+Shift+/`, editor context-menu actions, and alternate commands)
- M1.3 repeat per action family until all listed actions have usable entries
  (complete: indent/unindent are available through `Ctrl+]`, `Ctrl+[`, editor
  context-menu actions, and alternate commands)
- M1.4 add reliable replace and goto line entry points, with focused editor
  tests
  (complete: replace uses `Ctrl+H`, context menu, and `replace`; goto line uses
  `Ctrl+G`, context menu, and `goto_line`)

### 2. Slot Mode After Template Insertion

Goal: after `;;cmd` template insertion, users can fill multiple editable points
without manually moving the cursor.

Allowed scope:

- editable slots in inserted templates
- jump between slots
- exit/cancel behavior
- integration with `;;cmd`

First milestones:

- M2.1 define slot model and editor state without changing templates
- M2.2 enable slots for the parameter declaration template family
- M2.3 expand slots to existing signal declaration template families

M2.1 design contract:

- Slot metadata belongs to template expansion data as ranges relative to the
  inserted text. Existing `selectionStart` / `selectionLength` is the
  compatibility path for a single primary slot.
- `EditorCompletionWorkflow` applies template insertion and starts Slot Mode
  after activation; `MyCodeEditor` editor state owns active slot ranges,
  highlight rendering, navigation, and stale-session invalidation.
- Tab advances, Shift+Tab goes backward, Tab on the last slot completes, and
  Esc exits Slot Mode without reverting inserted text.
- Slot Mode exits when the cursor leaves the session, the document edit stream
  makes slot ranges unsafe, the tab changes/closes, COM Mode starts, or Global
  Control starts.
- First implementation target is `;;p` / `;;lp` because parameter declarations
  naturally have at least name and value fill points.
- Focused verification cases for implementation: activation selects the first
  slot, Tab advances, Shift+Tab returns, Tab on the last slot exits, Esc exits
  without reverting inserted text, editing a slot keeps later ranges correct,
  moving the cursor outside the session exits, and undo that removes the
  inserted template clears the session safely.

M2.2 implementation status:

- Complete: `;;p` and `;;lp` produce name/value slot metadata.
- Complete: parameter template activation starts editor-local Slot Mode through
  the completion workflow.
- Complete: Slot Mode owns ordered ranges, highlights non-empty slots, supports
  Tab, Shift+Tab, final Tab completion, Esc cancel, name-edit range shifting,
  and cursor-outside stale exit.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M2.3 implementation status:

- Complete: `;;l`, `;;w`, and `;;r` produce a signal-name slot.
- Complete: signal template activation reuses the existing editor-local Slot
  Mode path from parameter templates.
- Complete: signal name editing preserves packed/unpacked dimensions and final
  Tab exits before the semicolon.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

### 3. Batch RTL Edit Actions

Goal: support focused RTL batch edits that reduce repetitive cleanup.

Allowed scope:

- first target: clear assignment RHS and create fill slots
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`
- slot flow after the batch edit

First milestones:

- M3.1 service/report design for selected assignment cleanup
  (complete: `RtlBatchEditService` plans selected-text RHS clearing without
  mutating editor text)
- M3.2 editor command for clear-RHS on selected assignments
  (complete: alternate command `clear_rhs` applies the report in one undoable
  edit block)
- M3.3 connect result to slot mode
  (complete: successful clear-RHS reports start Slot Mode on `rhsN` fill slots)

M3.1 implementation status:

- Complete: `RtlBatchEditService::planClearAssignmentRhs` returns a
  `RtlClearAssignmentRhsReport` containing replacement text, edit metadata,
  zero-length `rhsN` template slots, and validation failure reasons.
- Complete: the service supports complete selected blocking assignments,
  nonblocking assignments, and continuous `assign` statements, while preserving
  surrounding whitespace/comments.
- Complete: unsupported selections fail without mutation for empty selection,
  incomplete statements, declaration initializers, control-flow statements,
  macro statements, ambiguous top-level assignments, unmatched delimiters,
  unterminated strings, and unterminated comments.
- Complete: focused verification covers ready multi-assignment cleanup, slot
  metadata, document-relative edit offsets, declaration rejection, incomplete
  statement rejection, and empty selection reporting.
- Not done in M3.1: editor command wiring, undo block application, and Slot Mode
  entry after replacement. Those stay in M3.2/M3.3.
- The future implementation must remain editor-local or service-owned and must
  not scan the workspace, run Slang from UI, or use regex as SystemVerilog
  semantic analysis.

M3.2 implementation status:

- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` routes the current
  selection through `RtlBatchEditService`.
- Complete: successful reports are applied in one undoable edit block, and the
  replacement range remains selected.
- Complete: validation failures are surfaced through editor status feedback
  without mutating text.
- Complete: alternate command `clear_rhs` is registered and dispatched through
  the existing alternate-command path.
- Not done in M3.2: Slot Mode entry after replacement. That stays in M3.3.
- Verification: Release `completion_test` covers alternate command mapping,
  selection application, undo restore, and declaration rejection; Release
  `completion_test` and `gui_smoke_test` targets compile/link.

M3.3 implementation status:

- Complete: successful `clear_rhs` execution starts Slot Mode using the
  `RtlClearAssignmentRhsReport` template-slot metadata.
- Complete: the text replacement itself remains one undoable edit block.
- Complete: G3.2 failure behavior is unchanged: unsupported selections do not
  mutate text and surface service-owned failure reasons.
- Complete: Tab advances between cleared RHS slots, final Tab exits Slot Mode,
  and slot edits shift later RHS ranges through the existing Slot Mode state.
- Verification: Release `completion_test` covers Slot Mode start, slot editing,
  Tab advance, final Tab exit, undo restore, and declaration rejection; Release
  `completion_test` and `gui_smoke_test` targets compile/link.

### 4. COM Mode Framework Completion

Goal: improve COM Mode quality before adding more commands.

Allowed scope:

- unified command registry
- help and hints
- conflict checks
- failure reason display
- command metadata
- existing g-domain preservation

First milestones:

- M4.1 introduce registry metadata for existing commands and prefixes
  (complete: `commodecommandregistry` now owns existing command metadata)
- M4.2 add help/hint rendering in the command strip or picker
  (complete: command strip renders registry-backed prefix and line hints)
- M4.3 centralize conflict validation and failure messages
  (complete: registry validates conflicts and supplies COM failure messages)

### 5. Limited Workspace Workflow Additions

Goal: improve workspace workflow only in the explicitly allowed ways.

Allowed scope:

- ignored directories
- restore Global Control `ow r` for recent workspaces

Not in scope:

- session restore
- include dirs/defines configuration UI
- recent files
- broad workspace UX expansion

First milestones:

- M5.1 document and audit current workspace open/recent behavior
  (complete: owner classes, existing recent-workspace persistence, current
  Global Control `ow` behavior, hidden `ow r` compatibility, and ignored-path
  baseline are documented)
- M5.2 add ignored-directory model/service path
  (complete: `WorkspaceIgnoreService` and `WorkspaceManager` now provide the
  workspace-owned path for active ignored directories)
- M5.3 restore `ow r` as a displayed Global Control child command
  (complete: `ow r` is displayed in the `ow` domain and routes to the existing
  recent-workspaces dialog)

M5.1 audit status:

- Complete: `WorkspaceManager` owns open/close/switch, aliases, cached scanned
  files, directory scan lifecycle, file watching, and recent-workspace storage.
- Complete: `WorkspaceEntry` already caches scanned files and `scanComplete`;
  switching to a scanned workspace restores cached files without a new scan.
- Complete: recent workspaces are persisted with `QSettings`, deduplicated,
  capped at 20, and exposed through `recentWorkspaceEntries()`.
- Complete: Global Control currently shows root domains only, and the `ow`
  domain displays `ow 1`, `ow 2`, and parameterized `ow <num>` behavior.
- Complete: `ow r` is hidden from `GlobalControlService`, but `MainWindow`
  still has an internal `ow r` handler and recent-workspaces dialog.
- Complete: `ProjectModel` already filters `ignoredPaths`, but no workspace
  workflow/service path sets ignored directories yet.
- Verification: documentation inspection plus `git diff --check`.

M5.2 implementation status:

- Complete: `WorkspaceIgnoreService` validates ignored-directory requests for a
  workspace, normalizes relative/absolute paths, deduplicates them, and rejects
  the workspace root, outside paths, and existing non-directory paths.
- Complete: `WorkspaceManager::setIgnoredDirectories()` applies the normalized
  ignored directories through `ProjectModel::setIgnoredPaths()`, refreshes
  current file lists from the model, updates file watching, and emits existing
  workspace/file-list signals.
- Complete: each open `WorkspaceEntry` stores its ignored directories; cached
  workspace restore reapplies raw scanned files before ignored paths, so hidden
  files reappear after clearing ignores.
- Not done in M5.2: ignored-directory UI, `ow r`, session restore, include
  dirs/defines UI, recent files, or broader workspace workflow changes.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `completion_test` passed
  directly with 669 checks and 0 failures; `ctest -R "^completion_test$"`
  passed.

M5.3 implementation status:

- Complete: `GlobalControlService` now displays `ow r` as an `ow` domain child
  command next to `ow 1` and `ow 2`.
- Complete: exact `ow r` queries return the recent-workspaces command instead
  of the numeric count hint.
- Complete: dispatch continues through the existing `MainWindow` recent
  workspaces dialog path; no new session restore, include dirs/defines UI,
  recent files, or broad workspace UX was added.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `completion_test` passed
  directly with 672 checks and 0 failures; `ctest -R "^completion_test$"`
  passed. `gui_smoke_test` was not launched.

### 6. Completion / `;cmd` / `;;cmd`

Goal: make completion commands customizable and clearly separated from COM Mode.

Allowed scope:

- user templates
- custom abbreviations
- slot mode integration
- responsibility boundary between `;cmd`, `;;cmd`, and COM Mode

First milestones:

- M6.1 document current command/template responsibilities
  (complete: `;cmd`, `;;cmd`, alternate commands, COM Mode, and Global Control
  ownership and conflict boundaries are documented)
- M6.2 add user-template storage/query model
  (complete: `UserTemplateService` persists and queries validated user template
  records as `CodeTemplateItem`-compatible data)
- M6.3 add custom abbreviation resolution
  (complete: `CustomAbbreviationService` persists and resolves compact aliases
  for existing `;cmd` and `;;cmd` tokens)
- M6.4 integrate user templates with slot mode
  (complete: service-owned user templates enter the `;;cmd` completion path and
  preserve slot metadata through activation)

M6.1 audit status:

- Complete: `;cmd` is documented as inline semantic command completion owned by
  `InlineCommandMode`, `CompletionCommandMode`, `CompletionService`, and
  `CompletionSemanticQuery` over semantic snapshot data.
- Complete: `;;cmd` is documented as inline template expansion owned by
  `CodeTemplateService`, with `EditorCompletionWorkflow` applying insertion and
  starting Slot Mode when template slot metadata exists.
- Complete: alternate commands are documented as daily editor/app action names
  owned by `AlternateCommandService` and dispatched by `FileCommandCoordinator`
  or existing editor/file APIs.
- Complete: COM Mode and Global Control are documented as separate command
  surfaces with separate registries/services/coordinators.
- Complete: conflict boundaries are documented: do not revive `;:cmd`, keep
  `;cmd` semantic, keep `;;cmd` template-only, keep COM editor-local, and keep
  Global Control app/workspace/global.
- Verification: documentation inspection plus `git diff --check`.

M6.2 implementation constraints:

- User-template storage/query must extend service-owned template data, not UI
  widgets.
- Built-in template behavior and current Slot Mode metadata must remain
  compatible.
- `;cmd`, COM Mode, and Global Control namespaces must not be changed by the
  user-template storage milestone.

M6.2 implementation status:

- Complete: `UserTemplateService` owns user template validation, persistence,
  reload, exact-token query, add/update, remove, and clear operations.
- Complete: user templates are stored under `QSettings`
  `userTemplates/items`, with injectable ini-file storage for focused tests.
- Complete: user template records use compact `;;` command tokens, require
  unique ids and non-empty template text, reject invalid selection/slot ranges,
  and preserve `CodeTemplateSlotList` metadata through serialization.
- Complete: query results are `CodeTemplateItem`-compatible, so future template
  popup and Slot Mode work can consume the same data shape.
- Complete: built-in `CodeTemplateService` behavior remains separate and
  unchanged in this milestone; arbitrary user template tokens are not wired into
  the inline `;;cmd` popup yet.
- Not done in M6.2: user-template UI, custom abbreviations, changes to
  `;cmd`, COM Mode, Global Control, or broader Slot Mode activation.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `completion_test` passed
  directly with 680 checks and 0 failures; `ctest -R "^completion_test$"`
  passed. `gui_smoke_test` was not launched.

M6.3 implementation constraints:

- Custom abbreviation storage/query must stay in a service layer, not UI
  widgets.
- Custom abbreviations may resolve only to existing compact `;cmd` semantic
  command tokens or `;;cmd` template command tokens.
- `;:cmd`, COM Mode, and Global Control namespaces must remain unchanged.
- Built-in command/template behavior and current Slot Mode activation must
  remain compatible.

M6.3 implementation status:

- Complete: `CustomAbbreviationService` owns validation, persistence, reload,
  prefix query, intent-scoped query, exact resolution, add/update, remove, and
  clear operations for compact custom abbreviations.
- Complete: custom abbreviations are stored under `QSettings`
  `customAbbreviations/items`, with injectable ini-file storage for focused
  tests.
- Complete: records reject empty or non-compact abbreviations, command-surface
  marker aliases, duplicate aliases, invalid command tokens, and reserved
  `;:` action tokens.
- Complete: resolution returns the target inline command intent and command
  token without scanning workspaces, running Slang, or touching UI rendering.
- Complete: built-in `;cmd` and `;;cmd` behavior remains unchanged; custom
  abbreviations are not COM Mode or Global Control commands.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M6.4 implementation constraints:

- User template integration must consume `UserTemplateService` from
  `CompletionService`, not from UI widgets.
- User template command recognition must stay inside the `;;cmd` template
  namespace and must not change `;cmd`, COM Mode, Global Control, or custom
  abbreviation behavior.
- Built-in `CodeTemplateService` templates must remain first-class and
  compatible.
- User template activation must preserve `CodeTemplateSlotList` and use the
  existing `EditorCompletionWorkflow` / `MyCodeEditor` Slot Mode path.

M6.4 implementation status:

- Complete: `CompletionService` can consume an injected `UserTemplateService`
  for tests and falls back to the singleton service in normal use.
- Complete: compact user `;;token ` commands are recognized as
  `InlineCommandIntent::CodeTemplate` through service-owned user template
  records, while built-in inline descriptors keep priority.
- Complete: template completion results merge built-in `CodeTemplateService`
  items with exact-token `UserTemplateService` items.
- Complete: user template activation carries insert text, primary selection,
  and `CodeTemplateSlotList` metadata through the existing completion
  activation and Slot Mode path.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

### 7. Fold / Fold Shelf

Goal: make Fold Shelf durable and maintainable.

Allowed scope:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

First milestones:

- M7.1 define shelf persistence model
  (complete: owner inventory, versioned item schema, service/model boundary,
  and G7.2 same-file restore requirements are documented)
- M7.2 persist and restore same-file items
  (complete: `FoldShelfPersistenceService` plus model mutation persistence and
  same-file stale restore handling)
- M7.3 cross-file restore
  (complete: `FoldShelfRestoreService` restores selected items into the active
  editor through the existing insertion path and persists consume/remove/stale
  outcomes)
- M7.4 rename/search/clean management actions
  (complete: `FoldBlockShelfModel` owns rename, query/filter, and
  stale/consumed cleanup mutations; the panel provides only management UI)

M7.1 inventory:

- `FoldBlockShelfModel` owns the current in-memory `FoldShelfItem` list and
  item lifecycle: add, consume, remove, and clear.
- `FoldShelfItem` currently carries id, alias, text, source file/module,
  source line range, line count, origin kind, consumed, and stale fields.
- `encodeFoldShelfItem` / `decodeFoldShelfItem` serialize the current item
  shape for drag/drop MIME transport, not durable storage ownership.
- `FoldBlockShelfPanel` renders model items, previews text, accepts editor
  fold-block drops, initiates shelf-item drags, and handles delete/restore
  prompts for moved unconsumed items.
- `EditorFoldingController` owns shelf mode, custom fold extraction,
  move/delete source behavior, shelf-item insertion, and custom fold marker
  editing.
- `MainWindow` owns the dock/model wiring, Global Control `fd s` entry, panel
  visibility, mode highlighting, and restore command dispatch through
  `TabManager` plus the editor insertion API.

M7.1 persistence schema:

- Use a versioned durable root such as `foldShelf/v1/items`, scoped by
  normalized workspace root when available.
- Persist only model/service data: `id`, `alias`, `text`, `sourceFile`,
  `sourceModule`, `sourceStartLine`, `sourceEndLine`, `lineCount`,
  `originKind`, `consumed`, and `stale`.
- Keep `originKind` limited to `moved` and `copied`.
- Store `sourceFile` normalized; UI may derive relative display text but must
  not be the persistence owner.
- Treat MIME JSON as drag/drop transport. Durable load/save may share fields
  but must own validation, migration, and write timing separately.

M7.1 ownership and G7.2 requirements:

- Persistence policy belongs in `FoldBlockShelfModel` or a dedicated Fold
  Shelf persistence service. `FoldBlockShelfPanel` must remain a consumer.
- `MainWindow` may wire model/service lifetime and route restore actions, but
  must not hand-roll serialization.
- G7.2 should persist on model mutations, reload shelf items for the current
  workspace, restore same-file items through the existing editor insertion
  path, and remove/consume items only after successful insertion.
- G7.2 should mark missing/invalid source locations stale and leave cross-file
  relocation, rename, search, and cleanup for later milestones.
- Verification: source inspection of Fold Shelf owner classes plus
  `git diff --check`.

M7.2 implementation constraints:

- Persistence must be service/model-owned; `FoldBlockShelfPanel` must remain a
  consumer.
- Use the G7.1 `foldShelf/v1/items` schema and scope items by normalized
  workspace root.
- Persist add, consume, stale mark, remove, and clear mutations.
- Restore same-file items through the existing `MainWindow` / `TabManager` /
  `MyCodeEditor` insertion path.
- Remove or consume a persisted item only after successful insertion.
- Mark unavailable source file, failed file open, or failed source-location
  insertion as stale.
- Do not add cross-file relocation, rename, search, or clean management.

M7.2 implementation status:

- Complete: `FoldShelfPersistenceService` owns versioned QSettings-backed
  load/save under `foldShelf/v1/workspaces/<scope>/items`, with workspace-root
  hashing and normalized source paths.
- Complete: `FoldBlockShelfModel` can attach a persistence service and
  workspace root, reload persisted items on root changes, and persist add,
  consume, stale mark, remove, and clear mutations.
- Complete: `MainWindow` wires the singleton persistence service into the model
  and updates the model workspace scope on workspace activation/close.
- Complete: restore failure for missing source file, failed open, or failed
  source insertion marks the item stale; successful restore removes the item
  after insertion.
- Complete: `FoldBlockShelfPanel` remains storage-policy free.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M7.3 implementation constraints:

- Cross-file restore must be service/model-owned; `FoldBlockShelfPanel` remains
  a consumer that emits restore requests.
- Restore must target the active editor and use the existing fold-shelf editor
  insertion API instead of UI-owned text editing.
- Persisted items are consumed or removed only after successful insertion.
- Missing active editor or failed insertion marks the item stale and reports a
  clear failure reason.
- Do not add rename, search, or clean management in M7.3.

M7.3 implementation status:

- Complete: `FoldShelfRestoreService` owns active-editor restore reports,
  validates model/item/editor availability, inserts through the existing
  editor fold-shelf insertion path, and records target file/line details.
- Complete: successful restore can consume or remove the item only after
  insertion succeeds; failed target availability or insertion marks the item
  stale with a service-owned failure reason.
- Complete: `FoldBlockShelfPanel` adds a restore request UI action while
  staying storage-policy free.
- Complete: `MainWindow` wires the panel request to the active editor/cursor
  line and reports restore success or failure through existing status/log
  paths.
- Not done in M7.3: rename, search, clean management, broader shelf UX, or
  semantic/workspace analysis changes.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M7.4 implementation constraints:

- Rename, search/filter, and clean management must stay model/service-owned.
- `FoldBlockShelfPanel` may render controls and route user requests, but it
  must not own persistence policy, scan workspaces, or inspect editor text.
- Clean management is limited to explicit removal of stale or consumed shelf
  items.
- Do not add broad Fold Shelf UX beyond rename, search/filter, and clean.

M7.4 implementation status:

- Complete: `FoldBlockShelfModel::renameItem` trims aliases, rejects blank
  aliases, persists successful changes, and emits the existing change signal.
- Complete: `FoldBlockShelfModel::itemsMatching` filters against shelf item
  data only: id, alias, source file, source module, fold text, origin kind,
  and stale/consumed state.
- Complete: `FoldBlockShelfModel::removeConsumedOrStaleItems` removes only
  stale or consumed items, persists the mutation, and reports the removal
  count.
- Complete: `FoldBlockShelfPanel` adds a search field plus Rename and
  Clean Stale/Consumed buttons while remaining a model consumer.
- Not done in M7.4: broader shelf UX, editor semantic behavior, workspace
  scanning, or unrelated Fold Shelf workflow.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

### 8. Signal Kernel Graph

Goal: make dense signal graphs usable.

Allowed scope:

- high fanout collapse/grouping
- filtering
- in-graph search

First milestones:

- M8.1 fanout grouping model in service/report layer
  (complete: `SignalKernelGraphReport` now carries high-fanout grouping
  metadata without changing raw nodes/edges)
- M8.2 UI collapse/expand for grouped fanout
  (complete: `SignalKernelGraphPanelCoordinator` renders collapsible fanout
  group summary/header items while preserving raw visible-node actions)
- M8.3 filtering and in-graph search
  (complete: panel-local input/output/cross-module filters and graph search
  consume existing report/node data while preserving fanout collapse behavior)

M8.1 implementation constraints:

- Fanout grouping policy belongs in `SignalKernelGraphService` and report data,
  not in UI widgets.
- Preserve existing `inputs`, `outputs`, and `edges` so non-grouped rendering
  remains unchanged.
- Do not add collapse/expand UI, filtering UI, or graph search in M8.1.

M8.1 implementation status:

- Complete: `SignalKernelGraphFanoutGroup` records group id, role, lane,
  module, display name, grouped node ids, per-group count, total side count,
  cross-module state, and high-fanout state.
- Complete: `SignalKernelGraphReport` exposes the service threshold plus input
  and output fanout group lists.
- Complete: `SignalKernelGraphService` emits grouping metadata only when a
  graph side reaches the service-owned fanout threshold; original nodes, edges,
  and module groups are preserved.
- Complete: focused synthetic graph coverage verifies non-dense graphs stay
  ungrouped and dense output fanout reports group metadata.
- Verification: `git diff --check`; Release `relationship_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^relationship_test$"`
  passed. `gui_smoke_test` was not launched.

M8.2 implementation constraints:

- Use the G8.1 `SignalKernelGraphReport` fanout group metadata; do not move
  grouping policy into the panel.
- Keep collapse/expand state in `SignalKernelGraphPanelCoordinator`.
- Preserve visible raw node preview, navigation, and rebase handlers.
- Do not add filtering UI or graph search in M8.2.

M8.2 implementation status:

- Complete: high-fanout groups default to collapsed the first time a group key
  appears in the panel.
- Complete: collapsed groups hide their raw nodes, render a summary item, and
  route grouped edges through the summary while deduplicating only collapsed
  group edges.
- Complete: expanded groups render the raw nodes with existing preview,
  navigation, and rebase handlers, plus a clickable group header to collapse
  again.
- Complete: collapse/expand state is panel-owned and is verified through
  offscreen `completion_test` coverage.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M8.3 implementation constraints:

- Use existing `SignalKernelGraphReport` and node fields; do not scan the
  workspace or run Slang from panel code.
- Keep filtering and graph-search state in `SignalKernelGraphPanelCoordinator`.
- Preserve G8.2 fanout group collapse/expand state, grouped edge routing, and
  raw visible-node preview/navigation/rebase handlers.

M8.3 implementation status:

- Complete: the panel exposes search plus input, output, and cross-module
  filters above the graph.
- Complete: filtering rebuilds the visible graph from existing report/node data
  and hides fanout groups when all of their members are filtered out.
- Complete: graph search highlights/focuses visible node matches and collapsed
  fanout-group matches without expanding groups implicitly.
- Complete: offscreen `completion_test` coverage verifies filters, collapsed
  group search, and expanded filtered search behavior.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

### 9. Wave Preview

Goal: show a waveform sketch for selected RTL context only.

Allowed scope:

- selected `always` block waveform sketch
- selected module waveform sketch
- no simulation

Not in scope:

- value simulation
- testbench execution
- waveform database import
- timing-accurate verification

First milestones:

- M9.1 selected-always entry and report refresh
- M9.2 selected-module entry and scoped report
- M9.3 UI polish for sketch readability

### 10. State Transition Graph

Goal: show state transition graph only for selected next-state variables.

Allowed scope:

- trigger only when selected identifier is `ns` or `next_state`
- selected `cs` / `current_state` must not show the graph
- service-owned transition extraction/report

First milestones:

- M10.1 trigger gating test for `ns` / `next_state` only
- M10.2 transition report service
- M10.3 graph UI rendering and navigation evidence

### 11. Module Block Diagram

Goal: show module containment as a block diagram from a selected module.

Allowed scope:

- selected module name as diagram top
- module/interface instance wrapping relationship only
- no signals
- click module block to jump to module definition

First milestones:

- M11.1 service report for module-instance containment from selected module
- M11.2 render module-only block diagram
- M11.3 click navigation to module definitions

## Huge Workspace Status Audit

Huge Workspace is not an active UX expansion track in this plan. Only audit and
document whether existing low-level strategies still work:

- current/open/dirty-open priority
- analysis-band metadata and query ordering
- stale request coalescing/expiration
- cancellation boundaries
- staged publication
- Activity/status telemetry
- Release performance references for `huge_prj`

Audit milestones:

- HWA.1 list current strategy owners and tests/harnesses
- HWA.2 run or compile appropriate verification based on environment safety
- HWA.3 update docs with confirmed status and gaps

## Milestone Definition Of Done

Every implementation milestone must be small, verifiable, and deliverable.

Before a milestone is complete:

- update `readme.md`, `plan.md`, and `goal.md`
- run verification appropriate to the change
- keep UI semantic policy out of UI code
- do not add functionality outside the allowed long-term scope
- commit and push the milestone when the user has authorized that goal flow

Documentation-only milestones:

- update the relevant docs
- run `git diff --check`
- commit/push only if the user explicitly asks or the active goal flow requires
  this documentation milestone to be published

## Verification Guidance

- Docs only: `git diff --check`.
- Editor-only compile-safe changes: build affected targets such as
  `completion_test`, `ts_doc_test`, or `gui_smoke_test` without launching known
  problematic executables.
- Shared semantic/scheduler/project/snapshot changes: full Ninja and full CTest
  unless executable tests are blocked by the user/environment.
- During CTest, if execution stalls or runs much longer than expected, inspect
  the desktop for a Windows application-error or memory-read dialog before
  treating the run as ordinary long-running test work.
- Visual panels: verify service reports first, then UI smoke if executable runs
  are safe.

## Commit Policy

- Keep commits coherent and architecture-oriented.
- Do not mix unrelated dirty code into documentation commits.
- Do not push unless explicitly asked or required by the active milestone goal
  flow.
- Prefer short commit messages that name the product/architecture change.
