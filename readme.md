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
- Phase I Semantic Store Native / Collector Native is active on this branch: replace the remaining `sym_list::SymbolInfo` / `sym_type_e` collector and store body with semantic-native collection, storage, scope, and relationship internals.
- I0 is complete: `legacy_field_policy_guard.ctest` now defines the Phase I zero-legacy target terms and an opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` final scan for I5.
- G0 is complete: `SemanticRelationshipResult` no longer carries legacy `fromSymbol` / `toSymbol` endpoint payloads; relationship consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries legacy `symbol` payloads; consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` no longer expose the retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, or `findDefinitions`.
- G3 is complete: direct `symbolId`, `symbolType`, `moduleScope`, `dataType`, and `sym_type_e` product-facing use has been removed or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The Phase G release gate passed locally with full Ninja and full CTest.
- The Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.
- Completion compatibility, snapshot/store `SymbolInfo` carriers, feature-service internal helpers, `syminfo` legacy queries, and collector/import adapter surfaces were tightened during Phase H.
- Phase I progress tracking uses per-subphase accounting: each I subphase starts at 100% remaining and each turn must report the current subphase plus that subphase's remaining percentage. Current subphase: I4.
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
- I4 is current: delete the remaining legacy carrier APIs and compatibility taxonomy surface now that semantic store, scope, and relationship paths no longer depend on them.
- `SemanticIndexSnapshot` is the intended single UI query truth.
- Do not add feature-specific workarounds in UI, scheduler, or analyzer code.
- During Phase I, move the remaining raw collector compatibility out of the collector/store implementation itself so the legacy carrier can be deleted rather than merely guarded.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing; it does not own feature policy.
- `SemanticIndexSnapshot` should be the single query truth for UI-facing semantic reads.
- Query services and feature services own feature-specific semantic reads and report shaping.
- UI code renders service/model output and must not run Slang or scan workspace files directly.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
