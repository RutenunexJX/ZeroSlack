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

- Current priority remains semantic product-logic consolidation before broad RTL feature expansion.
- `SemanticIndexSnapshot` is the intended single UI query truth.
- Phase B stabilizes taxonomy/source-role helper rules; Phase C lands stable semantic metadata and Query Service/UI contracts.
- `sym_type_e` remains raw collector compatibility during migration.
- Do not add feature-specific workarounds in UI, scheduler, or analyzer code.
- Validate semantic behavior against `test_sv/new`.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing; it does not own feature policy.
- `SemanticIndexSnapshot` should be the single query truth for UI-facing semantic reads.
- Query services and feature services own feature-specific semantic reads and report shaping.
- UI code renders service/model output and must not run Slang or scan workspace files directly.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
