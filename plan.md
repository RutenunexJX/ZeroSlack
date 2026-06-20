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

### Phase F0: Legacy Field Retirement

- Status: complete on this branch.
- This was a cleanup gate, not a feature phase.
- Direct product dependence on legacy `SymbolInfo` fields has been retired or fenced:
  - `symbolId`: keep as snapshot-local handle only; introduce or consistently name `SymbolLocalHandle` where local handles must remain visible.
  - `symbolType` / `sym_type_e`: keep as raw collector compatibility only; feature policy should use semantic metadata, declaration groups, search intent, and semantic roles.
  - `moduleScope`: split owner semantics into explicit owner kind/name/path/stable key or equivalent owner metadata.
  - `dataType`: split raw type text from resolved type name, resolved type kind, modport name, and type stable key where useful.
- Completion APIs now expose structured items with label, insert text, kind, detail, source, semantic role, and stable key metadata.
- Query services normalize UI string inputs into stable subject/context handles before feature policy where the current service contract needs it.
- Diagnostic contracts expose explicit source and ownership metadata.
- `sym_list` remains collector/storage compatibility; broad feature work should read `SemanticIndexSnapshot` and typed query/report models.
- Product-facing legacy field guards are in CTest and passed with the F0 baseline gate.

### Phase F1: Semantic Symbol Record Replacement

- Status: complete on this branch.
- Introduce or expand a real semantic symbol record.
- Let `SymbolInfo` degrade into a collector/adapter compatibility carrier.
- Product, service, report, and query layers should prefer semantic symbol records, stable identity, and semantic metadata.
- Owner, type, source-role, provenance, and not-found reason metadata should be carried by the semantic model, not by overloading `symbolType`, `moduleScope`, or `dataType`.
- Keep Qt 6 + CMake + Ninja only.
- New features must still flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.

### Phase F2: Stable Relationship And Index Migration

- Status: complete on this branch.
- Move relationship engine, snapshot, lookup, and query-service main paths from int `symbolId` / `sym_type_e` to stable identity plus semantic enum/model contracts.
- Keep compatibility APIs only as transition layers.
- Add or extend guards so product-facing code cannot continue using compatibility APIs.
- Relationship, reference, hierarchy, and RTL feature reports must expose stable keys, owner/type metadata, source role, provenance, and not-found reasons.
- Do not restore regex relationship analysis, the old Tree-sitter symbol parser, direct UI Slang execution, or UI workspace scans.

### Phase F3: Legacy Compatibility Removal

- Status: complete on this branch for product-facing compatibility payloads and guarded service/report paths.
- Delete or isolate legacy fields and APIs: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, `getSymbolById`, `findSymbolId`, int-id `getRelationships`, and similar compatibility surfaces.
- If an internal adapter is still required, keep it limited to the collector/import boundary and guarded.
- Clean tests and docs that assume legacy identity.
- After F3, run the release gate before broad RTL feature expansion.

### Release Gate After Phase F3

- Full Ninja must pass.
- Full CTest must pass.
- Legacy guard and product-facing API audit must pass.
- Docs, goal, and plan consistency checks must pass.
- Real RTL workspace smoke pass must be covered.
- Confirm qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, and long-lived scattered perflog probes have not returned.

### Phase G: Complete Legacy Field Deletion

- Status: complete on this branch.
- Goal: delete remaining legacy compatibility payloads and remove or isolate old fields instead of merely guarding product-facing use.
- G0 is complete: `SemanticRelationshipResult` no longer carries `fromSymbol` / `toSymbol`; relationship, reference, hierarchy, module brief, signal journey, clock/reset, completion, and module-context consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries `symbol`; module brief, signal journey, FSM graph, and definition consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, and `findDefinitions`; callers use records, stable keys, query services, or feature services.
- G3 is complete: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, and raw collector compatibility are deleted from product-facing contracts or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The full Phase G release gate passed locally with full Ninja, full CTest, legacy guard, product-facing API audit, docs consistency review, and forbidden-file regression checks.

### Phase H: Semantic Core Slimdown

- Status: complete on this branch.
- Goal achieved: remaining legacy fields, APIs, redundant interfaces, and compatibility logic were removed from semantic core contracts or confined to the final guarded transition boundary.
- H0 is complete: `legacy_field_policy_guard.ctest` defines and enforces the Phase H allowlist for `sym_list::SymbolInfo`, `sym_list::sym_type_e`, `.symbolId`, `.symbolType`, `.moduleScope`, `.dataType`, int-id lookup APIs, raw collector query APIs, and retired completion compatibility APIs.
- H1 is complete: completion command, selected module, global, and scope query contracts use semantic-native `CompletionCommandKind`; raw collector mapping is isolated inside the semantic-query adapter and taxonomy boundary.
- H2 is complete for Phase H scope: snapshot/store public contracts and publication paths consume semantic records; remaining `SymbolInfo` conversion is confined to the guarded legacy store/collector adapter boundary.
- H3 is complete: retired completion compatibility entry points such as `getCommandCompletionSymbols`, `getTypedCompletionSymbols`, `getGlobalSymbolInfosByType`, `getModuleInternalSymbolsByType`, and `getModuleContextSymbolsByType` are removed and guarded.
- H4 is complete: feature-service helpers use `SemanticSymbolRecord`, stable keys, local handles, and semantic metadata instead of exposed `SymbolInfo` contracts.
- H5 is complete: `syminfo` legacy query APIs and indexes were deleted or shrunk, including raw name/file/type query helpers.
- H6 is complete: redundant adapter helpers, conversion helpers, compatibility query paths, and duplicated display/filtering surfaces were removed or made private.
- The full Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.

### Release Gate After Phase H

- Full Ninja must pass.
- Full CTest must pass.
- `legacy_field_policy_guard` must prove legacy field/API use is absent outside the final allowlist.
- Static scans must confirm no product, service, report, completion, UI, scheduler, analyzer, or snapshot contract exposes `SymbolInfo`, `sym_type_e`, `.symbolId`, `.symbolType`, `.moduleScope`, `.dataType`, `getSymbolById`, `findSymbolId`, int-id relationships, or retired completion compatibility APIs.
- Docs, goal, and plan consistency checks must pass.
- Confirm qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, and long-lived scattered perflog probes have not returned.

### Phase I: Semantic Store Native / Collector Native

- Status: active; I0-I3 complete, I4 current.
- Goal: delete the remaining legacy collector/store body by moving Slang collection, semantic storage, scope rebuild, and relationship containment to semantic-native records or store entries.
- I0 is complete: the native store/collector migration contract is documented, `legacy_field_policy_guard.ctest` defines the Phase I zero-legacy target terms, and `ZEROSLACK_PHASE_I_ZERO_TARGET` provides the opt-in final scan for I5 without changing runtime behavior.
- I1 is complete: Slang collection exposes `collectSymbolRecords`, `SlangManager` exposes `extractSymbolRecords` / `extractWorkspaceSymbolRecords`, and `SymbolAnalyzer` plus tracked tests consume those record-native APIs directly. `collectSymbols`, `extractSymbols`, and `extractWorkspaceSymbols` are deleted.
- I2 is complete: `SemanticIndex` owns a semantic-native record/content store with file replacement, local handle assignment, stable-key indexes, native content state, native file coverage, native refresh, native/snapshot-only record queries, native content/state queries, and native-over-snapshot replacement semantics; snapshot publication reads records/content from `SemanticIndex` instead of rebuilding records from `sym_list`.
- I3 is complete: scope rebuild, module containment, relationship containment, and module lookup have moved away from `symbolId`, `symbolType`, `moduleScope`, and `dataType` to semantic metadata, owner/type records, stable keys, and explicit local handles. `SmartRelationshipBuilder`, `SymbolRelationshipEngine`, and `SemanticIndex` relationship rebuilds route containment through semantic records; legacy `sym_list` current-module lookup and module-scope auto-inference are deleted. `SymbolRelationshipEngine` no longer accepts or stores a `sym_list` database pointer, `SemanticIndex` relationship queries, completion relationship facts, and snapshot publication use the native attached relationship engine pointer, `sym_list` no longer stores, exposes, or forwards a relationship engine, and the legacy semantic-record-to-`sym_list` relationship/scope mirror is deleted.
- I4 is current: delete the legacy carrier and compatibility taxonomy surface: `sym_list::SymbolInfo`, `sym_list::sym_type_e`, legacy fields, `symboltaxonomylegacy.h`, and remaining adapter reverse conversions. The unused `semanticcollectoradapter` source/header are deleted and guarded against returning, `SemanticIndex` no longer exposes or stores a legacy `sym_list` database injection/access API, dead `sym_list` symbol database mutator/accessor/index/scope-tree storage has been deleted, native taxonomy metadata checks no longer route through the retired private raw-kind bridge, unused legacy metadata write-back helpers have been removed from the taxonomy compatibility surface, and legacy `SymbolInfo` owner/visibility/interface/declaration helper overloads are no longer public taxonomy API.
- I5: run the Phase I release gate and update docs. The target is zero remaining legacy collector/store terms in core source except historical docs or explicitly named migration notes; the opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` guard must pass.

### Phase I Goal Mode

- Track Phase I progress by subphase, not by the whole phase.
- Each subphase I0 through I5 starts at 100% remaining when it becomes current.
- At the end of every work turn, report the current subphase and the remaining percentage for that subphase.
- When a subphase is complete and verified, report it as 0% remaining, then make the next subphase current at 100% remaining.
- Do not mark all Phase I complete until I0-I5 are complete, docs are current, full Ninja and full CTest pass, and final legacy scans prove the zero-legacy target.

## Batch Policy

- Phase D can proceed in batches when blocks do not share service contracts or UI surfaces.
- Suitable Phase D batches: focused service tests, independent feature-service reports, UI render-only cleanup, and fixture expansion.
- E1, E2, and E3 are mostly serial.
- E4 publication rules are serial, but focused tests and isolated report metadata can batch.
- E5 and E6 can be split by service once earlier contracts are stable.
- Safe batch blocks: focused service tests, independent query-service result migration, UI render-only conversion, and report display-field cleanup.
- Not suitable for batching: symbol identity changes, `SymbolInfo` layout changes, `sym_type_e` compatibility changes, owner/type model migration, source role migration, snapshot publication rules, relationship rebind rules, and cross-service query contract changes.
- F1-F3 should proceed in small serial blocks when changing semantic symbol records, relationship identity, compatibility APIs, owner/type metadata, completion item models, or query normalization.
- Phase H should proceed in small serial blocks; avoid mixing guard expansion, snapshot/store migration, completion contract migration, feature-service helper migration, and collector adapter deletion in one commit.
- Phase I should proceed in strict subphase order unless a later subphase exposes a small prerequisite cleanup; do not mix collector-native emission, store replacement, relationship/scope migration, and carrier deletion in one commit.
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
