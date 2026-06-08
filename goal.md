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
- SemanticIndex facade
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
- GUI smoke, relationship fixture, completion, jump, Tree-sitter document, and large-file perf tests

Recent test coverage includes real fixture assertions for:

- cross-file instantiation,
- task calls,
- condition reads,
- assignment writes,
- current-file and workspace-file reference filtering,
- diagnostic file filtering,
- incoming relationship browsing,
- incoming timing relationships (`CLOCKS` / `RESETS`),
- incoming timing reference report shape.

## Remaining Gaps

- Continue reducing live `sym_list` consumers.
- Continue making services snapshot-backed where possible.
- Continue thinning MainWindow analysis/progress/refresh policy.
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

Continue with one small, verifiable increment:

- add another focused real fixture assertion, or
- extract one clear MainWindow refresh/progress policy boundary into AnalysisScheduler, or
- move one UI/editor semantic read behind SemanticIndex or a Query Service.

Stop after a coherent validated increment and update the compact handoff.
