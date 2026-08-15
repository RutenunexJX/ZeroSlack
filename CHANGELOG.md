# Changelog

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
