# ZeroSlack Development Plan

This file is the short execution plan. It intentionally avoids long session history. Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals.

## Current Direction

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
- AnalysisScheduler for major analysis triggers, relationship work, diagnostics refresh requests, and relationship data refresh requests
- AnalysisProgressCoordinator for workspace analysis progress dialog policy and cancel state
- SemanticIndex facade over current semantic data
- SemanticIndexSnapshot for symbols, relationships, diagnostics, cached file content, and scope names
- Query services for definition, completion, relationship, hierarchy, reference, diagnostics, and search
- DefinitionService owns struct-member definition context resolution from editor line-prefix context
- Problems / References / Relationships panels backed by service reports for sorting, grouping, filtering, and counts
- Real multi-file relationship fixture coverage for instantiation, calls, reads, writes, diagnostics filtering, workspace/current-file filtering, and timing relationships

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- MainWindow still owns UI rendering and some coordination logic.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Current Handoff Work

Latest handoff work:

- `analysisprogresscoordinator.cpp` / `analysisprogresscoordinator.h`
  - Adds a focused coordinator for workspace analysis progress dialog lifetime,
    cancel state, progress status messages, progress log text, and errors.
- `mainwindow.cpp` / `mainwindow.h`
  - Removes direct `RelationshipProgressDialog` ownership, progress helper
    methods, progress signal formatting, and the local cancel flag.
- `CMakeLists.txt`
  - Adds the coordinator to `zeroslack_core`.
- `definitionservice.cpp` / `definitionservice.h`
  - Lets DefinitionService resolve `var.member` line-prefix context into the
    struct type before selecting a definition.
- `mycodeeditor.cpp` / `mycodeeditor.h`
  - Removes the direct CompletionManager dependency from definition jumps and
    removes unused semantic wrapper helpers.
- `test_sv/jump_test.cpp`
  - Verifies DefinitionService chooses the right same-name struct member from
    `pixel.red` context.
- `readme.md`
  - Compacted English / ASCII handoff.
- `plan.md`
  - Compacted English / ASCII plan.
- `goal.md`
  - Compacted English / ASCII architecture goal.

Expected real diff before commit: progress coordinator extraction,
`MainWindow` progress-dialog cleanup, definition context service move,
`jump_test`, CMake, and handoff docs.

## Next Small Increments

Choose one:

1. Move another UI/editor semantic read behind SemanticIndex or a Query Service.
2. Add one focused snapshot-backed service regression.
3. Improve report/service tests for edge filters without changing UI behavior.
4. Move another small MainWindow coordination responsibility into a focused coordinator.
5. Thin another `MainWindow` refresh path without changing UI behavior.

Avoid:

- broad rewrites,
- unrelated cleanup,
- qmake files,
- `.claude`,
- old Tree-sitter symbol parser,
- regex relationship analysis,
- long-lived perflog.

## Validation Policy

For docs-only cleanup:

- `git diff --check`
- source/test/UI/CMake non-ASCII scan
- forbidden-file guard

For code/test changes:

- build the affected target,
- run the focused CTest,
- run full `ctest --output-on-failure` when shared behavior or service reports are touched,
- run `git diff --check`,
- run the non-ASCII scan,
- run the forbidden-file guard.

## Handoff Policy

Do not append every session forever. Keep this file current and compact.

Stop and hand off when:

- one coherent increment is validated,
- context is getting large,
- the next step is broad or risky,
- validation is blocked,
- or the user asks to stop.

At handoff, record only:

- current diff,
- latest completed work,
- latest validation,
- next best step,
- any rule changes.
