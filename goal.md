# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It is not trying to become a full IDE in one jump.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably,
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, and relationships,
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

AnalysisCoordinator
  Owns scheduler/progress/workspace/symbol signal routing and active-editor refresh policy.

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
- Analysis triggers belong in AnalysisScheduler.
- UI panels should render service/model output, not derive semantic policy from widgets.
- MainWindow should compose and coordinate windows, not own analysis event routing, panel rendering, or editor workflow policy.
- MyCodeEditor should provide editor UI and live syntax behavior, not project semantic decisions.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.
- Use Qt 6 + CMake + Ninja only.

## Completed Foundation

Already present:

- ProjectModel and DocumentModel minimal boundaries
- AnalysisScheduler, AnalysisProgressCoordinator, AnalysisCoordinator, FileCommandCoordinator, NavigationCommandCoordinator, ModeCommandCoordinator, and SemanticRuntimeCoordinator
- SemanticIndex facade and SemanticIndexSnapshot helpers
- DefinitionService, CompletionService, RelationshipService, HierarchyService, ReferenceService, DiagnosticService, and SearchService
- Problems, References, and Relationships panels backed by service reports and owned by focused coordinators
- Shared semantic panel display helpers for relationship labels, count labels, and tree expansion state
- Navigation pane UI ownership split into a focused coordinator
- Navigation command routing, tab activation/opening, and editor cursor placement split into a focused coordinator
- Editor setup, editor-originated commands, active-tab refresh coordination, and alternate-mode propagation split into a focused coordinator
- Scheduler/progress/workspace/symbol analysis event routing split into a focused coordinator
- Editor-originated analysis commands and relationship-work cancellation split into a focused coordinator
- File/edit/workspace commands and close-event unsaved-change confirmation split into a focused coordinator
- Mode key event routing and navigation-pane toggle routing split into a focused coordinator
- Semantic runtime lifetimes and SemanticIndex/CompletionManager dependency injection split into a focused coordinator
- Semantic panel provider/navigation/status wiring, refresh commands, and active-editor Problems refresh policy split into a focused coordinator
- CompletionManager reads routed through DefinitionService, RelationshipService, and SearchService for key semantic paths
- GUI smoke, relationship fixture, completion, jump, Tree-sitter document, and large-file perf tests

Recent coverage is strongest around:

- real multi-file relationship fixtures for instantiation, calls, reads, writes, clocks/resets, diagnostics, references, hierarchy traversal, and workspace/current-file filtering,
- snapshot-backed DefinitionService, CompletionService, SearchService, DiagnosticService, RelationshipService, ReferenceService, and HierarchyService behavior,
- scheduler-owned lifecycle behavior including relationship refresh, project-close cleanup, diagnostics refresh, document-close cleanup, and remaining open-document reanalysis,
- editor/MainWindow decoupling through signals, callbacks, include resolution, file opening, navigation command routing, active-tab refresh, mode propagation, mode command routing, analysis event routing, file command routing, and semantic runtime setup,
- GUI smoke coverage for include resolution, panel behavior, navigation, and workspace close/reopen paths.

## Remaining Gaps

- Continue reducing live `sym_list` consumers.
- Continue making services snapshot-backed where possible.
- Continue thinning MainWindow coordination policy.
- Continue moving editor/completion semantic decisions behind services and facades.
- Continue tightening real fixture coverage when production changes need it.

## Definition Of Done

The foundation is healthy when:

- new feature reads mainly use ProjectModel, DocumentModel, AnalysisScheduler, SemanticIndex, and Query Services,
- MainWindow is mostly UI composition and high-level callback wiring,
- MyCodeEditor is mostly editor UI plus Tree-sitter live syntax,
- UI/services can read stable snapshot-backed semantic data,
- all CTest targets pass,
- real multi-file fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, diagnostics, and relationship browsing,
- handoff docs are short enough for a new session to read without wasting context.

## Current Goal Step

Continue with one medium-sized, coherent, verifiable architecture block:

- move a related group of UI/editor/completion semantic reads or analysis policy checks behind SemanticIndex, Query Services, models, or AnalysisScheduler, or
- extract one complete MainWindow refresh/progress/coordination boundary into a focused coordinator or existing scheduler/model boundary, or
- thin one editor/completion workflow end-to-end without changing visible behavior.

Batch related production-code changes, then validate and update the compact handoff after the block is complete.
