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

- The app is a lightweight SystemVerilog editor/workspace browser with tabs,
  workspace file navigation, semantic indexing, diagnostics, completion,
  jump-to-definition, references/relationships, and focused RTL visual helpers.
- Tree-sitter owns low-latency editor syntax and live editor structure. Slang
  owns semantic facts. `SemanticIndexSnapshot` is the UI-facing query truth.
- COM Mode is an editor-local command layer, not a Vim clone. It has INSERT and
  COM states, enters with Esc when an editor tab is open, exits with backtick,
  and shows an app-level command strip. Current g-domain commands are `gm`,
  `g<num><Enter>`, `gp`, `gpk`, `gpa`, `gpo`, `gsi`, `gsd`, `gii`, `gac`,
  `gpi`, `ge`, and `gef`. Existing fixed commands, prefixes, and the
  module-relative line command are described by shared `commodecommandregistry`
  metadata. The command strip renders registry-backed hints for prefixes and
  module-relative line buffers. Registry validation owns duplicate, executable
  prefix conflict, and malformed prefix reasons, and COM command failures use
  centralized registry-backed messages where practical.
- Ctrl+Space opens Global Control as a domain-first surface. Current root
  domains are `ow` and `fd`; displayed commands are `ow <num>`, `ow r`,
  `fd r`, and `fd s`.
- `;cmd` remains semantic command completion. `;;cmd` remains template
  expansion. COM Mode is for editor-local command actions. Slot Mode is the
  post-template editor-local fill flow; it is active for `;;p` / `;;lp`
  parameter templates and `;;l` / `;;w` / `;;r` signal declaration templates,
  and not yet active for other template families.
- Batch RTL editing has a first editor-local action: alternate command
  `clear_rhs` clears RHS expressions in the current selection for supported
  assignment statements, then starts Slot Mode on the cleared RHS fill points.
- Fold Region and Fold Shelf are available through Global Control. Fold Shelf
  now has a service/model-owned persistence baseline and explicit cross-file
  restore flow plus basic rename, search, and stale/consumed cleanup
  management.
- Signal Kernel Graph exists as a signal-centric exploration graph.
  Service/report-layer high-fanout grouping metadata is available for dense
  input/output sides; UI collapse/expand, filtering, and graph search remain
  pending.
- Wave Preview exists as a code-understanding sketch, not a simulator.
- Formatter support exists as conservative editor formatting. Current daily
  editor action inventory: `Ctrl+F` opens Find; formatter document/selection
  actions live in the editor context menu; line comment actions are available
  through `Ctrl+/`, `Ctrl+Shift+/`, editor context-menu actions, and alternate
  commands `comment` / `uncomment`; line indent actions are available through
  `Ctrl+]`, `Ctrl+[`, editor context-menu actions, and alternate commands
  `indent` / `unindent`; replace is available through `Ctrl+H`, editor
  context-menu action, and alternate command `replace`; goto line is available
  through `Ctrl+G`, editor context-menu action, and alternate command
  `goto_line`. The first-pass daily editor operation entry points are complete.

## Huge Workspace Status

Huge Workspace work is in status-audit mode only. Do not add new Huge Workspace
UX features as part of the current long-term plan.

Current low-level strategies to preserve and verify:

- current/open/dirty-open file priority in workspace analysis planning
- analysis bands for current, dirty-open, open, background, and unbanded data
- stale request coalescing and expiration
- cancellation boundaries for symbol and relationship work
- staged symbol publication before full background completion where safe
- Activity/status telemetry for analysis progress, request restarts, bands,
  cancellation, symbol publication, diagnostics, and relationship timing
- Release-oriented performance measurements for `huge_prj`

Known reference points captured before this cleanup:

- `test_sv/huge_prj`: 428 HDL files, about 18.59 MiB.
- Release relationship analysis reference: about 3.891s in the focused harness.
- Release async symbol publication reference: about 5.096s in the focused
  harness after the first-publication native-store scan fix.

## Workspace Workflow Status

The current long-term workspace workflow scope is limited to ignored directories
and restoring Global Control `ow r` for recent workspaces.

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
  directories so clearing ignores can reveal previously hidden files. No broad
  ignored-directory UI exists yet.

## Current Architecture

- `ProjectModel` owns workspace root, file list, include dirs, defines, top, and
  ignored paths.
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
  Slot state must not live in UI panels or semantic services.
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
  for dense input/output sides. UI widgets may render that metadata later, but
  grouping policy belongs in the service/report layer.

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
- User template storage/query is service-owned by `UserTemplateService`. It
  persists validated `;;` template records in `QSettings`, exposes
  `CodeTemplateItem`-compatible catalog and exact-token query results, and is
  consumed by `CompletionService` for inline `;;cmd` template completion.
  Activation preserves template slot metadata and starts Slot Mode through the
  existing `EditorCompletionWorkflow` path when a user template carries slots.
- Custom abbreviation resolution is service-owned by
  `CustomAbbreviationService`. It persists validated compact abbreviations in
  `QSettings`, resolves them to existing `;cmd` semantic command tokens or
  `;;cmd` template command tokens, exposes prefix and intent-scoped exact
  queries, and does not own UI rendering or semantic lookup policy.
- Alternate commands such as `replace`, `goto_line`, `comment`, `uncomment`,
  `indent`, `unindent`, and `clear_rhs` are named editor/app actions owned by
  `AlternateCommandService`. `EditorCompletionWorkflow` may emit an alternate
  command request, and `FileCommandCoordinator` maps known actions to the
  existing editor or file APIs.
- COM Mode is editor-local and registry-backed. `commodecommandregistry` owns
  command metadata and conflict validation; `ComModeService` owns picker/query
  reports; `ComModeCoordinator` owns mode state, strip text, picker UI, and
  navigation dispatch.
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
- Tab moves to the next slot; Shift+Tab moves to the previous slot. Tab on the
  last slot completes Slot Mode and leaves the cursor at the final slot end.
- Esc cancels Slot Mode only: inserted text and user edits stay in the document,
  slot highlights clear, and normal editor input resumes.
- Cursor movement outside the active slot/session, document edits that make slot
  ranges stale, tab switch/close, COM Mode entry, or Global Control entry exits
  Slot Mode without rollback.
- Slot Mode is editor-local text state. It must not run Slang, scan the
  workspace, or add semantic policy.
- Parameter template slots are ordered as name then value. Editing the name
  shifts the value slot; Tab reaches the value; final Tab exits before the
  semicolon.
- Signal declaration templates use a single name slot. Editing the signal name
  keeps packed/unpacked dimensions intact; final Tab exits before the semicolon.

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
- Editor command wiring is available through `MyCodeEditor` and alternate
  command `clear_rhs`. The command applies the report in one undoable edit
  block and preserves failure reasons in editor status feedback.
- Slot Mode entry is active for this report. The first RHS slot is selected
  after replacement; Tab/Shift+Tab/final Tab/Esc reuse the existing Slot Mode
  state.

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
10. State transition graph for selected `ns` / `next_state` only.
11. Module block diagram for selected module names only.

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
