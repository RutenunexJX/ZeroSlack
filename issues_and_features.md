# Issues And Feature Ideas

This file is a running checklist for problems to fix and new features to consider.

## Issues

### Completed in v0.3.1

- [x] Formatter alignment remains incorrect in multiple constructs in the
  reproduction file `C:\Users\14971\Desktop\temp.sv`. The eventual fix must
  identify general syntax-aware alignment defects from this file and must not
  add file-specific or line-specific rules.
- [x] In column mode, the rendered red caret can be displaced by one column
  from the position where editing actually takes effect. Rendering and edit
  coordinates must resolve to the same real or virtual column.
- [x] The `Ctrl+Space` palette is not anchored near the active editor caret.
  It must open beside the insertion point while remaining inside the usable
  screen or editor viewport.
- [x] Left and Right do not switch the `Ctrl+Space` palette among Symbols,
  Templates, and Commands. Horizontal keys must change the active category
  without transferring focus away from filtering and result navigation.

- [x] A clean `Ctrl+S` still wrote the file and scheduled semantic lifecycle
  work. It is now a true no-op, while changed saves skip already-current
  snapshots, classify trivia-only edits without Slang, avoid unrelated
  hierarchy/editor refreshes, and no longer label incremental work as a
  workspace-wide analysis request.

### Completed in v0.3.0

- [x] Saving an existing SystemVerilog file could be observed as a directory
  change and trigger a full workspace rescan. Workspace watching now monitors
  source files directly and reserves directory refreshes for source membership
  changes; one save emits one file-level change.
- [x] Undo/redo restored logical cursors but could move the editor viewport.
  Both scrollbar positions are now restored after synchronous presentation.
- [x] Keyword ghost completion accepted by Tab omitted the separating space.
  It now appends one space unless a space or Tab already follows.
- [x] Inline `;cmd` / `;;cmd` activation mixed symbols and templates and could
  still be triggered accidentally. `Ctrl+Space` now explicitly opens separate
  Symbols, Templates, and Commands categories; semicolons remain ordinary
  source input.
- [x] F24 contained low-value assignment/conditional navigation, independent
  add-signal/add-parameter/add-port actions, file operations, and actions that
  already have direct selected-occurrence shortcuts. Those entries were
  removed or hidden while repeat action remains available.
- [x] Module port-list changes had no guarded way to synchronize every source
  instance. Synchronize Instance Connections now adds missing and removes
  obsolete named connections across all provable instances in one High+Diff,
  all-or-nothing plan.

### Completed in v0.2.2

- [x] Undo cursor restoration is unreliable. A stable reproduction is editing
  in column mode and pressing `Ctrl+Z`: the text is undone, but the caret jumps
  to the start of the first line instead of returning to the pre-edit logical
  position. The same first-line jump occurs in several other edit workflows,
  so the eventual fix must audit the shared undo transaction/cursor-anchor
  path rather than patching column mode alone. Undo must restore the relevant
  caret, selection, virtual-column, and mode-local cursor state without
  defaulting to document offset 0.

- [x] Replacing a rectangular column selection positions the carets on the
  wrong side of the first inserted character. After typing the first character
  to replace the selected rectangle, every column caret must land immediately
  after that character so subsequent typing appends normally; currently the
  carets remain before it. The fix must cover real and virtual columns and keep
  the entire replacement as one undo transaction.

- [x] The `Alt+C` Column Number Tool must remember its last-used configuration,
  including radix, initial value, increment, repeat count, and padding mode,
  and restore those values when the dialog is opened again. Redesign the dialog
  as the compact form shown in the supplied reference: a radix group followed
  by aligned parameter rows in one small window. Remove the preview area and
  all preview-only update logic; the dialog should contain only the settings
  required to perform the insertion.

- [x] A single mouse click on a signal must only place the caret; it must not
  highlight or otherwise display every occurrence of the same signal name.
  Same-name signal highlighting and occurrence display must be activated only
  by double-clicking the signal.

- [x] The column-mode caret must be rendered in a clearly visible red and form
  one continuous vertical line across the complete selected row span, rather
  than appearing as separate or faint per-line caret fragments. This visual
  treatment applies only to column mode; the ordinary editor caret is unchanged.

- [x] `Shift+Alt+Arrow` does not currently provide column-mode keyboard
  selection. Holding `Shift+Alt` while pressing the arrow keys must create a
  rectangular selection and extend or contract its row and column boundaries;
  it must not be ignored or fall back to ordinary linear selection. Horizontal
  movement must continue to support virtual columns on shorter lines.

- [x] Typing `)` near an existing closing parenthesis can consume the new input
  and merely move across the existing character. A closing parenthesis must be
  inserted normally unless the editor can identify that exact character as the
  still-paired counterpart it automatically inserted for the current opening
  parenthesis; an arbitrary adjacent `)` must never cause the input to be lost.

- [x] Opening-parenthesis completion must be context-sensitive instead of
  unconditionally inserting `()`. When the caret is immediately before a
  clearly complete symbol, typing `(` must insert only the opening parenthesis.
  When the caret is inside a signal identifier, typing `(` must wrap the entire
  identifier as `(signal)` rather than insert an empty pair or split the token.
  Symbol boundaries must come from the editor's syntax context, not a loose
  character-only guess.

- [x] Formatter width-column alignment is still incorrect at the stable
  reproduction `pwm_codec_top.sv:59` in
  `C:\Users\14971\Desktop\codec\EN_DE_CODER_22\EN_DE_CODER_22.srcs\sources_1\new`.
  The declaration's packed or unpacked width does not align with peer entries.
  The eventual fix must correct the general syntax-aware width alignment rule
  for equivalent declaration/port rows and must not special-case this file or
  line number.

- [x] The synchronous `Ctrl+S` path repeatedly materialized the complete
  `QTextDocument`, probed the source more than once, and reread the saved file
  to establish baselines. Saving now reuses the editor's immutable cached
  `QString`, encodes it once, derives both raw-disk and logical-text SHA-256
  fingerprints from that encoding, performs one final conflict probe, and
  passes the fingerprints to document/external-sync state without rereading.

- [x] `DocumentSessionState::markSaved` and document-registry queries forced
  deep text copies while updating multiple views of the same document. They
  now pass Qt implicitly shared `QString` snapshots and assign the same saved
  snapshot to every view without per-view character-buffer duplication.

- [x] Hidden Problems and Activity panels continued to mutate expensive text
  widgets during analysis publication. Hidden Problems refreshes are skipped;
  Activity events are coalesced while visible and rebuilt from the service's
  bounded event history when the dock becomes visible again.

- [x] Semantic request coalescing could omit exactly one pending clean file
  when another file initiated the next save request. The common one-file path
  now merges that file without allocating multi-file lookup sets, while the
  existing worker-prepared immutable snapshot publication and asynchronous
  retirement boundary remain the sole semantic publication mechanism.

## Feature Ideas

## Discussion Needed

## Done

- [x] Replace the former persistent short-code editor mode with the application-level F24 Command Layer. Its retained canonical commands include `go <number>`, `go module`, `go package`, `go endmodule`, `clear right`, `select begin end`, `repeat action`, and `help`; Enter explicitly executes the ranked selection, and module/package pickers retain keyboard ownership after opening. Column Number Tool remains independent on Alt+C.
- [x] After switching or closing workspace tabs, the first return to the Design tab still rebuilt the design hierarchy and caused a visible pause. Navigation now saves Design hierarchy state per workspace/file scope, restores it on `workspaceActivated`, keeps the restored cache valid across unrelated global snapshot revision changes, and invalidates only the active workspace Design cache when files, symbol analysis, or explicit Design top changes require it.
- [x] Switching or closing workspace tabs still behaved like reopening a project: it cleared the active ProjectModel, discarded the scanned file list, emitted `workspaceOpened`, rescanned the directory, and retriggered full workspace symbol/relationship analysis. Workspace entries now retain their scanned file cache, activation restores root+files in one ProjectModel update, `workspaceOpened` is reserved for newly opened workspaces, Navigation follows `workspaceActivated`, and the workspace symbol controller skips automatic analysis for a workspace that already completed in the current session while cancelling stale relationship work on activation.
- [x] Design view can show stale hierarchy from a previously active workspace and closing/switching workspaces can stall on unnecessary hierarchy rebuilds, reproduced with `huge_prj`. Design top inference and hierarchy expansion now accept an active workspace file scope, NavigationManager caches that scope with the hierarchy, empty or not-yet-scanned workspaces render an empty Design instead of falling back to the global index, and `workspaceClosed` clears the Design view without rebuilding in the no-active-workspace intermediate state.
- [x] Regression: after double-click selecting a symbol, Ctrl+click jump-to-definition can select all text between the call site and the definition in large design contexts, reproduced on `test_sv/huge_prj/vendor_ip_ctl.sv:1660` for `CC_IB_MCPL_SEG_BUF_RAM_ADDR_WD`. Ctrl+click navigation now avoids moving the system mouse while the left button is still pressed and consumes pending navigation mouse moves/releases, so the editor cannot turn the jump into a drag selection.
- [x] Column selection zero-width rendering is wrong. Zero-width rectangular selections now render as one vertical caret per selected line, including virtual-column positions on short lines, without broad text-selection highlighting.
- [x] Add `Ctrl+D` duplicate selection/line editor action. With a selection, it duplicates the selected text and selects the new copy; without a selection, it duplicates the current logical line below while preserving the cursor column, including final lines without trailing newlines.
- [x] Refine column selection to the requested Notepad++-like interaction model. The normal editor caret is the anchor, `Shift+Alt+click` sets or updates the endpoint, `Shift+Alt+Arrow` adjusts the endpoint, plain click or `Esc` exits, and text input, Backspace, Delete, virtual-column padding, rectangular Copy/Cut/Paste operate on every selected line.
- [x] Add `Alt+Up` / `Alt+Down` line-block move. It moves the current logical line or every logical line touched by the selection, preserves cursor or selection range, treats a selection ending at next-line column 0 as excluding that line, and reports that `Alt+Up/Down` is disabled while column selection is active.
- [x] Global or package-defined enum values are not semantically highlighted at use sites, reproduced on `test_sv/new/elec_phy_import/elec/elec_inj.sv:1094` for `E_NOISE_WGN`. Semantic decorations now derive usage candidates from the current file plus visible imported, qualified, include, and global/package records, including imports discovered through recursively resolved include files such as `_svh.svh` and package-owned enum values whose direct owner is the typedef enum such as `noise_type_e`, while current-file symbols suppress external same-name candidates.
- [x] Signal Kernel Graph hover preview flickers and can move outside the graph viewport. The graph no longer opens the code preview on hover; right-clicking a node opens or switches the popup, left-clicking or right-clicking blank graph space closes it, double-click navigation remains on left button, and popup placement uses the clicked node's viewport/global rectangle so the popup stays inside the graph viewport without overlapping the node box.
- [x] Signal Kernel Graph node double-click navigation is currently broken. Node double-click now closes the hover popup and navigates through both item-level handling and a graph-view fallback that resolves the node under the cursor when `ScrollHandDrag` intercepts item events.
- [x] Redesign Signal Kernel Graph node and module presentation. Nodes now show the signal/access-path on line 1 and type/source information on line 2, while relationship/evidence/module detail moves to hover. The graph draws a module frame for every represented module, including the kernel module, and frames all input, kernel, and output nodes owned by that module.
- [x] Split Signal Kernel Graph inputs into dependency lanes instead of a flat input list. Inputs now carry Data, Control, or Timing lane metadata; the panel renders lane bands before nodes, classifies clocks/resets as Timing, condition-like evidence as Control, and ordinary RHS dependencies as Data. Module frames show ownership while lanes show how each input affects the kernel.
- [x] Ctrl+S save shortcut does not work in manual testing, even though the Save action still declares `Ctrl+S` in `mainwindow.ui`. Registered the Save action as an application-level shortcut on the main window and verified current-editor save behavior.
- [x] After Ctrl+click jumping to a package definition, mouse side-button back navigation behaves abnormally and requires two presses. Navigation history now records the pre-jump source once before cross-file line jumps.
- [x] After Ctrl+click jumping to an include file, mouse side-button back navigation fails. Include Ctrl+click now routes through the navigation coordinator so the source location is pushed to back history.
- [x] After double-click selecting a symbol, Ctrl+click navigation selects all text from the jump origin to the definition location. Ctrl+click navigation now clears the active selection at the clicked position before jumping.
- [x] If the mouse is already hovering a symbol, pressing Ctrl does not immediately show the definition hover; it only appears after a small mouse move. The editor now remembers the last mouse position and refreshes hover there when Ctrl is pressed.
- [x] Double-click numeric base-conversion hover for values such as `'haaaa` disappears after a tiny mouse move. Numeric hover pinning now accepts double-click selections that cover the literal digits even when the quote/base prefix is outside the selection.
- [x] Ctrl+click jump-to-definition can still show a duplicate definition hover at the definition site. Ctrl hover now suppresses definition preview popups when the resolved target is the current editor location.
- [x] When multiple workspaces are open, switching the active workspace should hide editor tabs that belong to other workspaces, so each workspace tab shows only its own files instead of mixing all open editor tabs together. Scratch tabs and files outside all opened workspaces are scoped as global tabs and stay visible in every workspace.
- [x] Add a visible close action for workspace tabs/workspaces. Workspace tabs now show close buttons, closing a workspace prompts for unsaved files in that workspace before removing its editor tabs, switches the active workspace predictably, tears down the previous watcher, and leaves scratch/external tabs available.
- [x] Add workspace alias rename from the workspace tab right-click menu. The action is named `Rename`, edits only the workspace alias/display name, preserves the disk folder path, rejects empty or duplicate aliases, and updates workspace tabs plus recent-workspace metadata.
- [x] Add editor font zoom with `Ctrl+Shift+mouse wheel`: wheel up increases editor font size, wheel down decreases editor font size, and `Ctrl+mouse wheel` remains a fast-scroll gesture instead of changing font size.
- [x] Change bracket range quick-select from `Ctrl+left-click` to `Alt+left-click`, so `Ctrl+left-click` inside `[PW-1:0]` and similar ranges remains available for jump-to-definition while `Alt+left-click` selects the range body.
- [x] Enhance bracket range Up/Down adjustment for parameter or expression bounds. Parameter bounds now become `P_TEST+1` / `P_TEST-1`, numeric left-bound Down is clamped so it does not move below a numeric right bound, and complex expressions are parenthesized before arithmetic such as `[(P_TEST0*P_TEST1)-1:0]`.
- [x] Add a dedicated semantic highlight role for enum values at declaration and use sites, so enum constants such as `IDLE`, `BUSY`, and `DONE` are visually distinct from enum types, enum variables, parameters, macros, and ordinary identifiers while richer enum details remain in hover/double-click flows.
- [x] Add a dedicated semantic highlight role for typedef/type-alias names at use sites, such as `type_def_type test;`, so alias types are visually distinct from ordinary identifiers and not limited to declaration-site `PackageClassType` coloring.
- [x] Add semantic highlighting for parameter and localparam references at use sites, such as `[P_WIDTH-1:0]`, `assign x = P_DEPTH;`, and parameter expressions. References now reuse the parameter semantic role instead of falling back to ordinary identifier coloring.
- [x] Add dedicated semantic highlighting for module ports, so input/output/inout port names are visually distinct from internal signals at declaration and use sites.
- [x] Add `Ctrl+W` smart selection expansion. It now expands symbol -> member/hierarchical expression -> current parenthesized expression content -> next outer parenthesized expression content, and starts from operator/number positions inside parentheses by selecting the parenthesized expression content.
- [x] Add selected-symbol occurrence navigation in the current file. When a symbol is selected, `Ctrl+E` jumps to the next occurrence/call site and `Ctrl+Q` jumps to the previous occurrence/call site, preserving selection and wrapping at file boundaries.
- [x] Add current-file `Ctrl+R` safe rename MVP. It prompts for a valid identifier, renames all current-file word-boundary occurrences in one edit block, preserves similarly prefixed names, and offers `Force rename` / `Rename conflicting definition first` choices for current-file name conflicts.
- [x] Extend `Ctrl+R` safe rename beyond the current-file MVP. The app-level path now resolves the selected symbol, builds a cross-file edit plan for definition and resolved reference occurrences from semantic file contents/open editors, applies edits through `TabManager` so files stay tracked as open/dirty documents, detects new-name definition conflicts with `Force rename` and `Rename conflicting definition first`, rejects intermediate conflict names, and supports missing-definition creation through raw declarations or structured template expansion inserted before first use and after required type definitions such as enum/typedef records.
