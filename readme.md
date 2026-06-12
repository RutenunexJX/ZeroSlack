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
- Batch architecture work, verify once, then commit each coherent block separately.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Never discard user changes or use destructive git commands unless explicitly requested.

## Current Workflow

Prefer batches of three or more medium-sized, clearly themed architecture blocks.
After each block, run only light sanity checks such as affected build, focused tests, `git diff --check`, or static boundary scans.
After two or three blocks, run full Ninja, full `ctest --output-on-failure`, hygiene scans, and the forbidden-file guard.
When full verification passes, create separate local commits for each block. Reduce batch size when blocks touch the same core files or unsettled API boundary.

## Current Architecture Snapshot

- `ProjectModel` owns workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` owns open document snapshots, cached open-document text, text/saved versions, dirty/saved state, cursor/file-name refresh events, and live module names.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, and scheduler-level analysis events.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes, `SymbolAnalyzer`, and dependency injection into `SemanticIndex`.
- `AnalysisCoordinator` owns scheduler/progress/workspace signal routing and active-editor refresh policy.
- `SemanticIndex` owns semantic facts, snapshot publication, relationship lifecycle data, diagnostics, and shared semantic read helpers.
- Query services own feature-specific reads for completion, definition, relationship, hierarchy, references, diagnostics, search, source navigation, and panel query shaping.
- Coordinators own UI/editor command routing, dock/panel refresh, progress policy, navigation commands, file commands, mode commands, and semantic runtime setup.
- Relationship graph helpers can use injected semantic data sources in tests and runtime wiring instead of hard-coded global reads.
- `TabManager` owns tab lifecycle, file reads/writes, tab titles, and `DocumentModel` registration/save updates.
- `MyCodeEditor` owns editor UI behavior and live syntax state; project semantic decisions should stay in models, services, scheduler, runtime, and coordinators.
