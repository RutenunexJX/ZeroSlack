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
  domains are `ow` and `fd`; displayed commands are `ow <num>`, `fd r`, and
  `fd s`.
- `;cmd` remains semantic command completion. `;;cmd` remains template
  expansion. COM Mode is for editor-local command actions. Slot Mode is the
  post-template editor-local fill flow; it is active for `;;p` / `;;lp`
  parameter templates and `;;l` / `;;w` / `;;r` signal declaration templates,
  and not yet active for other template families.
- Batch RTL editing has a first editor-local action: alternate command
  `clear_rhs` clears RHS expressions in the current selection for supported
  assignment statements, then starts Slot Mode on the cleared RHS fill points.
- Fold Region and Fold Shelf are available through Global Control. Fold Shelf
  is not yet the long-term persistent shelf system.
- Signal Kernel Graph exists as a signal-centric exploration graph. Dense
  fanout still needs grouping, filtering, and search.
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

## Slot Mode Contract

Slot Mode is the `;;cmd` follow-up state for filling editable points in an
inserted template. It currently applies to `;;p` / `;;lp` parameter declaration
templates and `;;l` / `;;w` / `;;r` signal declaration templates. Other
template families keep current insertion behavior until an implementation
milestone explicitly enables slots for that family.

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
