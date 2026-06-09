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
- SemanticIndex facade and SemanticIndexSnapshot storage
- Query services for definition, completion, relationship, hierarchy, reference, diagnostics, and search
- Problems, References, and Relationships panels backed by service reports
- Problems, References, Relationships, Navigation pane, and editor/tab/mode workflows extracted from `MainWindow` into focused coordinators
- Real multi-file relationship fixture coverage for instantiation, calls, reads, writes, diagnostics filtering, workspace/current-file filtering, hierarchy, references, and timing relationships

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- `MainWindow` still has more high-level UI wiring than desired.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Latest Completed Block

The latest block extracted a broad but coherent `MainWindow` coordination layer:

- `ProblemsPanelCoordinator`
- `ReferencesPanelCoordinator`
- `RelationshipsPanelCoordinator`
- `SemanticPanelUtils`
- `NavigationPaneCoordinator`
- `EditorCoordinator`
- `TabManager::editorCount()` for coordinator-owned editor enumeration
- GUI smoke coverage adapted to validate the visible workflows through these boundaries

The block was validated with focused GUI smoke build/test, full build, full CTest, diff hygiene, ASCII/trailing scans, and forbidden-file guard.

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

## Handoff Policy

Keep this file compact. At handoff, record only:

- current diff shape,
- latest completed work,
- latest validation,
- next best step,
- any rule changes.
