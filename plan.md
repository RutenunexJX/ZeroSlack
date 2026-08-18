# ZeroSlack Current Plan

Product version: `v0.4.2`

## Current status

- Enter in the compact `Ctrl+R` editor validates and applies the semantic
  rename transaction across open and previously closed document buffers.
- Trivia-only saves remap diagnostic revisions and precise source ranges, so
  editor underlines, gutter indicators, and overview markers remain visible.
- `Ctrl+Space` Symbols accepts `m <filter>` for workspace module lookup while
  all other selectors retain scope-aware filtering.
- `Ctrl+Shift+/` handles the Windows Shift+/ key representation and executes
  the registered Uncomment Action.
- The editor context menu includes an undoable Toggle Selection Case operation.
  Replace, comment, uncomment, indent, and unindent remain available through
  their shortcuts but no longer occupy the context menu.
- Qt and Windows executable resources use the new ZeroSlack application icon.
- A clean configure and full Debug build pass. The complete configured suite
  passes all 86 tests, including the line-operation, structural-input, GUI,
  workspace, semantic, performance, architecture, and version-policy coverage.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
