# ZeroSlack Current Plan

Product version: `v0.4.3`

## Current status

- Triple-clicking with the unmodified left mouse button selects the complete
  logical line at the pointer.
- Non-final line selection includes its line break; the last line remains
  bounded correctly when no trailing newline exists.
- Double-click symbol highlighting remains active after the second click and
  is cleared when the third click becomes a line selection.
- Column selection, signal selection, folding, source navigation, and
  multi-cursor handlers retain priority over ordinary triple-click selection.
- The complete configured Debug suite passes all 86 tests, including focused
  structural-input coverage and shared-core ABI/runtime guards.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
