# ZeroSlack Handoff

ZeroSlack is a lightweight SystemVerilog workspace browser/editor. It is moving toward:

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
- Latest commit: verify with `git log -1 --oneline`
- Expected tree after handoff commit/push: clean except local Git warning noise.

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
- Do not commit or push unless explicitly asked.
- Never discard user changes or use destructive git commands unless explicitly requested.

## Latest Completed Work

The latest architecture block extracted `ModeCommandCoordinator`, which owns mode/input command routing that had remained in `MainWindow`:

- Owns key press and key release routing to `ModeManager`.
- Owns `ModeManager::navigationToggleRequested` routing to `NavigationPaneCoordinator`.
- `MainWindow` key event overrides now only delegate to the mode command boundary before falling back to `QMainWindow`.

This builds on the recent `SemanticRuntimeCoordinator` block:

- Owns `SymbolRelationshipEngine`, `SlangManager`, and `SmartRelationshipBuilder` lifetimes.
- Attaches the relationship engine to `SemanticIndex`.
- Injects Slang and relationship runtime dependencies into `CompletionManager`.
- Creates the relationship builder through `SemanticIndex`.
- Clears relationships during runtime teardown.
- `MainWindow` now passes runtime engine/builder pointers to `AnalysisCoordinator` without owning setup details.
- GUI/perf test drain helpers now cancel relationship work through the runtime coordinator.

This builds on the recent `NavigationCommandCoordinator` block:

- Connects `NavigationManager::navigationRequested` and `symbolNavigationRequested`.
- Owns file/line/column navigation, including opening missing files, activating already-open tabs, cursor placement, centering, focus, and mouse-to-cursor behavior.
- Provides a common navigation callback target for Problems, References, Relationships, and editor definition jumps.
- `TabManager::activateOpenFile()` provides the narrow tab activation API needed by navigation without exposing `QTabWidget`.
- `MainWindow` no longer directly handles navigation signals or cursor movement.

This builds on the previous `FileCommandCoordinator` block, which owns file/edit/workspace commands that had remained in `MainWindow`:

- File actions: new, open, save, and save-as.
- Edit actions: copy, paste, cut, undo, and redo on the active editor.
- Workspace command: open directory as workspace.
- Close-event unsaved-change confirmation.
- `EditorCoordinator` command callbacks now call the file command boundary instead of bouncing through `MainWindow` slots.
- `MainWindow` keeps the Qt auto-connected menu slots, but they are thin delegators.

It also builds on the recent `AnalysisCoordinator` block, which owns analysis event routing that had remained in `MainWindow`:

- Configures `AnalysisScheduler` with `DocumentModel`, `ProjectModel`, `SymbolAnalyzer`, open-file content, workspace-open state, cancellation, relationship engine, and relationship builder dependencies.
- Routes scheduler relationship invalidation/refresh signals to `CompletionManager` and `NavigationManager`.
- Routes scheduler diagnostics refresh requests to the Problems panel callback.
- Routes scheduler and symbol-analysis document refresh signals to the active editor scope/current-line refresh workflow.
- Bridges workspace file-change notifications into scheduler debounced external-file handling.
- Bridges `AnalysisProgressCoordinator` status/error signals to injected status-message callbacks.
- Keeps single-file smart relationship completion/error status text out of `MainWindow`.

This builds on the previous architecture block that thinned `MainWindow` by extracting focused UI/editor coordination boundaries:

- `ProblemsPanelCoordinator` owns Problems dock/filter/tree setup, `DiagnosticQuery` construction, report rendering, expansion preservation, and double-click navigation callbacks.
- `ReferencesPanelCoordinator` owns References dock/filter/tree setup, `ReferenceQuery` construction, report rendering, expansion preservation, status messages, and navigation callbacks.
- `RelationshipsPanelCoordinator` owns Relationships dock/filter/tree setup, direct/tree filters, `RelationshipBrowseQuery` / `HierarchyQuery` construction, report rendering, expansion preservation, status messages, and navigation callbacks.
- `SemanticPanelUtils` owns shared relationship labels, count labels, normalized tree expansion keys, expanded-state capture, and restore.
- `NavigationPaneCoordinator` owns Navigation dock/widget creation, sizing, dock features, `NavigationManager` widget attachment, and visibility toggling.
- `EditorCoordinator` owns editor callback/signal setup, editor-originated command routing, active-tab refresh coordination, and alternate-mode propagation across open editors.
- `TabManager::editorCount()` provides narrow read-only open-editor enumeration for coordinator-owned mode propagation.
- `MainWindow` now injects mode command, semantic runtime, file command, navigation command, analysis, and panel callbacks instead of directly owning these workflows.
- `gui_smoke_test` was adapted to exercise the new coordinator boundaries through visible workflows.

This builds on earlier work where `MyCodeEditor` was decoupled from `MainWindow`/manager classes, include fallback moved to `WorkspaceManager::resolveIncludePath`, document-close semantic cleanup moved into `AnalysisScheduler`, open-document analysis was decoupled from `TabManager`, and CompletionManager reads were routed through query services.

## Latest Validation

Validation passed after the latest code/doc update:

- `E:\QT6\Tools\CMake_64\bin\cmake.exe --build ... --target gui_smoke_test`
- `ctest -R "gui_smoke_test" --output-on-failure`
- full default target rebuild
- full `ctest --output-on-failure`: 6/6 passed
- `git diff --check`
- changed/new source/test/UI/CMake/handoff non-ASCII scan: empty except local Git warning noise
- changed/new source/test/UI/CMake/handoff trailing whitespace scan: empty except local Git warning noise
- forbidden-file guard: `FORBIDDEN_GUARD_OK`

Full default target rebuild may exceed 300 seconds while linking; rerunning the same build command has completed the remaining target.
In a bare PowerShell session, prepend `E:\QT6\Tools\mingw1310_64\bin` to `PATH` before CMake/CTest so MinGW child tools such as `cc1plus.exe` can find their runtime DLLs. For GUI tests, also prepend `E:\QT6\6.10.2\mingw_64\bin`.

## Current Architecture Summary

- `ProjectModel` publishes workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` tracks open document state.
- `AnalysisScheduler` owns analysis trigger timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, and relationship data refresh requests.
- `AnalysisProgressCoordinator` owns workspace analysis progress dialog policy and cancel state.
- `AnalysisCoordinator` owns scheduler/progress/workspace/symbol analysis signal routing and active-editor refresh policy.
- `FileCommandCoordinator` owns file/edit/workspace commands and close-event unsaved-change confirmation.
- `NavigationCommandCoordinator` owns navigation signal routing, tab activation/opening, and editor cursor placement.
- `ModeCommandCoordinator` owns mode key event routing and navigation-pane toggle routing.
- `SemanticRuntimeCoordinator` owns semantic runtime object lifetimes and dependency injection into SemanticIndex/CompletionManager.
- `SemanticIndex` is the semantic facade and still wraps live `sym_list` in some transitional paths.
- `SemanticIndexSnapshot` stores read-only symbols, relationships, diagnostics, cached file content, and minimal scope names.
- Query services exist for definition, completion, relationship, hierarchy, reference, diagnostics, and search.
- Problems, References, Relationships, Navigation pane, navigation commands, mode command routing, editor/tab/mode workflows, analysis event routing, file/edit/workspace commands, and semantic runtime setup are now split out of `MainWindow` into focused coordinators.
- `MainWindow` should keep shrinking toward UI composition and high-level callback wiring.

## Next Best Steps

Pick one medium-sized, coherent, verifiable architecture block:

- Move a related set of UI/editor/completion semantic reads or analysis policy checks behind `SemanticIndex`, Query Services, models, or `AnalysisScheduler`.
- Extract another complete `MainWindow` coordination boundary into a focused coordinator or existing scheduler/model boundary.
- Thin one editor/completion workflow end-to-end without changing visible behavior.

Prioritize production-code architecture progress. Add tests only as focused regression protection directly tied to a production code change.

## New Session Opener

```text
Please take over ZeroSlack and continue iterative development.

First read readme.md, plan.md, goal.md, and version.h, then inspect:
- git log -1 --oneline
- git branch --show-current
- git diff --name-only
- git status -sb

Known current state:
- Workspace: E:\ZeroSlack\ZeroSlack
- Branch: tree_sitter_and_slang
- Version: 0.0.20/slang25
- Build path: Qt 6 + CMake + Ninja only
- The latest pushed commit should include the MainWindow coordinator extraction block.
- Working tree should be clean except local Git warning noise.

Rules:
- Do not restore demo.pro, *.pro, *.pri, qmake, .claude, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived perflog.
- Keep source/test/UI/CMake and handoff docs English / ASCII.
- Do one medium-sized, coherent, verifiable architecture block at a time.
- Prioritize production-code architecture progress; tests should be focused regressions tied to the production change.
- Stop for handoff when a coherent block is validated, context is getting large, validation is blocked, or the user asks to stop.

Continue toward goal.md.
```
