# ZeroSlack Handoff

ZeroSlack is a lightweight SystemVerilog workspace browser and editor.

```text
Tree-sitter live syntax and editor scope
Slang semantic facts
ProjectModel / DocumentModel / AnalysisScheduler
SemanticIndex / SemanticIndexSnapshot
Query Services and feature services
Thin UI consumers
```

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Never discard user changes or use destructive git commands unless explicitly requested.
- Do not touch user dirty RTL fixture files.
- Do not push unless explicitly asked.
- Keep local commits coherent, concise, and architecture-oriented.
- Keep source, tests, UI strings, CMake, and these docs English / ASCII.

## Latest Strategy

- Phase E semantic data model hardening is complete on this branch.
- Phase F0 Legacy Field Retirement is complete on this branch.
- Phase F1-F3 semantic migration is complete on this branch, including record-first relationship/reference/report paths and the post-F3 release gate.
- Phase G Complete Legacy Field Deletion is complete on this branch, including the post-G release gate.
- Phase H Semantic Core Slimdown is complete on this branch: remaining legacy type adapters, redundant APIs, and compatibility logic have been deleted or confined to the collector/import, taxonomy, and `syminfo` transition boundary.
- Phase I Semantic Store Native / Collector Native is complete on this branch: the remaining legacy collector/store body has been replaced or confined to fixture-only test carriers, and the zero-legacy core scan passes.
- Phase J Test Fixture Native Cleanup is complete: tracked tests use semantic-native fixture builders, `test_sv/syminfo*` and the reverse fixture adapter have been deleted, and the final zero target scans repo source except docs and guard definitions.
- I0 is complete: `legacy_field_policy_guard.ctest` now defines the Phase I zero-legacy target terms and an opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` final scan for I5.
- G0 is complete: `SemanticRelationshipResult` no longer carries legacy `fromSymbol` / `toSymbol` endpoint payloads; relationship consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries legacy `symbol` payloads; consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` no longer expose the retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, or `findDefinitions`.
- G3 is complete: direct `symbolId`, `symbolType`, `moduleScope`, `dataType`, and `sym_type_e` product-facing use has been removed or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The Phase G release gate passed locally with full Ninja and full CTest.
- The Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.
- Completion compatibility, snapshot/store `SymbolInfo` carriers, feature-service internal helpers, `syminfo` legacy queries, and collector/import adapter surfaces were tightened during Phase H.
- Phase I progress tracking uses per-subphase accounting: each I subphase starts at 100% remaining and each turn must report the current subphase plus that subphase's remaining percentage. Current subphase: I5 complete.
- I1 first block is complete: Slang collection now exposes semantic-native record APIs and `SymbolAnalyzer` consumes `extractSymbolRecords` / `extractWorkspaceSymbolRecords` directly instead of converting collector symbols in the analyzer layer.
- I1 second block is complete: `slangsymbolcollector` now builds `SemanticSymbolRecord` as the primary collector output.
- I1 is complete: `collectSymbols`, `extractSymbols`, and `extractWorkspaceSymbols` are deleted from the tracked collector/manager/test source, leaving Slang symbol extraction record-native.
- I2 first block is complete: `SemanticIndex` now owns a semantic-native record/content store with local handles and stable-key indexes before bridging records into the remaining `sym_list` compatibility layer.
- I2 second block is complete: native file content state now drives `contentAffectsSymbols`, and native records/content override older snapshot entries after file replacement.
- I2 third block is complete: native file coverage now prevents stale snapshot or `sym_list` fallback records from resurfacing after a file is replaced with zero semantic symbols.
- I2 fourth block is complete: `SemanticIndex::refreshStructTypedefEnumForFile` is native-only, and the redundant `sym_list` refresh API is deleted and guarded against returning.
- I2 fifth block is complete: `SemanticIndex::getSymbolRecords` no longer reads legacy `sym_list` records through the adapter; record queries merge only native store and snapshot data.
- I2 sixth block is complete: `SemanticIndex` cached-content and content-change queries no longer fall back to `sym_list`; missing native state conservatively requests analysis.
- I2 is complete: `SemanticIndex` no longer reads legacy `sym_list` records through the adapter, and the former I3 write mirror has been removed.
- I3 is complete: scope rebuild, module containment, relationship containment, and module lookup have moved away from the legacy relationship mirror.
- I3 first block is complete: `SmartRelationshipBuilder` consumes semantic records through a `SemanticIndex` record provider instead of reading records back from `sym_list` or the collector adapter.
- I3 second block is complete: `SymbolRelationshipEngine` file rebuild paths consume semantic records through a provider and no longer expose the legacy `symbols()` DB-to-record accessor.
- I3 third block is complete: `sym_list` relationship rebuild and scope refresh now forward containment work to the semantic-record relationship engine instead of adding CONTAINS edges from legacy fields.
- I3 fourth block is complete: legacy `sym_list` current-module lookup and module-scope auto-inference are deleted; module lookup stays on semantic-record `SemanticIndex` paths.
- I3 fifth block is complete: `SymbolRelationshipEngine` no longer accepts, stores, or exposes a `sym_list` database pointer; relationship engine files are guarded against reintroducing `sym_list`.
- I3 sixth block is complete: `SemanticIndex` now owns the attached relationship engine pointer for relationship queries, completion relationship facts, and snapshot publication instead of fetching it from `sym_list`.
- I3 seventh block is complete: `sym_list` no longer stores, exposes, or forwards a relationship engine; `SemanticIndex` directly rebuilds native relationship facts when records change or an engine is attached.
- I3 eighth block is complete: `SemanticIndex` no longer mirrors semantic records back into `sym_list`, and the reverse relationship/scope mirror conversion is deleted and guarded against returning.
- I4 is complete: remaining legacy carrier APIs and compatibility taxonomy surface have been deleted or isolated now that semantic store, scope, and relationship paths no longer depend on them.
- I4 first block is complete: the unused `semanticcollectoradapter` source/header have been removed from CMake and guarded as forbidden legacy collector adapter files.
- I4 second block is complete: `SemanticIndex` no longer exposes or stores a legacy `sym_list` database injection/access API.
- I4 third block is complete: dead `sym_list` symbol database mutator/accessor/index/scope-tree storage has been deleted, including `syminfoindex.cpp`, `syminfoscope.cpp`, and `scope_tree.h`.
- I4 fourth block is complete: native taxonomy metadata checks no longer route through the retired private raw-kind bridge.
- I4 fifth block is complete: unused legacy metadata write-back helpers have been deleted from the taxonomy compatibility surface.
- I4 sixth block is complete: legacy `SymbolInfo` owner/visibility taxonomy overloads have been removed from the public compatibility header; callers verify the semantic metadata contract instead.
- I4 seventh block is complete: legacy `SymbolInfo` interface helper and declaration-helper overloads have been removed from the public taxonomy compatibility header.
- I4 eighth block is complete: legacy `SymbolInfo` scope/visibility helper APIs have been deleted; definition visibility callers use the native semantic metadata overload.
- I4 ninth block is complete: legacy taxonomy no longer exposes `packageScopeNames(QList<SymbolInfo>)`; tests compute package scopes locally before building semantic records.
- I4 tenth block is complete: legacy completion type matching compatibility APIs have been deleted in favor of `SemanticCompletionKind` metadata matching.
- I4 eleventh block is complete: legacy outline grouping compatibility APIs have been deleted; outline checks now use native semantic metadata and raw collector kind directly.
- I4 twelfth block is complete: legacy raw collector kind round-trip helpers have been removed from the public taxonomy compatibility header.
- I4 thirteenth block is complete: tracked tests no longer include the removed `semanticcollectoradapter` header; legacy fixture-to-record conversion is isolated in a test fixture helper and guarded against adapter include regressions.
- I4 fourteenth block is complete: direct `symboltaxonomylegacy.h` includes in tracked tests are isolated to the legacy fixture conversion helper and guarded against spreading back into test bodies.
- I4 fifteenth block is complete: global `symboltaxonomylegacy.h` includes are guarded to remain isolated to `symboltaxonomy.cpp` and the test fixture conversion helper.
- I5 is complete: the Phase I release gate passed with full Ninja, full CTest, normal `legacy_field_policy_guard.ctest`, opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET`, static core legacy scans, and `git diff --check`.
- I5 first block is complete: lowercase `rawCollectorKind` / `requestedRawCollectorKind` field and helper names were renamed to semantic-native `collectorKind` / `CollectorKind` naming, and the guard continues to reserve the raw names as retired compatibility terms.
- I5 second block is complete: `symboltaxonomylegacy.h` and the production `sym_type_e` taxonomy overloads were deleted; test fixture conversion now owns the remaining `SymbolInfo` to semantic metadata adapter needed by tracked tests.
- I5 third block is complete: root/core `syminfo*` carrier files moved into `test_sv` fixture scope, core CMake no longer builds them, and the Phase I zero target now passes for core source while fixture-only `sym_list` remains under `test_sv`.
- I5 fourth block is complete: stale core `syminfo` includes and `SymbolInfo` naming were removed, tracked tests were migrated to fixture-local/native semantic helpers, GUI smoke fixtures synchronize native records before snapshot-only checks, and the final Phase I release gate passed.
- J0 is complete: `legacy_field_policy_guard.ctest` defines the repo-wide fixture cleanup allowlist and guards new `sym_list` / `syminfo` legacy terms outside the current cleanup scope.
- J1 is complete: semantic-native test record builders cover metadata, owners, type info, local handles, stable keys, and relationship endpoints.
- J2 is complete: completion and jump tests use semantic-native records instead of hand-authored `sym_list::SymbolInfo` fixture data.
- J3 is complete: relationship and GUI smoke tests use semantic-native records and no longer include or populate the fixture-only `sym_list` carrier.
- J4 is complete: `test_sv/syminfo*` files and the reverse fixture adapter in `semantic_fixture_records.h` have been deleted, CMake no longer builds fixture carrier sources, and full Ninja/full CTest/guard checks passed.
- J5 is complete: the final zero target now scans first-party repo source, including tracked test fixtures, while excluding docs and guard definitions; full Ninja, full CTest, normal guard, final zero-target guard, static scans, and `git diff --check` passed.
- Phase J progress tracking uses per-subphase accounting: each J subphase starts at 100% remaining and each turn must report the current subphase plus that subphase's remaining percentage. Current subphase: J5 complete.
- `SemanticIndexSnapshot` is the intended single UI query truth.
- Do not add feature-specific workarounds in UI, scheduler, or analyzer code.
- During Phase I, move the remaining raw collector compatibility out of the collector/store implementation itself so the legacy carrier can be deleted rather than merely guarded.
- During Phase J, do not touch product semantics for feature expansion; focus on replacing test fixtures with native semantic records, deleting `test_sv/syminfo*`, and tightening guards after each migrated block.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing; it does not own feature policy.
- `SemanticIndexSnapshot` should be the single query truth for UI-facing semantic reads.
- Query services and feature services own feature-specific semantic reads and report shaping.
- UI code renders service/model output and must not run Slang or scan workspace files directly.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
