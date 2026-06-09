# ZeroSlack Development Plan

This is the short execution plan. Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals.

## Direction

Keep moving ZeroSlack toward:

- `ProjectModel`
- `DocumentModel`
- `AnalysisScheduler`
- `SemanticIndex`
- `SemanticIndexSnapshot`
- Query Services
- snapshot-backed UI/service reads
- thinner `MainWindow`

Tree-sitter remains the live syntax/editing layer. Slang remains the semantic fact source.

## Current Status

Implemented and in active use:

- CTest targets: `ts_doc_test`, `completion_test`, `jump_test`, `relationship_test`, `gui_smoke_test`, `large_file_perf_test`
- ProjectModel and DocumentModel minimal boundaries
- AnalysisScheduler for major analysis triggers, relationship work, diagnostics refresh requests, lifecycle cleanup, and relationship data refresh requests
- AnalysisProgressCoordinator for workspace analysis progress dialog policy and cancel state
- AnalysisCoordinator for scheduler/progress/workspace/symbol signal routing and active-editor refresh policy
- AnalysisCommandCoordinator for editor-originated analysis command routing and relationship-work cancellation
- FileCommandCoordinator for file/edit/workspace commands and close-event unsaved-change confirmation
- NavigationCommandCoordinator for navigation signals, tab activation/opening, and editor cursor placement
- ModeCommandCoordinator for mode key event routing and navigation-pane toggle routing
- SemanticRuntimeCoordinator for semantic runtime lifetimes and SemanticIndex/CompletionManager dependency injection
- SemanticPanelRefreshCoordinator for Problems/References/Relationships provider, navigation, status, refresh, and active-editor refresh routing
- SemanticIndex facade and SemanticIndexSnapshot storage
- Query services for definition, completion, relationship, hierarchy, reference, diagnostics, and search
- Problems, References, and Relationships panels backed by service reports
- Problems, References, Relationships, Navigation pane, navigation commands, mode command routing, editor/tab/mode workflows, analysis event routing, file/edit/workspace commands, and semantic runtime setup extracted from `MainWindow` into focused coordinators
- Real multi-file relationship fixture coverage for instantiation, calls, reads, writes, diagnostics filtering, workspace/current-file filtering, hierarchy, references, and timing relationships

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- `MainWindow` still has more high-level UI wiring than desired.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Latest Completed Block

The latest block extracted `AnalysisCommandCoordinator` from `MainWindow`:

- editor-originated single-file relationship analysis requests
- a narrow open-file analysis schedule/cancel command boundary
- relationship-work cancellation during coordinator teardown
- removal of public analysis command wrappers from `MainWindow`
- GUI smoke and large-file perf drain helpers updated to exercise the new analysis command boundary

The block was validated with focused GUI smoke build/test, full build, full CTest, diff hygiene, ASCII/trailing scans, and forbidden-file guard. In a plain shell, prepend `E:\QT6\Tools\mingw1310_64\bin` to `PATH` before CMake/CTest; GUI tests also need `E:\QT6\6.10.2\mingw_64\bin`.

The previous blocks extracted `SemanticPanelRefreshCoordinator`, `ModeCommandCoordinator`, `SemanticRuntimeCoordinator`, `NavigationCommandCoordinator`, `FileCommandCoordinator`, and `AnalysisCoordinator`. The earlier UI/editor coordination block extracted:

- `ProblemsPanelCoordinator`
- `ReferencesPanelCoordinator`
- `RelationshipsPanelCoordinator`
- `SemanticPanelUtils`
- `NavigationPaneCoordinator`
- `EditorCoordinator`
- `TabManager::editorCount()` for coordinator-owned editor enumeration
- GUI smoke coverage adapted to validate the visible workflows through these boundaries

## Next Architecture Blocks

Choose one:

1. Move a related group of UI/editor/completion semantic reads or analysis policy checks behind `SemanticIndex`, Query Services, models, or `AnalysisScheduler`.
2. Extract another complete `MainWindow` coordination responsibility into a focused coordinator or existing scheduler/model boundary.
3. Thin one editor/completion workflow end-to-end without changing visible behavior.
4. Move a related completion/editor legacy state path behind existing service or model APIs.

Prioritize production-code architecture progress. Do not use pure assertion expansion as the main increment. Add tests only as focused regression protection directly tied to a production code change.

Avoid:

- broad scattered rewrites,
- unrelated cleanup,
- qmake files,
- `.claude`,
- old Tree-sitter symbol parser,
- regex relationship analysis,
- long-lived perflog.

## Validation Policy

For code/test changes:

- batch related production-code changes first,
- build the affected target when compile risk is meaningful or the block is done,
- run the focused CTest after the coherent block is complete,
- run full `ctest --output-on-failure` when shared behavior or service reports are touched,
- run `git diff --check`,
- run changed/new source/doc ASCII and trailing-whitespace scans,
- run the forbidden-file guard.

For docs-only cleanup:

- `git diff --check`
- changed doc ASCII and trailing-whitespace scans
- forbidden-file guard

## Commit Policy

- After a medium-sized coherent architecture block passes the agreed build/test/hygiene gates, create a local commit automatically.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.

## Handoff Policy

Keep this file compact. At handoff, record only:

- current diff shape,
- latest completed work,
- latest validation,
- next best step,
- any rule changes.
