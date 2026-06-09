# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It is not trying to become a full IDE in one jump.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, and relationships
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, and relationship browsing
- remain responsive on large files and multi-file workspaces
- keep semantic behavior testable through real fixtures

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

Coordinators
  Own UI/editor command routing, progress policy, navigation commands, panel refresh, semantic runtime setup, and other workflow glue.

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

Already present at a high level:

- ProjectModel and DocumentModel minimal boundaries
- AnalysisScheduler and analysis/progress/command coordinators
- File, navigation, mode, semantic runtime, semantic panel, and editor workflow coordinators
- SemanticIndex facade and SemanticIndexSnapshot helpers
- Definition, completion, smart/all-symbol, typed symbol, and keyword/abbreviation scoring, context-aware completion, typed symbol and `SymbolInfo` completion, scope/current-module, relationship-driven completion, relationship, hierarchy, reference, diagnostics, and search services
- CompletionManager compatibility facade over CompletionService, without manager-owned result caches or cache lifecycle hooks
- Problems, References, Relationships, and Navigation UI backed by coordinators/services
- GUI smoke, relationship fixture, completion, jump, Tree-sitter document, and large-file perf tests

Keep detailed completion history in Git and `readme.md` handoff snapshots, not here.

## Remaining Gaps

- Continue reducing live `sym_list` consumers.
- Continue making services snapshot-backed where possible.
- Continue thinning remaining MainWindow dependency assembly and coordination policy.
- Continue moving remaining editor/completion semantic decisions behind services and facades.
- Continue tightening real fixture coverage when production changes need it.

## Documentation Quality Goal

The handoff docs should stay useful under repeated automatic updates.

- `goal.md` should not become a changelog.
- Completed-work detail belongs in commits and current handoff snapshots.
- Replace stale specifics with current architecture summaries.
- Delete low-reference notes when they no longer guide product or architecture decisions.

## Definition Of Done

The foundation is healthy when:

- new feature reads mainly use ProjectModel, DocumentModel, AnalysisScheduler, SemanticIndex, and Query Services
- MainWindow is mostly UI composition and high-level callback wiring
- MyCodeEditor is mostly editor UI plus Tree-sitter live syntax
- UI/services can read stable snapshot-backed semantic data
- all CTest targets pass
- real multi-file fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, diagnostics, and relationship browsing
- handoff docs are short enough for a new session to read without wasting context

## Current Goal Step

Continue with one medium-sized, coherent, verifiable architecture block:

- move a related group of UI/editor/completion semantic reads or analysis policy checks behind SemanticIndex, Query Services, models, or AnalysisScheduler, or
- extract one complete MainWindow refresh/progress/coordination boundary into a focused coordinator or existing scheduler/model boundary, or
- thin one editor/completion workflow end-to-end without changing visible behavior.

Batch related production-code changes, then validate and update the compact handoff after the block is complete.
