# Changelog

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
