# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It should grow through stable semantic boundaries instead of ad hoc UI logic.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, diagnostics, references, and relationships
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, relationship browsing, and RTL insight reports
- remain responsive on large files and multi-file workspaces
- keep semantic behavior testable through real fixtures

## Target Architecture

```text
ProjectModel
  Owns workspace inputs: root, file list, include dirs, defines, top, ignored paths.

DocumentModel
  Owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module, and registry-backed text queries.

AnalysisScheduler
  Owns analysis timing, debounce, cancellation, refresh requests, relationship work, lifecycle, and analysis event routing.

SemanticIndex / SemanticIndexSnapshot
  Own semantic facts: symbols, definitions, relationships, references, diagnostics, and cached content.

Query Services / Feature Services
  Own feature-specific reads, report shaping, and semantic policy.

Coordinators
  Own UI command routing, progress policy, navigation commands, panel refresh, semantic runtime setup, and workflow glue.

UI Layer
  Renders service/model output.
```

## Development Phases

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
- Do not add feature-specific workarounds around semantic gaps in UI, scheduler, analyzer, or coordinator code.
- Keep feature reports explicit enough for UI render without understanding semantic internals.

### Phase E: Semantic Data Model Hardening

- Status: complete on this branch.
- Query services and RTL report models carry stable identity, semantic metadata, source-role display, relationship provenance/confidence/evidence, and not-found reasons where useful.
- E1 Symbol Identity And Snapshot Handles: treat `symbolId` as snapshot-local, add stable identity, carry stable identity plus local handles, and define merge/rebind rules.
- E2 Semantic Metadata On Symbols: keep `sym_type_e` raw, add stable declaration/usage/owner/visibility/source-role metadata, and generate it consistently.
- E3 Owner Scope And Type Reference Model: reduce `moduleScope` and `dataType` overloads, model owner/type/modport explicitly, and resolve interface modports through type rules.
- E4 Source Role, Diagnostics, And Snapshot Publication Metadata: formalize source roles, diagnostic ownership, snapshot metadata, and stale-result protection.
- E5 Relationship Provenance And Query Result Contracts: add provenance, confidence, evidence, candidate counts, failure reasons, and consistent query contracts.
- E6 Completion Item And RTL Report Model Hardening: formalize completion items and RTL report models with display fields, evidence, confidence, not-found reasons, and stable identity references.

### Phase F0: Legacy Field Retirement

- Status: complete on this branch.
- This cleanup gate retired or fenced product dependence on legacy `SymbolInfo` fields.
- `symbolId` is snapshot-local unless a stable identity rule says otherwise.
- `symbolType` / `sym_type_e` remain raw collector compatibility, not product policy.
- Product logic should use explicit owner metadata instead of overloaded `moduleScope`.
- Product logic should use explicit type metadata instead of overloaded `dataType`.
- Completion models expose structured items with semantic identity and insertion metadata.
- Query services resolve UI string inputs into stable subject/context handles before feature policy where needed.
- Diagnostic contracts expose explicit source and ownership metadata.
- Product-facing guard tests prevent UI, scheduler, analyzer, and feature code from reintroducing direct legacy-field policy.

### Phase F1: Semantic Symbol Record Replacement

- Status: complete on this branch.
- Introduce or expand a real semantic symbol record.
- Let `SymbolInfo` degrade into a collector/adapter compatibility carrier.
- Product, service, report, and query layers should consume semantic symbol records, stable identity, and semantic metadata first.
- Owner, type, source-role, provenance, and not-found reason metadata should be carried by the semantic model, not by reusing `symbolType`, `moduleScope`, or `dataType`.
- Keep Qt 6 + CMake + Ninja only.
- New features must still flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.

### Phase F2: Stable Relationship And Index Migration

- Status: complete on this branch.
- Move relationship engine, snapshot, lookup, and query-service main paths from int `symbolId` / `sym_type_e` to stable identity plus semantic enum/model contracts.
- Keep compatibility APIs only as transition layers.
- Guard product-facing code against continued compatibility API use.
- Relationship, reference, hierarchy, and RTL feature reports must expose stable keys, owner/type metadata, source role, provenance, and not-found reasons.
- Do not restore regex relationship analysis, the old Tree-sitter symbol parser, direct UI Slang execution, or UI workspace scans.

### Phase F3: Legacy Compatibility Removal

- Status: complete on this branch for product-facing compatibility payloads and guarded service/report paths.
- Delete or isolate legacy fields and APIs: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, `getSymbolById`, `findSymbolId`, int-id `getRelationships`, and similar compatibility surfaces.
- If an internal adapter is still required, it must stay limited to the collector/import boundary and be constrained by guards.
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
- G0 is complete: relationship result endpoint payloads `fromSymbol` and `toSymbol` were deleted; consumers now use endpoint `SemanticSymbolRecord` and stable keys.
- G1 is complete: definition result payload `symbol` was deleted; consumers now use `symbolRecord` and `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` retired `SymbolInfo` public APIs; callers use semantic records and stable identity.
- G3 is complete: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, and raw collector compatibility are deleted from product-facing contracts or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The Phase G release gate passed locally; broad RTL feature expansion is no longer blocked by Phase G, but must still preserve the architecture rules below.

### Phase H: Semantic Core Slimdown

- Status: complete on this branch.
- Purpose achieved: Phase G's isolated compatibility boundaries were tightened into real deletion where possible, so legacy collector fields and APIs no longer shape product, service, report, completion, snapshot, or query contracts.
- Remaining `sym_list::SymbolInfo` and `sym_list::sym_type_e` exposure is confined to collector/import, taxonomy, legacy store, and guarded adapter transition code.
- Raw collector-kind query inputs have been replaced in completion and query contracts by semantic-native models such as `CompletionCommandKind`, semantic metadata, stable keys, owner/type metadata, and source-role records.
- Snapshot/store public contracts consume semantic records; any required `SymbolInfo` conversion remains inside the guarded legacy store/collector adapter boundary.
- Redundant conversion, compatibility, filtering, sorting, display helper, and raw query APIs were deleted or made private.
- Phase H release gate passed with guards enforcing the final allowlist and full Ninja plus full CTest passing locally.

### Phase I: Semantic Store Native / Collector Native

- Status: active; I0-I1 complete, I2 current.
- Purpose: delete the remaining legacy collector/store body instead of only guarding its boundary.
- I0 is complete: the Phase I migration contract is documented and the opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` guard defines the final zero-legacy source scan for I5.
- Slang collection should emit semantic-native records or store entries as the primary output.
- I1 is complete: the analyzer-facing collection path consumes semantic-native records directly, the collector internals build `SemanticSymbolRecord` as the primary carrier, and the legacy `collectSymbols` / `extractSymbols` / `extractWorkspaceSymbols` extraction APIs are deleted.
- The backing semantic store should own semantic-native records, cached content, local handles, stable-key indexes, and file replacement without depending on `sym_list::SymbolInfo`.
- I2 is in progress: `SemanticIndex` now owns native symbol records, file contents, local handles, and stable-key indexes before bridging to the remaining `sym_list` compatibility layer.
- Scope rebuild, module containment, module lookup, and relationship containment should use semantic metadata, owner/type records, stable keys, and explicit local handles instead of `symbolId`, `symbolType`, `moduleScope`, or `dataType`.
- `symboltaxonomylegacy.h`, `sym_list::SymbolInfo`, `sym_list::sym_type_e`, legacy fields, and reverse adapter conversions should be deleted when collector/store migration is complete.
- Phase I is complete only when the final release gate proves zero remaining legacy collector/store terms in core source except historical docs or explicitly named migration notes, including a passing `ZEROSLACK_PHASE_I_ZERO_TARGET` scan.

### Phase I Goal Mode

- Progress is tracked per subphase I0-I5.
- Each subphase is its own 100% unit.
- End every work turn by naming the current subphase and reporting that subphase's remaining percentage.
- Move to the next subphase only after the current subphase is implemented, verified, and committed.
- Phase I completion requires I0-I5 completion, current docs, full Ninja, full CTest, guard success, and final static legacy scans.

## Architecture Rules

- New semantic features should flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must not run Slang directly or scan workspace files directly.
- Scheduler and analyzer code must not own feature policy.
- Slang owns semantic truth; Tree-sitter owns low-latency editor syntax and live scope.
- Real engineering fixtures, especially `test_sv/new`, should guide feature expansion.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Do not discard user changes.
- Do not touch user dirty RTL fixture files.
- Do not push unless explicitly asked.
- Keep local commits coherent and architecture-oriented.

## Definition Of Done

The foundation is healthy when:

- feature reads use stable models, snapshots, Query Services, or feature services
- stale workspace, open-document, and relationship analysis results cannot overwrite newer semantic snapshots
- product logic is stable only when snapshot publication, taxonomy/source-role helpers, stable semantic metadata, query services, and UI data flow have clear contracts and tests
- product logic is not ready for broad feature expansion until Phase E, Phase F0, Phase F1, Phase F2, Phase F3, Phase G, and the release gate after Phase G pass
- Phase H is complete on this branch: remaining legacy collector compatibility is deleted from semantic core contracts or confined to the final guarded transition allowlist
- Phase I is done only when the remaining legacy collector/store body is replaced by semantic-native collection and storage, and the final scans prove legacy carrier names are gone from core source
- `sym_type_e` is not used as a product, service, report, completion, snapshot, or query contract surface
- `symbolId` is not used as a product identity and should disappear from non-adapter contracts in favor of stable keys and explicit local handles where local handles are truly needed
- `moduleScope` and `dataType` are not used as overloaded product-policy fields and should disappear from semantic-native contracts
- completion items expose structured semantic identity and insertion metadata
- query services normalize string inputs into stable subject/context handles before feature logic
- semantic symbol records carry owner, type, source-role, provenance, and not-found reason metadata instead of overloading legacy fields
- relationship and index main paths use stable identity plus semantic enum/model contracts
- compatibility APIs are transition-only and guarded away from product-facing code
- Query Services and RTL feature services consume stable semantic metadata and contracts
- RTL Insights expansion should not proceed broadly until stable semantic metadata, Query Service contracts, Phase E, Phase F0, F1, F2, F3, Phase G, and the release gate after Phase G are in place
- Broad RTL feature expansion may resume, but any new semantic model work must keep the Phase H compatibility boundaries intact.
- Phase D features are done only when service-level behavior, report shape, UI render path, and real fixture evidence are covered
- UI panels render reports/models without owning semantic policy
- scheduler, analyzer, project, document, and editor ownership boundaries stay clear
- real fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, FSMs, diagnostics, relationship browsing, signal journeys, module briefs, semantic diff, and large-file response
- verification may be batched, but commits remain coherent by block
- full Ninja and full `ctest --output-on-failure` pass after shared semantic state, scheduler, editor, project, symbol identity, `SymbolInfo`, source role, or query contract boundary changes
- handoff docs are short, current, and easy to reread
