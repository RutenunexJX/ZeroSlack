# ZeroSlack Current Plan

Product version: `v0.3.0`

## Current status

- `Ctrl+Space` is the sole explicit insertion/control surface. Its Symbols,
  Templates, and Commands categories keep semantic scope, reusable templates,
  package/header workflows, and application actions visibly separate.
- The former `;cmd` / `;;cmd` runtime trigger is retired. F24 is limited to
  editor-oriented command-layer actions; file operations and shortcut-only
  occurrence navigation are not shown there.
- Saving an existing source file produces a file-level workspace change and
  queues file-granular semantic work. Directory rescans are reserved for source
  membership changes.
- Undo/redo preserves vertical and horizontal viewport positions; keyword
  ghost completion appends a separating space; ternary continuation alignment
  remains covered by formatter regressions.
- Synchronize Instance Connections can add missing named ports and remove
  obsolete named ports across every provable source instance in one High+Diff,
  all-or-nothing workspace transaction.
- Cleanup and documentation are complete. The configured Debug suite passes
  all 86 tests, including the large-workspace `ow` workflow and the original
  editor performance budgets, without threshold changes.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
