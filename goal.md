# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It should grow through stable semantic boundaries instead of ad hoc UI logic.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, diagnostics, references, and relationships
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, relationship browsing, RTL insight reports, and signal-centric driver/consumer graphs
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
  Signal Kernel Graphs are built here from Signal Journey and relationship evidence.

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
- Phase H reduced `sym_list::SymbolInfo` and `sym_list::sym_type_e` exposure to guarded transition code; later Phase I/J cleanup removed the remaining first-party carrier and fixture paths.
- Raw collector-kind query inputs have been replaced in completion and query contracts by semantic-native models such as `CompletionCommandKind`, semantic metadata, stable keys, owner/type metadata, and source-role records.
- Snapshot/store public contracts consume semantic records; the former `SymbolInfo` conversion boundary has since been retired from first-party source.
- Redundant conversion, compatibility, filtering, sorting, display helper, and raw query APIs were deleted or made private.
- Phase H release gate passed with guards enforcing the final allowlist and full Ninja plus full CTest passing locally.

### Phase I: Semantic Store Native / Collector Native

- Status: complete on this branch.
- Purpose achieved: delete the former legacy collector/store body instead of only guarding its boundary.
- I0 is complete: the Phase I migration contract is documented and the opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` guard defines the final zero-legacy source scan for I5.
- Slang collection should emit semantic-native records or store entries as the primary output.
- I1 is complete: the analyzer-facing collection path consumes semantic-native records directly, the collector internals build `SemanticSymbolRecord` as the primary carrier, and the legacy `collectSymbols` / `extractSymbols` / `extractWorkspaceSymbols` extraction APIs are deleted.
- The backing semantic store owns semantic-native records, cached content, local handles, stable-key indexes, and file replacement without depending on `sym_list::SymbolInfo`.
- I2 is complete: `SemanticIndex` now owns native symbol records, file contents, file-state hashes, file coverage, refresh state, local handles, stable-key indexes, native/snapshot-only record queries, native content/state queries, and native-over-snapshot replacement.
- I3 is complete: scope rebuild, module containment, module lookup, and relationship containment use semantic metadata, owner/type records, stable keys, and explicit local handles instead of `symbolId`, `symbolType`, `moduleScope`, or `dataType`. Relationship builder, relationship engine, and `SemanticIndex` relationship rebuilds route containment through semantic records; legacy `sym_list` current-module lookup and module-scope auto-inference are deleted. `SymbolRelationshipEngine` no longer accepts or stores a `sym_list` database pointer, `SemanticIndex` relationship queries, completion relationship facts, and snapshot publication use the native attached relationship engine pointer, `sym_list` no longer stores, exposes, or forwards a relationship engine, and the legacy semantic-record-to-`sym_list` relationship/scope mirror is deleted.
- I4 is complete: legacy carrier APIs, compatibility taxonomy surface, and adapter conversions were deleted or isolated; unused legacy adapter files, legacy taxonomy includes, semantic-index legacy injection APIs, dead symbol database storage, legacy metadata helpers, legacy taxonomy overloads, legacy completion matching APIs, and legacy outline grouping APIs were removed or guarded.
- I5 is complete: the Phase I release gate passed for core source, and Phase J later expanded the zero target to first-party repo source.
- I5 first block is complete: lowercase `rawCollectorKind` / `requestedRawCollectorKind` field and helper names were replaced by `collectorKind` fields and `CollectorKind` enum/type names.
- I5 second block is complete: `symboltaxonomylegacy.h` and production `sym_type_e` / `SymbolInfo` taxonomy overloads were deleted; tracked tests temporarily used a fixture-local conversion helper until Phase J removed it.
- I5 third block is complete: root/core `syminfo*` carrier files moved into `test_sv` fixture scope, core CMake stopped building them, and Phase J later deleted the fixture carrier.
- I5 fourth block is complete: stale core `syminfo` includes and `SymbolInfo`-named helpers were removed, tracked tests moved to semantic-native helpers, GUI smoke fixtures synchronized native records before snapshot-based panel checks, and the final Phase I full Ninja/full CTest/guard/static-scan gate passed.
- Phase I is complete: the release gate proves zero remaining legacy collector/store terms in core source except historical docs or explicitly named migration notes; Phase J proves the broader repo-source target.

### Phase I Goal Mode

- Progress is tracked per subphase I0-I5.
- Each subphase is its own 100% unit.
- End every work turn by naming the current subphase and reporting that subphase's remaining percentage.
- Move to the next subphase only after the current subphase is implemented, verified, and committed.
- Phase I completion requires I0-I5 completion, current docs, full Ninja, full CTest, guard success, and final static legacy scans.

### Phase J: Test Fixture Native Cleanup

- Status: complete.
- Purpose achieved: delete the fixture-only legacy `sym_list` / `syminfo` carrier after Phase I proved the core source was clean.
- Phase J should not add RTL feature behavior; it should preserve existing behavior while replacing test fixture inputs with semantic-native builders.
- J0 is complete: it defines the repo-wide fixture cleanup allowlist and guards the remaining test-only legacy terms.
- J1 is complete: native test builders cover semantic records, metadata, owners, type info, stable keys, local handles, and relationship endpoints.
- J2 is complete: completion and jump tests have moved away from `sym_list::SymbolInfo`.
- J3 is complete: relationship and GUI smoke tests have moved away from `sym_list::SymbolInfo`.
- J4 is complete: `test_sv/syminfo*` and the reverse fixture adapter have been deleted now that no tracked test includes or builds them.
- J5 is complete: the release gate passes and the zero target now scans repo source except docs and guard definitions.

### Phase J Goal Mode

- Progress is tracked per subphase J0-J5.
- Each subphase is its own 100% unit.
- End every work turn by naming the current subphase and reporting that subphase's remaining percentage.
- Move to the next subphase only after the current subphase is implemented, verified, and committed.
- Phase J completion requires J0-J5 completion, current docs, full Ninja, full CTest, guard success, final repo-source static legacy scans, and deletion of the fixture-only legacy carrier.

### Phase K: Follow-up Editor Structure Features

- Status: complete.
- Product goal achieved: editor structure tools now make large SystemVerilog workspaces easier to understand, fold, rearrange, and reuse without moving semantic policy into UI code.
- K1 Design Hierarchy view provides Files/Design-only Navigation, right-click-only Design Top selection, cached module-only hierarchy reports, instance nodes rendered as `instance_name : module_type`, instantiation and module-definition navigation, and Files view dimming based on hierarchy participation.
- K2 Code Folding and Custom Folding provides Tree-sitter folding ranges, gutter collapse/expand controls, custom `// fold <alias>` / `// endfold` ranges parsed from comment nodes, persistent custom-fold background tint, manual marker creation/rename behavior, silent removal when marker pairs are broken, and the Global Control `fd` action for inserting custom fold markers as one undoable edit with visible mode status, live range preview, and gutter start/end badges.
- K3 Fold Block Shelf provides the Global Control `fds` action, visible mode status, active shelf dock/list styling, custom fold block hover highlighting with a clear bottom boundary, move/copy drag into a shelf with a compact drag pixmap, protected text input while drag mode is active, consume/copy drag back into editors, preview, protected delete, restore paths for moved code, and Activity/Output logging for shelf operations.
- `;:` is reserved for future expansion and currently has no completion or execution behavior. Global Control currently exposes only `ow`, `fd`, and `fds`; `ow` replaces the removed toolbar Open Workspace path and requires an alias for interactively chosen workspaces.
- Startup keeps Editor Appearance, RTL Insights, and Activity hidden until needed; workspace open/switch raises Activity/Output. Multiple workspaces are represented as alias tabs above editor tabs, and selecting one reloads the active project model and watcher.
- Phase K verification passed with focused Ninja targets, `completion_test` with 315 checks, `gui_smoke_test` with 331 checks, `git diff --check`, normal `legacy_field_policy_guard`, and opt-in `ZEROSLACK_PHASE_J_ZERO_TARGET`. The latest workspace/UI/Fold/Global Control polish additionally passed focused `completion_test` with 321 checks and `gui_smoke_test` with 338 checks.
- Design hierarchy reports must be produced by `HierarchyService` or an equivalent query-service boundary from `SemanticIndexSnapshot`, not by UI file scans or direct Slang runs.
- Folding and Fold Shelf must reuse Tree-sitter/comment-node parsing and a shared folding range model; regex parsing of code structure or fold directives is not allowed.
- Fold and shelf edits must use document/editor edit APIs so undo/redo remains coherent and source code cannot be silently lost.
- Activity/Output logs should expose hierarchy builds, top changes, fold/shelf operations, and malformed custom fold warnings without adding scattered long-lived perflog.

### Phase K Goal Mode

- Progress is tracked per feature subphase K1-K3.
- Each subphase is its own 100% unit.
- Current subphase: K3 Fold Block Shelf complete, 0% remaining.
- End every work turn by naming the current Phase K subphase and reporting that subphase's remaining percentage.
- K1 is complete only when Design Hierarchy view, right-click top selection, cached reports, navigation actions, Files dimming, and real-fixture tests are implemented and verified.
- K2 is complete only when syntax folding, custom comment-node folding, Global Control `fd` marker mode, gutter interactions, undo behavior, and tests are implemented and verified without regex parsing.
- K3 is complete: Fold Shelf mode, shelf panel/model, move/copy/insert/delete/restore flows, preview, Activity logs, and tests are implemented and verified without regex parsing.

### Phase L: Regex Logic Native Cleanup

- Status: complete.
- Product goal achieved: regex-driven production logic was removed from semantic/editor feature paths while preserving SystemVerilog understanding through semantic records, `SemanticIndex` / `SemanticIndexSnapshot` flow, service-owned query contracts, and deterministic SV token helpers.
- L0 Regex Inventory and Guard Baseline is complete: first-party production regex cleanup files are inventoried, docs and guard definitions remain valid regex locations, normal guard blocks new regex API spread, and optional `ZEROSLACK_PHASE_L_ZERO_TARGET` is defined for L6.
- L1 Utility Regex Replacement is complete: low-risk regex helpers for identifiers, keyword boundaries, module/endmodule scans, and whitespace normalization now use deterministic token helpers.
- L2 Completion Context Native is complete: completion context parsing no longer uses regex and deterministic token helpers cover struct members, enum values, assignments, and instantiations.
- L3 Module Range / Include / Import Native is complete: module-range fallback, include/import context collection, and scope-band module end lookup no longer use regex.
- L4 FSM Graph Native Extraction is complete: FSM transition discovery no longer uses regex and relies on deterministic token parsing plus semantic state records.
- L5 Open Document Scheduling Cleanup is complete: structural-change scheduling no longer uses keyword regex and preserves the existing lightweight policy with deterministic token-boundary checks.
- L6 Final Regex Zero Target is complete: guards reject regex APIs outside docs and guard definitions, and focused tests, affected GUI smoke tests, full Ninja/full CTest, guard scans, static scans, and `git diff --check` passed.
- Phase L must not restore the old Tree-sitter symbol parser, SVLexer, regex relationship analysis, direct UI Slang execution, UI workspace scans, or timer-delay responsiveness masking.

### Phase L Goal Mode

- Progress is tracked per cleanup subphase L0-L6.
- Each subphase is its own 100% unit.
- Current subphase: L6 Final Regex Zero Target complete, 0% remaining.
- End every work turn by naming the current Phase L subphase and reporting that subphase's remaining percentage.
- L0 is complete only when the regex inventory, cleanup allowlist, and guard plan are documented and the guard baseline can prevent new regex spread.
- L1 is complete only when low-risk utility regex use is replaced and covered by focused tests or existing guard scans.
- L2 is complete only when completion context no longer depends on regex parsing and completion tests cover the migrated contexts.
- L3 is complete only when module range, include, and import context logic no longer depends on regex parsing and real fixture coverage remains intact.
- L4 is complete: FSM graph transition extraction no longer depends on regex parsing and GUI smoke RTL Insights coverage remains green against `test_sv/new`.
- L5 is complete: open-document structural scheduling no longer depends on keyword regex and editor workflow coverage remains green.
- L6 is complete: first-party source/tests reject regex APIs outside docs and guard definitions, docs are current, verification passes, and final scans are clean.

## Architecture Rules

- New semantic features must flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must render service/model output only; it must not run Slang directly, scan workspace files directly, own semantic policy, or patch around missing semantic data.
- Scheduler and analyzer code must stay limited to timing, lifecycle, extraction, publication, and refresh routing; they must not own feature policy.
- Slang owns semantic truth; Tree-sitter owns low-latency editor syntax and live scope.
- Real engineering fixtures, especially `test_sv/new`, should guide feature expansion.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.

## Feature Expansion Guardrails

These constraints are mandatory for every new feature.

- New semantic or RTL behavior must flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must render service/model output only; it must not run Slang, scan workspace files, own semantic policy, or patch around missing semantic data.
- `AnalysisScheduler` and `SymbolAnalyzer` must stay limited to timing, lifecycle, extraction, publication, and refresh routing; they must not own feature policy.
- New tests and fixtures must use semantic-native records/builders and must not reintroduce `sym_list`, `syminfo`, reverse adapters, legacy taxonomy headers, or legacy field/API names.
- New feature reports must expose stable keys, semantic records or typed report rows, owner/type/source-role metadata, navigation payloads, evidence, confidence, and not-found reasons where relevant.
- Real workspace fixtures, especially `test_sv/new`, must cover behavior that depends on package/import/include/interface/modport/cross-file semantics.
- `legacy_field_policy_guard.ctest` and the final repo-source zero target must keep passing after every feature change.
- Feature work must be rejected or redesigned if it requires qmake/pro/pri, `.claude`, SVLexer, the old Tree-sitter symbol parser, direct UI Slang execution, UI workspace scans, regex relationship analysis, long-lived scattered perflog, or legacy carrier/API resurrection.

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
- Phase H is complete on this branch: legacy collector compatibility is deleted from semantic core contracts or held only in historical guard definitions
- Phase I is complete on this branch: the legacy collector/store body is replaced by semantic-native collection and storage, and final scans proved legacy carrier names gone from core source
- Phase J is done only when tracked tests use semantic-native fixture builders, `test_sv/syminfo*` and reverse fixture adapters are deleted, and final scans prove legacy carrier names are gone from repo source except docs and guard definitions
- Post-J editor correctness includes usable completion popup sizing, normal completion that triggers from identifier prefixes or strong semantic contexts, semantic command completion that requires `;cmd` plus Space, code template insertion through `;;cmd` plus Space, reserved-but-inactive `;:`, scoped `;?` / `;;?` help, removal of old single-letter plus Space command triggers, cross-file interface/header definition targets, named instance port formal-to-child-port jumps, actual-signal local jumps, and Navigation responsiveness fixes that remove unnecessary synchronous rebuilds instead of masking stalls with delayed timers
- Global app control is available through Ctrl+Space from editor and non-editor focus; the current surfaced command set is intentionally limited to `ow`, `fd`, and `fds`, while broader ProjectModel/SemanticIndex/template/action search remains reserved for future expansion
- Signal Kernel Graph is available from a signal right-click action and is done through `SignalJourneyService` / `SignalKernelGraphService` reports: drivers render left of the kernel, consumers render right, cross-module nodes are module-wrapped, hover previews show precise evidence code, Ctrl+left-click rebases the kernel, and double-click navigation uses evidence or declaration links
- The post-J editor workflow baseline was verified by `completion_test` with 311 checks and `gui_smoke_test` with 276 checks for the layered inline command and Global Control paths
- Phase K is done only when K1 Design Hierarchy, K2 Code Folding/Custom Folding, and K3 Fold Block Shelf are implemented through service/model-backed UI boundaries, mode state is visible and cancelable, shelf drag mode guards against accidental text edits, affected flows are verified with focused tests and GUI smoke coverage, and the implementation is proven not to use UI workspace scans, direct UI Slang runs, regex structure/fold parsing, or timer-delay responsiveness masking
- Phase L is done only when first-party production regex logic is removed from semantic/editor feature paths, docs and guard definitions are the only regex allowlist, completion/module/import/FSM/scheduler behavior is backed by Tree-sitter, Slang/semantic records, `SemanticIndexSnapshot`, or deterministic token helpers, and final guards prove the cleanup
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
- new feature work is done only when the Feature Expansion Guardrails are satisfied and verified for the changed scope
- UI panels render reports/models without owning semantic policy
- scheduler, analyzer, project, document, and editor ownership boundaries stay clear
- real fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, FSMs, diagnostics, relationship browsing, signal journeys, module briefs, semantic diff, and large-file response
- verification may be batched, but commits remain coherent by block
- full Ninja and full `ctest --output-on-failure` pass after shared semantic state, scheduler, editor, project, symbol identity, `SymbolInfo`, source role, or query contract boundary changes
- handoff docs are short, current, and easy to reread
