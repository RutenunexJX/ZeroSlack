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
- SemanticIndex owns struct-variable type lookup, struct-member symbol reads, and
  module-context command symbol reads
- DefinitionService owns struct-member definition context resolution from editor line-prefix context and uses its injected SemanticIndex for the semantic lookup
- Problems / References / Relationships panels backed by service reports for sorting, grouping, filtering, and counts
- Real multi-file relationship fixture coverage for instantiation, calls, reads, writes, diagnostics filtering, workspace/current-file filtering, and timing relationships

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- MainWindow still owns UI rendering and some coordination logic.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Current Handoff Work

Latest handoff work:

- `test_sv/relationship_test.cpp`
  - Adds a real fixture assertion that the grouped `ReferenceReport` row for
    the `rel_top` -> `rel_stage` instantiation carries the concrete referencing
    and referenced symbols.
  - Adds a real fixture assertion that the grouped incoming `RelationshipReport`
    row for `rel_stage` carries the incoming direction and concrete `rel_top`
    peer symbol.
  - Adds a diagnostic report assertion that the current-file filtered group
    carries only diagnostics from the requested file.
  - Adds a hierarchy report assertion that the child node row keeps the
    `rel_top` parent, `rel_stage` child, direction, and relationship type.
  - Adds a snapshot-backed `SearchService` assertion for real module symbols
    through `SemanticIndexSnapshot`.
  - Adds a snapshot-backed `RelationshipService` assertion for real
    instantiation relationships and enriched endpoint symbols.
  - Adds a snapshot-backed `ReferenceService` assertion for real instantiation
    references and converted referencing/referenced symbols.
  - Adds a snapshot-backed `HierarchyService` assertion for real instantiation
    hierarchy traversal.
- `analysisscheduler.cpp`, `mainwindow.cpp`, `mainwindow.h`
  - Moves post-apply relationship data refresh scheduling into
    `AnalysisScheduler`.
  - Routes completion relationship data refresh through the scheduler refresh
    request path in `MainWindow`.
- `readme.md`, `plan.md`, `goal.md`
  - Updated compact handoff state.

Expected real diff before commit: focused `relationship_test` grouped report
assertions for diagnostics, relationships, references, and hierarchy;
snapshot-backed SearchService, RelationshipService, ReferenceService, and
HierarchyService coverage; scheduler-owned relationship refresh requests;
MainWindow duplicate completion refresh removal; and handoff docs.

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
