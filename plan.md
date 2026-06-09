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
- `mycodeeditor.cpp`
  - No longer includes or casts to `MainWindow`, `TabManager`,
    `WorkspaceManager`, `ModeManager`, `SymbolAnalyzer`, or
    `NavigationManager`.
  - Relationship-analysis debounce emits `relationshipAnalysisRequested`, and
    `MainWindow` connects that signal to the scheduler-backed request path.
  - Alternate-mode state and file commands now use `MainWindow`-supplied state
    and editor signals instead of direct manager reads or `TabManager` calls.
  - Include fallback resolution and file opening now use callbacks installed by
    `MainWindow`.
- `mainwindow.h`
  - No longer includes `mycodeeditor.h` or grants `friend class MyCodeEditor`;
    editor-specific use is kept in `mainwindow.cpp`.
- `mainwindow.cpp`
  - Centralizes editor callback/signal setup in `configureEditor()`.
- `workspacemanager.cpp`, `workspacemanager.h`
  - Own current-file-relative and workspace include fallback through
    `resolveIncludePath()`.
- `test_sv/gui_smoke_test.cpp`
  - Adds focused regression assertions for workspace include basename resolution
    and current-file-relative include resolution.
- `analysisscheduler.cpp`, `analysisscheduler.h`
  - Own document-close semantic cleanup by cancelling pending open-file
    analysis, dropping cached relationship content, reanalyzing remaining open
    documents, and invalidating closed-file relationships.
- `symbolanalyzer.cpp`, `symbolanalyzer.h`
  - Replace the `TabManager`-based open-tabs analysis entrypoint with
    `analyzeOpenDocuments()` over explicit file/content inputs.
- `test_sv/relationship_test.cpp`
  - Adds a focused regression that document close reanalysis requests only the
    remaining open document content through `AnalysisScheduler`.
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

Latest committed architecture work includes `HierarchyService` root direction
report groups, `CompletionManager` reads routed through Definition,
Relationship, and Search services, `MyCodeEditor` decoupled from
`MainWindow`/manager classes, include fallback moved to
`WorkspaceManager::resolveIncludePath`, document-close semantic cleanup moved
into `AnalysisScheduler`, `SymbolAnalyzer` open-document analysis decoupled
from `TabManager`, focused regressions, and handoff docs.

## Next Architecture Blocks

Choose one:

1. Move a related group of UI/editor/completion semantic reads or analysis
   policy checks behind `SemanticIndex`, Query Services, models, or
   `AnalysisScheduler`.
2. Extract one complete MainWindow coordination responsibility into a focused
   coordinator or an existing scheduler/model boundary.
3. Thin one UI panel or editor workflow end-to-end without changing visible
   behavior.
4. Move a related completion/editor legacy state path behind existing service
   or model APIs.

Prioritize production-code architecture progress. Do not use pure
`relationship_test.cpp` assertion expansion as the main increment. Add tests
only as focused regression protection directly tied to a production code change.

Avoid:

- broad scattered rewrites,
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

- batch related production-code changes first,
- build the affected target when compile risk is meaningful or the block is done,
- run the focused CTest after the coherent block is complete,
- run full `ctest --output-on-failure` when shared behavior or service reports are touched,
- run `git diff --check`,
- run the non-ASCII scan,
- run the forbidden-file guard.

## Handoff Policy

Do not append every session forever. Keep this file current and compact.

Stop and hand off when:

- one coherent architecture block is validated,
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
