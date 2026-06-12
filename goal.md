# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It is not trying to become a full IDE in one jump.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, and relationships
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, and relationship browsing
- remain responsive on large files and multi-file workspaces
- keep semantic behavior testable through real fixtures

## Target Architecture

```text
ProjectModel
  Owns workspace inputs: root, file list, include dirs, defines, top, ignored paths.

DocumentModel
  Owns open document state: file identity, text snapshot/version, saved text version, dirty/saved state, cursor, live module, registry-backed text queries, and eventually Tree-sitter document ownership.

AnalysisScheduler
  Owns analysis timing: scheduled semantic runs, relationship runs, cancellation, debounce, refresh requests, lifecycle, and analysis event routing.

SemanticIndex
  Owns semantic facts: symbols, definitions, relationships, references, diagnostics, cached content.

Query Services
  Own feature-specific reads and panel query shaping: definition, completion, relationship, hierarchy, reference, diagnostics, search.

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
- Panel filters and UI read policy should become service options, not scattered widget-derived query logic.
- Analysis triggers belong in AnalysisScheduler.
- UI panels should render service/model output, not derive semantic policy from widgets.
- MainWindow should compose and coordinate windows, not own analysis event routing, panel rendering, or editor workflow policy.
- MyCodeEditor should provide editor UI, stable editor adapters, event flow, and live syntax behavior while document state and project semantic decisions stay in models, services, scheduler, runtime, and coordinators.
- New test code should prefer stable production-facing APIs; existing private-access test debt should shrink only when replacement APIs are real production boundaries.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.
- Use Qt 6 + CMake + Ninja only.
- Verification may be batched across independent architecture blocks, but commits remain coherent by block.

## Definition Of Done

The foundation is healthy when:

- new feature reads mainly use ProjectModel, DocumentModel, AnalysisScheduler, SemanticIndex, and Query Services
- MainWindow is mostly UI composition and high-level callback wiring
- MyCodeEditor is mostly editor UI plus Tree-sitter live syntax
- private-access test debt is isolated and shrinking as stable production APIs appear
- UI/services can read stable snapshot-backed semantic data
- all CTest targets pass
- real multi-file fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, diagnostics, and relationship browsing
- verification may be batched across independent blocks, but product quality and fixture coverage do not shrink
- batched work passes full Ninja, full `ctest --output-on-failure`, hygiene scans, and forbidden-file guard before block commits are created
- local commits stay separated by coherent architecture block
- handoff docs are short enough for a new session to read without wasting context
