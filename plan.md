# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product and architecture goals. This file defines execution policy.

## Architecture Flow

- Start each turn from the current worktree and current tests.
- New semantic features should move through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- Keep UI panels as renderers of service or model output.
- Keep scheduler and analyzer code focused on timing, lifecycle, extraction, and publication; they must not own feature policy.
- Move policy into services only when there is a real production boundary.
- Expand RTL Insights only through stable semantic contracts and service-owned reports.
- Signal-centric visual features must build graph/report models in feature services before UI render, and must use precise relationship evidence for preview and navigation when available.

## Feature Expansion Guardrails

These constraints are mandatory. Do not merge, commit, or continue a new
feature direction that violates them.

- New semantic or RTL behavior must flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must render service/model output only; it must not run Slang, scan workspace files, own semantic policy, or patch around missing semantic data.
- `AnalysisScheduler` and `SymbolAnalyzer` must stay limited to timing, lifecycle, extraction, publication, and refresh routing; they must not own feature policy.
- New tests and fixtures must use semantic-native records/builders and must not reintroduce `sym_list`, `syminfo`, reverse adapters, legacy taxonomy headers, or legacy field/API names.
- New feature reports must expose stable keys, semantic records or typed report rows, owner/type/source-role metadata, navigation payloads, evidence, confidence, and not-found reasons where relevant.
- Real workspace fixtures, especially `test_sv/new`, must cover behavior that depends on package/import/include/interface/modport/cross-file semantics.
- `legacy_field_policy_guard.ctest` and the final repo-source zero target must keep passing after every feature change.
- Feature work must be rejected or redesigned if it requires qmake/pro/pri, `.claude`, SVLexer, the old Tree-sitter symbol parser, direct UI Slang execution, UI workspace scans, regex relationship analysis, long-lived scattered perflog, or legacy carrier/API resurrection.

## Phases

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

- Status: complete baseline.
- Expand FSM graph, Signal Journey, Module Brief, Clock/Reset Domain Map, Semantic Diff, and code/document links through feature services.
- Continue small RTL Insights improvements only when they do not require semantic data model changes.
- Defer features that need `SymbolInfo`, `sym_type_e`, identity, owner/type metadata, source role, relationship provenance, query result, completion item, or report-model changes to Phase E.
- Use `test_sv/new` and similar real projects as first-class fixtures.
- Add focused tests at service level first, then UI smoke coverage where needed.
- Do not add feature-specific workarounds in UI, scheduler, analyzer, or coordinator code.
- Keep reports explicit: rows, display names, navigation payloads, diagnostics, and evidence should be shaped before UI render.

### Phase E: Semantic Data Model Hardening

- Status: complete on this branch.
- Query services and RTL report models now carry stable identity, semantic metadata, source-role display, relationship provenance/confidence/evidence, and not-found reasons where useful.
- The local release gate has passed with full Ninja, full CTest, hygiene scans, and forbidden-file guards.

#### E1: Symbol Identity And Snapshot Handles

- Treat `symbolId` as a snapshot-local handle unless stable identity rules say otherwise.
- Add `SymbolStableKey` or an equivalent stable identity model.
- Let relationships, reports, and query results carry stable identity plus local handles.
- Define snapshot merge and rebind rules for relationships and reports.
- Keep this serial; it blocks deeper metadata migration.

#### E2: Semantic Metadata On Symbols

- Keep `sym_type_e` as raw/legacy collector kind.
- Add stable semantic metadata on `SymbolInfo` or an equivalent metadata layer.
- Cover declaration kind, usage role, owner scope, visibility, source role, and raw collector kind.
- Generate metadata consistently through `SymbolTaxonomy` or a dedicated mapper.
- Query Services and RTL feature services should prefer stable metadata over raw enum checks.
- Keep this serial and avoid unrelated feature work.

#### E3: Owner Scope And Type Reference Model

- Reduce `moduleScope` overload with owner name, kind, path, or equivalent owner metadata.
- Reduce `dataType` overload by separating raw type text, resolved type name, resolved type kind, and modport name where needed.
- Give package, interface, interface instance, modport, struct/enum/member, parameter/localparam, port, signal, and module instance explicit rules.
- Support `lr_genr_if.si` and `LR_GENR_IF.si` through type/interface/modport resolution, not scattered string guesses.
- Keep this serial.

#### E4: Source Role, Diagnostics, And Snapshot Publication Metadata

- Formalize source roles: design source, header, external header, generated, and unknown.
- Separate diagnostic ownership for Slang, include/source-role, ZeroSlack semantic, stale, current-file, and workspace diagnostics.
- Formalize snapshot metadata such as snapshot kind, project generation, document version, source hash, analysis kind, and created revision/time where useful.
- Prevent stale async diagnostics or semantic results from replacing newer visible truth.
- Batch focused tests and isolated report metadata only; publication rules stay serial.

#### E5: Relationship Provenance And Query Result Contracts

- Add relationship provenance, confidence, and evidence location/range where useful.
- Distinguish Slang-confirmed, inferred, lexical fallback, open-document, workspace, and feature-generated relationships.
- Extend query results with reason, evidence, candidate count, and confidence where useful.
- Definition, Completion, Reference, Hierarchy, Relationship, and RTL Insights services should expose consistent result contracts.
- Failed navigation/completion should explain missing include, missing import, unknown source role, unresolved interface instance type, missing modport, or no symbol.
- Split by service after E1-E4 contracts are stable.

#### E6: Completion Item And RTL Report Model Hardening

- Formalize completion item models with label, insert text, kind, detail, source, priority, replacement range, and semantic role.
- Harden RTL report models so UI receives display fields, evidence, confidence, not-found reasons, and stable identity references.
- Keep UI render-only.
- Module Brief, Signal Journey, Clock/Reset Domain Map, FSM Graph, Semantic Diff, and code/document links should share report conventions.
- Proceed in service-sized batches.

### Phase F0: Legacy Field Retirement

- Status: complete on this branch.
- This was a cleanup gate, not a feature phase.
- Direct product dependence on legacy `SymbolInfo` fields has been retired or fenced:
  - `symbolId`: keep as snapshot-local handle only; introduce or consistently name `SymbolLocalHandle` where local handles must remain visible.
  - `symbolType` / `sym_type_e`: keep as raw collector compatibility only; feature policy should use semantic metadata, declaration groups, search intent, and semantic roles.
  - `moduleScope`: split owner semantics into explicit owner kind/name/path/stable key or equivalent owner metadata.
  - `dataType`: split raw type text from resolved type name, resolved type kind, modport name, and type stable key where useful.
- Completion APIs now expose structured items with label, insert text, kind, detail, source, semantic role, and stable key metadata.
- Query services normalize UI string inputs into stable subject/context handles before feature policy where the current service contract needs it.
- Diagnostic contracts expose explicit source and ownership metadata.
- `sym_list` remains collector/storage compatibility; broad feature work should read `SemanticIndexSnapshot` and typed query/report models.
- Product-facing legacy field guards are in CTest and passed with the F0 baseline gate.

### Phase F1: Semantic Symbol Record Replacement

- Status: complete on this branch.
- Introduce or expand a real semantic symbol record.
- Let `SymbolInfo` degrade into a collector/adapter compatibility carrier.
- Product, service, report, and query layers should prefer semantic symbol records, stable identity, and semantic metadata.
- Owner, type, source-role, provenance, and not-found reason metadata should be carried by the semantic model, not by overloading `symbolType`, `moduleScope`, or `dataType`.
- Keep Qt 6 + CMake + Ninja only.
- New features must still flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.

### Phase F2: Stable Relationship And Index Migration

- Status: complete on this branch.
- Move relationship engine, snapshot, lookup, and query-service main paths from int `symbolId` / `sym_type_e` to stable identity plus semantic enum/model contracts.
- Keep compatibility APIs only as transition layers.
- Add or extend guards so product-facing code cannot continue using compatibility APIs.
- Relationship, reference, hierarchy, and RTL feature reports must expose stable keys, owner/type metadata, source role, provenance, and not-found reasons.
- Do not restore regex relationship analysis, the old Tree-sitter symbol parser, direct UI Slang execution, or UI workspace scans.

### Phase F3: Legacy Compatibility Removal

- Status: complete on this branch for product-facing compatibility payloads and guarded service/report paths.
- Delete or isolate legacy fields and APIs: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, `getSymbolById`, `findSymbolId`, int-id `getRelationships`, and similar compatibility surfaces.
- If an internal adapter is still required, keep it limited to the collector/import boundary and guarded.
- Clean tests and docs that assume legacy identity.
- After F3, run the release gate before broad RTL feature expansion.

### Release Gate After Phase F3

- Full Ninja must pass.
- Full CTest must pass.
- Legacy guard and product-facing API audit must pass.
- Docs, goal, and plan consistency checks must pass.
- Real RTL workspace smoke pass must be covered.
- Confirm qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, and long-lived scattered perflog probes have not returned.

### Phase G: Complete Legacy Field Deletion

- Status: complete on this branch.
- Goal: delete remaining legacy compatibility payloads and remove or isolate old fields instead of merely guarding product-facing use.
- G0 is complete: `SemanticRelationshipResult` no longer carries `fromSymbol` / `toSymbol`; relationship, reference, hierarchy, module brief, signal journey, clock/reset, completion, and module-context consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries `symbol`; module brief, signal journey, FSM graph, and definition consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, and `findDefinitions`; callers use records, stable keys, query services, or feature services.
- G3 is complete: `symbolId`, `symbolType`, `moduleScope`, `dataType`, `sym_type_e`, and raw collector compatibility are deleted from product-facing contracts or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The full Phase G release gate passed locally with full Ninja, full CTest, legacy guard, product-facing API audit, docs consistency review, and forbidden-file regression checks.

### Phase H: Semantic Core Slimdown

- Status: complete on this branch.
- Goal achieved: remaining legacy fields, APIs, redundant interfaces, and compatibility logic were removed from semantic core contracts or confined to the final guarded transition boundary.
- H0 is complete: `legacy_field_policy_guard.ctest` defines and enforces the Phase H allowlist for `sym_list::SymbolInfo`, `sym_list::sym_type_e`, `.symbolId`, `.symbolType`, `.moduleScope`, `.dataType`, int-id lookup APIs, raw collector query APIs, and retired completion compatibility APIs.
- H1 is complete: completion command, selected module, global, and scope query contracts use semantic-native `CompletionCommandKind`; raw collector mapping is isolated inside the semantic-query adapter and taxonomy boundary.
- H2 is complete for Phase H scope: snapshot/store public contracts and publication paths consume semantic records; later Phase I/J cleanup removed the first-party `SymbolInfo` carrier path entirely.
- H3 is complete: retired completion compatibility entry points such as `getCommandCompletionSymbols`, `getTypedCompletionSymbols`, `getGlobalSymbolInfosByType`, `getModuleInternalSymbolsByType`, and `getModuleContextSymbolsByType` are removed and guarded.
- H4 is complete: feature-service helpers use `SemanticSymbolRecord`, stable keys, local handles, and semantic metadata instead of exposed `SymbolInfo` contracts.
- H5 is complete: `syminfo` legacy query APIs and indexes were deleted or shrunk, including raw name/file/type query helpers.
- H6 is complete: redundant adapter helpers, conversion helpers, compatibility query paths, and duplicated display/filtering surfaces were removed or made private.
- The full Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.

### Release Gate After Phase H

- Full Ninja must pass.
- Full CTest must pass.
- `legacy_field_policy_guard` must prove legacy field/API use is absent outside the final allowlist.
- Static scans must confirm no product, service, report, completion, UI, scheduler, analyzer, or snapshot contract exposes `SymbolInfo`, `sym_type_e`, `.symbolId`, `.symbolType`, `.moduleScope`, `.dataType`, `getSymbolById`, `findSymbolId`, int-id relationships, or retired completion compatibility APIs.
- Docs, goal, and plan consistency checks must pass.
- Confirm qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, and long-lived scattered perflog probes have not returned.

### Phase I: Semantic Store Native / Collector Native

- Status: complete on this branch.
- Goal achieved: delete the former legacy collector/store body by moving Slang collection, semantic storage, scope rebuild, and relationship containment to semantic-native records or store entries.
- I0 is complete: the native store/collector migration contract is documented, `legacy_field_policy_guard.ctest` defines the Phase I zero-legacy target terms, and `ZEROSLACK_PHASE_I_ZERO_TARGET` provides the opt-in final scan for I5 without changing runtime behavior.
- I1 is complete: Slang collection exposes `collectSymbolRecords`, `SlangManager` exposes `extractSymbolRecords` / `extractWorkspaceSymbolRecords`, and `SymbolAnalyzer` plus tracked tests consume those record-native APIs directly. `collectSymbols`, `extractSymbols`, and `extractWorkspaceSymbols` are deleted.
- I2 is complete: `SemanticIndex` owns a semantic-native record/content store with file replacement, local handle assignment, stable-key indexes, native content state, native file coverage, native refresh, native/snapshot-only record queries, native content/state queries, and native-over-snapshot replacement semantics; snapshot publication reads records/content from `SemanticIndex` instead of rebuilding records from `sym_list`.
- I3 is complete: scope rebuild, module containment, relationship containment, and module lookup have moved away from `symbolId`, `symbolType`, `moduleScope`, and `dataType` to semantic metadata, owner/type records, stable keys, and explicit local handles. `SmartRelationshipBuilder`, `SymbolRelationshipEngine`, and `SemanticIndex` relationship rebuilds route containment through semantic records; legacy `sym_list` current-module lookup and module-scope auto-inference are deleted. `SymbolRelationshipEngine` no longer accepts or stores a `sym_list` database pointer, `SemanticIndex` relationship queries, completion relationship facts, and snapshot publication use the native attached relationship engine pointer, `sym_list` no longer stores, exposes, or forwards a relationship engine, and the legacy semantic-record-to-`sym_list` relationship/scope mirror is deleted.
- I4 is complete: legacy carrier and compatibility taxonomy surface was deleted or isolated; unused adapter files, legacy taxonomy includes, semantic-index legacy injection APIs, dead symbol database storage, legacy metadata helpers, legacy taxonomy overloads, legacy completion matching APIs, and legacy outline grouping APIs were removed or guarded.
- I5 is complete: the Phase I release gate passed for core source, and Phase J later expanded the final zero target to first-party repo source.
- I5 first block is complete: lowercase `rawCollectorKind` / `requestedRawCollectorKind` field and helper names were removed from source contracts in favor of `collectorKind` fields and `CollectorKind` enum/type names.
- I5 second block is complete: the retired `symboltaxonomylegacy.h` header was deleted and production `SymbolTaxonomy` no longer exposes or implements `sym_list::sym_type_e` / `sym_list::SymbolInfo` overloads; tracked tests temporarily used a fixture-local conversion helper until Phase J removed it.
- I5 third block is complete: root/core `syminfo*` carrier files moved into `test_sv` fixture scope, core CMake stopped building them, and Phase J later deleted the fixture carrier.
- I5 fourth block is complete: stale core `syminfo` includes and `SymbolInfo`-named helpers were removed, tracked tests moved to semantic-native helpers, GUI smoke fixtures synchronized native records before snapshot-based panel checks, and the final full Ninja/full CTest/guard/static-scan release gate passed.

### Phase I Goal Mode

- Track Phase I progress by subphase, not by the whole phase.
- Each subphase I0 through I5 starts at 100% remaining when it becomes current.
- At the end of every work turn, report the current subphase and the remaining percentage for that subphase.
- When a subphase is complete and verified, report it as 0% remaining, then make the next subphase current at 100% remaining.
- Do not mark all Phase I complete until I0-I5 are complete, docs are current, full Ninja and full CTest pass, and final legacy scans prove the zero-legacy target.

### Phase J: Test Fixture Native Cleanup

- Status: complete.
- Goal achieved: remove the fixture-only legacy carrier from tracked tests so legacy names are absent from first-party repo source except historical docs, migration notes, and guard definitions.
- J0 is complete: define the final repo-wide fixture cleanup allowlist in `legacy_field_policy_guard.ctest`; keep docs and guard patterns as explicit exceptions, and fail on new `sym_list::SymbolInfo`, `sym_list::sym_type_e`, `symbolId`, `symbolType`, `moduleScope`, `dataType`, `syminfo`, `getSymbolById`, and `findSymbolId` uses outside the current fixture cleanup scope.
- J1 is complete: introduce semantic-native test builders for symbol records, metadata, owners, type info, local handles, stable keys, and relationship endpoints so tests no longer need to hand-author `sym_list::SymbolInfo`.
- J2 is complete: migrate completion and jump tests from `sym_list` fixture data to native builders and query/report contracts.
- J3 is complete: migrate relationship and GUI smoke tests from `sym_list` fixture data to native builders, preserving current behavior coverage.
- J4 is complete: delete `test_sv/syminfo*` and the reverse fixture adapter once no tracked test includes or builds them.
- J5 is complete: upgrade the final zero target from core-source-only to repo-source except docs/guard definitions, then pass full Ninja, full CTest, normal guard, final zero-target guard, static scans, and `git diff --check`.

### Phase J Goal Mode

- Track Phase J progress by subphase, not by the whole phase.
- Each subphase J0 through J5 starts at 100% remaining when it becomes current.
- At the end of every work turn, report the current subphase and the remaining percentage for that subphase.
- When a subphase is complete and verified, report it as 0% remaining, then make the next subphase current at 100% remaining.
- Do not mark all Phase J complete until J0-J5 are complete, docs are current, full Ninja and full CTest pass, and final repo-source legacy scans prove the fixture carrier is gone.

### Post-J Focused Editor Workflow Fixes

- Status: complete and committed through `d7c72d5`.
- Scope: bug fixes and responsiveness hardening after Phase J, not a new migration phase.
- Completion popup sizing now derives item width from rendered text and clamps the popup to usable bounds.
- Completion trigger product logic now separates normal completion from command completion: normal completion can trigger from any identifier position after a two-character prefix, strong contexts such as `.`, backtick, `$`, and `::` trigger earlier, ordinary spaces close or keep completion closed, and command completion requires `;cmd` plus Space.
- The old single-letter plus Space command triggers such as `l `, `m `, and `r ` are removed; command input is temporary and replaced on accept, `;?` renders semantic command help, `;;?` renders template help, Esc cleans command input, and command mode is limited to command-safe positions outside comments and strings.
- Inline command mode is layered by intent: `;cmd` stays semantic completion, `;;cmd` expands snippets through `CodeTemplateService`, and `;:` is reserved for future expansion with no active completion or execution behavior.
- Ctrl+Space opens a centered ZeroSlack Global Control overlay from editor or non-editor focus; the current command surface is intentionally trimmed to `ow`, `fd`, and `fds`, with broader file/symbol/template/action search reserved for later expansion.
- Interface definition navigation now preserves Slang header/source locations instead of rewriting include-origin records to the current analysis file.
- Named instance port connections now emit semantic-native `InstPin` records; definition resolution uses cursor line/column to jump `.port` names to child module port declarations while preserving actual signal jumps in the parent module.
- Navigation responsiveness is addressed at the source: opening files from Navigation no longer rebuilds the file tree, hierarchy queries are keyed by file/filter input, and tree population pauses sorting/repaint churn instead of using delayed refresh timers.
- Verification passed with focused Ninja targets, `jump_test`, `completion_test`, `gui_smoke_test`, normal `legacy_field_policy_guard`, opt-in `ZEROSLACK_PHASE_J_ZERO_TARGET`, and `git diff --check`; the completion trigger update additionally passed `completion_test` with 299 checks and `gui_smoke_test` with 249 checks. The layered inline command and Global Control update passed `completion_test` with 311 checks and `gui_smoke_test` with 276 checks.

### Phase K: Follow-up Editor Structure Features

- Status: complete.
- Purpose achieved: delivered the three editor structure features as service/model-backed UI work without weakening the semantic, document, scheduler, or analyzer boundaries.
- K1 Design Hierarchy view is complete: Navigation exposes only Files and Design tabs, Design Top is selected from right-click menus, hierarchy nodes are module-only and render as `instance_name : module_type`, double-click and context-menu navigation use existing navigation paths, reports are cached by snapshot generation plus top, and Files view dimming uses normalized participating files.
- K2 Code Folding and Custom Folding is complete: folding ranges come from Tree-sitter syntax nodes, custom markers are parsed from Tree-sitter comment nodes without regex, gutter markers collapse/expand block-number ranges, collapsed regions show compact placeholders, selecting `fd` from Global Control enters Fold Region Mark Mode to insert custom markers as one undo block with visible mode status, live range preview, gutter start/end badges, and custom fold blocks keep a persistent background tint. Manual `// fold <name>` / `// endfold` pairs create blocks, marker damage silently removes them, and editing the fold name renames the block.
- K3 Fold Block Shelf is complete: selecting `fds` from Global Control enters Fold Shelf mode and opens the Fold Shelf dock with visible mode status and active shelf styling, custom fold blocks highlight on hover with a clear bottom boundary, fold blocks move or Ctrl-copy into the shelf through structured MIME data and a compact drag pixmap, shelf mode blocks accidental text input while drag mode is active, shelf items consume or Ctrl-copy back into editors at line boundaries, double-click preview is read-only, moved item deletion offers restore/delete/cancel, restore uses editor/tab edit APIs, and fold/shelf operations log to Activity/Output.
- `;:` is reserved and intentionally inactive; the active Global Control command set is currently `ow`, `fd`, and `fds`, and `ow` replaces the removed toolbar Open Workspace button.
- Workspace/UI polish is complete: the main toolbar buttons are removed, Editor Appearance/RTL Insights/Activity are hidden on first launch, opening or switching a workspace raises Activity/Output, interactively opened workspaces require aliases, multiple workspaces are shown as top alias tabs, and selected workspace/editor tabs use stronger color contrast.
- K1-K3 tests should prefer `test_sv/new` real SV files, using smaller fixtures only for interaction edges that real files do not cover.
- Minimum verification for each K subphase: focused target build, relevant service tests, `gui_smoke_test` for editor/UI paths, `git diff --check`, and guard scans when semantic/test source changes.

### Phase K Goal Mode

- Track Phase K progress by feature subphase.
- K1, K2, and K3 are each their own 100% unit.
- Current subphase: K3 Fold Block Shelf complete, 0% remaining.
- End every work turn by naming the current Phase K subphase and reporting that subphase's remaining percentage.
- Move from K1 to K2 only after Design Hierarchy behavior, tests, docs, and requested coherent local commit are complete.
- Move from K2 to K3 only after folding behavior, tests, docs, and requested coherent local commit are complete.
- Phase K completion verification passed with focused Ninja targets, `completion_test` with 315 checks, `gui_smoke_test` with 331 checks, `git diff --check`, normal `legacy_field_policy_guard`, and opt-in `ZEROSLACK_PHASE_J_ZERO_TARGET`. The latest workspace/UI/Fold/Global Control polish additionally passed focused `completion_test` with 321 checks and `gui_smoke_test` with 338 checks.

### Phase L: Regex Logic Native Cleanup

- Status: complete.
- Purpose achieved: first-party production regex logic has been removed from semantic/editor feature paths and replaced with deterministic SV token boundaries while preserving semantic, snapshot, and service data flow.
- L0 Regex Inventory and Guard Baseline is complete: current regex cleanup targets are inventoried, docs/guard definitions are the explicit exception class, normal guard prevents new first-party production regex API spread outside the cleanup allowlist, and optional `ZEROSLACK_PHASE_L_ZERO_TARGET` is available for the final L6 gate.
- L1 Utility Regex Replacement is complete: SV identifier validation, keyword word-boundary checks, module/endmodule scans, and whitespace normalization use shared deterministic token helpers instead of regex in low-risk utility paths.
- L2 Completion Context Native is complete: `completioncontexthelper.cpp` no longer uses regex parsing; struct member, enum, assignment, and instantiation context use deterministic token helpers with focused completion tests.
- L3 Module Range / Include / Import Native is complete: module range and scope-band helpers use deterministic token boundaries, and module-context include/import extraction no longer uses regex while preserving the `SemanticIndex` record path.
- L4 FSM Graph Native Extraction is complete: `fsmgraphservice.cpp` no longer uses raw-file regex transition parsing; case selectors, labels, if conditions, assignment targets, assigned state values, and ternary branch state checks use deterministic token helpers plus semantic state records.
- L5 Open Document Scheduling Cleanup is complete: keyword-regex structural-change checks are gone and the existing lightweight scheduling policy uses deterministic token-boundary matching.
- L6 Final Regex Zero Target is complete: guard checks reject regex APIs outside docs and guard definitions; focused tests, affected GUI smoke tests, full Ninja/full CTest, normal/final guards, static scans, and `git diff --check` passed.
- Do not remove or weaken guard-definition regex. Guard regex is allowed only inside guard definitions because it enforces the cleanup.

### Phase L Goal Mode

- Track Phase L progress by cleanup subphase.
- L0, L1, L2, L3, L4, L5, and L6 are each their own 100% unit.
- Current subphase: L6 Final Regex Zero Target complete, 0% remaining.
- End every work turn by naming the current Phase L subphase and reporting that subphase's remaining percentage.
- Move from one L subphase to the next only after implementation, verification, docs, and any requested coherent local commit for the current cleanup block are complete.
- Phase L completion is achieved: L0-L6 are complete, docs are current, first-party production regex logic is removed, docs/guard definitions are the only regex allowlist, focused tests and affected GUI smoke tests passed, full Ninja/full CTest passed, normal/final guards passed, and `git diff --check` is clean.

### Post-L: Signal Kernel Graph

- Status: implemented in the current work block.
- Scope: feature expansion after Phase L, not a new migration phase.
- `SignalKernelGraphService` builds a driver/kernel/consumer graph from `SignalJourneyService` output and precise relationship evidence ranges.
- The graph UI is render-only: it receives nodes, edges, module groups, stable keys, source-role/type metadata, evidence code links, and not-found state from services.
- Right-clicking a signal exposes `Show Signal Kernel Graph`; drivers render on the left, the selected signal renders as the center kernel, consumers render on the right, and cross-module groups render with module wrappers.
- Hover enlarges a node and shows local code preview with exact file/line/column evidence and caret marking when available. Ctrl+left-click rebases the graph to that node's stable key. Double-click navigates to evidence for input/output nodes or declaration for the kernel.
- Verification for this block: focused build targets `completion_test`, `relationship_test`, and `gui_smoke_test`; CTest for all three; `git diff --check`.

### Post-L: Inline Command And Declaration Templates

- Status: implemented in the current worktree.
- Scope: editor completion/template ergonomics after Phase L, not a new migration phase.
- Semantic `;cmd` remains symbol completion only. `;l`, `;w`, `;r`, `;p`, and `;lp` complete existing logic, wire, reg, parameter, and localparam symbols through `CompletionService`.
- Template `;;cmd` owns declaration generation. `;;l`, `;;w`, and `;;r` share the same deterministic dimension parser: dimensions before the name are packed, dimensions after the name are unpacked, `-s` adds `signed`, `:P_W` expands to `[P_W - 1:0]`, and explicit ranges such as `:PW+DW:0` are preserved.
- `;;p` and `;;lp` generate scalar parameters without forcing a value expression. Default scalar output is `parameter NAME = ;` or `localparam NAME = ;`, with the cursor placed after `=`.
- Parameter arrays use the same dimension ordering as signal templates. Unpacked dimensions generate an array assignment pattern skeleton, for example `;;p 8 test 8` becomes `parameter [7:0] test [7:0] = '{};`, with the cursor placed inside the braces.
- Command-local type suggestions are triggered by `-` in parameter templates. `;;p -` and `;;lp -` offer `int`, `integer`, `logic`, `bit`, `byte`, `shortint`, and `longint`; choosing a type keeps the command active as `;;p -logic ` or similar so the user can continue typing dimensions and the name.
- Editor bracket range helpers pair `[` to `[]`, expand bracket contents with Tab, and let Ctrl+left-click select the range body. Up/Down adjusts the left numeric bound, while Shift+Up/Down adjusts the right numeric bound.
- Implementation and tests must remain no-regex. Parsing is by deterministic token scans in `CodeTemplateService` and editor runtime helpers.
- Verification for this block: focused build target `completion_test`, CTest `completion_test`, changed-file regex scan, and `git diff --check`. GUI smoke should be rerun when range editing event flow changes.

### Post-L: Ghost Inline Values

- Status: implemented in the current worktree.
- Scope: passive editor overlays after Phase L, not a new migration phase.
- `GhostAnnotationService` owns the report model. It reads current document text plus `SemanticIndex` records and emits `GhostAnnotation` rows with kind, placement, text, line, and anchor position.
- The editor runtime only stores and paints ghost annotations. It must not derive semantic meaning or insert real document text.
- First supported scenarios: module instance formal port details, parameter/localparam literal values, parameter overrides inside `#(...)`, parameter/macro-derived signal widths, nonzero numeric ranges, unpacked array extents, enum values, indexed part-select widths, generate loop instance counts, and concatenation widths. Obvious literal-only widths such as `[7:0]` are intentionally suppressed.
- Numeric literal conversion is hover-only and shows all three common bases as `(D)... (B)... (H)...` for exact binary, decimal, or hex literals. Comments and strings are ignored, and the hover result is recomputed from current document text.
- Implementation and future extensions must remain no-regex. Parsing is by deterministic token scans plus semantic records.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run, focused build target `gui_smoke_test`, and direct `gui_smoke_test` run.

### Post-L: Formatter MVP

- Status: implemented in the current worktree.
- Scope: first usable formatter milestone for the long-term Formatter feature. It is intentionally conservative and does not attempt structural alignment or expression rewriting yet.
- `FormatterService` owns formatting policy and returns a `FormatterReport`; editor runtime only invokes the service and applies the result as one undoable edit block.
- The MVP is indent-only: it changes leading whitespace, leaves line-internal text untouched, keeps blank lines empty, ignores strings and comments while counting structure, and preserves preprocessor directive lines so macro-heavy code is not reshaped.
- Supported structure counters include `module`/`endmodule`, `interface`/`endinterface`, `package`/`endpackage`, `class`/`endclass`, `function`/`endfunction`, `task`/`endtask`, `generate`/`endgenerate`, `begin`/`end`, `case`/`endcase`, and `fork`/`join*`.
- The first UI entry was editor context menu `Format Document`; it reports status and can be undone in one step. Later formatter milestones have since added format selection, alignment of ports/instances/parameters, and trailing `//` comment preservation; format-on-save and user-configurable formatter profiles remain future work.
- Implementation and future formatter extensions must remain no-regex. Prefer Tree-sitter/semantic records for future structural formatting and deterministic token scans for small local policies.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`, direct `completion_test` run, direct `gui_smoke_test` run against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, and `git diff --check`.

### Post-L: Formatter Declaration Alignment MVP

- Status: implemented in the current worktree.
- Scope: second usable formatter milestone. It keeps the formatter conservative but moves beyond indent-only for common declaration blocks.
- `FormatterService` now has an `alignDeclarationBlocks` option enabled by default. Alignment runs after indentation and stays inside the service/report boundary; editor runtime still applies one undoable formatted-text replacement.
- The MVP aligns consecutive simple signal declarations by declaration-name column and aligns consecutive `parameter` / `localparam` assignment columns. It supports packed and unpacked dimensions on the declaration side.
- The alignment pass is intentionally narrow: it skips preprocessor lines, block-comment-bearing lines, multi-declaration lines, typedefs, and non-declaration statements. A later formatter milestone adds trailing `//` comment preservation for simple aligned declarations.
- Implementation remains no-regex. It uses deterministic token scans, bracket-depth checks, and conservative declaration parsing.
- Next Formatter milestones: formatter profile controls and eventually deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run with 404 checks, focused build target `gui_smoke_test`, direct `gui_smoke_test` run with 361 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Port List Alignment MVP

- Status: implemented in the current worktree.
- Scope: third usable formatter milestone. It extends the conservative formatter from declarations into common ANSI module port lists without changing expressions or comments.
- `FormatterService` now has an `alignPortLists` option enabled by default. The pass runs after indentation and declaration alignment, stays inside the formatter report boundary, and still reaches the editor only as one undoable formatted-text replacement.
- The MVP aligns contiguous simple ANSI `input` / `output` / `inout` lines by direction, type/range prefix, and port name. It preserves unpacked dimensions after the port name and keeps trailing commas where they already exist.
- The alignment pass is intentionally narrow: it skips preprocessor lines, block-comment-bearing lines, multi-port lines, default-value ports, inline closing forms such as `);`, and complex port declarations. Instance port maps and parameter override maps remain later formatter work.
- Implementation remains no-regex. It uses deterministic token scans, bracket-depth checks, and conservative port-line parsing.
- Next Formatter milestones: formatter profile controls and eventually deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run with 414 checks, focused build target `gui_smoke_test`, and direct `gui_smoke_test` run with 365 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Instance Map Alignment MVP

- Status: implemented in the current worktree.
- Scope: fourth usable formatter milestone. It extends conservative alignment into common named association maps without attempting broad expression formatting.
- `FormatterService` now has an `alignInstanceMaps` option enabled by default. The pass runs after indentation, declaration alignment, and port-list alignment, stays inside the formatter report boundary, and still reaches the editor only as one undoable formatted-text replacement.
- The MVP aligns contiguous simple named association lines by the association name before the opening parenthesis. It covers both parameter override maps such as `.PARAM(value)` and instance port maps such as `.clk(clk)`.
- Expression text inside parentheses is preserved. The pass only normalizes spacing between the association name and `(`, and it keeps trailing commas where they already exist.
- The alignment pass is intentionally narrow: it skips preprocessor lines, block-comment-bearing lines, semicolon-terminated inline forms, malformed parentheses, and complex non-single-line associations. Positional maps and mixed inline maps remain later formatter work.
- Implementation remains no-regex. It uses deterministic token scans, quote-aware parenthesis matching, and conservative named-association parsing.
- Next Formatter milestones: formatter profile controls and eventually deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build target `completion_test` and direct `completion_test` run with 417 checks. Final release verification for the commit also includes focused `gui_smoke_test`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Trailing Comment Alignment MVP

- Status: implemented in the current worktree.
- Scope: fifth usable formatter milestone. It makes the existing conservative alignment passes useful on common real RTL lines that carry trailing explanatory comments.
- `FormatterService` now splits trailing `//` comments with quote-aware token scans before parsing simple aligned lines. Declaration blocks, ANSI port-list blocks, and named instance-map blocks consume only the code portion for alignment, then reattach the comment at an aligned comment column.
- The MVP preserves the comment text from `//` onward and keeps expression or declaration text under the same conservative parsing rules as the earlier alignment passes.
- The pass intentionally skips block comments, preprocessor lines, comment-only lines, and any declaration/port/map form that the existing conservative parser would skip. It does not attempt to reflow comment text or support multiline block comments.
- Implementation remains no-regex. It uses deterministic token scans plus the existing bracket/parenthesis helpers and keeps formatting policy inside `FormatterService`.
- Next Formatter milestones: formatter profile controls and eventually deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build target `completion_test` and direct `completion_test` run with 420 checks. Final release verification for the commit also includes focused `gui_smoke_test`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Format Selection MVP

- Status: implemented in the current worktree.
- Scope: sixth usable formatter milestone. It exposes the conservative formatter on selected code ranges without requiring users to format the whole document.
- `FormatterService::formatSelection` owns selection formatting policy. It formats selected full-line ranges by removing common base indentation before formatting, then restoring that base indentation afterward so nested module or block selections do not get pulled back to column 0.
- `MyCodeEditor::formatSelection` expands an arbitrary text selection to the affected full lines, applies the formatter result as one undoable edit, reselects the formatted range, and reports status. The editor context menu exposes `Format Selection` when text is selected, beside `Format Document`.
- The MVP intentionally formats line ranges, not arbitrary character spans. It preserves existing formatter conservatism: complex statements, skipped aligned forms, and block comments remain under the same rules as document formatting.
- Implementation remains no-regex. UI only routes selection text and applies the returned formatter report; formatting policy stays inside `FormatterService`.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run with 423 checks, focused build target `gui_smoke_test`, and direct `gui_smoke_test` run with 368 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Profile Controls MVP

- Status: implemented in the current worktree.
- Scope: seventh usable formatter milestone. It lets users pick between the existing conservative structured formatting path and a pure indentation path before the formatter grows deeper structural passes.
- `FormatterProfile` adds `Structured` and `Indent Only`. `FormatterService::optionsForProfile` maps `Indent Only` to indentation plus preprocessor preservation while disabling declaration, ANSI port-list, and named instance-map alignment; `Structured` keeps the existing default behavior.
- `MyCodeEditorState` stores the active profile per editor. `EditorSourceNavigationUi` adds a `Formatter Profile` submenu to the editor context menu with checkable profile actions, and document/selection formatting status messages include the active profile name.
- This milestone intentionally does not add format-on-save or persistent application settings yet. It keeps policy in `FormatterService` and editor state, with UI only selecting a profile and applying the service report as one undoable edit.
- Implementation remains no-regex.
- Next Formatter milestones: format-on-save policy if wanted and eventually deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 435 checks and direct `gui_smoke_test` run with 377 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Profile Persistence MVP

- Status: implemented in the current worktree.
- Scope: eighth usable formatter milestone. It makes the selected formatter profile behave like an application preference instead of a temporary editor-only toggle.
- `FormatterSettings` persists the selected profile with `QSettings` under the `formatter/profile` key. Invalid or unknown profile keys fall back to `Structured`.
- `EditorCoordinator` owns formatter-setting distribution: it applies the saved profile to existing editors, applies it to newly-created editors, and listens for editor profile changes so context-menu profile selections are written back to settings.
- `MainWindow` creates the shared `FormatterSettings` beside existing editor appearance settings. `MyCodeEditor` emits `formatterProfileChanged` only when the profile actually changes, preventing profile-setting feedback loops.
- This milestone intentionally does not add format-on-save. It only preserves and distributes the profile used by explicit document/selection formatting.
- Implementation remains no-regex.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 435 checks and direct `gui_smoke_test` run with 386 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Format-on-Save MVP

- Status: implemented in the current worktree.
- Scope: ninth usable formatter milestone. It makes formatter profiles useful in a repeated editing workflow while keeping save-time formatting explicitly opt-in.
- `FormatterSettings` persists `formatter/formatOnSave` beside `formatter/profile`. The default is disabled, and toggling the setting emits `formatOnSaveChanged`.
- `EditorSourceNavigationUi` exposes `Format On Save` as a checkable action in the `Formatter Profile` context-menu submenu. `EditorCoordinator` applies the setting to open editors, new editors, and writes editor changes back to settings without feedback loops.
- `TabSaveController` calls `MyCodeEditor::formatDocumentForSave()` before resolving the save text. The editor formats with the active profile, updates its buffer as one undoable edit, and `DocumentModel` is refreshed before the file is written.
- This milestone intentionally does not make format-on-save the default and does not expand formatter semantics beyond the current conservative service.
- Implementation remains no-regex.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 435 checks and direct `gui_smoke_test` run with 396 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Case Item Alignment MVP

- Status: implemented in the current worktree.
- Scope: tenth usable formatter milestone and first deeper structured block-alignment step after save/profile plumbing. It aligns simple case-item labels without attempting expression rewriting or full AST formatting.
- `FormatterOptions::alignCaseItems` is enabled for the `Structured` profile and disabled for `Indent Only`, matching existing declaration, port-list, and instance-map alignment behavior.
- `FormatterService` tracks formatted `case` / `casex` / `casez` indentation levels, aligns only same-level item labels inside the current case body, preserves trailing `//` comments at a stable comment column, and skips preprocessor lines, block-comment lines, nested/uncertain lines, and `pkg::name` style scope separators.
- Implementation remains no-regex. It uses deterministic line/token scans and top-level delimiter tracking; future milestones can replace or enrich this with Tree-sitter-backed structural formatting.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, including richer block-aware statement alignment and safer multi-line structural edits.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 446 checks and direct `gui_smoke_test` run with 403 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Data MVP

- Status: implemented in the current worktree.
- Scope: first usable data milestone for the long-term Wave Preview feature. It is a code-understanding sketch model, not a simulator, VCD engine, or project-wide timing proof.
- `WavePreviewService` owns current-text extraction and emits `WavePreviewReport` rows for procedural blocks, per-signal lanes, and assignment events. UI work should consume this report and render lanes without deriving semantics in paint/event code.
- The MVP supports continuous `assign`, `always_comb`, `always_ff`, `always_latch`, and plain `always` blocks. Multiple `always` blocks are preserved as separate block records and merged into signal lanes by assignment target.
- Assignment events include target, expression text, source signal names, assignment kind, trigger text, line/column evidence, and a simple cycle offset: clocked `always_ff` / posedge-negedge `always` events are offset by 1, while combinational, latch, level-sensitive, and continuous assignments stay at 0.
- Comments and strings are ignored by the tokenizer, so fake assignments in comments or string literals do not create lanes. Implementation and future extensions must remain no-regex; use deterministic token scans first and semantic records where cross-file fidelity is needed.
- Next Wave Preview milestones: graphical lane/canvas rendering, guard-condition labels, clock/reset grouping, and later semantic relationships for cross-file/module context.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`, direct `completion_test` run with 392 checks, direct `gui_smoke_test` run with 354 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, and `git diff --check`.

### Post-L: Wave Preview Preview-Panel MVP

- Status: implemented in the current worktree.
- Scope: first visible Wave Preview milestone. It renders the existing data-service report so users can inspect a lightweight waveform sketch before richer timing visualization exists.
- `WavePreviewPanelCoordinator` owns the dock and tree rendering. MainWindow only routes active editor text, visibility-triggered refresh, and source navigation; it does not derive assignments, scan workspace files, or simulate RTL.
- The panel is available from View -> Wave Preview, starts hidden on launch, participates in reset panel layout, and refreshes from the current dirty editor buffer while the dock is visible. Opening the dock or resetting the panel layout forces a fresh preview.
- The rendered model shows per-signal lanes, assignment events, timing hints (`t+0`, `t+1 cycle`, continuous), source signal lists, and line/column evidence. Double-clicking an event navigates to the assignment location when a file-backed location is available.
- This remains a code-understanding aid. It does not evaluate values, honor all guards, resolve clocks across modules, or replace simulation.
- Next Wave Preview milestones: guard-condition labels, clock/reset grouping, and semantic cross-file enrichment.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`, direct `completion_test` run with 392 checks, direct `gui_smoke_test` run with 361 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, and `git diff --check`.

### Post-L: Wave Preview Canvas MVP

- Status: implemented in the current worktree.
- Scope: first graphical rendering milestone for Wave Preview. It adds a compact timing canvas to the existing dock while keeping extraction and timing-sketch policy in `WavePreviewService`.
- `WavePreviewPanelCoordinator` now renders each lane twice from the same report: a graphical canvas with signal rows, `t+N` timing guides, and colored assignment blocks, plus the existing tree with event/source/location details.
- The canvas is intentionally lightweight. It visualizes assignment timing hints and lane grouping, not real signal values, clock resolution, guards, or simulation results.
- GUI ownership remains thin: the canvas paints `WavePreviewReport` data, clears with unavailable reports, and does not scan workspace files or parse RTL itself.
- Next Wave Preview milestones: clock/reset grouping and semantic cross-file enrichment.
- Verification for this block: focused build target `gui_smoke_test`, direct `gui_smoke_test` run with 363 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, offscreen canvas render nonblank check, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Guard Labels MVP

- Status: implemented in the current worktree.
- Scope: first control-context milestone for Wave Preview. It explains why a displayed assignment appears under a common RTL branch without evaluating whether that branch is true.
- `WavePreviewAssignment` now carries `guardText`. `WavePreviewService` derives it with deterministic token scans from active `if` / `else if` bodies and `case` / `default` item labels near the assignment.
- The Wave Preview tree adds a Guard column, event tooltips include the guard, and the canvas block label includes guarded assignment context. The UI still consumes `WavePreviewReport`; it does not parse RTL or scan workspace files itself.
- Supported guard labels are intentionally conservative: simple inline or `begin`/`end` `if` bodies, `else if` conditions, `else`, and basic `case` item/default labels. This is not symbolic execution, guard simplification, or branch coverage.
- Implementation remains no-regex and does not change the non-simulator positioning of Wave Preview.
- Next Wave Preview milestones: semantic cross-file enrichment and guard labels for more statement forms.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`, direct `completion_test` run with 408 checks, direct `gui_smoke_test` run with 364 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Clock/Reset Groups MVP

- Status: implemented in the current worktree.
- Scope: first clock/reset context milestone for Wave Preview. It groups procedural blocks by the clock/reset signals visible in common edge-triggered event controls.
- `WavePreviewBlock` now carries `clockSignals` and `resetSignals`; `WavePreviewReport` adds `clockResetGroups` that aggregate block indexes and assignment counts for matching domains.
- `WavePreviewService` extracts edge signals from `posedge` / `negedge` event controls with deterministic token scans. Reset classification is name-based (`rst` / `reset`) and intentionally conservative; this is not clock-domain crossing analysis or reset polarity proof.
- The Wave Preview tree adds a Clock/Reset column and a Clock/Reset Groups section, while event tooltips include the detected domain. The UI still renders report data only and does not parse RTL or scan workspace files itself.
- Next Wave Preview milestones: semantic cross-file enrichment, guard labels for more statement forms, and richer canvas interaction for dense reports.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`, direct `completion_test` run with 411 checks, direct `gui_smoke_test` run with 365 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Queued Refresh MVP

- Status: implemented in the current worktree.
- Scope: first responsiveness milestone for Wave Preview on large active buffers. It keeps `WavePreviewService` synchronous and report-focused, and puts refresh coalescing in the panel coordinator where UI visibility and active document routing already live.
- `WavePreviewPanelCoordinator` now queues refreshes for document text at or above 64 KiB behind a 180 ms single-shot timer. Repeated refresh requests replace the pending file/text/dirty tuple, so only the latest large buffer is rendered when the timer fires.
- Normal-sized buffers still refresh immediately, preserving the live-feeling editor feedback for everyday modules. Empty text still renders unavailable immediately.
- Pending large-buffer refresh work is canceled when the Wave Preview dock is hidden; reopening the dock asks MainWindow for a fresh active-editor snapshot instead of rendering stale pending text.
- This milestone is not threaded extraction, not simulation, and not semantic cross-file enrichment. It only prevents visible Wave Preview from recomputing a large report on every edit burst.
- Next Wave Preview milestones: semantic cross-file enrichment, guard labels for more statement forms, and clock/reset polarity hints.
- Verification for this block: focused build targets `completion_test`, `large_file_perf_test`, and `gui_smoke_test`; direct `completion_test` run with 430 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, and `gui_smoke_test` with 372 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Hover Details MVP

- Status: implemented in the current worktree.
- Scope: first source/target inspection milestone for Wave Preview. It improves local hover evidence in the existing dock and canvas without adding semantic cross-file enrichment or simulation.
- `WavePreviewPanelCoordinator` now builds one shared assignment detail tooltip from `WavePreviewReport` data and applies it to both tree event rows and canvas assignment blocks. Details include target, expression, source signals, assignment kind, block kind, timing hint, trigger, clock/reset summary, guard label, and line/column evidence.
- Canvas assignment blocks now keep lightweight hit rectangles during paint and show the same detail tooltip on mouse hover. Lane rows summarize the signal, event count, and unique source signals.
- This remains a UI rendering/report-inspection milestone: the panel still does not parse RTL, scan workspace files, run Slang, or evaluate waveform values.
- Next Wave Preview milestones: semantic cross-file enrichment, guard labels for more statement forms, and richer canvas interaction for dense reports.
- Verification for this block: focused build target `gui_smoke_test` and direct `gui_smoke_test` run with 374 checks against `test_sv/new` plus `test_sv/test_symbols.sv`; final release verification for the commit also includes focused `completion_test`, focused `large_file_perf_test`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Clock/Reset Polarity Hints MVP

- Status: implemented in the current worktree.
- Scope: first syntax-visible edge-polarity milestone for Wave Preview. It preserves the `posedge` / `negedge` words already present in event controls so users can see clock/reset edge context in the sketch.
- `WavePreviewEdgeSignal` carries the signal name plus optional edge text. `WavePreviewService` stores edge-aware clock/reset hints on blocks and clock/reset groups while keeping the existing `clockSignals` / `resetSignals` lists for compatibility.
- Clock/reset group keys now include edge labels when available, so `posedge clk` and `negedge clk` do not collapse into the same displayed group.
- `WavePreviewPanelCoordinator` renders edge-aware labels such as `clk posedge clk / rst negedge rst_n` in Clock/Reset columns and shared hover details.
- This remains a report/display hint, not CDC analysis, reset-polarity proof, or simulation. The parser only reports event-control text visible in the current buffer.
- Next Wave Preview milestones: semantic cross-file enrichment, guard labels for more statement forms, and richer canvas interaction for dense reports.
- Verification for this block: focused build targets `completion_test`, `large_file_perf_test`, and `gui_smoke_test`; direct `completion_test` run with 432 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, and `gui_smoke_test` with 374 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Declaration Context MVP

- Status: implemented in the current worktree.
- Scope: first local semantic-context milestone for Wave Preview. It enriches the existing lane/event sketch with current-document declaration context while staying short of cross-file elaboration, value simulation, or workspace scans.
- `WavePreviewSignalContext` records signal name, port direction or internal role, type/range text, declaration text, and declaration location. `WavePreviewReport` carries these contexts, and each lane stores the context for its target when available.
- `WavePreviewService` collects simple current-document port and built-in signal declarations with deterministic token scans. It intentionally skips typedef/parameter/localparam regions and avoids regex, keeping complex semantic inference for later Slang-backed enrichment.
- `WavePreviewPanelCoordinator` adds a Context column and includes target/source declaration context in the shared tree/canvas hover details. The panel still renders report data only; it does not parse RTL, scan the workspace, or simulate values.
- Next Wave Preview milestones: cross-file/semantic enrichment using existing semantic records, guard labels for more statement forms, and richer canvas interaction for dense reports.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 443 checks and `gui_smoke_test` with 403 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Analysis Plan MVP

- Status: implemented in the current worktree.
- Scope: first usable scheduling-policy milestone for Huge Workspace Mode. It does not change Slang extraction or introduce incremental publication yet; it makes the workspace symbol-analysis batch order explicit and testable.
- `WorkspaceAnalysisPlanService` owns workspace-analysis ordering policy. It receives a `ProjectSnapshot`, the active file, and open document snapshots, then returns a planned `ProjectSnapshot` plus dirty protected files.
- Planned order is active file first, dirty open files next, clean open files next, and the remaining workspace SystemVerilog files in existing project order. Native-path and absolute-path inputs normalize to the same workspace entry, duplicates are removed, and current files outside the workspace are ignored.
- Dirty open files remain protected so workspace disk analysis cannot overwrite the live editor buffer. `AnalysisCoordinator` injects the active-file provider into `AnalysisScheduler`; `WorkspaceSymbolAnalysisController` consumes the plan and passes only planned execution data to `SymbolAnalyzer`.
- This is groundwork for the larger Huge Workspace Mode: later milestones should add incremental/early publication, cancellable/expirable queued workspace batches, tiered indexes, and stronger current-file foreground analysis for very large projects.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run with 397 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, `gui_smoke_test` with 361 checks, full default CMake build, full `ctest --output-on-failure` 7/7, changed-file regex API scan, and `git diff --check`.

### Post-L: Huge Workspace Expirable Request MVP

- Status: implemented in the current worktree.
- Scope: first usable stale-work handling milestone for Huge Workspace Mode. It does not yet make Slang extraction internally interruptible or publish partial workspace results; it prevents new workspace analysis requests from synchronously waiting on older background work.
- `WorkspaceAnalysisRequestQueue` owns active request state plus one latest pending request. While a workspace analysis is active, repeated requests replace the pending project instead of building a backlog.
- `SymbolAnalyzer::expireWorkspaceAnalysis()` bumps the workspace-analysis generation and cancels the active watcher without waiting. Canceled or generation-stale watcher completion emits `workspaceAnalysisExpired()` instead of publishing stale results.
- `WorkspaceSymbolAnalysisController` queues the newest request, expires the active run, clears request state when projects close, and starts the latest pending project after an expired or completed watcher returns.
- Dirty open-document protection and the existing `WorkspaceAnalysisPlanService` order remain in force for the restarted request.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and broader foreground scheduling that stays responsive under very large projects.
- Verification for this block: focused build target `completion_test`, direct `completion_test` run with 401 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, `gui_smoke_test` with 361 checks, full default CMake build, full `ctest --output-on-failure` 7/7, changed-file regex API scan, and `git diff --check`.

### Post-L: Huge Workspace Request Telemetry MVP

- Status: implemented in the current worktree.
- Scope: third usable Huge Workspace Mode milestone. It does not change extraction, publication, or cancellation semantics; it makes stale-work pressure observable at the queue boundary.
- `WorkspaceAnalysisRequestQueue` now exposes `WorkspaceAnalysisRequestTelemetry` with active/pending state, active request age, pending request age, pending update count, and the last finished active/pending wait metrics.
- Pending update count tracks how many workspace-analysis requests were coalesced into the latest pending request while an active run was still in flight. The last-taken metrics remain available after `finishAndTakePending()` so callers can report or later tune stale-work pressure even after the pending request has been consumed.
- Telemetry remains owned by the queue. `WorkspaceSymbolAnalysisController` still routes lifecycle and expiration; it does not own timing policy or calculate queue ages itself.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and broader foreground scheduling that stays responsive under very large projects.
- Verification for this block: focused build target `completion_test` and direct `completion_test` run with 427 checks. Final release verification for the commit also includes `large_file_perf_test` against `test_sv/new`, focused `gui_smoke_test`, changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Telemetry Activity MVP

- Status: implemented in the current worktree.
- Scope: fourth usable Huge Workspace Mode milestone. It makes the request telemetry visible in the existing Activity/Output channel without changing extraction, cancellation, or publication semantics.
- `WorkspaceSymbolAnalysisController` emits queued and resolved request telemetry events after queue updates. `AnalysisScheduler` forwards those events, and `AnalysisProgressCoordinator` formats them into Activity/Output log entries.
- Queued logs include active age, pending age, and pending update count. Resolved logs include active wait time plus pending wait/update data when a coalesced pending request was consumed.
- Timing data remains owned by `WorkspaceAnalysisRequestQueue`; controller and scheduler only route report data, and Activity/Output only renders it.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and broader foreground scheduling that stays responsive under very large projects.
- Verification for this block: focused build targets `completion_test`, `large_file_perf_test`, and `gui_smoke_test`; direct `completion_test` run with 427 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, and `gui_smoke_test` with 370 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Foreground Open Documents MVP

- Status: implemented in the current worktree.
- Scope: fifth usable Huge Workspace Mode milestone. It does not make Slang extraction internally interruptible, publish partial workspace batches, or change request coalescing; it makes open editor buffers refresh before a background workspace batch starts.
- `AnalysisScheduler` handles `workspaceSymbolAnalysisStarted` by calling `OpenDocumentAnalysisController::analyzeOpenDocumentsNow()` before re-emitting the workspace start signal. Because `WorkspaceSymbolAnalysisController` emits the start signal before `SymbolAnalyzer::startAnalyzeProjectAsync()`, open tabs are analyzed from live editor text before the workspace disk batch is launched.
- The existing finish-time open-document refresh remains in place as a reconciliation pass after workspace analysis completes, so dirty/live buffers are refreshed both before and after the background batch without moving feature policy into `SymbolAnalyzer`.
- A focused completion test now records `SymbolAnalyzer::analysisStarted` order for a temporary workspace and proves `open_tabs` starts before the workspace root analysis.
- Next Huge Workspace milestones: foreground refresh for workspace relationship analysis, deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and diagnostics scheduling that stays responsive under very large projects.
- Verification for this block: focused build targets `completion_test`, `large_file_perf_test`, and `gui_smoke_test`; direct `completion_test` run with 430 checks, `large_file_perf_test` with 14 checks against `test_sv/new`, and `gui_smoke_test` with 370 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Relationship Foreground Refresh MVP

- Status: implemented in the current worktree.
- Scope: sixth usable Huge Workspace Mode milestone. It keeps relationship extraction and publication unchanged, but moves the same foreground open-document refresh policy in front of workspace relationship analysis so the relationship snapshot is built after live editor buffers have been refreshed.
- `AnalysisScheduler::refreshOpenDocumentsForForegroundAnalysis()` now owns the shared foreground-refresh call. Workspace symbol start, workspace symbol finish reconciliation, and workspace relationship requests route through that helper instead of duplicating direct controller calls.
- `requestWorkspaceRelationshipAnalysis()` refreshes open documents before delegating to `RelationshipAnalysisController::requestWorkspaceAnalysis()`. The relationship controller still owns async relationship work, cancellation, and result publication.
- A focused completion test records `SymbolAnalyzer::analysisStarted("open_tabs")` and `workspaceRelationshipAnalysisStarted` order for a temporary workspace and proves open tabs refresh before workspace relationship analysis starts.
- Next Huge Workspace milestones: diagnostics refresh coalescing, deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, and tiered symbol indexes.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 436 checks against `test_sv/new`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostics Refresh Coalescing MVP

- Status: implemented in the current worktree.
- Scope: seventh usable Huge Workspace Mode milestone. It keeps diagnostics computation and panels unchanged, but makes refresh delivery scope-safe under bursty workspace analysis.
- `DiagnosticsRefreshController` now tracks whether a full diagnostics refresh is pending. A file-specific refresh can still replace another file-specific refresh during debounce, but once a full refresh is pending, later file-specific requests keep the pending scope full until the timer emits.
- Empty `fileName` remains the all-files refresh contract. The controller still emits only one debounced `diagnosticsRefreshRequested` signal for a burst and does not trigger semantic analysis itself, avoiding refresh/analyze feedback loops.
- A focused completion test requests file, full, and file refreshes in one debounce window and proves the emitted scope remains full; it also proves an isolated file refresh still emits the file name.
- Next Huge Workspace milestones: progress checkpoint Activity visibility, deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, and tiered symbol indexes.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 440 checks against `test_sv/new`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Progress Checkpoint Activity MVP

- Status: implemented in the current worktree.
- Scope: eighth usable Huge Workspace Mode milestone. It improves long-run visibility without changing analysis execution, cancellation, semantic publication, or UI query policy.
- `AnalysisProgressCoordinator` now connects `AnalysisScheduler::workspaceSymbolAnalysisProgress` to the existing symbol progress handler, so symbol pass progress updates are routed through the coordinator instead of being silently dropped.
- Symbol and workspace relationship progress now write Activity/Output checkpoints at 25%, 50%, and 75%. The 100% state remains represented by the existing completion logs to avoid duplicate final messages.
- Checkpoint logging is bounded and non-spammy: each stage keeps its own last checkpoint, resets when the stage starts, and does not log every file.
- A focused GUI smoke regression drives symbol and relationship progress and proves Activity/Output records expected checkpoints while avoiding duplicate 100% progress records.
- Next Huge Workspace milestones: request restart/status visibility, deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, and tiered symbol indexes.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 398 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Request Restart Visibility MVP

- Status: implemented in the current worktree.
- Scope: ninth usable Huge Workspace Mode milestone. It does not change coalescing, expiration, or background execution; it makes the existing latest-request restart behavior visible to the user.
- `AnalysisProgressCoordinator::handleWorkspaceAnalysisRequestQueued()` now emits a status-bar message when an active workspace analysis has a pending replacement request, making it clear that the newest request will replace stale work.
- `handleWorkspaceAnalysisRequestResolved()` now marks resolved pending telemetry with `restarting latest request` in Activity/Output and emits a status message when the pending request is taken.
- The existing `WorkspaceAnalysisRequestQueue` remains the timing/state owner. This milestone only renders the queue state more clearly through coordinator-owned UI/status channels.
- A focused GUI smoke regression verifies the Activity restart message and the queued/restarting status messages.
- Next Huge Workspace milestones: workspace relationship cancellation visibility, deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, and tiered symbol indexes.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 400 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Workspace-Relationship Cancellation Visibility MVP

- Status: implemented in the current worktree.
- Scope: tenth usable Huge Workspace Mode milestone. It does not change cancellation execution; it renders the existing workspace relationship cancellation signal through the same status and Activity/Output ownership boundary as other long-analysis feedback.
- `AnalysisProgressCoordinator` now connects `AnalysisScheduler::workspaceRelationshipAnalysisCancelled` and handles it separately from single-file relationship cancellation.
- Workspace relationship cancellation records a warning-level `Analyzer` Activity/Output event and emits `Workspace relationship analysis cancelled` to the status path.
- This keeps relationship work and cancellation execution inside `RelationshipAnalysisController`; the coordinator only renders user-visible feedback.
- A focused GUI smoke regression directly drives the cancellation rendering path and verifies both the Activity/Output warning and status message.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and richer direct cancellation controls for long analysis runs.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 402 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Plan Summary Visibility MVP

- Status: implemented in the current worktree.
- Scope: eleventh usable Huge Workspace Mode milestone. It does not change analysis execution, cancellation, or result publication; it makes the existing active/open/protected/background scheduling plan visible at the scheduler boundary.
- `WorkspaceAnalysisPlan` now records `backgroundFileCount` beside priority/open/protected/current-file data.
- `WorkspaceSymbolAnalysisController` emits `workspaceAnalysisPlanPrepared`, `AnalysisScheduler` forwards it, and `AnalysisProgressCoordinator` records an Activity/Output summary plus status-bar message.
- The summary reports total files, priority files, background files, open files, protected dirty files, and whether the current file is prioritized.
- This keeps priority policy in `WorkspaceAnalysisPlanService`; UI/progress code renders the plan instead of recomputing it.
- Focused regressions cover the background count in the plan service, the scheduler signal chain, and Activity/Output plus status rendering for the summary.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, incremental/early publish of safe per-file results, tiered symbol indexes, and richer direct cancellation controls for long analysis runs.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 447 checks; direct `gui_smoke_test` run with 405 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

## Batch Policy

- Phase D can proceed in batches when blocks do not share service contracts or UI surfaces.
- Suitable Phase D batches: focused service tests, independent feature-service reports, UI render-only cleanup, and fixture expansion.
- E1, E2, and E3 are mostly serial.
- E4 publication rules are serial, but focused tests and isolated report metadata can batch.
- E5 and E6 can be split by service once earlier contracts are stable.
- Safe batch blocks: focused service tests, independent query-service result migration, UI render-only conversion, and report display-field cleanup.
- Not suitable for batching: symbol identity changes, `SymbolInfo` layout changes, `sym_type_e` compatibility changes, owner/type model migration, source role migration, snapshot publication rules, relationship rebind rules, and cross-service query contract changes.
- F1-F3 should proceed in small serial blocks when changing semantic symbol records, relationship identity, compatibility APIs, owner/type metadata, completion item models, or query normalization.
- Phase H should proceed in small serial blocks; avoid mixing guard expansion, snapshot/store migration, completion contract migration, feature-service helper migration, and collector adapter deletion in one commit.
- Phase I should proceed in strict subphase order unless a later subphase exposes a small prerequisite cleanup; do not mix collector-native emission, store replacement, relationship/scope migration, and carrier deletion in one commit.
- Phase J should proceed in strict subphase order; do not mix test-builder introduction, completion/jump migration, relationship/GUI migration, carrier deletion, and final guard tightening in one commit.
- Phase L should proceed in strict subphase order; avoid mixing guard expansion, completion context migration, include/import/module range migration, FSM extraction migration, and scheduler change detection in one commit.
- Reduce batch size when blocks share core files, API boundaries, or real fixture expectations.

## Good Work Blocks

- Query Service or feature service read paths.
- Report/model shaping for semantic panels.
- Service-built graph models for visual semantic panels.
- Focused UI rendering of stable reports.
- Real fixture coverage tied to production changes.
- RTL Insights service expansion with clear report contracts.
- Snapshot publication metadata and guard plumbing when done in one ownership boundary.
- Editor or project boundary cleanup with a clear ownership target.

## Risky Work Blocks

- Broad simultaneous changes across `TabManager`, `DocumentModel`, and `MyCodeEditor`.
- Async scheduler lifecycle changes.
- `SemanticIndex` snapshot publication or analyzer write-back changes.
- Relationship analysis lifecycle changes that share controller state.
- Migrations where signal lifetime or API shape is not stable.

Reduce batch size when blocks share core files or API boundaries.

## Validation

- For narrow feature changes, run focused build/tests and static boundary scans first.
- For shared semantic, scheduler, editor, or project boundary changes, run full Ninja and full `ctest --output-on-failure`.
- Before committing, run `git diff --check`, changed-file ASCII/trailing-whitespace scans, and forbidden-file guards.
- Prefer real workspace fixtures such as `test_sv/new` for semantic feature expansion.
- Phase D feature work should prove behavior at the service level before UI smoke coverage.
- Signal graph features must verify node direction, module grouping, evidence links, hover preview data, and dock/menu registration.
- Fold Region / Fold Shelf changes must verify visible mode chip updates, gutter or viewport previews, shelf active styling, Esc cancellation, and input blocking where a mode owns editor interaction.
- Every feature change must explicitly verify the Feature Expansion Guardrails that apply to its scope, including the final repo-source zero target when semantic/test code changes.

## Commit Policy

- Keep local commits coherent and architecture-oriented.
- Do not push unless explicitly asked.
- Do not discard user changes.
- Do not touch user dirty RTL fixture files.
- Keep docs short and current; replace stale content instead of accumulating history.
