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
- Latest local commit is local-only; check `git log -1 --oneline` at session start.
- Local branch is intentionally ahead of origin until the user explicitly asks to push.

Expected local warning noise:

- `unable to access C:\Users\14971/.config/git/ignore: Permission denied`
- `LF will be replaced by CRLF`

Prefer `git diff --name-only`, `git diff --stat`, and `git diff --check` over noisy status output.

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

## Documentation Upkeep

These docs are living handoff material, not a changelog.

- `readme.md` keeps only current handoff state, latest verified block, validation, and next start instructions.
- `plan.md` keeps execution policy and the next block menu.
- `goal.md` keeps stable product and architecture goals.
- When a new block completes, replace the prior "latest block" text instead of appending a history chain.
- Remove stale commit IDs, validation notes, next steps, and implementation details that no longer help the next session.
- Keep detailed history in Git commits, not in these files.

## Latest Verified Block

The latest block moved smart and all-symbol completion scoring behind `CompletionService`.

- `CompletionService` now owns all-symbol scoring, all-symbol name completions, smart completion assembly, and relationship/scope scoring over `SemanticIndex`.
- Legacy `CompletionManager` smart/all-symbol APIs delegate to `CompletionService`, and obsolete all-symbol cache plus manager-side smart scoring helpers were removed.
- Snapshot-backed completion tests cover service smart/all-symbol completions and the `CompletionManager` delegation path.

## Latest Validation

Validation passed after the latest code/doc update:

- affected build: `completion_test`
- focused CTest: `completion_test` passed
- full default target rebuild
- full `ctest --output-on-failure`: 6/6 passed
- `git diff --check`
- changed/new source/test/UI/CMake/handoff non-ASCII scan: `ASCII_SCAN_OK`
- changed/new source/test/UI/CMake/handoff trailing whitespace scan: `TRAILING_WHITESPACE_SCAN_OK`
- forbidden-file guard: `FORBIDDEN_GUARD_OK`

In a bare PowerShell session, prepend `E:\QT6\Tools\mingw1310_64\bin` to `PATH` before CMake/CTest so MinGW child tools can find runtime DLLs. For GUI tests, also prepend `E:\QT6\6.10.2\mingw_64\bin`.

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
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes and dependency injection into `SemanticIndex` / `CompletionManager`.
- `SemanticPanelRefreshCoordinator` owns semantic panel provider/navigation/status wiring and refresh commands.
- `CompletionService` owns module/global/command completions, smart/all-symbol scoring, typed symbol and `SymbolInfo` completions, context-aware completion assembly, struct member parsing/completion, scope completion, current-module lookup, and relationship-driven completion candidates over `SemanticIndex`.
- Problems, References, Relationships, Navigation pane, editor/tab/mode workflows, analysis commands, analysis event routing, file/edit/workspace commands, and semantic runtime setup are split out of `MainWindow`.

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
