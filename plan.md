# ZeroSlack Current Plan

Product version: `v0.3.1`

## Current status

- `Ctrl+Space` is the sole explicit insertion/control surface. Its Symbols,
  Templates, and Commands categories keep semantic scope, reusable templates,
  package/header workflows, and application actions visibly separate. It is
  anchored to the editor caret and Left/Right switches categories in place.
- The former `;cmd` / `;;cmd` runtime trigger is retired. F24 is limited to
  editor-oriented command-layer actions; file operations and shortcut-only
  occurrence navigation are not shown there.
- A clean `Ctrl+S` performs no disk write and emits no save/analysis signal.
  Changed saves classify their impact, avoid Slang for trivia-only edits, and
  publish semantic work from the existing low-priority worker path. Directory
  rescans remain reserved for source membership changes.
- Undo/redo preserves vertical and horizontal viewport positions; keyword
  ghost completion appends a separating space; ternary continuation alignment
  and bracket-edge whitespace normalization are covered by formatter
  regressions and a full-file idempotence probe.
- Column-mode editing, mouse mapping, and red-caret rendering now share one Qt
  text-layout geometry source, including tabbed text and virtual columns.
- Synchronize Instance Connections can add missing named ports and remove
  obsolete named ports across every provable source instance in one High+Diff,
  all-or-nothing workspace transaction.
- Implementation and verification are complete. The configured Debug suite
  passes all 86 tests, including formatter, GUI, scheduler, large-file
  performance, workspace, semantic, RTL workflow, and policy-guard coverage.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
