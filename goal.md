# ZeroSlack Current Goal

Product version: `v0.2.2`

## Objective

Deliver a verifiable 0.2.2 maintenance revision that makes column editing,
undo cursor restoration, delimiter input, declaration alignment, and manual
save analysis deterministic without adding file-specific rules or deferred
idle refreshes.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.2.2.
- Column replacement, Shift+Alt keyboard selection, virtual columns, and
  undo/redo preserve their logical caret state.
- Contextual parentheses never swallow an unrelated closer, and same-name
  highlighting is entered only by double-click.
- Structured formatting aligns general packed-width declaration columns while
  changing only whitespace and line endings; format-on-save is absent.
- Ctrl+S reuses the cached text, performs one UTF-8 encoding for the atomic
  write and both fingerprints, queues semantic work asynchronously, and
  avoids hidden-panel or duplicate file-baseline work.
- Coalesced semantic requests retain a single pending clean file even when a
  different saved file triggers publication.
- Focused and full regression targets pass without fixture-specific rules or
  relaxed thresholds.

## Current status

Implementation and verification are complete. The optimized Release build
passes all 86 registered tests, including editor incremental/performance,
analysis scheduling, formatter, external-document synchronization, completion,
GUI smoke, and version/documentation guards. This task does not publish or
package the revision.
