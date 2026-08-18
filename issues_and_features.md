# Issues And Feature Ideas

This file is a running checklist for problems to fix and new features to consider.

## Issues

### Completed in v0.4.3

- [x] Triple-clicking now selects the complete logical line, including the line
  break when present, while preserving existing double-click symbol
  highlighting and mode-specific mouse gestures.

### Completed in v0.4.2

- [x] Enter in the compact `Ctrl+R` rename popup now prepares and applies the
  validated semantic transaction instead of stopping after parameter capture.
- [x] Trivia-only save remapping now updates diagnostic document revisions and
  exact ranges, preserving error/warning underlines, gutter icons, and overview
  markers after formatter or whitespace edits.
- [x] `Ctrl+Space` Symbols supports `m <filter>` for module-only lookup.
- [x] Windows Shift+/ key reporting is normalized so `Ctrl+Shift+/` reliably
  executes Uncomment through the Action Registry.
- [x] The editor context menu provides Toggle Selection Case and no longer
  lists Replace, comment, uncomment, indent, or unindent; the removed entries
  retain their commands and shortcuts.
- [x] ZeroSlack has an application icon shared by Qt windows and the Windows
  executable resource.

### Completed in v0.4.1

- [x] Holding `Alt+Up` or `Alt+Down` must continuously move the current logical
  line or selected lines. Auto-repeat now executes one move per event, and
  downward cursor restoration uses a stable pre-edit line number so the caret
  follows the moved text through the final non-empty line. This supersedes the
  v0.3.2 decision to consume repeated events.
- [x] After column mode is active, `Shift+Left` and `Shift+Right` now extend or
  contract the rectangular selection without requiring Alt to remain held.
  `Shift+Alt+Arrow` remains the explicit gesture for entering column mode.

### Completed in v0.3.2

- [x] A column-mode caret selected immediately after a semicolon could render
  one character earlier. Text columns now use discrete insertion boundaries,
  real positions use exact `QTextCursor` geometry, and the final half-cell
  snaps to EOL at normal and fractional display scaling.
- [x] Holding `Alt+Up` or `Alt+Down` could feed repeated move actions and make
  the current logical line continue moving. Auto-repeat events are consumed
  after the initial action; separate physical presses still move one row. This
  historical behavior was superseded by the v0.4.1 continuous-move decision.

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

### F24 command layer gestures and input reliability

- [x] Pressing and holding F24 opens the command search layer immediately;
  do not require `F24+Space` or another explicit search-mode chord. Releasing
  F24 closes the layer and never executes the currently highlighted search
  result. Enter is the only way to execute a fuzzy-search result.
- [x] Reserve direct one-key actions only while the search query is empty. If
  the first key pressed while F24 is held has a registered direct binding, keep
  it as a pending chord instead of appending it to the query; release that key
  while F24 remains held to execute the action exactly once. Releasing F24
  first, pressing Escape, losing application focus, or changing modal context
  cancels the pending action. Ignore key auto-repeat. Once an ordinary search
  query has begun, all subsequent printable keys are search text and direct
  bindings are disabled until the query is cleared.
- [x] Add the first direct binding as `F24+D`: delete the current selection
  without using the clipboard. It supports ordinary, multi-cursor, and column
  selections as one undo transaction, preserves the viewport, and is disabled
  with a precise reason when no deletable selection exists. The action must use
  the common Action Registry and execution history rather than a private editor
  shortcut path.
- [x] Treat an F24 press/release with no intervening keyboard, mouse, picker,
  or modal action as a tap that repeats the latest successful repeatable Action.
  Re-evaluate that Action's current workspace/editor/semantic preconditions
  before executing; unavailable actions produce a concise reason and no side
  effect. Cancelled and failed actions do not replace history, Repeat Last does
  not record itself, and High+Diff actions retain their preview requirement.
- [x] Fix rapid F24 search input loss at the input-state boundary. Capture each
  accepted `QKeyEvent::text()` synchronously in one authoritative query model,
  then update matching and presentation; do not reconstruct characters from
  `event->key()`, depend on a focused text widget, or use debounce/timers to hide
  dropped events. Update the result model incrementally instead of clearing and
  rebuilding the complete command list for each character.
- [x] Add event-level regression tests for rapid text entry without delays,
  overlapping physical key presses/releases, auto-repeat, direct-key release,
  F24 released before the direct key, Enter-only search execution, tap-to-repeat,
  and repeat rejection after context invalidation. Assert the exact query text
  and exactly-once Action execution rather than only checking that the panel is
  visible.

### Ctrl+Space Symbols contextual filtering

- [x] Extend `Ctrl+Space > Symbols` with an optional leading type selector.
  The grammar is `<selector><space><name filter>`: `w ` filters wire symbols,
  while `w` and `w_data` remain ordinary name searches. An empty query already
  means all visible symbols, so there is no `v` selector. Unknown selectors are
  treated as ordinary name text.
- [x] Retain the simple selectors `w` (wire), `r` (reg), `l` (logic), `p`
  (parameter), `lp` (localparam), `t` (task), and `f` (function). Redesign the
  type-family selectors as `td` (all typedefs, including enum/struct typedefs),
  `e` (all enum symbols), `et` (enum type), `ee` (enum entry/value), `ev`
  (enum variable), `s` (all struct symbols), `st` (packed or unpacked struct
  type), `sv` (packed or unpacked struct variable), and `sm` (struct member).
  Remove the former `u`, `ne`, `nsp`, `ns`, and `sp` selector aliases rather
  than retaining two vocabularies. Keep module/interface/template/macro/process/
  assign operations out of Symbols so category responsibilities remain clear.
- [x] When the caret is inside or immediately after an existing identifier,
  seed the Symbols name query from the identifier prefix to the left of the
  caret and anchor the complete identifier as the replacement range. Activating
  a result replaces that complete identifier; opening on whitespace inserts at
  the caret, and Escape changes nothing. A selected text range is the explicit
  query/replacement seed. Keep the anchor valid while the popup is open and
  close safely if the underlying edit invalidates it.
- [x] Rank unqualified candidates by lexical visibility first: current local
  scope, current module/interface/package, then visible imported package
  records. Within the same visibility tier, rank by name score and source-line
  distance. Never expose another module's internal symbols merely because they
  are textually nearby.
- [x] In a member-access context such as `struct_var.|`, `nested.member.|`, or
  `array[i].|`, resolve the receiver expression's semantic struct type and show
  only members of that type. Replace only the member fragment after the dot.
  An explicit `sm ` selector remains a hard member filter, but unresolved
  receiver types fall back to ordinary visible-symbol completion rather than
  showing every struct member in the workspace.
- [x] Infer the expected enum type in blocking assignment (`enum_var = |`),
  nonblocking assignment (`enum_var <= |`), equality/inequality conditions,
  reversed comparisons, and enum `case` item positions. Matching enum values
  rank before other visible symbols by default; explicit `ee ` is a hard filter
  and, when an expected type is known, returns only that enum's values. Parse
  `<=` from the syntax tree so a nonblocking assignment is not confused with a
  less-than-or-equal expression.
- [x] Apply query precedence in this order: explicit type selector, resolved
  member/expected-type context, then ordinary visible-symbol search. Use the
  current Tree-sitter buffer for incomplete syntax and the semantic snapshot
  for symbol type, ownership, import visibility, and enum membership; do not
  implement these decisions with regular-expression scans or subtitle text.

### Parenthesis range wrapping

- [x] Keep unselected `(` insertion conservative and deterministic. When the
  caret is inside a token, wrap only the smallest complete syntax atom, such as
  an identifier, member access, index expression, or call. For example,
  `~signal` becomes `~(signal)`; do not infer that the unary operator or a wider
  logical expression belongs inside the new parentheses.
- [x] When text is selected, typing `(` wraps exactly that selection. Use the
  existing `Ctrl+W` smart-selection expansion to grow from a symbol to wider
  expression ranges before wrapping, so users explicitly choose whether the
  result is `(signal)`, `(~signal)`, or a larger condition.
- [x] Tree-sitter may provide selectable syntax ranges but must not decide the
  intended logical grouping. In incomplete or temporarily invalid code, fall
  back to stable lexical boundaries and preserve exact manual selection. Do not
  add per-operator automatic inclusion rules or a separate modal parenthesis
  adjustment state.

### Unified Ctrl+R semantic rename

- [x] Make `Ctrl+R` a unified symbol-rename entry point. Resolve the symbol at
  its definition or any bound reference, then open a compact rename popup near
  the editor caret instead of opening the bottom High+Diff page immediately.
  Show the old name, semantic kind, owner/scope, new-name field, and affected
  reference/file counts. Escape cancels without edits; Enter builds the plan.
- [x] Retain port, parameter, and localparam rename, and add wire, logic, reg,
  enum type/variable/value, struct type/variable/member, typedef type, and
  variables declared with typedef types. A typedef-typed variable is renamed as
  the variable only; renaming it must not rename its declared typedef type.
- [x] Resolve identity and references from the current Slang snapshot and
  stable symbol key rather than bulk replacement of equal identifier text.
  Local variables remain inside their exact lexical scope; enum values remain
  bound to their enum; struct members remain bound to their resolved struct
  type; imported and qualified package references may update across workspace
  files. Same-name symbols in other scopes, enums, or structs must remain
  unchanged.
- [x] Validate identifier syntax and same-scope conflicts inline, but defer the
  complete plan until Enter so typing in the popup does not repeatedly scan the
  workspace. Reject stale semantic generations, incomplete bindings, ambiguous
  definitions, changed documents, and conflicting declarations with a precise
  reason in the popup.
- [x] Apply a proven single-file non-structural rename as one undoable atomic
  edit. Route cross-file and structural renames through the existing High+Diff
  preview and explicit confirmation transaction. Keep the current structured
  instance-association handling for ports and parameters behind this same
  popup. Do not restore the old full-text same-name scan as the authoritative
  rename implementation.

### Lexical and structural editor movement

- [x] Use one shared lexical-boundary engine for mouse word selection,
  Ctrl+Left/Right, Ctrl+Shift+Left/Right, and Ctrl+Backspace/Delete. Split
  identifiers into underscore, camel-case, acronym, and numeric fragments
  (`aaa_bbb_ccc`, `axiReadData`, `AXIReadData`, `data32Width`) while treating
  numbers, strings, operators, punctuation, and horizontal whitespace as
  deterministic units. Double-clicking spaces or tabs selects the complete
  contiguous horizontal whitespace run on that line, never the newline.
- [x] Keep Ctrl+Left/Right at lexical-fragment granularity and skip horizontal
  whitespace as a navigation destination. Ctrl+Shift+Left/Right extends the
  selection over the same boundaries. Ctrl+Backspace/Delete removes one
  adjacent fragment; deleting a complete identifier remains explicit through
  Ctrl+W followed by Backspace/Delete or through mouse selection, so the editor
  never guesses which granularity the user intended.
- [x] Add Ctrl+Alt+Left/Right structural movement between high-value editable
  fields obtained from the current Tree-sitter buffer. Conditions navigate
  principal operands; declarations navigate direction/type, packed range,
  name, unpacked dimensions, and initializer; calls and ternaries navigate
  arguments/branches; module instantiations navigate module type, parameter
  formal/actual, instance name, and port formal/actual fields. Ordinary lexical
  movement remains available inside each field.
- [x] In list-shaped constructs such as module ports, parameters, declarations,
  and instance associations, Ctrl+Alt+Up/Down moves to the same field role in
  the preceding or following item. Adding Shift selects by the corresponding
  structural movement. Do not use screen-column proximity as the field match.
- [x] Structural navigation must be a read-only aid: use live Tree-sitter nodes
  only to propose caret anchors, fall back to lexical boundaries when the
  incomplete buffer cannot provide a stable structure, and never consult a
  stale Slang snapshot or infer intended expression grouping. Briefly flash the
  destination field after a structural jump so variable-distance movement is
  visually attributable.
- [x] Add configurable line-boundary selection actions on Alt+Left and
  Alt+Right. The first press selects from the fixed original caret anchor to
  the first or last non-whitespace content boundary; a repeated press in the
  same direction extends to the physical line edge and includes indentation or
  trailing whitespace. Never include the newline. Reversing direction retains
  the original anchor, Escape cancels the selection, and multi-cursor mode
  applies the action independently to each caret. Column Selection and Slot
  Mode retain ownership of their active key handling.

### Formatter bracket-suffix alignment

- [x] Treat a top-level index, bit-select, part-select, or unpacked-dimension
  chain as a syntax field separate from its base expression. Within one
  homogeneous alignment group, place the first `[` of that suffix chain in a
  shared column. Keep consecutive suffixes tight (`value[i][j]`,
  `value[index][23:0]`) and do not align brackets nested inside index
  expressions, calls, concatenations, or unrelated syntax roles.
- [x] Apply the rule to consecutive assignment statements in the same lexical
  block. Align the base left-hand expression, optional suffix-chain column,
  assignment operator, and the existing ternary columns independently. Within
  corresponding true or false ternary operands, align comparable indexed
  references by their own base-expression and suffix fields; never pull every
  bracket in the complete expression into one global column. Thus rows such as
  `rx_step_len[i]` and `tx_axi_wr_eff_len[i]` gain a common `[i]` column while
  `bar_reg[index][23:0]` remains one tight access chain. Do not merge assignments
  across `if`, `else`, loop, case, macro, indentation, or blank-line boundaries.
- [x] Apply the same suffix-field model to named module/interface instance
  associations. Preserve the existing formal-name, opening parenthesis,
  closing parenthesis, comma, and trailing-comment columns, while aligning the
  first suffix of actual expressions such as `tx_driv_flag[i]`,
  `tx_parity_check[i]`, and `tx_axi_wr_eff_len[i]`. Unindexed actuals reserve
  the suffix column only when needed by peers in the same association list.
- [x] Derive all fields from the live Tree-sitter syntax tree rather than
  regular-expression or character-column guesses. Formatting may change only
  horizontal whitespace and permitted line breaks; it must preserve every
  token, comment, expression, and association. Use the minimum padding needed,
  emit spaces rather than Tabs, and remain idempotent for both Format Selection
  and Format Document.
- [x] Add general regression fixtures for indexed and unindexed assignment
  rows, named associations, chained suffixes, part-selects, ternary expressions,
  comments, incomplete syntax, and mixed-width identifiers. Assert token-stream
  preservation, stable grouping, exact alignment columns, and second-pass
  idempotence; do not add file-specific rules for the supplied screenshots.

## Discussion Needed

## Done

- [x] Replace the former persistent short-code editor mode with the application-level F24 Command Layer. Its retained canonical commands include `go <number>`, `go module`, `go package`, `go endmodule`, `clear right`, `select begin end`, `repeat action`, and `help`; Enter explicitly executes the ranked selection, and module/package pickers retain keyboard ownership after opening. Column Number Tool remains independent on Alt+C.
- [x] After switching or closing workspace tabs, the first return to the Design tab still rebuilt the design hierarchy and caused a visible pause. Navigation now saves Design hierarchy state per workspace/file scope, restores it on `workspaceActivated`, keeps the restored cache valid across unrelated global snapshot revision changes, and invalidates only the active workspace Design cache when files, symbol analysis, or explicit Design top changes require it.
- [x] Switching or closing workspace tabs still behaved like reopening a project: it cleared the active ProjectModel, discarded the scanned file list, emitted `workspaceOpened`, rescanned the directory, and retriggered full workspace symbol/relationship analysis. Workspace entries now retain their scanned file cache, activation restores root+files in one ProjectModel update, `workspaceOpened` is reserved for newly opened workspaces, Navigation follows `workspaceActivated`, and the workspace symbol controller skips automatic analysis for a workspace that already completed in the current session while cancelling stale relationship work on activation.
- [x] Design view can show stale hierarchy from a previously active workspace and closing/switching workspaces can stall on unnecessary hierarchy rebuilds, reproduced with `huge_prj`. Design top inference and hierarchy expansion now accept an active workspace file scope, NavigationManager caches that scope with the hierarchy, empty or not-yet-scanned workspaces render an empty Design instead of falling back to the global index, and `workspaceClosed` clears the Design view without rebuilding in the no-active-workspace intermediate state.
- [x] Regression: after double-click selecting a symbol, Ctrl+click jump-to-definition can select all text between the call site and the definition in large design contexts, reproduced on `test_sv/huge_prj/vendor_ip_ctl.sv:1660` for `CC_IB_MCPL_SEG_BUF_RAM_ADDR_WD`. Ctrl+click navigation now avoids moving the system mouse while the left button is still pressed and consumes pending navigation mouse moves/releases, so the editor cannot turn the jump into a drag selection.
- [x] Column selection zero-width rendering is wrong. Zero-width rectangular selections now render as one vertical caret per selected line, including virtual-column positions on short lines, without broad text-selection highlighting.
- [x] Add `Ctrl+D` duplicate selection/line editor action. With a selection, it duplicates the selected text and selects the new copy; without a selection, it duplicates the current logical line below while preserving the cursor column, including final lines without trailing newlines.
- [x] Refine column selection to the requested Notepad++-like interaction model. The normal editor caret is the anchor, `Shift+Alt+click` sets or updates the endpoint, `Shift+Alt+Arrow` enters or adjusts the endpoint, active column mode also accepts `Shift+Left` / `Shift+Right`, plain click or `Esc` exits, and text input, Backspace, Delete, virtual-column padding, rectangular Copy/Cut/Paste operate on every selected line.
- [x] Add `Alt+Up` / `Alt+Down` line-block move. It moves the current logical line or every logical line touched by the selection, supports continuous key auto-repeat, preserves cursor or selection range, treats a selection ending at next-line column 0 as excluding that line, and reports that `Alt+Up/Down` is disabled while column selection is active.
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
