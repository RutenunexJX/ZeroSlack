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
- CompletionService reads normal module/global completions, command-mode
  module/global completions, and struct-member completions through its
  configured SemanticIndex, and owns struct-member context parsing directly.
- Problems / References / Relationships panels backed by service reports for sorting, grouping, filtering, and counts
- Real multi-file relationship fixture coverage for instantiation, calls, reads, writes, diagnostics filtering, workspace/current-file filtering, and timing relationships

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- MainWindow still owns UI rendering and some coordination logic.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Current Handoff Work

Latest handoff work:

- `hierarchyservice.h`, `hierarchyservice.cpp`
  - Adds `HierarchyRootDirectionGroup` and populates
    `HierarchyReport::rootDirectionGroups`.
  - Moves root-level hierarchy direction grouping/count data into the query
    service report.
- `mainwindow.cpp`
  - Relationship tree rendering consumes `HierarchyReport` root direction
    groups instead of deriving those counts from string-keyed UI items.
- `completionmanager.cpp`
  - `findSemanticDefinitions` now uses `DefinitionService` instead of reading
    definitions directly from `SemanticIndex`.
  - Relationship completion and scoring helpers now use name-based
    `RelationshipService` queries instead of pre-resolving symbol IDs in
    `CompletionManager`.
  - `getSemanticSymbolsByType` now uses `SearchService` instead of reading
    typed symbols directly from `SemanticIndex`.
  - `getSemanticSymbolsForFile` now uses `SearchService` instead of reading
    file-scoped symbols directly from `SemanticIndex`.
  - `getAllSemanticSymbols` now uses `SearchService` instead of reading all
    symbols directly from `SemanticIndex`.
- `test_sv/relationship_test.cpp`
  - Adds focused snapshot-backed `RelationshipReport` assertions for outgoing
    and incoming instantiation direction/type groups.
  - Verifies grouped direction, relationship type, count, and peer symbol
    identity for the Relationships panel report shape.
  - Adds focused snapshot-backed `ReferenceReport` assertions for file/type
    counts and file group metadata.
  - Verifies file key, display name, and type bucket count for the References
    panel report shape.
  - Adds minimal regression coverage for snapshot-backed `HierarchyReport`
    root direction groups and node links.
- `readme.md`, `plan.md`, `goal.md`
  - Updated compact handoff state.

Expected real diff before commit: `HierarchyService` root direction report
groups, `MainWindow` relationship tree consumption of those groups, focused
`CompletionManager` definition reads through `DefinitionService`,
`CompletionManager` relationship helper queries through name-based
`RelationshipService`, `CompletionManager` typed symbol reads through
`SearchService`, `CompletionManager` file-scoped symbol reads through
`SearchService`, `CompletionManager` all-symbol reads through `SearchService`,
focused report regressions, and handoff docs.

## Next Small Increments

Choose one:

1. Move another UI/editor semantic read behind SemanticIndex or a Query Service.
2. Move another small MainWindow coordination responsibility into a focused coordinator.
3. Thin another `MainWindow` refresh path without changing UI behavior.
4. Extract one remaining clean MainWindow progress or refresh policy boundary
   into AnalysisScheduler.

Prioritize production-code architecture progress. Do not use pure
`relationship_test.cpp` assertion expansion as the main increment. Add tests
only as the smallest regression protection directly tied to a production code
change.

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
