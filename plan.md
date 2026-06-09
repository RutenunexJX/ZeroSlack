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

- `analysisscheduler.cpp`, `analysisscheduler.h`
  - Adds a guarded project semantic-state clear path shared by `projectClosed`
    and closed `projectChanged` notifications.
  - Project close now cancels workspace relationship analysis, clears the
    `SemanticIndex` snapshot, clears relationship-engine data, emits
    relationship refresh signals once, and schedules diagnostics refresh.
- `test_sv/relationship_test.cpp`
  - Adds scheduler lifecycle coverage that project close clears stale
    relationship data and emits relationship invalidation, relationship
    refresh, and diagnostics refresh requests.
- `completionservice.cpp`, `completionservice.h`
  - Keeps command-mode module, interface, package, and define completions in
    the global scope even when the editor provides a current module context.
  - Keeps local command completion types scoped to the current module.
- `test_sv/completion_test.cpp`
  - Adds snapshot-backed assertions for module, interface, package, and define
    command completion names and returned symbol identity while a module
    context is present.
- `test_sv/jump_test.cpp`
  - Adds snapshot-backed `DefinitionService` coverage for cross-file module
    definition resolution and local-file precedence for same-name module
    definitions.
  - Adds snapshot-backed `DefinitionService` coverage for cross-file interface
    and package definition resolution.
  - Adds wrapper coverage for `canResolveDefinition` and `findDefinitions`
    using the same snapshot-backed queries.
- `test_sv/relationship_test.cpp`
  - Adds snapshot-backed `SearchService` coverage for `hasMatches` positive
    and negative behavior, file-scoped module filtering, exact matching,
    case sensitivity, scoring, max-result limiting, and empty-text typed/file
    filtering.
  - Adds snapshot-backed `DiagnosticService` `hasDiagnostics` coverage for
    severity and workspace-file filters.
  - Adds snapshot-backed `RelationshipService`, `ReferenceService`, and
    `HierarchyService` report/wrapper coverage for counts and grouped symbol
    identity.
  - Adds snapshot-backed `ReferenceService` current-file/workspace filters and
    `HierarchyService` parent report coverage.
  - Adds name-based snapshot query resolution coverage for relationship,
    reference, and hierarchy service report paths.
- `readme.md`, `plan.md`, `goal.md`
  - Updated compact handoff state.

Expected real diff before commit: `CompletionService` always-global command
scope filtering, focused `completion_test`, `jump_test`, and
`relationship_test` snapshot coverage, and handoff docs.

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
