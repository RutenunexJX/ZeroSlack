# ZeroSlack Current Goal

Product version: `v0.3.1`

## Objective

Deliver a verifiable 0.3.1 reliability patch that corrects formatter and
column-mode geometry, keeps the explicit `Ctrl+Space` palette attached to the
editing context, and removes avoidable synchronous and semantic work from
`Ctrl+S`.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.3.1.
- `Ctrl+Space` exposes Symbols, Templates, and Commands with keyboard category
  switching, caret-relative placement, screen clamping, and context-correct
  semantic filtering.
- Column-mode rendering and edits resolve through the same Qt text-layout
  geometry for tabs, proportional widths, and virtual columns.
- Formatter output preserves non-whitespace tokens, aligns top-level ternary
  operators, normalizes bracket-edge whitespace, and remains idempotent on the
  supplied full-file reproduction.
- A clean `Ctrl+S` performs no write or analysis request. Changed saves remain
  dependency-aware, asynchronous, generation-safe, and avoid Slang when the
  semantic snapshot already matches or the edit is trivia-only.
- Save completion does not rebuild hierarchy or refresh every editor when the
  classified change cannot alter those views.
- Focused and full regression targets pass without fixture-specific rules or
  relaxed thresholds.

## Current status

Implementation and verification are complete. The configured Debug suite
passes all 86 tests, including formatter, GUI, scheduler, large-file
performance, workspace, semantic, RTL workflow, and policy-guard coverage. No
test threshold was relaxed. This task does not publish or package the revision.
