# ZeroSlack Current Goal

Product version: `v0.3.0`

## Objective

Deliver a verifiable 0.3.0 workflow revision that consolidates explicit
insertion under `Ctrl+Space`, narrows F24 to useful editor actions, prevents
save-time workspace rescans, and synchronizes module-port changes across
provable source instances through the guarded RTL edit architecture.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.3.0.
- `Ctrl+Space` exposes Symbols, Templates, and Commands with keyboard category
  switching and context-correct semantic filtering.
- Standard/user templates, complete module instantiation, package import,
  header include, and header creation retain structured insertion and slots.
- `;cmd` / `;;cmd` do not activate an inline completion session.
- F24 omits removed navigation/add actions, file operations, and shortcut-only
  selected-occurrence navigation while retaining repeat action.
- Ctrl+S does not rescan a workspace when only an existing file changes.
- Undo/redo preserves both scroll axes and keyword Tab completion inserts a
  separating space.
- Module-port synchronization updates every provable instance atomically and
  rejects stale, ambiguous, or unsafe plans.
- Focused and full regression targets pass without fixture-specific rules or
  relaxed thresholds.

## Current status

Implementation and verification are complete. The configured Debug suite
passes all 86 tests, including completion, GUI, action-registry, editor input
and performance, workspace watching, RTL connection planning, formatter,
shared-core, and policy-guard coverage. No test threshold was relaxed. This
task does not publish or package the revision.
