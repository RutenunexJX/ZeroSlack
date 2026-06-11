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

Alternate-command execution now routes through coordinators.
`MyCodeEditor` resolves alternate-mode command text into `AlternateCommandAction` requests only; `EditorCoordinator` forwards those requests to `FileCommandCoordinator`, which owns file/edit command execution.

## Current Architecture Snapshot

- `ProjectModel` owns workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` owns open document state.
- `AnalysisScheduler` owns analysis timing, open-document relationship debounce, debounce/cancel policy, relationship background work, diagnostics refresh requests, lifecycle cleanup, and relationship data refresh requests.
- `SemanticIndex` owns semantic facts, analysis write-back, snapshot publication, relationship-analysis snapshot lifecycle, definition candidate resolution, relationship endpoint enrichment, generic symbol search, relationship-driven/enum/module-port/typed completion symbol candidate reads, and shared semantic read helpers for symbols, relationships, diagnostics, current module lookup, module-internal symbols, and scope scoring.
- `AnalysisProgressCoordinator` owns workspace analysis progress dialog policy and cancel state.
- `AnalysisCoordinator` owns scheduler/progress/workspace/symbol signal routing and active-editor refresh policy.
- `EditorCoordinator` owns editor signal routing, alternate-mode application, include/open-file handlers, alternate-command action routing, navigation commands, reference/relationship panel requests, and semantic panel refresh requests.
- `EditorSemanticContextService` owns editor-context-to-query assembly and editor-facing completion, command mode, definition navigation, source navigation target, and source symbol action reads.
- `FileCommandCoordinator` owns file/edit/workspace action routing, alternate-command execution, commands, and close-event unsaved-change confirmation.
- `NavigationCommandCoordinator` owns navigation signal routing, tab activation/opening, and editor cursor placement.
- `NavigationPaneCoordinator` owns the navigation dock/widget and `NavigationManager` input wiring.
- `NavigationManager` owns navigation view cache/refresh state and delegates semantic navigation reads to `NavigationService`.
- `ModeCommandCoordinator` owns mode key event routing and navigation-pane toggle routing.
- `SemanticDockCoordinator` owns Problems, References, and Relationships dock creation, placement, and semantic refresh coordinator assembly.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes and dependency injection into `SemanticIndex`.
- `SemanticRuntimeCoordinator` configures query service singleton dependencies on the shared `SemanticIndex`.
- `SemanticPanelRefreshCoordinator` owns semantic panel provider/navigation/status wiring and refresh commands.
- `CompletionService` owns module/global/command/editor completion results over `SemanticIndex`, command-mode completion state, completion trigger and activation state, row scoring, symbol display descriptions, command-mode catalog/matching/input/exit policy and symbol presentation, smart/all-symbol scoring, typed symbol scoring and `SymbolInfo` completion presentation, keyword/abbreviation scoring, context-aware completion assembly, struct member parsing/completion, scope completion orchestration, and relationship-driven completion candidates.
- `AlternateCommandService` owns alternate-mode command catalog, filtering, normalization, command membership checks, command action classification, and command completion state.
- `SourceNavigationService` owns SystemVerilog include directive, package-import, identifier hit-testing, and source navigation target primitives.
- `CompletionModel` renders editor completion items, owns selectable-row policy, and uses `CompletionService` for command symbol presentation and scoring.
- `CompletionManager` is now a stateless compatibility facade over `CompletionService`.
- `SearchService` owns feature-facing symbol search queries over `SemanticIndex`.
- `DefinitionService` owns feature-facing definition queries and editor/member context handling over `SemanticIndex`.
- `RelationshipService` owns feature-facing relationship queries, filtering, sorting, and reports over `SemanticIndex`.
- `NavigationService` owns module hierarchy, symbol outline, and module target semantic assembly for the navigation pane.
- `DefinitionNavigationService` owns editor-facing definition query assembly, jump targets, availability, and tooltip text over `DefinitionService`.
- Problems, References, Relationships, Navigation pane, editor/tab/mode workflows, analysis commands, analysis event routing, file/edit/workspace commands, and semantic runtime setup are out of `MainWindow`.
