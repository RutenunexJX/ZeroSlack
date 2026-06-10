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

## Current State

- Workspace: `E:\ZeroSlack\ZeroSlack`
- Branch: `tree_sitter_and_slang`
- Version: `0.0.20/slang25` in `version.h`
- Build path: Qt 6 + CMake + Ninja only
- Latest local commit is local-only; check `git log -1 --oneline`.
- Local branch is intentionally ahead of origin until the user explicitly asks to push.

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

Source navigation target selection now lives behind `SourceNavigationService`.
`MyCodeEditor` maps service targets to hover state, cursors, and jump/open dispatch instead of duplicating include/import/identifier selection in event handlers.

## Latest Validation

Latest code validation passed:

- focused `completion_test` and `gui_smoke_test`
- full Ninja build
- full CTest: 6/6 passed
- `git diff --check`, ASCII scan, trailing-whitespace scan, forbidden-file guard

Build/test commands need `E:\QT6\Tools\mingw1310_64\bin` on `PATH` so MinGW `cc1plus.exe` can load `libwinpthread-1.dll`.

## Current Architecture Snapshot

- `ProjectModel` owns workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` owns open document state.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, lifecycle cleanup, and relationship data refresh requests.
- `AnalysisProgressCoordinator` owns workspace analysis progress dialog policy and cancel state.
- `AnalysisCoordinator` owns scheduler/progress/workspace/symbol signal routing and active-editor refresh policy.
- `AnalysisCommandCoordinator` owns editor-originated analysis commands and relationship-work cancellation.
- `EditorCoordinator` owns editor signal routing, alternate-mode application, include/open-file handlers, file commands, navigation commands, relationship analysis requests, and semantic panel refresh requests.
- `FileCommandCoordinator` owns file/edit/workspace commands and close-event unsaved-change confirmation.
- `NavigationCommandCoordinator` owns navigation signal routing, tab activation/opening, and editor cursor placement.
- `ModeCommandCoordinator` owns mode key event routing and navigation-pane toggle routing.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes and dependency injection into `SemanticIndex`.
- `SemanticRuntimeCoordinator` configures query service singleton dependencies on the shared `SemanticIndex`.
- `SemanticPanelRefreshCoordinator` owns semantic panel provider/navigation/status wiring and refresh commands.
- `CompletionService` owns module/global/command completion results, trigger policy, row scoring, symbol display descriptions, command-mode catalog/matching and symbol presentation, smart/all-symbol scoring, typed symbol scoring and `SymbolInfo` completions, keyword/abbreviation scoring, context-aware completion assembly, struct member parsing/completion, scope completion, current-module lookup, and relationship-driven completion candidates over `SemanticIndex`.
- `AlternateCommandService` owns alternate-mode command catalog, filtering, normalization, and command membership checks.
- `SourceNavigationService` owns SystemVerilog include directive, package-import, identifier hit-testing, and source navigation target selection.
- `CompletionModel` renders editor completion items, owns selectable-row policy, and uses `CompletionService` for command symbol presentation and scoring.
- `CompletionManager` is now a stateless compatibility facade over `CompletionService`.
- `NavigationService` owns module hierarchy and symbol outline semantic assembly for the navigation pane.
- `DefinitionNavigationService` owns editor-facing definition jump targets, availability, and tooltip text over `DefinitionService`.
- Problems, References, Relationships, Navigation pane, editor/tab/mode workflows, analysis commands, analysis event routing, file/edit/workspace commands, and semantic runtime setup are out of `MainWindow`.

## Next Best Steps

Pick one medium-sized, coherent, verifiable architecture block:

- Move a related group of UI/editor/completion semantic reads or analysis policy checks behind `SemanticIndex`, Query Services, models, or `AnalysisScheduler`.
- Extract another complete `MainWindow` coordination responsibility into a focused coordinator or existing scheduler/model boundary.
- Thin one editor/completion workflow end-to-end without changing visible behavior.

## Session Start Checklist

Read `readme.md`, `plan.md`, `goal.md`, and `version.h`, then inspect:

```text
git log -1 --oneline
git branch --show-current
git diff --name-only
git status -sb
```

Continue toward `goal.md` with one coherent architecture block at a time.
