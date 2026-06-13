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
- Batch architecture work when boundaries are independent, verify once, then create one large local commit for the completed turn.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Never discard user changes or use destructive git commands unless explicitly requested.

## Current Development Workflow

Use goal-mode progress for Phase 3 RTL understanding work.
Each turn should report the remaining Phase 3 percentage.
Prefer service/report increments that read `SemanticIndexSnapshot` data before adding UI rendering.
Batch independent blocks only when their files, lifetimes, and API boundaries do not overlap.
After each block, run light sanity: affected build or compile, focused tests, `git diff --check`, or static boundary scans.
Before committing, run full Ninja, full `ctest --output-on-failure`, hygiene scans, and the forbidden-file guard.
When full verification passes, create one large local commit for the completed turn.
Reduce batch size when blocks share core files or unsettled API boundaries.

## Current Architecture Snapshot

- `ProjectModel` owns workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` owns open document snapshots, cached open-document text, text/saved versions, dirty/saved state, cursor/file-name refresh events, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, and scheduler-level analysis events. Its construction wiring is split from document and relationship lifecycle operations.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes and dependency injection into semantic runtime services.
- `AnalysisCoordinator` owns scheduler/progress/workspace signal routing and active-editor refresh policy.
- `SemanticIndex` owns semantic facts, snapshot publication, relationship lifecycle data, diagnostics, and shared semantic read helpers.
- `SymbolAnalyzer` owns Slang extraction orchestration behind scheduler/runtime boundaries. Slang execution, async lifecycle, workspace assembly, and SemanticIndex publication/write-back live in focused files.
- Query services own feature-specific reads for completion, definition, relationship, hierarchy, references, diagnostics, search, source navigation, and panel query shaping.
- Coordinators own UI/editor command routing, dock/panel refresh, progress policy, navigation commands, file commands, mode commands, and semantic runtime setup.
- Relationship graph helpers can use injected semantic data sources in tests and runtime wiring instead of hard-coded global reads.
- `TabManager` owns tab lifecycle, file reads/writes, tab titles, and `DocumentModel` registration/save updates.
- `MyCodeEditor` owns editor event flow and public editor adapters. Gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation live in focused editor subsystem files.

## Phase 3 Focus

Completed baseline services: Module Brief, Signal Journey, and relationship report explanations.
Continue with Clock/Reset Domain Map, FSM State Transition Graph, Semantic Diff, and UI rendering for service reports.
Keep the path `snapshot -> Query Service/service report -> UI render`.
