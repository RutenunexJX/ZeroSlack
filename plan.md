# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product and architecture goals. This file defines execution policy.

## Architecture Flow

- Start each turn from the current worktree and current tests.
- New semantic features should move through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- Keep UI panels as renderers of service or model output.
- Keep scheduler and analyzer code focused on timing, lifecycle, extraction, and publication.
- Move policy into services only when there is a real production boundary.

## Good Work Blocks

- Query Service or feature service read paths.
- Report/model shaping for semantic panels.
- Focused UI rendering of stable reports.
- Real fixture coverage tied to production changes.
- Editor or project boundary cleanup with a clear ownership target.

## Risky Work Blocks

- Broad simultaneous changes across `TabManager`, `DocumentModel`, and `MyCodeEditor`.
- Async scheduler lifecycle changes.
- `SemanticIndex` snapshot publication or analyzer write-back changes.
- Relationship analysis lifecycle changes that share controller state.
- Migrations where signal lifetime or API shape is not stable.

Reduce batch size when blocks share core files or API boundaries.

## Validation

- For narrow feature changes, run focused build/tests and static boundary scans first.
- For shared semantic, scheduler, editor, or project boundary changes, run full Ninja and full `ctest --output-on-failure`.
- Before committing, run `git diff --check`, changed-file ASCII/trailing-whitespace scans, and forbidden-file guards.
- Prefer real workspace fixtures such as `test_sv/new` for semantic feature expansion.

## Commit Policy

- Keep local commits coherent and architecture-oriented.
- Do not push unless explicitly asked.
- Do not discard user changes.
- Keep docs short and current; replace stale content instead of accumulating history.
