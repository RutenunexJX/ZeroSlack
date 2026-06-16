# ZeroSlack Product And Architecture Goal

ZeroSlack is a reliable SystemVerilog workspace browser and lightweight editor. It should grow through stable semantic boundaries instead of ad hoc UI logic.

## Product Goal

ZeroSlack should:

- open real SV workspaces reliably
- understand modules, packages, includes, typedefs, enums, structs, interfaces, instances, tasks, functions, ports, variables, diagnostics, references, and relationships
- provide trustworthy completion, jump-to-definition, navigation, diagnostics, references, relationship browsing, and RTL insight reports
- remain responsive on large files and multi-file workspaces
- keep semantic behavior testable through real fixtures

## Target Architecture

```text
ProjectModel
  Owns workspace inputs: root, file list, include dirs, defines, top, ignored paths.

DocumentModel
  Owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module, and registry-backed text queries.

AnalysisScheduler
  Owns analysis timing, debounce, cancellation, refresh requests, relationship work, lifecycle, and analysis event routing.

SemanticIndex / SemanticIndexSnapshot
  Own semantic facts: symbols, definitions, relationships, references, diagnostics, and cached content.

Query Services / Feature Services
  Own feature-specific reads, report shaping, and semantic policy.

Coordinators
  Own UI command routing, progress policy, navigation commands, panel refresh, semantic runtime setup, and workflow glue.

UI Layer
  Renders service/model output.
```

## Development Phases

### Phase A: Semantic State And Snapshot Publication

- Status: complete baseline.
- Make `SemanticIndexSnapshot` the single UI query truth.
- Define one publication and merge policy for workspace, open-document, dirty text, external file, and relationship analysis results.
- Add generation, version, and hash guards so stale async results cannot overwrite newer semantic state.
- Preserve open, dirty, and out-of-workspace documents when workspace results publish.
- Relationship analysis must not replace the visible semantic truth with an older or narrower snapshot.

### Phase B: Symbol Taxonomy And Source Role

- Status: complete baseline.
- Stabilize taxonomy/source-role helper rules first.
- Keep `sym_type_e` as legacy/raw collector kind during Phase B.
- Do not rush a broad `SymbolInfo` layout migration in Phase B.
- Move product semantic policy into `SymbolTaxonomy`, source-role helpers, Query Services, or feature services.

### Phase C: Semantic Metadata, Query Service, And UI Data Flow

- Status: complete baseline.
- Introduce stable semantic metadata after Phase B helper rules are stable.
- `SymbolInfo` may gain fields, or an equivalent metadata layer, for declaration kind, usage role, owner scope, visibility, source role, and raw collector kind.
- Keep `sym_type_e` available as raw/legacy collector kind during migration.
- Query Services and feature services should prefer stable semantic metadata over scattered raw enum checks.
- UI should consume reports/models, not semantic internals.
- CompletionService, DefinitionService, DiagnosticService, RelationshipService, and RTL Insights services should own query policy.
- Coordinators route state and refreshes; they should not encode semantic feature rules.
- Scheduler schedules and Analyzer executes analysis; neither owns product feature policy.

### Phase D: RTL Insights Expansion

- Current phase.
- Expand FSM graph, Signal Journey, Module Brief, Clock/Reset Domain Map, Semantic Diff, and code/document links through feature services.
- Use `test_sv/new` and similar real projects as first-class fixtures.
- Add focused tests at service level first, then UI smoke coverage where needed.
- Do not add feature-specific workarounds around semantic gaps in UI, scheduler, analyzer, or coordinator code.
- Keep feature reports explicit enough for UI render without understanding semantic internals.

## Architecture Rules

- New semantic features should flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must not run Slang directly or scan workspace files directly.
- Scheduler and analyzer code must not own feature policy.
- Slang owns semantic truth; Tree-sitter owns low-latency editor syntax and live scope.
- Real engineering fixtures, especially `test_sv/new`, should guide feature expansion.
- Performance probes should be targeted and removable; do not restore scattered long-lived perflog.

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Do not discard user changes.
- Do not touch user dirty RTL fixture files.
- Do not push unless explicitly asked.
- Keep local commits coherent and architecture-oriented.

## Definition Of Done

The foundation is healthy when:

- feature reads use stable models, snapshots, Query Services, or feature services
- stale workspace, open-document, and relationship analysis results cannot overwrite newer semantic snapshots
- product logic is stable only when snapshot publication, taxonomy/source-role helpers, stable semantic metadata, query services, and UI data flow have clear contracts and tests
- `sym_type_e` should remain a raw compatibility field unless and until all consumers have migrated safely
- RTL Insights expansion should not proceed broadly until stable semantic metadata and Query Service contracts are in place
- Phase D features are done only when service-level behavior, report shape, UI render path, and real fixture evidence are covered
- UI panels render reports/models without owning semantic policy
- scheduler, analyzer, project, document, and editor ownership boundaries stay clear
- real fixtures cover package/import, cross-file jump, instantiation, calls, assignments, reads, clocks/resets, FSMs, diagnostics, relationship browsing, signal journeys, module briefs, semantic diff, and large-file response
- verification may be batched, but commits remain coherent by block
- full Ninja and full `ctest --output-on-failure` pass after shared semantic state, scheduler, editor, or project boundary changes
- handoff docs are short, current, and easy to reread
