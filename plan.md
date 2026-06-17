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

- Status: complete baseline.
- Expand FSM graph, Signal Journey, Module Brief, Clock/Reset Domain Map, Semantic Diff, and code/document links through feature services.
- Continue small RTL Insights improvements only when they do not require semantic data model changes.
- Defer features that need `SymbolInfo`, `sym_type_e`, identity, owner/type metadata, source role, relationship provenance, query result, completion item, or report-model changes to Phase E.
- Use `test_sv/new` and similar real projects as first-class fixtures.
- Add focused tests at service level first, then UI smoke coverage where needed.
- Do not add feature-specific workarounds in UI, scheduler, analyzer, or coordinator code.
- Keep reports explicit: rows, display names, navigation payloads, diagnostics, and evidence should be shaped before UI render.

### Phase E: Semantic Data Model Hardening

- Status: complete on this branch.
- Query services and RTL report models now carry stable identity, semantic metadata, source-role display, relationship provenance/confidence/evidence, and not-found reasons where useful.
- The local release gate has passed with full Ninja, full CTest, hygiene scans, and forbidden-file guards.

#### E1: Symbol Identity And Snapshot Handles

- Treat `symbolId` as a snapshot-local handle unless stable identity rules say otherwise.
- Add `SymbolStableKey` or an equivalent stable identity model.
- Let relationships, reports, and query results carry stable identity plus local handles.
- Define snapshot merge and rebind rules for relationships and reports.
- Keep this serial; it blocks deeper metadata migration.

#### E2: Semantic Metadata On Symbols

- Keep `sym_type_e` as raw/legacy collector kind.
- Add stable semantic metadata on `SymbolInfo` or an equivalent metadata layer.
- Cover declaration kind, usage role, owner scope, visibility, source role, and raw collector kind.
- Generate metadata consistently through `SymbolTaxonomy` or a dedicated mapper.
- Query Services and RTL feature services should prefer stable metadata over raw enum checks.
- Keep this serial and avoid unrelated feature work.

#### E3: Owner Scope And Type Reference Model

- Reduce `moduleScope` overload with owner name, kind, path, or equivalent owner metadata.
- Reduce `dataType` overload by separating raw type text, resolved type name, resolved type kind, and modport name where needed.
- Give package, interface, interface instance, modport, struct/enum/member, parameter/localparam, port, signal, and module instance explicit rules.
- Support `lr_genr_if.si` and `LR_GENR_IF.si` through type/interface/modport resolution, not scattered string guesses.
- Keep this serial.

#### E4: Source Role, Diagnostics, And Snapshot Publication Metadata

- Formalize source roles: design source, header, external header, generated, and unknown.
- Separate diagnostic ownership for Slang, include/source-role, ZeroSlack semantic, stale, current-file, and workspace diagnostics.
- Formalize snapshot metadata such as snapshot kind, project generation, document version, source hash, analysis kind, and created revision/time where useful.
- Prevent stale async diagnostics or semantic results from replacing newer visible truth.
- Batch focused tests and isolated report metadata only; publication rules stay serial.

#### E5: Relationship Provenance And Query Result Contracts

- Add relationship provenance, confidence, and evidence location/range where useful.
- Distinguish Slang-confirmed, inferred, lexical fallback, open-document, workspace, and feature-generated relationships.
- Extend query results with reason, evidence, candidate count, and confidence where useful.
- Definition, Completion, Reference, Hierarchy, Relationship, and RTL Insights services should expose consistent result contracts.
- Failed navigation/completion should explain missing include, missing import, unknown source role, unresolved interface instance type, missing modport, or no symbol.
- Split by service after E1-E4 contracts are stable.

#### E6: Completion Item And RTL Report Model Hardening

- Formalize completion item models with label, insert text, kind, detail, source, priority, replacement range, and semantic role.
- Harden RTL report models so UI receives display fields, evidence, confidence, not-found reasons, and stable identity references.
- Keep UI render-only.
- Module Brief, Signal Journey, Clock/Reset Domain Map, FSM Graph, Semantic Diff, and code/document links should share report conventions.
- Proceed in service-sized batches.

### Release Gate

- Status: passed locally after Phase E.
- Keep this product baseline gate before broad RTL feature expansion.
- `test_sv/new` must validate package/import/typedef/parameter, interface/interface instance/modport, `.svh`/`.vh` includes, completion, navigation, Problems, and RTL Insights sharing one semantic truth.
- Full Ninja and full CTest must pass.
- Hygiene scans and forbidden-file guard must pass.
- No dirty user RTL fixture files may be touched or committed.

## Batch Policy

- Phase D can proceed in batches when blocks do not share service contracts or UI surfaces.
- Suitable Phase D batches: focused service tests, independent feature-service reports, UI render-only cleanup, and fixture expansion.
- E1, E2, and E3 are mostly serial.
- E4 publication rules are serial, but focused tests and isolated report metadata can batch.
- E5 and E6 can be split by service once earlier contracts are stable.
- Safe batch blocks: focused service tests, independent query-service result migration, UI render-only conversion, and report display-field cleanup.
- Not suitable for batching: symbol identity changes, `SymbolInfo` layout changes, `sym_type_e` compatibility changes, owner/type model migration, source role migration, snapshot publication rules, relationship rebind rules, and cross-service query contract changes.
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
