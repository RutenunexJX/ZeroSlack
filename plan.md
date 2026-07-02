# ZeroSlack Development Plan

Use `readme.md` for the current handoff baseline and `goal.md` for the active
long-term goal model. This file defines how to execute work.

## Current Identity

ZeroSlack is a SystemVerilog code editor and workspace browser. It should help
users read, navigate, edit, and understand RTL projects without turning UI code
into an analyzer.

## Current Execution Baseline

- State Transition Graph real usability repair is complete for the
  full-feature/corpus audit. Structural FSM discovery now handles delayed
  `current <= next` update pairs (`#TP`, `# TP`, and `#(...)`) while preserving
  next-state-only graph triggering and current-state rejection.
- Verification baseline for this repair: Debug builds of `completion_test`,
  `gui_smoke_test`, `corpus_audit_test`, `full_feature_audit_test`,
  `jump_test`, and `relationship_test` passed. Full `corpus_audit_test`
  regenerated the JSON/Markdown corpus reports, and `full_feature_audit_test`
  regenerated the full feature reports.
- Current audit deltas: State Transition Graph is now 68 pass / 0 fail / 398
  skipped, up from 2 pass / 0 fail / 423 skipped. Signal Kernel Graph remains
  32 pass / 0 fail / 1960 skipped, and the full-feature matrix reports 23 pass
  / 0 known-issue.
- No implementation milestone is active. Start the next milestone only from a
  new explicit scoped request.
- The current completed baseline includes import-aware package member
  visibility for unqualified completion, definition, and hover; user template
  JSON usage actions; explicit header/include and package import commands;
  semantic `;m` module instantiation Slot Mode; Macro / Define semantics;
  Package Tools phase 1; References / Relationships workflow closure; and Fold
  Shelf persistence/restore/management.
- `;pk` remains an explicit import insertion command. Package member lookup is
  import-aware, and Package Tools remain package-file editing tools.

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
  (complete: baseline captured for find, formatter, shortcut/context-menu
  entries, and missing daily action entry points)
- M1.2 add reliable entry points for one action family, with focused editor
  tests
  (complete: comment/uncomment are available through `Ctrl+/`,
  `Ctrl+Shift+/`, and editor context-menu actions)
- M1.3 repeat per action family until all listed actions have usable entries
  (complete: indent/unindent are available through `Ctrl+]`, `Ctrl+[`, editor
  context-menu actions)
- M1.4 add reliable replace and goto line entry points, with focused editor
  tests
  (complete: replace uses `Ctrl+H` and context menu; goto line uses `Ctrl+G`
  and context menu)

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
- M2.4 make Slot Mode visible and cyclic
  (complete: all slots blink, active slot is emphasized, status reports
  `SLOT n/m`, and Tab / Shift+Tab wrap without implicit exit)

M2.1 design contract:

- Slot metadata belongs to template expansion data as ranges relative to the
  inserted text. Existing `selectionStart` / `selectionLength` is the
  compatibility path for a single primary slot.
- `EditorCompletionWorkflow` applies template insertion and starts Slot Mode
  after activation; `MyCodeEditor` editor state owns active slot ranges,
  highlight rendering, navigation, and stale-session invalidation.
- Tab advances, Shift+Tab goes backward, both directions wrap, and Esc exits
  Slot Mode without reverting inserted text. Tab / Shift+Tab never complete
  Slot Mode implicitly.
- Slot Mode exits when the cursor leaves the session, the document edit stream
  makes slot ranges unsafe, the tab changes/closes, or Global Control starts.
  COM Mode toggle does not clear Slot Mode by itself.
- First implementation target is `;;p` / `;;lp` because parameter declarations
  naturally have at least name and value fill points.
- Focused verification cases for implementation: activation selects the first
  slot, Tab advances, Shift+Tab returns, Tab and Shift+Tab wrap, Esc exits
  without reverting inserted text, editing a slot keeps later ranges correct,
  moving the cursor outside the session exits, and undo that removes the
  inserted template clears the session safely.

M2.2 implementation status:

- Complete: `;;p` and `;;lp` produce name/value slot metadata.
- Complete: parameter template activation starts editor-local Slot Mode through
  the completion workflow.
- Complete: Slot Mode owns ordered ranges, highlights slots, supports Tab,
  Shift+Tab, explicit Esc exit, name-edit range shifting, and cursor-outside
  stale exit.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M2.3 implementation status:

- Complete: `;;l`, `;;w`, and `;;r` produce a signal-name slot.
- Complete: signal template activation reuses the existing editor-local Slot
  Mode path from parameter templates.
- Complete: signal name editing preserves packed/unpacked dimensions; Tab keeps
  cycling the signal slot until Esc exits Slot Mode.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M2.4 implementation status:

- Complete: Slot Mode paints every slot with a weak blinking highlight and a
  stronger active-slot highlight, including zero-length RHS fill points.
- Complete: status messages report `SLOT n/m`.
- Complete: Tab and Shift+Tab wrap between slots and are captured by the Slot
  handler, including Qt Backtab events, without inserting tab characters.
- Complete: Slot Mode now exits through explicit Esc or stale-session actions,
  not by reaching the final slot with Tab.
- Verification: Release `completion_test` covers Slot Mode blink state, Tab
  wrap, Shift+Tab wrap, Backtab capture without text mutation, and Esc exit.

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
  (complete: `MyCodeEditor::clearSelectedAssignmentRhs` applies the report in
  one undoable edit block)
- M3.3 connect result to slot mode
  (complete: successful clear-RHS reports start Slot Mode on `rhsN` fill slots)
- M3.4 expose clear-RHS as a COM Mode command
  (complete: `cr` clears the selected/current assignment RHS and starts Slot
  Mode through the editor-local clear-RHS path)
- M3.5 make clear-RHS line-oriented for selections
  (complete: selected text expands to touched complete lines before planning
  assignment RHS cleanup)

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
- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` exposes the editor-local
  action without the obsolete named-action layer.
- Not done in M3.2: Slot Mode entry after replacement. That stays in M3.3.
- Verification: Release `completion_test` covers selection application, undo
  restore, and declaration rejection; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M3.3 implementation status:

- Complete: successful clear-RHS editor-action execution starts Slot Mode using
  the `RtlClearAssignmentRhsReport` template-slot metadata.
- Complete: the text replacement itself remains one undoable edit block.
- Complete: G3.2 failure behavior is unchanged: unsupported selections do not
  mutate text and surface service-owned failure reasons.
- Complete: Tab advances between cleared RHS slots, Esc exits Slot Mode, and
  slot edits shift later RHS ranges through the existing Slot Mode state.
- Verification: Release `completion_test` covers Slot Mode start, slot editing,
  Tab advance, Esc exit, undo restore, and declaration rejection; Release
  `completion_test` and `gui_smoke_test` targets compile/link.

M3.4 implementation status:

- Complete: COM Mode registry exposes `cr` as the only clear-domain command
  with command-strip hint metadata.
- Complete: `ComModeCoordinator` dispatches `cr` through
  `MyCodeEditor::clearSelectedAssignmentRhs` and leaves failures in COM Mode
  with a non-modal `No assignment RHS found` message.
- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` also supports the
  no-selection case by locating the current assignment statement and still
  applying the existing `RtlBatchEditService` report path.
- Complete: successful `cr` execution starts Slot Mode on the cleared RHS
  slots and exits COM Mode back to INSERT for immediate editing.
- Verification: Release `completion_test` covers selection cleanup,
  current-assignment cleanup, failure without mutation, undo restore, and Slot
  Mode slot order. Release `gui_smoke_test` target compile/link passed and the
  launched smoke output shows all new COM `cr` checks passing, but the full
  monolithic GUI smoke baseline still fails on unrelated existing checks.

M3.5 implementation status:

- Complete: when `clearSelectedAssignmentRhs` sees a selection, it expands the
  selection to the complete line range touched by the selection before passing
  text to `RtlBatchEditService`.
- Complete: partial selections on assignment lines no longer need to include
  the full assignment or RHS; no-selection behavior still targets the current
  assignment statement.
- Complete: Slot Mode slot ordering remains source ordered after line-oriented
  cleanup.
- Verification: Release `completion_test` covers partial multi-line selection
  cleanup, current-assignment cleanup, no-RHS failure without mutation, slot
  ordering, and undo restore. Release `gui_smoke_test` target compile/link
  passed and the launched smoke output shows the new COM `si -> cr` flow
  passing, while the full smoke baseline still fails on unrelated existing
  checks.

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
- M4.4 register the first non-g editor-local editing command
  (complete: `cr` uses the registry/dispatcher/hint/failure-message framework)
- M4.5 add select-inside begin-end COM command
  (complete: `si` uses Tree-sitter begin/end structure, stays in COM Mode, and
  can feed `cr`)
- M4.6 make COM entry explicit and non-destructive
  (complete: `Ctrl+Shift+Alt+backtick` toggles COM Mode from any app focus when
  an editor tab is open; Esc no longer enters COM Mode and remains
  cancellation-only)
- M4.7 add Column Number Tool through COM command `cn`
  (complete: `cn` opens the column-number popup only when column selection is
  active; formatting/inference lives in `columnnumbertool`)

M4.6 implementation status:

- Complete: `Ctrl+Shift+Alt+backtick` toggles COM Mode on/off when an editor
  tab is open, including when focus is outside the editor.
- Complete: Esc no longer enters COM Mode. It still cancels completion, hover,
  column selection, Slot Mode, selection, or COM buffers according to the
  highest-priority active temporary state.
- Complete: entering COM Mode through the toggle does not clear existing column
  selection and does not clear Slot Mode.
- Verification: Release `gui_smoke_test` target compile/link passed; launched
  smoke output shows the new COM toggle and Esc-entry regression checks
  passing. The full monolithic smoke baseline still fails on unrelated existing
  checks.

M4.7 implementation status:

- Complete: `commodecommandregistry` registers `c` as a non-executable prefix
  and `cn` as the Column Number Tool command without changing existing `g*`,
  `cr`, or Global Control behavior.
- Complete: `columnnumbertool` owns Dec/Hex/Bin formatting, Plain/C-like/SV
  unsized/SV sized style generation, step/repeat/direction, padding, digit
  width, hex case, and first-row inference for samples such as `8'h0A`,
  `'h0a`, `0x0A`, and `0010`.
- Complete: `ComModeCoordinator` opens a dark popup with Start/Base/Style/Bit
  width/Direction/Step/Repeat/Digit width/Pad/Hex case/Replace mode controls
  and a live preview. Enter applies one column edit; Esc closes without
  mutation and returns to COM Mode.
- Verification: Release `completion_test` passed through `ctest`; Release
  `gui_smoke_test` target compile/link passed; launched smoke output shows all
  new `cn`, tool-open, cancel, formatting, and inference checks passing. The
  full monolithic smoke baseline still fails on unrelated existing checks.

Column Selection visual-column repair status:

- Complete: column mode stores and edits by visual column, converting through
  the editor tab width for selection highlight, copy/cut/paste, delete,
  backspace, printable input, navigation, and mouse selection.
- Complete: column-mode Tab and Shift+Tab are captured by the column handler.
  Tab inserts spaces to the next visual tab stop; Shift+Tab performs visual
  outdent and does not leak to normal editor indentation.
- Verification: Release `gui_smoke_test` target compile/link passed; launched
  smoke output shows the new tab-containing column selection, column input, and
  Tab/Shift+Tab checks passing.

### 5. Limited Workspace Workflow Additions

Goal: improve workspace workflow only in the explicitly allowed ways.

Allowed scope:

- ignored directories
- restore Global Control `ow r` for recent workspaces

Not in scope:

- session restore
- include dirs/defines configuration UI in this track; that belongs to Track 12
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
  (complete: `;cmd`, `;;cmd`, COM Mode, Global Control, and removed
  named-action ownership/conflict boundaries are documented)
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
- Complete: the obsolete named-action layer is documented as removed;
  daily editor/app actions are owned by their normal shortcuts, context menus,
  COM Mode registrations, or direct editor/file APIs.
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
- Complete: user templates are file-maintained JSON records. The service reads
  a global `user_templates.json` and an optional workspace
  `.zeroslack/user_templates.json`; focused tests can inject both file paths.
- Complete: user template records use compact `;;` command tokens and
  non-empty `body` / `insertText`, reject invalid slot ranges and invalid
  commands, preserve `CodeTemplateSlotList` metadata, and expose load issues
  for invalid JSON or rejected records.
- Complete: query results are `CodeTemplateItem`-compatible, so future template
  popup and Slot Mode work can consume the same data shape.
- Complete: built-in `CodeTemplateService` behavior remains separate and
  unchanged; user templates cannot override built-in template tokens, and the
  reserved `;;h` / `;;pk` holes stay unavailable to user JSON in this phase.
- Not done in M6.2: user-template UI, custom abbreviations, changes to
  `;cmd`, COM Mode, Global Control, or broader Slot Mode activation.
- Verification: Release `completion_test` and `gui_smoke_test` passed through
  `ctest -R "^(completion_test|gui_smoke_test)$" --output-on-failure`.

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
  items with exact-token `UserTemplateService` items. Workspace user templates
  override global user templates with the same token, but user templates never
  override built-in `;;cmd` records.
- Complete: user template activation carries insert text, primary selection,
  and `CodeTemplateSlotList` metadata through the existing completion
  activation and Slot Mode path.
- Verification: Release `completion_test` and `gui_smoke_test` passed through
  `ctest -R "^(completion_test|gui_smoke_test)$" --output-on-failure`.

Latest user template JSON phase:

- Scope: first-stage file-maintained user templates only. This does not add a
  GUI template editor, import/export, macro recording, variables, new `;cmd`,
  COM Mode, Global Control, Package Tools, or `;:` entry.
- Storage: global templates live in the app config `user_templates.json`;
  workspace templates live at `.zeroslack/user_templates.json` under the active
  workspace. The JSON root may be an array or an object with a `templates`
  array.
- Record fields: `command`, `description`, `body` or `insertText`, and optional
  `slots` entries with relative `name`, `start`, and `length`.
- Merge policy: workspace user templates override global user templates by
  command token. Built-in template tokens cannot be overridden. `;;h` and
  `;;pk` remain reserved holes in this phase and are reported/ignored if user
  JSON tries to define them.
- Runtime: valid user templates enter the existing `;;cmd` template completion
  path. Activation replaces the command input and starts Slot Mode when slot
  metadata exists. Invalid JSON, invalid commands, invalid slots, and conflicts
  are exposed through `UserTemplateService` load reports and do not affect
  built-in templates.
- Coverage checks global load, workspace priority, insertion text, Slot Mode
  metadata, built-in conflicts, invalid JSON, invalid slot/command records,
  reload after file changes, and unchanged `;cmd`, COM Mode, and Global
  Control boundaries.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest user template JSON usage entry:

- Scope: lightweight menu/action access to the existing JSON workflow only.
  This does not add a template GUI editor, import/export, variables, macro
  recording, new `;cmd`, new `;;cmd`, COM Mode, Global Control, or Package
  Tools features.
- Tools / User Templates contains Open Global User Templates, Open Workspace
  User Templates, and Reload User Templates.
- Open Global uses `UserTemplateService`'s global path. Open Workspace requires
  an active workspace and uses `.zeroslack/user_templates.json` under that
  workspace. Missing files are created with only:
  `{ "templates": [] }`
- Reload calls `UserTemplateService::reload()`, shows loaded/ignored counts in
  the status bar, and shows a lightweight issue report when invalid JSON,
  invalid commands, invalid slots, reserved tokens, or conflicts are ignored.
  Issue rows include file, command, field, and reason.
- Coverage checks global skeleton creation/opening, workspace skeleton
  creation/opening, no-workspace failure, reload success status, visible issue
  reporting for invalid records, invalid JSON report text, unchanged built-in
  templates, unchanged `;cmd`, unchanged COM registry, unchanged Global
  Control template list, and the previous user-template insertion/Slot Mode
  regressions.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

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
  (complete: Wave Preview refresh is scoped to the selected/current `always`
  block through editor Tree-sitter scope detection and the existing service
  report path)
- M9.2 selected-module entry and scoped report
  (complete: Wave Preview falls back to selected/current module scope when no
  selected/current `always` scope is active)
- M9.3 UI polish for sketch readability
  (complete: canvas legend plus Scope/Legend overview rows make scoped
  sketches easier to interpret without adding simulator behavior)

M9.1 implementation constraints:

- Do not add simulator behavior, waveform database import, testbench execution,
  or timing-accurate verification.
- Use editor/Tree-sitter scope detection for the current/selected `always`
  block and pass a bounded scope into the existing Wave Preview query/report
  path.
- Keep selected-module Wave Preview out of scope until M9.2.

M9.1 implementation status:

- Complete: `TSDocument` exposes a current/selected `always_construct` scope
  target and rejects selections spanning multiple `always` blocks.
- Complete: `MyCodeEditor` exposes an editor-local always scope target, and
  `MainWindow` refreshes the Wave Preview dock from that scope only.
- Complete: `WavePreviewService` continues to own scoped report shaping through
  its existing query fields; UI code remains a report consumer.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M9.2 implementation constraints:

- Preserve M9.1 `always` priority: when the cursor or selection identifies an
  `always` block, the preview remains block-scoped.
- Use editor/Tree-sitter scope detection for the current/selected module and
  pass a bounded module scope into the existing Wave Preview query/report path.
- Do not add simulator behavior, workspace scans, UI-side Slang work, or sketch
  readability polish in M9.2.

M9.2 implementation status:

- Complete: `TSDocument` exposes a current/selected module/interface/program
  scope target and rejects selections spanning multiple RTL containers.
- Complete: `MyCodeEditor` exposes an editor-local module scope target, and
  `MainWindow` falls back to it only when no selected/current `always` scope is
  active.
- Complete: module-scoped reports continue through the existing
  `WavePreviewQuery` / `WavePreviewService` scoped report path.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M9.3 implementation constraints:

- Preserve M9.1 and M9.2 scope selection behavior.
- Do not add simulator behavior, waveform database import, testbench execution,
  or timing-accurate verification.
- Keep readability polish in `WavePreviewPanelCoordinator` as report
  rendering only; do not scan workspace files or run Slang from UI code.

M9.3 implementation status:

- Complete: Wave Preview canvas renders a compact legend for trace sketches and
  assign/blocking/nonblocking code sketches.
- Complete: the tree view adds Scope and Legend overview rows before detailed
  trace, warning, activity, lane, and event rows.
- Complete: focused panel coverage verifies scoped overview rows and reserved
  canvas space for the legend.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

### 10. State Transition Graph

Goal: show state transition graph only for selected symbols that structural FSM
discovery identifies as next-state roles.

Allowed scope:

- discover current/next roles from clocked current-state assignments and
  transition logic
- trigger only when the selected symbol is the discovered next-state role
- selected discovered current-state roles must not show the graph
- names are compatibility/display hints, not semantic gates
- service-owned transition extraction/report

First milestones:

- M10.1 structural role gating test
  (complete: editor source-symbol action exposes State Transition Graph only
  for selected symbols that are next-state roles in discovered FSM pairs, and
  rejects current-state roles or non-FSM symbols)
- M10.2 transition report service
  (complete: `StateTransitionGraphService` shapes accepted structural
  next-state role requests into selected next-state graph reports)
- M10.3 graph UI rendering and navigation evidence
  (complete: RTL Insights renders the service-owned State Transition Graph
  report as an interactive graph; double-click navigation follows graph element
  source links)

M10.1 implementation constraints:

- Do not add new state-transition extraction, report shaping, or graph
  rendering.
- Keep the trigger policy in a service path, not in direct UI checks.
- Preserve existing RTL Insights FSM graph behavior and use it only after the
  trigger gate accepts the selected symbol.

M10.1 implementation status:

- Complete: `StateTransitionTriggerService` owns structural FSM role gating for
  selected source symbols.
- Complete: editor source-symbol menu/request state enables `Show State
  Transition Graph` only for selected symbols that are discovered next-state
  roles.
- Complete: selected current-state roles, ordinary symbols, and missing module
  context do not dispatch the state-transition action.
- Complete: accepted requests route through `EditorCoordinator` and
  `SemanticPanelRefreshCoordinator` to the existing RTL Insights FSM graph path.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M10.2 implementation constraints:

- Preserve M10.1 structural role gating.
- Own accepted-request report shaping in a service path, not UI code.
- Reuse existing semantic/index-backed FSM graph data where practical.
- Do not add new broad graph UI rendering in this milestone.

M10.2 implementation status:

- Complete: `StateTransitionGraphService` validates accepted requests through
  `StateTransitionTriggerService`, builds module FSM graph data through
  `FsmGraphService`, and filters the result to the selected next-state signal.
- Complete: accepted structural next-state role reports expose selected signal,
  module, state count, transition count, and the matching `FsmGraph`.
- Complete: rejected current-state roles, no-FSM modules, and symbols without a
  matching structural next-state graph return service-owned failure reasons.
- Complete: the existing state-transition UI route consumes the new service
  report through RTL Insights without moving extraction or filtering into UI.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` and `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M10.3 implementation constraints:

- Preserve G10.1 structural role gating and G10.2 selected next-state report
  filtering.
- UI must render and route from `StateTransitionGraphReport` data only.
- Do not move transition extraction, graph filtering, workspace scanning, or
  Slang execution into UI code.

M10.3 implementation status:

- Complete: RTL Insights renders the service-owned State Transition Graph
  report as a `QGraphicsScene` with selectable state/register/signal nodes and
  selectable transition edges.
- Complete: panel-level tests verify that selecting a next-state role renders
  only the matching next-state graph and excludes sibling FSM graphs in the
  same module.
- Complete: panel-level tests invoke graph-element navigation for transition
  edges and next-state signal nodes and verify the source links from the report
  are delivered to the navigation handler.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` and `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

### 11. Module Block Diagram

Goal: show module containment as a block diagram from a selected module.

Allowed scope:

- selected module name as diagram top
- module/interface instance wrapping relationship only
- no signals
- click module block to jump to module definition

First milestones:

- M11.1 service report for module-instance containment from selected module
  (complete: `ModuleBlockDiagramService` builds selected-module containment
  reports from hierarchy/`INSTANTIATES` data, excludes signals, and carries
  module definition links)
- M11.2 render module-only block diagram
  (complete: RTL Insights renders `ModuleBlockDiagramReport` as a module-only
  interactive graph from active or selected module names)
- M11.3 click navigation to module definitions
  (complete: double-clicking rendered module graph elements follows
  report-carried definition links through the existing navigation handler)

M11.1 implementation constraints:

- Report shaping must live in a service path, not in UI code.
- Consume existing semantic/hierarchy/relationship data; do not scan workspace
  files or run Slang from UI.
- Include only module/interface containment or wrapping relationships.
- Carry module definition navigation links in report data.
- Defer all visual rendering to M11.2.

M11.1 implementation status:

- Complete: `ModuleBlockDiagramService` owns `ModuleBlockDiagramReport` for a
  selected module query.
- Complete: the report is built from `HierarchyService` with
  `INSTANTIATES` filtering and returns module nodes plus instantiation edges.
- Complete: child records are normalized to module/interface definitions when
  possible, so report links point to module definitions rather than UI-side
  lookups.
- Complete: focused relationship coverage verifies root/child containment,
  no signal nodes, edge type filtering, named query resolution, missing-root
  failure reason, and carried definition links.
- Verification: `git diff --check`; Release `relationship_test` target
  compile/link; `ctest -R "^relationship_test$"` passed.

M11.2 implementation constraints:

- UI must consume `ModuleBlockDiagramReport` only.
- Selected module names must become the diagram root.
- Render module/interface containment only; do not render signals.
- Do not add workspace scans, UI-side Slang work, or new relationship
  extraction.
- Leave explicit click-navigation evidence to M11.3.

M11.2 implementation status:

- Complete: RTL Insights has a `Module Block Diagram` action for the active
  module context.
- Complete: editor source-symbol actions can dispatch `Show Module Block
  Diagram` for selected names that `ModuleBlockDiagramService` accepts as
  modules.
- Complete: `RtlInsightsPanelCoordinator` renders root and child module nodes
  plus instantiation edges from `ModuleBlockDiagramReport` and does not render
  signals.
- Complete: the module diagram now uses a grid-backed canvas with the selected
  module rendered as a large container and child modules arranged inside it.
- Complete: visible `-` / `Fit` / `+` controls supplement mouse-wheel graph
  zoom.
- Complete: source-symbol routing passes through
  `SemanticPanelRefreshCoordinator`; UI code remains a report consumer.
- Verification: Release `relationship_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M11.3 implementation constraints:

- Preserve G11.1 report ownership and G11.2 module-only rendering.
- Navigation must use module definition links already carried by
  `ModuleBlockDiagramReport`.
- Do not render signals.
- Do not add workspace scans, UI-side Slang work, or new relationship
  extraction.

M11.3 implementation status:

- Complete: rendered top-module and child-module graph nodes carry definition
  links from `ModuleBlockDiagramReport`; instantiation edges carry child module
  definition links.
- Complete: focused panel coverage invokes graph-element navigation for root
  and child module nodes and verifies that the navigation handler receives the
  corresponding module definition file, line, and column.
- Complete: double-clicking or test-triggering a module graph element now
  navigates to that module definition and re-renders the Module Block Diagram
  around the jumped module, so its child modules remain visible.
- Verification: Release `relationship_test` target compile/link; `ctest -R
  "^relationship_test$"` passed.

### 12. Workspace Project Configuration And Diagnostics Workflow

Goal: make workspace-level SystemVerilog configuration explicit and make
diagnostics usable as a navigation workflow.

Allowed scope:

- per-workspace include dirs
- per-workspace defines
- per-workspace ignored dirs
- per-workspace file extensions
- optional top module / active top
- Problems/diagnostics next/previous navigation
- diagnostic status, owner, current-file summary, and stable jump feedback

Not in scope:

- session restore
- recent files
- broad workspace UX expansion
- new diagnostic engines beyond existing Slang and Semantic index paths
- UI-side Slang execution or workspace scanning

Milestones:

- M12.1 Workspace Configuration Service And Dialog
  (complete: `WorkspaceConfigurationService`, `WorkspaceConfigurationDialog`,
  and `WorkspaceManager::setWorkspaceConfiguration()` persist/apply include
  dirs, defines, ignored dirs, file extensions, and top module)
- M12.2 Diagnostics Navigation And Problems Status
  (complete: `DiagnosticNavigationService`, F8/Shift+F8 actions, owner column,
  current-file summary, analysis state label, and reveal/flash Problems jumps)
- M12.3 Verification And Documentation
  (complete: docs updated; focused Release tests/builds recorded below)

M12.1 implementation status:

- Complete: workspace configuration is stored per workspace through
  `QSettings` with a versioned `workspaceConfiguration/v1` key space.
- Complete: include dirs preserve user order; defines are key/value pairs;
  file extensions default to `.sv`, `.svh`, `.v`, and `.vh`; top module may be
  empty to preserve automatic/default behavior.
- Complete: applying configuration updates `ProjectModel`, refreshes filtered
  workspace file lists, persists ignored dirs through the existing validation
  service, emits existing workspace/file-list signals, and queues analysis
  through the project-change path.
- Complete: workspace analysis keys include include dirs, defines, file
  extensions, and top module so configuration changes do not reuse a stale
  completed workspace-analysis key.

M12.2 implementation status:

- Complete: Problems rows display diagnostic owner, currently `Slang` or
  `Semantic index`, without inventing a new diagnostic owner.
- Complete: the Problems panel displays current-file error/warning/info
  summary and a diagnostics state label (`current`, `stale`, `analyzing`, or
  `background`) using existing analysis signals and band metadata.
- Complete: F8 and Shift+F8 navigate next/previous diagnostics using the
  Problems panel's current scope and severity filters. The navigation service
  sorts by source location after filtering so workspace navigation is
  predictable.
- Complete: Problems double-click navigation now validates missing files and
  invalid locations, routes through `NavigationCommandCoordinator`, and flashes
  the target line after reveal.

M12.3 verification status:

- Passed: Release `completion_test` and `gui_smoke_test` targets compile/link.
- Passed: Release `ctest -R "^completion_test$" --output-on-failure`.
- Fixed after initial M12 acceptance: workspace activation/cached restore now
  applies configuration silently, avoiding extra `filesScanned` /
  `workspaceListChanged` signals and preserving Navigation design hierarchy
  caches for cached workspace switches and closes.
- Passed within Release `gui_smoke_test`: `workspace cached switch emits
  activation only`, `workspace cached switch still avoids rescan`, `navigation
  design cache restores A without rebuild`, and `navigation design cache
  restores B after close`.
- Attempted: Release `ctest -R "^gui_smoke_test$" --output-on-failure`.
  It still fails on existing VENDOR ctrl-click and Wave Preview checks, but the
  required workspace/cache regression checks pass.

### 13. References / Relationships Workflow Closure

Goal: make source-symbol navigation, references, relationship rows, and graph
result jumps easy to find, jump from, and return from.

Allowed scope:

- source-symbol right-click workflow organization
- References result grouping, query context, empty-state reasons, copying, and
  jumps
- Relationships result source jumps and existing graph entry points
- unified navigation path with Back/Forward history isolation per workspace
- status-bar or panel feedback for jump failures

Not in scope:

- package tools
- macro/define semantics, which is owned by Track 15
- Wave Preview expansion
- new graph types
- complex graph algorithms
- expanded State Transition Graph trigger rules

Milestones:

- M13.1 Unified Source-Symbol Context Menu
  (complete: the symbol context menu always shows Go to Definition, Find
  References, Show Relationships, Signal Kernel Graph, State Transition Graph,
  and Module Block Diagram; disabled actions expose reasons; Module Block
  Diagram is limited to existing module/interface definitions; current-state
  names remain rejected)
- M13.2 References Panel Workflow Closure
  (complete: query context, empty reasons, click/activated jump with flash,
  and copy path or file:line)
- M13.3 Relationships Panel Workflow Closure
  (complete: source-code jumps for rows, instance/module-definition jumps where
  existing data supports them, signal driver/consumer/declaration jumps from
  existing relationships, and only existing graph entry points)
- M13.4 Unified Navigation Failure Feedback And History Isolation
  (complete: References, Relationships, and graph jumps share one validated
  navigation path; Back/Forward history is isolated by workspace; failures are
  visible)
- M13.5 Verification And Documentation
  (complete: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release acceptance CTest
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed)

M13.1 implementation status:

- Complete: source-symbol context menu actions are stable and do not disappear
  when disabled.
- Complete: disabled source-symbol actions carry status/tooltip reasons such
  as no source file, no symbol under cursor, symbol not indexed, next-state
  gating, or module-block gating.
- Complete: Module Block Diagram activation is UI-gated to existing
  module/interface definition records, so ordinary signals remain disabled
  without adding new semantic analysis.
- Complete: this milestone is workflow/UI closure only. It does not implement
  package tools, Wave Preview expansion, new graph algorithms, or broader
  State Transition Graph triggers; Track 15 owns macro/define semantics.

M13.2-M13.4 implementation status:

- Complete: References and Relationships panels display query context for the
  active symbol, source file, scope/view/direction/type/depth filters.
- Complete: empty panels render explicit reasons: no symbol under cursor,
  symbol not indexed, no references/relationships found, or workspace analysis
  stale / not ready when the index has no records.
- Complete: result rows support path and `file:line` copy; click and activated
  rows route through the shared navigation path and flash the target line.
- Complete: Relationship rows store existing relationship data for driver,
  consumer, declaration, and instance navigation; instantiation rows prefer
  existing module/interface definition records when available.
- Complete: Relationship result context menus call only existing graphs:
  Signal Kernel Graph, Module Block Diagram, and State Transition Graph.
- Complete: panel and graph jumps are validated for missing files, invalid
  line/column data, stale semantic snapshots or missing symbols, and workspace
  mismatch; workspace mismatch means a target in a different open workspace,
  not an ordinary external result file; failures are surfaced through the
  status bar.
- Complete: `NavigationCommandCoordinator` history entries carry a workspace
  key and Back/Forward prunes entries from other workspaces.
- Complete: acceptance repair for the FSM Graph / State Transition Graph
  panel closure renders service-owned state-register and next-state signal
  nodes, preserves transition edges, adds the existing-data signal-flow edge,
  and lets next-state signal nodes invoke the existing navigation handler.
- Complete: final Release verification passed after the panel rendering and
  workspace-mismatch adjustments: `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link, and `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.

### 14. Package Tools

Goal: give SystemVerilog package scopes a lightweight, package-only insertion
surface for common definitions.

Allowed scope:

- show a Package Tools bar/buttons only when the active editor cursor is inside
  a parseable package scope
- insert first-phase package definition templates for `parameter`,
  `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function`
- choose insertion positions from Tree-sitter package structure
- default to the line before `endpackage`, with same-kind append anchors when
  a matching package item already exists
- start Slot Mode after insertion and highlight all editable slots
- report clear failure reasons when the current package cannot be determined

Not in scope:

- package/import semantic interpretation
- macro/define handling, which is owned by Track 15
- cross-file package management
- automatic sorting or broad package reordering
- new graph or insight surfaces
- changing existing `;;cmd` template behavior

Milestones:

- M14.1 Package Tools First Phase
  (complete: package-only buttons insert the six requested definition
  templates, stay inside the current package, reject module/interface scope,
  and start Slot Mode)
- M14.1a Verification Baseline Repair
  (complete: Release `relationship_test` rebuild baseline repaired and Package
  Tools phase-1 coverage reinforced without adding Package Tools phase 2)
- M14.2 Future Package Tools Expansion
  (not started: any additional templates, semantic package handling, sorting,
  or cross-file package workflows require a new scoped request)

M14.1 implementation status:

- Complete: `PackageToolService` owns package-only templates and slot metadata
  for parameter, localparam, enum typedef, struct typedef, packed struct
  typedef, and function definitions.
- Complete: `TSDocument::packageToolInsertTarget` derives the current package
  and insertion point from Tree-sitter nodes, rejects module/interface/program
  scope, appends near same-kind direct package items when present, and defaults
  before `endpackage`.
- Complete: `MyCodeEditor::executePackageToolInsert` applies the generated
  template in one edit block and starts Slot Mode with all remapped slots.
- Complete: `MainWindow` renders a lightweight Package Tools bar in the editor
  area only while the active cursor is in a valid package scope.
- Verification: Release `completion_test` and `gui_smoke_test` targets
  compile/link; Release `ctest -R "completion_test|gui_smoke_test"
  --output-on-failure` passed.

M14.1a verification baseline repair status:

- Complete: the Release `relationship_test` rebuild failure was traced to
  generated MinGW build rules that lacked the compiler bin on `PATH`, producing
  blank subcommand failures during object rebuilds. CMake now wraps generated
  compile and link rules with `cmake -E env PATH=...` using the detected
  compiler directory, so `cmake --build` can rebuild `relationship_test` from a
  plain shell without weakening assertions or skipping the target.
- Complete: `completion_test` now checks that all six Package Tools phase-1
  templates have non-empty insert text and slot metadata, and that the packed
  struct template text contains `typedef struct packed`. It also asserts the
  phase-1 tool list remains exactly six entries.
- Complete: `gui_smoke_test` now checks that the six stable
  `packageToolButton_<id>` buttons exist and that the Package Tools button
  count is exactly six; it still only needs one actual click to cover GUI
  insertion and Slot Mode.
- Boundary: no Package Tools phase 2, no package/import semantics, no
  cross-file package management, no package sorting, no graph/insight changes,
  and no `;;cmd` behavior change; Track 15 owns macro/define semantics.
- Verification: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$" --output-on-failure`
  passed; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'` passed.

### 15. Macro / Define Semantic Workflow

Goal: make common SystemVerilog macro and preprocessor-branch workflows usable
without turning ZeroSlack into a full preprocessor.

Allowed scope:

- static `define` symbol records and outline rows
- goto definition, hover, and Find References for `` `MACRO`` uses
- clearer undefined macro diagnostics as Semantic index supplements to Slang
- conservative inactive `ifdef` / `ifndef` / `elsif` / `else` / `endif`
  branch decorations using workspace configured defines and static current-file
  `define` / `undef` state

Not in scope:

- Vivado `.xpr` / Tcl parsing
- new define configuration UI
- complete SystemVerilog macro expansion or argument substitution
- formatter-visible text mutation
- changing Package Tools, COM Mode, Slot Mode, diagnostics, references, or
  relationship ownership boundaries

Milestones:

- M15.1 Macro / Define First-Class Semantics
  (complete: outline, goto definition, hover, references, supplemental
  undefined diagnostics, and inactive branch gray decorations are available
  with conservative static behavior)
- M15.1a Acceptance Stabilization
  (complete: GUI smoke reference/relationship dock regression isolates semantic
  services and panel filters; Release acceptance CTest passed twice
  consecutively)

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
  (complete: current owners and existing verification anchors are documented;
  no Huge Workspace UX features were added)
- HWA.2 run or compile appropriate verification based on environment safety
  (complete: Release build of the HWA-related test/harness targets passed
  without launching GUI executables)
- HWA.3 update docs with confirmed status and gaps
  (complete: current status and remaining audit gaps are documented)

HWA.1 inventory status:

- Current/open/dirty-open/background planning is owned by
  `WorkspaceAnalysisPlanService`; dirty-open files become protected files,
  priority files get band metadata, and checkpoints drive staged publication.
- Active/pending request coalescing, stale pending replacement, and request
  telemetry are owned by `WorkspaceAnalysisRequestQueue` and routed through
  `WorkspaceSymbolAnalysisController`.
- Symbol analysis cancellation/expiration and staged publication are owned by
  `WorkspaceSymbolAnalysisController` plus `SymbolAnalyzer`; `SemanticIndex`
  stores band metadata and publishes snapshots for priority-aware queries.
- Relationship cancellation and stale/cancelled result rejection are owned by
  `RelationshipAnalysisController`, `RelationshipAnalysisWorker`, and
  `RelationshipResultPublisher`.
- Activity visibility is owned by `AnalysisProgressCoordinator` and
  `ActivityLogService`.
- Existing coverage anchors are `completion_test`, `relationship_test`,
  `large_file_perf_test`, and `relationship_perf_test`.
- Release `huge_prj` references remain the current baseline: 428 HDL files,
  about 18.59 MiB, about 5.096s async symbol publication in the focused
  harness, and about 3.891s relationship analysis in the focused harness.

HWA.2 verification status:

- Chosen safe verification path: compile/link relevant Release targets instead
  of launching GUI executables, because this environment has recently shown
  external Windows application-error dialogs during GUI test runs.
- Passed command: Release CMake build target set `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`.
- Artifact check confirmed all four executables exist in the Release build
  directory.
- `ctest` was not run for HWA.2, so no long-running CTest or external
  memory-read dialog check was needed in this milestone.

HWA.3 confirmed status:

- Owner boundaries are documented for planning, request coalescing, symbol
  cancellation/expiration, staged publication, relationship cancellation,
  Activity telemetry, and `SemanticIndexSnapshot` query exposure.
- Existing verification anchors cover the intended strategy surfaces:
  `completion_test`, `relationship_test`, `large_file_perf_test`, and
  `relationship_perf_test`.
- Release compile/link verification for all four HWA-related targets passed in
  HWA.2.
- No Huge Workspace UX or behavior change was implemented during the audit.

HWA.3 open audit gaps:

- HWA.2 did not run the test executables, so runtime behavior is not newly
  confirmed by this audit.
- Full `ctest` was not run because the current environment has recently shown
  external Windows GUI/memory-read dialogs during test execution.
- `relationship_perf_test` was compiled but not executed against
  `test_sv/huge_prj`; the listed Release `huge_prj` timings remain prior
  reference points, not freshly refreshed HWA.3 measurements.
- Activity telemetry was inspected through owner wiring and test anchors, not
  revalidated in a live GUI session.
- Future Huge Workspace work should first close these audit gaps with a safe
  runtime setup before proposing UX or behavior changes.

## Functional Corpus Audit

Goal: run a broad service/report-layer audit over the real `test_sv/new` and
`test_sv/huge_prj` SystemVerilog corpus without mutating corpus files.

Milestones:

- FCA.1 Add repeatable corpus audit target and reports.
  (complete: `corpus_audit_test` recursively covers all 454 `.sv` / `.svh` /
  `.v` files and writes JSON + Markdown reports under `test_sv`)
- FCA.2 Exercise four priority features on real corpus.
  (complete: State Transition Graph, Signal Kernel Graph, Module Block
  Diagram, and Wave Preview are invoked through service/report-layer paths;
  GUI smoke remains out of this stage)
- FCA.3 Record failures and residual risk.
  (complete: current report generated 2026-07-02T07:19:45Z UTC and covered 454
  files, 424 modules, 3942 always/process records, 49,345 signal/port
  candidates, 117,069 semantic records, 82,636 relationships, and 704
  diagnostics. State Transition Graph is 68 pass / 0 fail / 398 skipped under a
  deterministic module sample cap; discovered pairs include delayed
  current<=next assignments such as `#TP`, `# TP`, and `#(...)`, with
  next-state positives matched by current-state negative trigger checks. Signal
  Kernel Graph is 32 pass / 0 fail /
  1,960 skipped under deterministic no-candidate and sample-cap reasons.
  Module Block Diagram is 208 pass / 216 empty-valid. Wave Preview uses
  source-discovered always/process records and reports 1047 pass / 0 fail /
  2957 skipped / 4 empty-valid under a deterministic process preview cap.
  Semantic
  baseline is 874 pass / 0 fail / 5 empty-but-valid, with empty outlines
  classified as comment/preprocessor-only or header-like empty.)

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

Latest acceptance baseline repair:

- Scope: test baseline only; no product feature behavior was changed.
- `completion_test` source-symbol context menu assertions were updated for the
  five-action menu: Find References, Relationships, Signal Kernel Graph, State
  Transition Graph, and Module Block Diagram.
- The test now explicitly verifies ordinary signals keep Module Block Diagram
  disabled, selected module names enable it, and state-transition `ns` /
  `next_state` versus `cs` / `current_state` behavior remains unchanged.
- Release verification passed: `ctest -R "^completion_test$"
  --output-on-failure` and `ctest -R "^relationship_test$"
  --output-on-failure`.

Latest RTL Insights FSM graph repair:

- Scope: FSM Graph rendering and candidate filtering only.
- `FsmGraphService` now requires parsed case-derived transition evidence before
  publishing an FSM graph candidate, which filters ordinary enum/register
  declarations that merely have a paired next-state-looking signal.
- RTL Insights `FSM Graph` now renders the service-owned report as selectable
  `QGraphicsScene` nodes and transition edges, matching the State Transition
  Graph and Module Block Diagram interaction model.
- Regression coverage must verify a non-case enum register is ignored and the
  real `chl_ctrl` `phy_pass_thrg_cfg_cs` / `phy_pass_thrg_cfg_ns` FSM remains
  present with transitions.
- Release verification passed: `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.

Latest COM Mode strip alert styling repair:

- Scope: command strip presentation only; COM command parsing and dispatch are
  unchanged.
- Normal COM input/prefix hints keep a cold dark strip. Command failure
  messages such as unknown commands, incomplete commands, invalid line numbers,
  missing current scope, and unclear insertion points now use a dark alert
  background, red alert text, and red border.
- Release verification passed: `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.

Latest COM/Column Selection repair:

- Scope: COM Mode entry, column-selection visual-column behavior, and the
  column-number editor-local tool only.
- COM Mode now toggles with `Ctrl+Shift+Alt+backtick` from editor or
  non-editor focus when an editor tab is open. Esc is no longer a COM entry
  key and remains cancellation-only.
- Column Selection stores visual columns and converts through editor tab width
  for selection, copy/cut/paste, text input, deletion, navigation, and mouse
  adjustment. Column-mode Tab and Shift+Tab are captured as visual alignment
  edits.
- COM command `cn` opens the Column Number Tool. Formatting/inference lives in
  `columnnumbertool`; the popup previews and applies one undoable column edit.
- Release verification: `completion_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$" --output-on-failure` passed.
  `gui_smoke_test` was launched and all new COM toggle, visual-column column
  mode, `cn`, Column Number Tool, and inference checks passed. The full smoke
  baseline still fails on existing non-current checks: `VENDOR ctrl-click fixture
  opens` and the Wave Preview rendering group.

Latest GUI smoke baseline repair:

- Scope: failing Release `gui_smoke_test` baseline only; no new product feature
  was added and no current regression coverage was deleted.
- VENDOR ctrl-click test fixture lookup now resolves from source or build roots so
  Release CTest can find `test_sv/huge_prj/vendor_ip_ctl.sv`.
- Wave Preview scope lookup now keeps safe module/always ranges available when
  Tree-sitter reports localized parse errors inside the scope; the Wave Preview
  test assertions were updated to the current scoped summary wording and canvas
  lane geometry.
- RTL Insights GUI smoke fixture installation now refreshes both the native
  semantic store and snapshot before each panel action, isolating synthetic
  RTL Insights checks from staged workspace publication.
- Problems external-diagnostic preservation now uses an unopened external probe
  file and checks for the exact diagnostic row instead of depending on a fragile
  item count.
- Release verification passed: `ctest -R "^completion_test$"
  --output-on-failure` and `ctest -R "^gui_smoke_test$"
  --output-on-failure`; `gui_smoke_test` was repeated successfully.

Latest Package Tools first phase:

- Scope: package-only insertion buttons for the first six requested templates.
- `PackageToolService` owns template text and slot metadata; `TSDocument`
  owns current-package validation and insertion targets; `MainWindow` only
  renders the lightweight button bar.
- Insertions stay inside the current package, default before `endpackage`, and
  append near existing same-kind direct package items when present. Module,
  interface, and program scopes are rejected with clear failure reasons.
- Existing `;;cmd` template behavior is unchanged. Package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Release verification passed: `completion_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "completion_test|gui_smoke_test"
  --output-on-failure` passed.

Latest Macro / Define semantic workflow:

- Scope: conservative static macro semantics, not a full SystemVerilog
  preprocessor.
- `SlangManager` symbol extraction supplements Slang records with static
  `define` macro records, so outline and definition lookup can treat macros as
  first-class symbols while keeping `ifdef` / `ifndef` / `else` / `endif` out
  of outline.
- `` `MACRO`` navigation now resolves current-file definitions first, then
  workspace/include-visible macro definitions from the semantic index.
- Macro hover shows the definition location plus signature/body text, with
  function-like macros showing their parameter list without performing argument
  substitution.
- Find References for macros returns the `define` row and all indexed
  backtick uses through the existing References panel jump/flash workflow.
- Undefined macro diagnostics are supplemented under the Semantic index owner
  without replacing Slang diagnostics.
- Inactive preprocessor branches are grayed conservatively using configured
  workspace defines and static current-file `define` / `undef` state across
  basic `ifdef` / `ifndef` / `elsif` / `else` / `endif` structures.
- Boundaries: no Vivado `.xpr` / Tcl parsing, no define configuration UI, no
  complete macro expansion, no formatter text mutation, and no Package Tools,
  COM Mode, Slot Mode, or relationship ownership change.
- Verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest Macro / Define acceptance stabilization:

- Scope: test isolation and acceptance stability only; no new macro/define
  feature surface.
- The GUI smoke reference/relationship dock regression now runs against an
  isolated local semantic snapshot injected into `ReferenceService`,
  `RelationshipService`, and `HierarchyService`, then restores the services to
  the global semantic index.
- The fixture resets reference and relationship dock filters at entry and exit
  so reference, direct relationship, and hierarchy tree assertions are not
  affected by prior panel state.
- Reference/relationship assertions were not weakened, and Package Tools, COM
  Mode, Slot Mode, diagnostics, references, and relationships behavior outside
  this test fixture was not expanded.
- Release verification passed: `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`; `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` twice consecutively.

Latest `;m` module instantiation semantic completion:

- Scope: existing `;m` semantic module completion only. `;;m` remains the
  module definition skeleton, and no Package Tools phase 2, include/package/
  import workflow, new command, or COM Mode change was added.
- `CompletionService` now builds a full named instantiation from indexed module
  parameter and port records when available, preserving module definition
  order. Modules without parameters omit `#(...)`; modules without usable port
  records conservatively fall back to the previous simple instantiation text.
- Module instantiation activation starts Slot Mode with slots ordered as
  instance name, parameter values, then port connections. Slot range updates
  now follow text position rather than Tab order so instance-first Slot Mode
  works even when parameter values appear earlier in the generated text.
- Coverage includes parameter+port instantiation text and slot order,
  no-parameter instantiation without `#(...)`, insufficient-semantics fallback,
  unchanged `;;m` skeleton behavior, and GUI activation into Slot Mode.
- Release verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.

Latest header/include command convergence:

- Scope: explicit header include command entry only. Package/import completion,
  COM Mode, Global Control, and `;;h` template descriptors remain out of scope.
- `;h <query>` now owns include insertion for existing headers. It reuses the
  existing include candidate/filter path and inserts a full SystemVerilog
  `` `include "..."`` statement.
- `;h -n <name>` creates a header and inserts the include. Names without a
  suffix default to `.svh`; `.vh` / `.svh` suffixes are accepted; creation
  prefers the current file directory and refuses to overwrite existing files
  with an explicit already-exists result.
- The old hidden `` `include `` + space trigger was removed, so typing an
  include directive no longer auto-inserts quotes or opens the include popup.
- Coverage now checks existing-header insertion, `.svh` default creation,
  no-overwrite behavior, absent `;;h`, and legacy include-space inactivity.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test`; `cmake --build . --target relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest package import explicit entry:

- Scope: explicit package import insertion only. This is not package
  navigation, Package Tools phase 2, `pkg::symbol` completion, cross-file
  package management, COM Mode, or Global Control work.
- `;pk <query>` searches existing package semantic records through the
  completion service and inserts `import pkg_name::*;` on activation.
- `;p` remains the parameter semantic command, COM `gpk` remains the package
  picker / package jump workflow, Package Tools remain package-file editing
  tools, and `;;pk` remains absent.
- Coverage checks package candidate matching, activation text insertion,
  absent `;;pk`, unchanged `;p`, and unchanged `gpk` behavior through the
  existing COM smoke coverage.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest package/import semantic repair:

- Scope: unqualified package member visibility for ordinary completion,
  definition, and hover. Package members marked `PackageVisible` are not global;
  they are visible only when the current file/scope has an active
  `import pkg::*;`.
- Local/module declarations keep priority over imported package members.
  Same-name members imported from multiple packages are treated as ambiguous
  and are not used for random unqualified jumps.
- `;pk` remains import insertion only, COM `gpk` remains package navigation,
  Package Tools remain unchanged, and `pkg::symbol` completion was not added.
- Debug verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest Verification Baseline Repair:

- Scope: baseline repair only after Package Tools phase 1; this is not Package
  Tools phase 2 and does not change product feature behavior beyond making the
  accepted build/test baseline reproducible.
- `relationship_test` was not skipped and no assertions were weakened. The
  rebuild failure was caused by generated MinGW compile/link rules not
  supplying the compiler bin on `PATH`, which allowed blank subcommand failures
  during object rebuilds. The top-level CMake file now injects the detected
  compiler bin into generated compile and link rule launchers.
- Package Tools coverage now verifies all six first-phase templates have
  non-empty text and slot metadata, verifies `typedef struct packed` appears in
  the packed struct template, and verifies the GUI exposes six stable package
  tool buttons with exactly six Package Tools buttons total while preserving
  one clicked insertion/Slot Mode path.
- Existing `;;cmd` template behavior is unchanged. Package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Release verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$" --output-on-failure`;
  `git diff --check -- . ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

## Commit Policy

- Keep commits coherent and architecture-oriented.
- Do not mix unrelated dirty code into documentation commits.
- Do not push unless explicitly asked or required by the active milestone goal
  flow.
- Prefer short commit messages that name the product/architecture change.
