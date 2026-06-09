# ZeroSlack Product And Architecture Goal

ZeroSlack is currently a reliable SystemVerilog workspace browser and lightweight editor. It is not trying to become a full IDE in one jump.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably,
- understand project structure such as modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, and relationships,
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, and relationship browsing,
- remain responsive on large files and multi-file workspaces,
- keep semantic behavior testable through real fixtures.

Product priority:

1. Read the workspace.
2. Understand the workspace.
3. Navigate definitions and relationships.
4. Expand editing features gradually.

## Target Architecture

```text
ProjectModel
  Owns workspace inputs: root, file list, include dirs, defines, top, ignored paths.

DocumentModel
  Owns open document state: text version, dirty state, cursor, live module, Tree-sitter document.

AnalysisScheduler
  Owns analysis timing: Slang runs, relationship runs, cancellation, debounce, refresh requests, lifecycle.

AnalysisProgressCoordinator
  Owns workspace analysis progress dialog state, progress text, errors, and cancel state.

SemanticIndex
  Owns semantic facts: symbols, definitions, relationships, references, diagnostics, cached content.

Query Services
  Own feature-specific reads: definition, completion, relationship, hierarchy, reference, diagnostics, search.

UI Layer
  Renders service/model output. It should not scan files directly or trigger Slang directly.
```

Tree-sitter and Slang split:

- Tree-sitter: instant, tolerant, low-latency editing experience; syntax highlighting; incremental parse trees; live scope.
- Slang: real semantic facts; symbols; types; definitions; diagnostics; relationships; project-level truth.

## Architecture Principles

- New semantic reads should go through SemanticIndex or Query Services.
- Short term: SemanticIndex may wrap live `sym_list`.
- Long term: UI/services should read snapshot-backed data.
- Analysis triggers belong in AnalysisScheduler.
- MainWindow should coordinate windows, not own analysis policy.
- MyCodeEditor should provide editor UI and live syntax behavior, not project semantic decisions.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.
- Use Qt 6 + CMake + Ninja only.

## Completed Foundation

Already present:

- ProjectModel minimal boundary
- DocumentModel minimal boundary
- AnalysisScheduler for major analysis and refresh timing
- AnalysisScheduler applies relationship analysis results, publishes enriched snapshots, reports workspace relationship progress totals, and debounces diagnostics refresh requests
- AnalysisProgressCoordinator for workspace analysis progress UI policy and cancel state
- SemanticIndex facade, including struct-variable type lookup, struct-member
  reads, and module-context command symbol reads
- SemanticIndexSnapshot production/switching helpers
- DefinitionService
- CompletionService
- RelationshipService
- HierarchyService
- ReferenceService
- DiagnosticService
- SearchService
- Problems panel backed by DiagnosticService reports
- References panel backed by ReferenceService reports
- Relationships Direct and Tree views backed by RelationshipService / HierarchyService reports
- CompletionService reads normal module/global completions, command-mode
  module/global completions, and struct-member completions through its
  configured SemanticIndex, and owns struct-member context parsing directly.
- GUI smoke, relationship fixture, completion, jump, Tree-sitter document, and large-file perf tests
- DefinitionService resolves struct-member definition context from editor line-prefix context through its injected SemanticIndex

Recent test coverage is strongest around:

- real multi-file relationship fixtures for instantiation, calls, reads,
  writes, clocks/resets, diagnostics, references, hierarchy traversal, and
  workspace/current-file filtering,
- snapshot-backed DefinitionService, CompletionService, SearchService,
  DiagnosticService, RelationshipService, ReferenceService, and
  HierarchyService behavior,
- CompletionManager reads routed through DefinitionService,
  RelationshipService, and SearchService instead of direct SemanticIndex
  queries,
- scheduler-owned lifecycle behavior, including relationship refresh requests,
  project-close cleanup, diagnostics refresh, document-close cleanup, and
  remaining open-document reanalysis,
- editor/MainWindow decoupling, including signal/callback driven relationship
  analysis, alternate-mode file commands, include resolution, and file opening,
- GUI smoke coverage for include resolution, panel behavior, navigation, and
  workspace close/reopen paths.

## Remaining Gaps

- Continue reducing live `sym_list` consumers.
- Continue making services snapshot-backed where possible.
- Continue thinning MainWindow analysis/refresh/coordination policy.
- Continue tightening Problems / References / Relationships real fixture coverage.
- Continue moving editor/completion semantic decisions behind services and facades.

## Definition Of Done

The foundation is healthy when:

- new feature reads mainly use ProjectModel, DocumentModel, AnalysisScheduler, SemanticIndex, and Query Services,
- MainWindow is mostly UI coordination,
- MyCodeEditor is mostly editor UI plus Tree-sitter live syntax,
- UI/services can read stable snapshot-backed semantic data,
- all CTest targets pass,
- real multi-file fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, diagnostics, and relationship browsing,
- handoff docs are short enough for a new session to read without wasting context.

## Current Goal Step

Continue with one medium-sized, coherent, verifiable architecture block:

- move a related group of UI/editor/completion semantic reads or analysis
  policy checks behind SemanticIndex, Query Services, models, or
  AnalysisScheduler, or
- extract one complete MainWindow refresh/progress/coordination boundary into a
  focused coordinator or existing scheduler/model boundary, or
- thin one UI panel or editor workflow end-to-end without changing visible
  behavior.

Batch related production-code changes, then validate and update the compact
handoff after the block is complete.
