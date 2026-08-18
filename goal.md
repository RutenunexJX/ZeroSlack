# ZeroSlack Current Goal

Product version: `v0.4.2`

## Objective

Deliver a verifiable 0.4.2 editing-command reliability patch: make compact
semantic rename apply on Enter, preserve diagnostics after trivia-only saves,
add module filtering and selection-case editing, restore Uncomment, simplify
the context menu, and establish a stable ZeroSlack application icon.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.4.2.
- Ctrl+R Enter applies a generation-checked rename to every affected document
  buffer and retains one protected transaction undo position.
- Diagnostic ranges remain current after formatter and whitespace-only saves.
- `m <filter>`, Toggle Selection Case, and `Ctrl+Shift+/` have focused tests;
  removed context entries retain their Action descriptors and shortcuts.
- The Qt window and packaged Windows executable expose the new icon.
- The complete configured test suite passes without relaxed assertions.

## Current status

Implementation and verification are complete. The generated GUI metadata and
current documents agree on `v0.4.2`, and all 86 configured Debug tests pass,
including the focused rename, completion, diagnostics, shortcut, context-menu,
GUI integration, performance, and version-policy coverage. This revision is
the `v0.4.2` release source.
