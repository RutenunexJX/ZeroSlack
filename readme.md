# ZeroSlack Handoff

ZeroSlack is a lightweight SystemVerilog workspace browser/editor moving toward:

```text
Tree-sitter live syntax and editor scope
Slang semantic facts
ProjectModel / DocumentModel / AnalysisScheduler
SemanticIndex / SemanticIndexSnapshot
Query Services
Thin UI consumers
```

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore `demo.pro`, any `*.pro`, any `*.pri`, or qmake.
- Do not restore `.claude/` or Claude local config.
- Do not restore SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Keep source, tests, UI strings, CMake, and these docs English / ASCII.
- Auto-create a local commit after each completed medium-sized coherent architecture block passes the agreed build/test/hygiene gates.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Never discard user changes or use destructive git commands unless explicitly requested.

## Latest Verified Block

Active document state now routes through `DocumentModel` snapshots.
`TabManager` exposes current/editor `DocumentSnapshot` queries and reads current-tab text from the model cache, while navigation, semantic panel refresh, tab titles, close/save notifications, and active-editor analysis refresh use snapshot file identity instead of direct editor file-name state.

## Current Architecture Snapshot

- `ProjectModel` owns workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` owns open document snapshots, cached open-document text, dirty/saved state, cursor/file-name refresh events, and live module names.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, and scheduler-level analysis events.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes, `SymbolAnalyzer`, and dependency injection into `SemanticIndex`.
- `AnalysisCoordinator` owns scheduler/progress/workspace signal routing and active-editor refresh policy.
- `SemanticIndex` owns semantic facts, snapshot publication, relationship lifecycle data, diagnostics, and shared semantic read helpers.
- Query services own feature-specific reads for completion, definition, relationship, hierarchy, references, diagnostics, search, and source navigation.
- Coordinators own UI/editor command routing, dock/panel refresh, progress policy, navigation commands, file commands, mode commands, and semantic runtime setup.
- `TabManager` owns tab lifecycle, file reads/writes, tab titles, and `DocumentModel` registration/save updates.
- `MyCodeEditor` owns editor UI behavior and live syntax state; project semantic decisions should stay in models, services, scheduler, runtime, and coordinators.
