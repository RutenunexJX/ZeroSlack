# Changelog

## [0.4.3] - 2026-08-18

### Whole-line mouse selection

- Added triple-click selection for a complete logical line while preserving
  double-click symbol highlighting and higher-priority column, multi-cursor,
  folding, and signal-selection gestures.
- Included a line break in the selection when present and handled the final
  line correctly when the document has no trailing newline.

## [0.4.2] - 2026-08-18

### Editing commands and desktop identity

- Made Enter in the compact `Ctrl+R` rename popup prepare and apply the
  validated rename transaction, including affected document buffers, while
  retaining the protected High+Diff transaction undo position.
- Preserved diagnostic revisions and exact source ranges across trivia-only
  saves so error/warning underlines, gutter icons, and overview markers remain
  current after formatter or whitespace edits.
- Added the `m <filter>` `Ctrl+Space` Symbols selector for workspace modules.
- Normalized the Windows `Ctrl+Shift+/` key report so Uncomment executes the
  registered shortcut reliably.
- Added an undoable, multiline-safe Toggle Selection Case context action and
  removed Replace, comment, uncomment, indent, and unindent from the editor
  context menu without removing their commands or shortcuts.
- Added a ZeroSlack circuit-trace application icon to the Qt resources and the
  Windows executable resource.

## [0.4.1] - 2026-08-18

### Keyboard movement reliability

- Restored continuous `Alt+Up` / `Alt+Down` movement on operating-system key
  auto-repeat instead of consuming repeated key presses.
- Stabilized repeated downward movement by restoring the caret from the
  pre-edit logical line number rather than a mutated `QTextBlock`.
- Allowed `Shift+Left` / `Shift+Right` to extend or contract an existing column
  selection while retaining `Shift+Alt+Arrow` as the entry gesture.
- Added controller, structural-input, and GUI regressions for repeated movement,
  cursor following, and horizontal column selection.

## [0.4.0] - 2026-08-18

### Contextual editing and command workflows

- Reworked F24 into an immediate fuzzy command layer with Enter-only search
  execution, cancellable direct-key gestures, selection deletion on `F24+D`,
  reliable rapid input, and an empty-tap repeat action.
- Extended `Ctrl+Space` Symbols with semantic kind selectors, identifier-aware
  replacement, lexical visibility ranking, struct-member resolution, and
  expected-enum-value ranking for assignment, comparison, and case contexts.
- Added a unified `Ctrl+R` semantic rename popup for ports, parameters,
  variables, typedefs, enums, structs, and members, retaining atomic local
  edits and High+Diff confirmation for cross-file or structural changes.
- Unified word selection, Ctrl movement, and Ctrl deletion on a shared lexical
  boundary engine; added Tree-sitter structural field navigation and anchored
  line-boundary selection, including multi-cursor behavior.
- Made parenthesis wrapping selection-driven and conservative for incomplete
  code, and aligned top-level index/part-select suffix fields in assignments,
  ternary operands, and named associations while preserving all source tokens.
- Added event-level, semantic-scope, rename, movement, multi-cursor, formatter,
  token-preservation, and idempotence regressions for the new behavior.

## [0.3.2] - 2026-08-17

### Column caret and line-move reliability

- Replaced accumulated pixel-width column inference with discrete document
  insertion columns and exact `QTextCursor` pixels for real text. The final
  half-cell now snaps to EOL, keeping a column caret selected after a semicolon
  on the boundary after that semicolon at normal and fractional display scale.
- Prevented operating-system key auto-repeat from repeatedly executing
  `Alt+Up` / `Alt+Down`; each physical press moves the current logical-line
  range at most once, while separate presses remain independent.
- Added GUI regressions for every ASCII insertion boundary, the semicolon/EOL
  caret, fractional scaling, and held-key repeat, while retaining the existing
  Tab, Unicode, virtual-column, clipboard, and line-operation coverage.

## [0.3.1] - 2026-08-17

### Editing geometry and save responsiveness

- Anchored the `Ctrl+Space` palette to the active editor caret, clamped it to
  the usable screen, and added in-place Left/Right category switching without
  interrupting query filtering.
- Unified column-mode edit, mouse, and red-caret coordinates on Qt text-layout
  geometry so tabbed text and virtual columns resolve to the same boundary.
- Extended whitespace-only formatting with lexical bracket-edge normalization
  and top-level ternary alignment, with idempotence and token-preservation
  coverage against the supplied full-file reproduction.
- Made a clean `Ctrl+S` a true no-op and removed redundant broad refreshes from
  changed saves. Matching semantic snapshots and trivia-only edits no longer
  invoke unnecessary Slang work; status messages no longer mislabel an
  incremental request as workspace-wide analysis.
- Replaced the diagnostics idle timer with one queued event-turn coalescing
  pass, preserving revision checks without adding save-time delay.

## [0.3.0] - 2026-08-16

### Explicit insertion and guarded synchronization

- Replaced the inline `;cmd` / `;;cmd` activation workflow with one explicit
  `Ctrl+Space` palette split into Symbols, Templates, and Commands. Semantic
  symbols remain limited to the active editor scope; templates cover built-in
  and user snippets, complete module instantiation, package import, header
  include, and header creation while preserving Slot Mode.
- Reduced F24 to editor-oriented actions by removing assignment/conditional
  navigation and independent add-signal/add-parameter/add-port actions, hiding
  file operations and shortcut-only selected-occurrence navigation, and
  retaining repeat action.
- Changed workspace watching so saves to an existing source emit one file-level
  change and do not trigger a directory rescan; source membership changes still
  refresh the workspace.
- Preserved both scroll axes across undo/redo and added the expected separating
  space when accepting keyword ghost completion with Tab.
- Extended guarded instance-connection editing to synchronize missing and
  obsolete named ports across all provable source instances in one High+Diff,
  all-or-nothing workspace transaction.
- Made MinGW shared-core export generation consume the active linker response
  file, excluding stale object files from export discovery.

## [0.2.2] - 2026-08-16

### Editor interaction and save-path update

- Restored logical cursor and mode-local column state across undo/redo, fixed
  rectangular replacement placement, added Shift+Alt keyboard column
  selection, and made column carets a continuous high-contrast red guide.
- Made delimiter completion context-sensitive, limited same-name occurrence
  highlighting to double-click, and redesigned Alt+C as a compact remembered
  configuration panel without a preview area.
- Extended structured declaration alignment to explicit packed-width columns
  while preserving the formatter's whitespace-only contract, and removed the
  format-on-save feature and its settings/action residue.
- Reduced synchronous Ctrl+S work by sharing the editor's cached immutable
  text snapshot, producing disk/logical fingerprints from one encoding,
  removing duplicate conflict probes and baseline disk reads, avoiding
  unchanged workspace-tab work, and coalescing Activity panel output.
- Preserved every clean saved change when semantic requests merge, including
  the one-pending-file case where a different file triggers the next request.
- Verified the revision with the optimized Release build and all 86 registered
  tests, including incremental editor and large-file performance coverage.

## [0.2.1] - 2026-08-15

### Editor reliability update

- Stabilized Format Document and Format Selection by retaining full syntax
  context, preserving immutable string/comment content, reporting conservative
  fallbacks, and aligning mixed ANSI parameter and port declarations.
- Corrected editor command ownership for ordinary and mode-specific Tab /
  Shift+Tab behavior, line duplication/deletion, structural Enter, completion,
  and template-slot navigation.
- Extended effective-literal inspection to display the packed numeric value of
  arbitrary ASCII strings while continuing to reject nonnumeric strings and
  include paths.
- Hardened editor action availability, multi-view formatting anchors, command
  dispatch, and expose-to-top regressions without adding source-file-specific
  behavior.
- Expanded formatter, effective-value, completion, line-operation,
  multi-cursor, template-slot, Tree-sitter, and GUI regression coverage.

## [0.2.0] - 2026-08-12

### Current milestone

- Established `VERSION` as the CMake, generated-header, application-display,
  and test baseline for the 0.2 series.
- Consolidated the Qt 6 application shell and shared `zeroslack_core` build,
  including theme-aware navigation, panels, editor surfaces, and the temporary
  editor drawer.
- Delivered incremental Tree-sitter editing and Slang-backed workspace
  semantics with revision-gated diagnostics, effective values, relationships,
  hierarchy, and analysis publication.
- Added workspace configuration, multi-workspace navigation, local session
  persistence with legacy `.zs` import, crash recovery, and external-document
  conflict handling.
- Added preview-first RTL inspection and editing workflows, including RTL
  Insights graphs, signal analysis, guarded rename/connection transformations,
  expose-to-top, instance-pair connection, and multi-signal propagation.
- Replaced cumulative handoff logs with current-facts documentation and a
  lightweight version/documentation consistency test.
- Extracted workspace session/lifecycle ownership from `MainWindow` into a
  dedicated coordinator with per-workspace isolation, debounced saves, and
  activation/close ordering regressions.
- Extracted four high-risk RTL action launch paths from `MainWindow` into an
  explicit coordinator that reuses the existing High+Diff, instance-pair,
  multi-signal, and shared workspace-document workflows.

Detailed historical verification records are preserved in the
[archive index](docs/archive/README.md).
