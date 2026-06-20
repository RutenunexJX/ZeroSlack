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
- G0 is complete: `SemanticRelationshipResult` no longer carries legacy `fromSymbol` / `toSymbol` endpoint payloads; relationship consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries legacy `symbol` payloads; consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` no longer expose the retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, or `findDefinitions`.
- G3 is complete: direct `symbolId`, `symbolType`, `moduleScope`, `dataType`, and `sym_type_e` product-facing use has been removed or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The Phase G release gate passed locally with full Ninja and full CTest.
- The Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.
- Completion compatibility, snapshot/store `SymbolInfo` carriers, feature-service internal helpers, `syminfo` legacy queries, and collector/import adapter surfaces were tightened during Phase H.
- `SemanticIndexSnapshot` is the intended single UI query truth.
- Do not add feature-specific workarounds in UI, scheduler, or analyzer code.
- During Phase H, move toward a final model where raw collector compatibility is concentrated in a minimal collector adapter and absent from product, service, report, completion, and snapshot contracts.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing; it does not own feature policy.
- `SemanticIndexSnapshot` should be the single query truth for UI-facing semantic reads.
- Query services and feature services own feature-specific semantic reads and report shaping.
- UI code renders service/model output and must not run Slang or scan workspace files directly.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
