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
- Do not push unless explicitly asked.
- Keep local commits coherent, concise, and architecture-oriented.
- Keep source, tests, UI strings, CMake, and these docs English / ASCII.

## Development Rules

- New semantic features should flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code should render service or model output; it should not run Slang directly or scan workspace files directly.
- Scheduler and analyzer code own timing, lifecycle, extraction, and publication; they should not own feature policy.
- Prefer real workspace fixtures, especially `test_sv/new`, when expanding semantic behavior.
- When shared semantic, scheduler, editor, or project boundaries change, run full Ninja and full `ctest --output-on-failure`.
- Use focused tests for narrow feature changes, then full verification before committing.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing.
- `SemanticIndex` owns semantic facts, snapshots, relationships, references, diagnostics, and cached content.
- Query services and feature services own feature-specific semantic reads and report shaping.
- Coordinators own UI command routing, panel refresh, progress policy, navigation commands, and runtime wiring.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
