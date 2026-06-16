# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product and architecture goals. This file defines execution policy.

## Architecture Flow

- Start each turn from the current worktree and current tests.
- New semantic features should move through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- Keep UI panels as renderers of service or model output.
- Keep scheduler and analyzer code focused on timing, lifecycle, extraction, and publication; they must not own feature policy.
- Move policy into services only when there is a real production boundary.
- Expand RTL Insights only through stable semantic contracts and service-owned reports.

## Phases

### Phase A: Semantic State And Snapshot Publication

- Status: complete baseline.
- Make `SemanticIndexSnapshot` the single UI query truth.
- Define one publication and merge policy for workspace, open-document, dirty text, external file, and relationship analysis results.
- Add generation, version, and hash guards so stale async results cannot overwrite newer semantic state.
- Preserve open, dirty, and out-of-workspace documents when workspace results publish.
- Relationship analysis must not replace the visible semantic truth with an older or narrower snapshot.

### Phase B: Symbol Taxonomy And Source Role

- Status: complete baseline.
- Stabilize taxonomy/source-role helper rules first.
- Keep `sym_type_e` as legacy/raw collector kind during Phase B.
- Do not rush a broad `SymbolInfo` layout migration in Phase B.
- Move product semantic policy into `SymbolTaxonomy`, source-role helpers, Query Services, or feature services.

### Phase C: Semantic Metadata, Query Service, And UI Data Flow

- Status: complete baseline.
- Introduce stable semantic metadata after Phase B helper rules are stable.
- `SymbolInfo` may gain fields, or an equivalent metadata layer, for declaration kind, usage role, owner scope, visibility, source role, and raw collector kind.
- Keep `sym_type_e` available as raw/legacy collector kind during migration.
- Query Services and feature services should prefer stable semantic metadata over scattered raw enum checks.
- UI should consume reports/models, not semantic internals.
- CompletionService, DefinitionService, DiagnosticService, RelationshipService, and RTL Insights services should own query policy.
- Coordinators route state and refreshes; they should not encode semantic feature rules.
- Scheduler schedules and Analyzer executes analysis; neither owns product feature policy.

### Phase D: RTL Insights Expansion

- Current phase.
- Expand FSM graph, Signal Journey, Module Brief, Clock/Reset Domain Map, Semantic Diff, and code/document links through feature services.
- Use `test_sv/new` and similar real projects as first-class fixtures.
- Add focused tests at service level first, then UI smoke coverage where needed.
- Do not add feature-specific workarounds in UI, scheduler, analyzer, or coordinator code.
- Keep reports explicit: rows, display names, navigation payloads, diagnostics, and evidence should be shaped before UI render.

## Batch Policy

- Phase D can proceed in batches when blocks do not share service contracts or UI surfaces.
- Suitable Phase D batches: focused service tests, independent feature-service reports, UI render-only cleanup, and fixture expansion.
- Keep serial: snapshot publication rules, async generation guards, `SemanticIndex`/`sym_list` ownership changes, symbol taxonomy migrations, `SymbolInfo` layout changes, semantic metadata publication, raw enum compatibility changes, source role migration, and cross-service contract changes.
- Reduce batch size when blocks share core files, API boundaries, or real fixture expectations.

## Good Work Blocks

- Query Service or feature service read paths.
- Report/model shaping for semantic panels.
- Focused UI rendering of stable reports.
- Real fixture coverage tied to production changes.
- RTL Insights service expansion with clear report contracts.
- Snapshot publication metadata and guard plumbing when done in one ownership boundary.
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
- Phase D feature work should prove behavior at the service level before UI smoke coverage.

## Commit Policy

- Keep local commits coherent and architecture-oriented.
- Do not push unless explicitly asked.
- Do not discard user changes.
- Do not touch user dirty RTL fixture files.
- Keep docs short and current; replace stale content instead of accumulating history.
