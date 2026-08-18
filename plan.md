# ZeroSlack Current Plan

Product version: `v0.4.1`

## Current status

- `Alt+Up` / `Alt+Down` accepts operating-system auto-repeat so holding either
  key continuously moves the logical line or selected line range. Cursor
  restoration uses the pre-edit logical line number and remains attached to
  the moved text near the final document line.
- An existing column selection accepts `Shift+Left` / `Shift+Right` to extend
  or contract its horizontal span; `Shift+Alt+Arrow` remains the entry gesture.
- F24 opens command search immediately. Enter alone executes fuzzy-search
  results; direct gestures are active only for an empty query, and an empty tap
  repeats the last currently valid repeatable action.
- `Ctrl+Space` Symbols supports explicit kind selectors, replacement of the
  current identifier, scope-ranked candidates, resolved struct members, and
  expected enum values without exposing unrelated module internals.
- `Ctrl+R` resolves semantic identity before opening one rename surface. Proven
  local renames are atomic; cross-file and structural plans retain High+Diff
  review and generation validation.
- Mouse word selection and Ctrl movement/deletion share one lexical model.
  Ctrl+Alt navigates Tree-sitter structural fields, while Alt+Left/Right selects
  anchored line boundaries and preserves independent multi-cursor anchors.
- Parenthesis insertion wraps an explicit selection or the smallest stable
  syntax atom. Wider grouping remains an explicit `Ctrl+W` selection decision.
- Formatter groups align terminal bracket suffixes independently in assignment
  targets, ternary operands, and named associations while preserving tokens,
  comments, grouping boundaries, and second-pass idempotence.
- A clean configure and full Debug build pass. The complete configured suite
  passes all 86 tests, including the line-operation, structural-input, GUI,
  workspace, semantic, performance, architecture, and version-policy coverage.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
