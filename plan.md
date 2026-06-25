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

### Post-L: Formatter Enum Item Alignment MVP

- Status: implemented in the current worktree.
- Scope: additional formatter usability milestone and a conservative enum readability step. It aligns simple enum member value columns without rewriting enum values, changing member order, or attempting full AST formatting.
- `FormatterOptions::alignEnumItems` is enabled for the `Structured` profile and disabled for `Indent Only`, matching the existing split between structural alignment and pure indentation.
- `FormatterService` tracks formatted multi-line `enum ... { ... }` regions, aligns only simple same-indent enum member rows by member name/value columns, preserves trailing commas and trailing `//` comments, and skips preprocessor lines, block-comment lines, complex brace-containing rows, single-line enums, and uncertain lines.
- Implementation remains no-regex. It uses deterministic token scans, comment/string masking, and delimiter balance tracking inside `FormatterService`.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 506 checks; direct `gui_smoke_test` run with 416 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Assignment Alignment MVP

- Status: implemented in the current worktree.
- Scope: eleventh usable formatter milestone and the next block-aware statement alignment step. It aligns simple assignment statements without rewriting expressions, changing statement order, or attempting multi-line formatting.
- `FormatterOptions::alignAssignments` is enabled for the `Structured` profile and disabled for `Indent Only`, matching the existing alignment-option split.
- `FormatterService` aligns consecutive same-indent assignment blocks by top-level `=` or `<=`, including continuous `assign` statements and procedural blocking/nonblocking statements.
- The pass preserves trailing `//` comments at a stable comment column and skips declarations, case-item suffixes, preprocessor lines, block-comment lines, invalid/uncertain statements, and single-line blocks.
- Implementation remains no-regex. It uses deterministic token scans, string-aware delimiter tracking, and top-level assignment detection inside `FormatterService`.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer multi-line statement handling, and safer alignment choices that can use parser structure instead of line-local heuristics.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 451 checks and direct `gui_smoke_test` run with 405 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Continuation Indent MVP

- Status: implemented in the current worktree.
- Scope: twelfth usable formatter milestone and first multi-line statement indentation step. It does not rewrite expressions or align multi-line operands; it adjusts leading whitespace for delimiter continuation lines.
- `FormatterOptions::indentContinuationLines` is enabled by default and remains enabled for `Indent Only`, because it only changes indentation. Structured alignment passes remain independently controlled.
- `FormatterService` tracks unterminated `(` / `[` / `{` delimiters with deterministic code-only scans and indents continuation lines one extra level until the matching closing delimiter line.
- Module/interface/class/function/task headers suppress delimiter continuation so ANSI port lists keep the existing one-level header depth instead of being over-indented.
- Existing instance-map and port-map blocks now receive clearer continuation indentation before their alignment passes run.
- Implementation remains no-regex and keeps formatter policy inside `FormatterService`.
- Next Formatter milestones: Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 459 checks and direct `gui_smoke_test` run with 408 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Continuation Operator Alignment MVP

- Status: implemented in the current worktree.
- Scope: thirteenth usable formatter milestone and a conservative multi-line expression readability step. It does not split, merge, reorder, or rewrite expressions; it only adjusts leading whitespace on already-multiline operator continuation lines.
- `FormatterOptions::alignContinuationOperators` is enabled for the `Structured` profile and disabled for `Indent Only`, matching the existing split between formatting structure and pure indentation.
- `FormatterService` tracks a preceding multi-line assignment's top-level `=` / `<=` RHS start column and aligns following leading binary-operator lines such as `+`, `-`, `|`, `^`, `&&`, and `||` to that column until the statement ends.
- The pass skips blank lines, preprocessor lines, block comments, and single-line assignments, preserves trailing comments and expression text, and remains no-regex through deterministic token/comment/operator scans.
- Next Formatter milestones: Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 485 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Ternary Continuation Alignment MVP

- Status: implemented in the current worktree.
- Scope: fourteenth usable formatter milestone and a narrow extension of Structured multi-line expression alignment. It covers common ternary expressions without rewriting expression text, changing operator order, or attempting AST-level formatting.
- `FormatterService` now treats leading `?` and `:` continuation lines as alignment markers when a preceding multi-line assignment establishes a RHS start column.
- The behavior is enabled through the existing `FormatterOptions::alignContinuationOperators` Structured-profile pass and remains disabled for `Indent Only`.
- The pass still skips blank lines, preprocessor lines, block comments, and single-line assignments, and remains no-regex through deterministic token/comment/operator scans.
- Next Formatter milestones: Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 489 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Declaration Array Dimension Alignment MVP

- Status: implemented in the current worktree.
- Scope: fifteenth usable formatter milestone and a conservative RTL declaration readability step. It improves existing declaration-block alignment for unpacked arrays without changing expressions, splitting declarations, or reordering code.
- `FormatterService` now tracks declaration names separately from unpacked array suffixes, so simple signal and parameter/localparam declaration blocks can align suffixes such as `[3:0]` and `[DEPTH-1:0]` into a stable column.
- Existing assignment and trailing-comment alignment stays intact for parameter/localparam arrays, while scalar declarations avoid useless trailing padding.
- The behavior is part of the existing Structured declaration alignment pass and remains disabled for `Indent Only`.
- Implementation remains no-regex and uses deterministic declaration parsing already scoped to simple single-declaration lines.
- Next Formatter milestones: Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 495 checks; direct `gui_smoke_test` run with 414 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Parameter Port List Alignment MVP

- Status: implemented in the current worktree.
- Scope: sixteenth usable formatter milestone and a conservative module-header readability step. It extends existing declaration-column alignment to simple parameter port list rows without rewriting expressions or changing parameter order.
- `FormatterService` now accepts simple `parameter` / `localparam` declaration lines that end with `,` or with no terminator inside module `#(...)` lists, then preserves that original terminator when rebuilding aligned rows.
- Existing declaration alignment now aligns prefix, name, unpacked suffix, and assignment columns for those parameter rows, including array suffixes, while scalar rows avoid useless trailing padding.
- Signal and ANSI port declarations still require `;` for this declaration-alignment path, so comma-terminated port-list rows continue to use the dedicated port-list alignment rules instead of the parameter path.
- The behavior is part of the existing Structured declaration alignment pass and remains disabled for `Indent Only`.
- Implementation remains no-regex and uses deterministic token/declaration parsing already scoped to simple single-declaration lines.
- Next Formatter milestones: Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 502 checks; direct `gui_smoke_test` run with 416 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Single Statement Body Indent MVP

- Status: implemented in the current worktree.
- Scope: seventeenth usable formatter milestone and a conservative single-statement readability step. It changes leading whitespace only and does not rewrite expressions, add or remove `begin`, or restructure control flow.
- `FormatterOptions::indentSingleStatementBodies` stays enabled for both `Structured` and `Indent Only`, matching the policy that `Indent Only` may improve leading whitespace while structural alignment passes remain disabled.
- `FormatterService` now recognizes simple single-line `if` / `else if` / `else` / `for` / `foreach` / `while` / `repeat` headers that do not end a statement and do not already open `begin`, `fork`, or `case`, then indents the next real body line by one level.
- The pass skips preprocessor lines, block-comment/uncertain body lines, `else`, and closing-token lines, and runs before structural alignment so later alignment sees final indentation.
- Implementation remains no-regex and uses deterministic token/comment scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 513 checks; direct `gui_smoke_test` run with 417 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter RHS Continuation Indent MVP

- Status: implemented in the current worktree.
- Scope: eighteenth usable formatter milestone and a conservative multi-line expression readability step. It changes leading whitespace only and does not rewrite, split, merge, or reorder expressions.
- `FormatterOptions::indentAssignmentRhsContinuations` stays enabled for both `Structured` and `Indent Only`, matching the policy that `Indent Only` may improve leading whitespace while structural alignment passes remain disabled.
- `FormatterService` now detects lines that end with a top-level assignment operator and no RHS text, then indents following RHS continuation lines one level until the statement terminates.
- The behavior composes with delimiter continuation, so an RHS that begins with a call or brace expression still gets nested argument/element indentation relative to the RHS block.
- Preprocessor lines reset the RHS continuation state, and implementation remains no-regex through deterministic token/comment/operator scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer multi-line operand alignment, and safer parser-aware continuation decisions.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 526 checks; direct `gui_smoke_test` run with 418 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Call Argument Continuation Alignment MVP

- Status: implemented in the current worktree.
- Scope: nineteenth usable formatter milestone and a conservative Structured-profile continuation alignment step. It does not split, merge, reorder, or rewrite call arguments; it only adjusts leading whitespace for already-multiline call argument rows.
- `FormatterOptions::alignCallArgumentContinuations` is enabled for `Structured` and disabled for `Indent Only`, matching the existing split between structural alignment and pure indentation.
- `FormatterService` now tracks unmatched call `(` anchors and aligns following non-closing, non-operator continuation rows to the column after the opening parenthesis.
- The pass deliberately skips module/interface/class/function/task headers, `if`/loop/always control headers, leading-operator continuation rows, and named instance-map associations beginning with `.`, so existing port-list, instance-map, and operator/ternary formatters keep owning those surfaces.
- The behavior composes with RHS continuation indentation: an RHS beginning with a call can still receive one-level RHS indentation first, then Structured call arguments align to the call parenthesis.
- Implementation remains no-regex and uses deterministic token/comment/paren scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer parser-aware continuation decisions, and safer expansion beyond simple call-argument rows.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 538 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 420 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter SystemVerilog Block Boundary Indent MVP

- Status: implemented in the current worktree.
- Scope: twentieth usable formatter milestone and a conservative Indent Only-compatible SystemVerilog block-boundary expansion. It does not split, merge, reorder, or rewrite statements; it only adjusts leading whitespace for additional SV block forms.
- `FormatterService` now treats `program`, `primitive`, `checker`, `clocking`, `covergroup`, `property`, `sequence`, `specify`, and `table` as opening block tokens with matching `end...` closers.
- New block keywords that also appear in expressions are context-aware: `property` / `sequence` / `covergroup` / related block declarations count only as block headers, while `default clocking` is still accepted as a clocking-block header.
- This keeps `assert property (...)` from opening a fake formatter block, so assertion statements inside checker/program code do not push following `endchecker` / `endprogram` lines too deep.
- The behavior is active in both `Structured` and `Indent Only` profiles because it only changes leading whitespace. Existing Structured alignment may still align simple clocking `input` / `output` rows, while `Indent Only` leaves those columns unchanged.
- `completion_test` verifies Structured output, Indent Only output, idempotence, `default clocking`, property/sequence/covergroup/checker blocks, and the `assert property (...)` non-opening case.
- Implementation remains no-regex and uses deterministic code-token scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer parser-aware continuation decisions, and safer expansion beyond simple call-argument rows.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 542 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Procedural Body Indent MVP

- Status: implemented in the current worktree.
- Scope: twenty-first usable formatter milestone and a conservative leading-whitespace expansion for common procedural headers. It does not split, merge, reorder, or rewrite statements, and it does not add `begin` / `end`.
- `FormatterService` now treats single-line `always`, `always_comb`, `always_ff`, `always_latch`, `initial`, `final`, and `forever` headers without `begin`, `fork`, or `case` as single-statement body headers.
- The existing single-statement body indentation pass indents the following real body line by one level, composing with normal block indentation for constructs such as `initial begin` followed by `forever`.
- The behavior is active in both `Structured` and `Indent Only` profiles because it only changes leading whitespace.
- `completion_test` verifies Structured output, Indent Only output, idempotence, sequential/combinational procedural headers, `initial`, `final`, and nested `forever` body indentation.
- Implementation remains no-regex and uses deterministic code-token/comment scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer parser-aware continuation decisions, and safer expansion beyond simple single-statement body cases.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 546 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Formatter Multiline Header Body Indent MVP

- Status: implemented in the current worktree.
- Scope: twenty-second usable formatter milestone and a conservative continuation-aware body-indent expansion. It does not split, merge, reorder, or rewrite statements, and it does not add `begin` / `end`.
- `FormatterService` now recognizes single-statement control/procedural headers that start on one line and finish on a later line once delimiter balance returns to zero.
- The scan remains conservative: header continuations are rejected if they hit preprocessor lines, unterminated block comments, `begin`, `fork`, `case` / `casex` / `casez`, or statement terminators before the header closes.
- The existing body-indent pass now searches for the body after the detected header end line, so split `always_ff @(...)`, plain `always @(...)`, and split `if (...)` headers indent their following statement correctly.
- The behavior is active in both `Structured` and `Indent Only` profiles because it only changes leading whitespace and composes with the existing delimiter continuation indentation.
- `completion_test` verifies Structured output, Indent Only output, idempotence, split procedural event controls, and a split `if` header inside an `always_comb begin` block.
- Implementation remains no-regex and uses deterministic code-token/comment/delimiter scans.
- Next Formatter milestones: deeper Tree-sitter-backed structural formatting, richer parser-aware continuation decisions, and safer expansion beyond conservative header scans.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 550 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

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

### Post-L: Wave Preview Lane Summary MVP

- Status: implemented in the current worktree.
- Scope: first dense-report readability milestone for Wave Preview. It does not simulate values, expand workspace semantics, or change event extraction; it adds service-owned lane-level summaries so users can scan busy signals faster.
- `WavePreviewLaneSummary` records event count, unique source count, procedural block count, max cycle offset, and whether the lane contains continuous, combinational, or sequential activity.
- `WavePreviewService` computes the summary after assignment extraction and block-index fixing, keeping summary policy in the report layer instead of recomputing it in tree or canvas UI code.
- `WavePreviewPanelCoordinator` renders the summary in lane rows, lane hover details, the top panel summary through a busiest-lane hint, and compact canvas lane labels.
- Implementation remains no-regex and keeps Wave Preview positioned as a code-understanding sketch rather than a simulator.
- Next Wave Preview milestones: cross-file/semantic enrichment using existing semantic records, guard labels for more statement forms, and richer canvas interaction for dense reports.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 453 checks and `gui_smoke_test` with 407 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Loop Guard Labels MVP

- Status: implemented in the current worktree.
- Scope: second guard-context milestone for Wave Preview. It extends existing branch/case guard labels to loop/repeat control statements without evaluating iteration counts, branch truth, or signal values.
- `WavePreviewService` now detects assignment bodies under `for`, `foreach`, `while`, and `repeat` statements and records guard labels from the local control header.
- The existing Guard column, shared hover details, and canvas event labels consume the same `WavePreviewAssignment::guardText`, so UI rendering remains report-driven and does not parse RTL.
- Implementation remains no-regex and uses deterministic token scans plus existing statement-body range helpers.
- Next Wave Preview milestones: cross-file/semantic enrichment using existing semantic records, richer canvas interaction for dense reports, and additional guard context only where it can stay evidence-based.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 456 checks and `gui_smoke_test` with 408 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Ternary Guard Hints MVP

- Status: implemented in the current worktree.
- Scope: third guard-context milestone for Wave Preview. It surfaces top-level ternary conditions in assignment right-hand expressions as evidence labels without evaluating branch truth, signal values, or nested waveform behavior.
- `WavePreviewService` now detects top-level `?:` expressions in continuous and procedural assignment expressions and records a `?: <condition>` guard hint for the affected assignment.
- Existing guard text combines enclosing branch/case/loop labels with ternary hints, so the Guard column, shared hover details, and canvas labels remain report-driven and UI code does not parse RTL.
- Implementation remains no-regex and uses deterministic token scans bounded to the assignment expression.
- Next Wave Preview milestones: richer canvas interaction for dense reports, semantic relationship overlays where report-driven, and additional guard context only where evidence-based.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 491 checks; direct `gui_smoke_test` run with 414 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Semantic Context MVP

- Status: implemented in the current worktree.
- Scope: first semantic-enrichment milestone for Wave Preview. It still does not simulate values, elaborate waveforms, or scan workspace files; it consumes the already-published semantic snapshot as optional context.
- `WavePreviewQuery` now carries an optional `SemanticIndexSnapshot`. `WavePreviewPanelCoordinator` passes `SemanticIndex::getInstance()->snapshot()` when refreshing the dock.
- `WavePreviewService` gathers target and source signal names from the generated waveform-sketch report, asks the supplied snapshot for definition records, and upserts port/signal context for names that are missing from the current-document declaration scan.
- Local declaration context remains dominant: semantic snapshot records can fill missing context and port directions, but ordinary semantic signal records do not overwrite a current-document `internal` declaration.
- Existing report consumers benefit through the same `signalContexts`, lane context, tree Context column, and shared hover details; UI code does not parse RTL or perform semantic lookups itself.
- Implementation remains no-regex and uses deterministic token scans plus existing semantic records.
- Next Wave Preview milestones: richer canvas interaction for dense reports, additional guard context only where evidence-based, and later semantic relationship overlays if they can stay report-driven.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 481 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Canvas Navigation MVP

- Status: implemented in the current worktree.
- Scope: first direct canvas-interaction milestone after the graphical Wave Preview sketch. It improves inspection flow without adding simulation, semantic extraction, or workspace scanning.
- `WavePreviewCanvas` now keeps hit-test records for painted assignment blocks that include tooltip text plus the report-owned assignment line/column evidence.
- Hovering a canvas event block shows the existing detail tooltip and a pointing cursor; double-clicking the block calls the coordinator navigation handler with the current report file and assignment location.
- `WavePreviewPanelCoordinator` remains the owner of navigation routing, matching the tree row double-click path. Canvas code only renders report data and performs hit testing.
- Implementation remains no-regex and does not parse RTL in UI code.
- Next Wave Preview milestones: richer dense-canvas interaction, additional evidence-based guard context, and later semantic relationship overlays if they can stay report-driven.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 416 checks against `test_sv/new` plus `test_sv/test_symbols.sv`; direct `completion_test` run with 495 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Canvas Event Selection MVP

- Status: implemented in the current worktree.
- Scope: second direct canvas-interaction milestone and a dense-report inspection step. It improves event focus without adding simulation, semantic extraction, or workspace scanning.
- `WavePreviewCanvas` now keeps a selected assignment key from single-click hit testing, redraws the selected block with a stronger outline, and clears the selection when the user clicks empty canvas space.
- `WavePreviewPanelCoordinator` receives the canvas selection text and pins a concise selected-event summary in the Wave Preview summary label, including target, timing, guard/source context, clock/reset context, and location. Empty selection restores the report summary text.
- Double-click navigation and hover tooltips continue to use the same report-owned assignment evidence as before; canvas code still only renders report data and performs hit testing.
- Implementation remains no-regex and does not parse RTL in UI code.
- Next Wave Preview milestones: richer dense-canvas interaction, additional evidence-based guard context, and later semantic relationship overlays if they can stay report-driven.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 417 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Lane Guard Summary MVP

- Status: implemented in the current worktree.
- Scope: dense-report readability milestone for Wave Preview. It does not add new parsing, value simulation, semantic extraction, or workspace scanning; it aggregates existing assignment guard evidence into lane-level report data.
- `WavePreviewLaneSummary` now carries unique `guardTexts` collected from assignments in that lane, preserving report order and keeping assignment-level guard labels unchanged.
- `WavePreviewPanelCoordinator` renders lane-level guard summaries in the Guard column for lane rows, includes guard count in `laneSummaryText()`, and adds the compact guard list to lane hover details.
- Canvas lane labels continue to consume `laneSummaryText()`, so dense canvas rows gain guard count context without canvas-side RTL parsing.
- The compact guard display shows the first two guards and a `+N more` suffix for busy lanes, keeping tree and canvas text bounded.
- Implementation remains no-regex and report-driven.
- Next Wave Preview milestones: richer dense-canvas interaction, additional evidence-based guard context, and later semantic relationship overlays if they can stay report-driven.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 522 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 418 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Activity Mix Summary MVP

- Status: implemented in the current worktree.
- Scope: dense-report readability milestone for Wave Preview. It does not change token scanning, assignment extraction, semantic enrichment, workspace scanning, or value simulation; it summarizes assignment kinds that already exist in the report.
- `WavePreviewLaneSummary` now carries exact `continuousEventCount`, `combinationalEventCount`, and `sequentialEventCount` values alongside the existing boolean activity flags.
- `WavePreviewService` computes those counts while refreshing lane summaries, keeping activity-mix policy in the report layer rather than recomputing it in tree, hover, or canvas UI code.
- `WavePreviewPanelCoordinator` renders activity counts as compact `assign N`, `comb N`, and `seq N` labels in lane activity text, lane summary text, lane hover details, the top busiest-lane hint, and compact canvas lane labels.
- Existing event rows remain unchanged: assignment kind, timing, guard, source, clock/reset, context, and navigation evidence still come from `WavePreviewAssignment`.
- Implementation remains no-regex and keeps Wave Preview positioned as a code-understanding sketch rather than a simulator.
- Next Wave Preview milestones: richer dense-canvas interaction, semantic relationship overlays where report-driven, and additional evidence-based guard/context summaries that stay out of UI-side parsing.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 534 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 420 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Report Activity Summary MVP

- Status: implemented in the current worktree.
- Scope: report-level readability and extraction-accuracy milestone for Wave Preview. It does not simulate values, evaluate branch truth, scan workspace files, change semantic enrichment, or alter canvas hit-test/navigation behavior.
- `WavePreviewReport` now carries a service-owned `WavePreviewActivitySummary` with total, continuous assign, blocking/combinational, and nonblocking/sequential event counts.
- `WavePreviewService` computes the report activity summary from parsed lane assignments after lane cleanup, keeping report-level activity policy outside tree, tooltip, and canvas rendering code.
- Procedural assignment extraction now ignores candidate lvalues while inside parenthesized/bracketed/braced expression regions, so `for (int i = 0; ...)` loop-counter initialization is not treated as a waveform event or busiest lane.
- `WavePreviewPanelCoordinator` renders the report activity mix in the top summary label and in a report-level `Activity Mix` tree row, while existing lane/event rows continue to render report-owned lane and assignment data.
- `gui_smoke_test` verifies the `Activity Mix` row, top summary text, and the corrected three-event mix for a fixture containing continuous, combinational, and sequential assignments plus a `for` loop header.
- Implementation remains no-regex and keeps Wave Preview positioned as a code-understanding sketch rather than a simulator.
- Next Wave Preview milestones: richer dense-canvas interaction, semantic relationship overlays where report-driven, and additional evidence-based guard/context summaries that stay out of UI-side parsing.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 428 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Canvas Lane Interaction MVP

- Status: implemented in the current worktree.
- Scope: dense-canvas interaction milestone for Wave Preview. It does not change assignment extraction, value/timing simulation policy, semantic enrichment, tree rendering data, or double-click assignment navigation.
- `WavePreviewCanvas` now builds lane hit targets for each rendered lane row in addition to assignment block hit targets.
- Event block hit testing remains first, so assignment hover/click/double-click behavior keeps priority when an event block overlaps the lane row.
- Hovering a canvas lane row shows the same report-owned lane detail tooltip used by the tree, including signal context, lane summary, activity mix, guards, and sources.
- Clicking a canvas lane row highlights that row and pins a concise `Selected lane ...` summary through the existing selection callback, while clicking empty canvas space restores the report summary.
- `gui_smoke_test` verifies q-lane hover tooltip content and q-lane click summary content after the existing event-hover, event-selection, and double-click navigation coverage.
- Implementation remains no-regex and keeps canvas code render/hit-test only; lane text is derived from `WavePreviewLane` / `WavePreviewLaneSummary`.
- Next Wave Preview milestones: richer dense-canvas interaction, semantic relationship overlays where report-driven, and additional evidence-based guard/context summaries that stay out of UI-side parsing.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 430 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Wave Preview Lane Warning Summary MVP

- Status: implemented in the current worktree.
- Scope: report-level readability milestone for Wave Preview. It does not change assignment extraction, semantic enrichment, canvas hit testing, or waveform simulation policy.
- `WavePreviewService` now populates `WavePreviewReport::warnings` from service-owned lane summaries after extraction and lane summary refresh.
- Warnings are intentionally lightweight evidence hints: lanes that mix continuous assign, combinational blocking, and/or sequential nonblocking activity are flagged, and lanes assigned from multiple procedural blocks are flagged.
- Warning text explicitly preserves the non-simulator contract by saying Wave Preview does not resolve writer priority; it asks users to inspect block ownership instead of pretending to prove final waveform values.
- `WavePreviewPanelCoordinator` renders the warning count in the top report summary and adds a `Warnings` tree section with one child row per service-owned warning.
- `completion_test` verifies service warning generation for mixed assign/comb/seq and multi-procedural-block lanes; `gui_smoke_test` verifies the warnings tree and summary count in the dock.
- Implementation remains no-regex and report-driven; UI code consumes `WavePreviewReport::warnings` and does not derive lane warning policy.
- Next Wave Preview milestones: richer dense-canvas interaction, semantic relationship overlays where report-driven, and additional evidence-based guard/context summaries that stay out of UI-side parsing.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 553 checks; direct `gui_smoke_test` run with 434 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

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

### Post-L: Huge Workspace Priority Bands MVP

- Status: implemented in the current worktree.
- Scope: twelfth usable Huge Workspace Mode milestone. It does not split analysis execution into separate batches yet; it makes the priority tiers explicit and testable so later tiered scheduling/index work has a concrete contract.
- `WorkspaceAnalysisPlan` now carries four ordered file bands: current-file priority, dirty-open priority, clean-open priority, and background files.
- `WorkspaceAnalysisPlanService` owns band construction and planned file order. Dirty open files remain protected from stale disk-backed publication, and files only appear in one band.
- `AnalysisProgressCoordinator` renders the band counts in the workspace plan Activity/Output message and status-bar summary without recomputing tier policy.
- Focused regressions cover the planned order, each priority band, external-current-file behavior, scheduler signal propagation, and Activity/Output plus status rendering for band counts.
- Next Huge Workspace milestones: using these explicit bands for staged/incremental publication, deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, and richer direct cancellation controls for long analysis runs.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 455 checks; direct `gui_smoke_test` run with 407 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Band Progress Visibility MVP

- Status: implemented in the current worktree.
- Scope: thirteenth usable Huge Workspace Mode milestone. It does not change execution order, batching, cancellation, or publication; it carries the existing priority-band contract into user-visible progress.
- `AnalysisProgressCoordinator` now remembers the latest prepared plan as a normalized file-to-band lookup for `current`, `dirty-open`, `open`, and `background`.
- Symbol-analysis status messages include the band for the current file when known, and Activity/Output progress checkpoints include the same band next to the file name.
- Priority ownership remains in `WorkspaceAnalysisPlanService`; progress rendering consumes the prepared plan instead of recomputing active/open/background policy.
- Focused GUI smoke coverage verifies both status-bar and Activity/Output band rendering from a prepared workspace plan.
- Next Huge Workspace milestones: using explicit bands for staged/incremental publication, deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, and richer direct cancellation controls for long analysis runs.
- Verification for this block: focused build targets `completion_test` and `gui_smoke_test`; direct `completion_test` run with 459 checks; direct `gui_smoke_test` run with 410 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Staged Symbol Publication MVP

- Status: implemented in the current worktree.
- Scope: fourteenth usable Huge Workspace Mode milestone. It is the first staged publication step: symbols from the priority segment can become visible before background files finish publishing, while Slang extraction, diagnostics extraction, relationship analysis, and final diagnostics replacement remain whole-workspace operations.
- `WorkspaceSymbolAnalysisController` passes `WorkspaceAnalysisPlan::priorityFileCount` into `SymbolAnalyzer` as a publication boundary after the plan has already been prepared by `WorkspaceAnalysisPlanService`.
- `SymbolAnalyzer::publishWorkspaceAnalysisResult` publishes a diagnostics-preserving `SemanticIndexSnapshot` after the priority segment when at least one unprotected priority file was updated. Dirty protected files still skip stale disk publication exactly as before.
- The final workspace publication still replaces diagnostics after every publishable file result has been processed, so partial priority snapshots do not expose half-built diagnostics.
- Focused completion coverage creates a two-file temporary workspace, sets the priority boundary to the first file, and proves the intermediate snapshot contains the priority module while omitting the background module; the final snapshot then contains both.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, richer direct cancellation controls for long analysis runs, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 464 checks; direct `gui_smoke_test` run with 410 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Band-Staged Symbol Publication MVP

- Status: implemented in the current worktree.
- Scope: fifteenth usable Huge Workspace Mode milestone. It refines staged symbol publication from one combined priority segment into explicit current-file, dirty-open, and clean-open priority-band checkpoints. It still does not split Slang extraction, diagnostics extraction, relationship analysis, or final diagnostics replacement.
- `WorkspaceAnalysisPlan` now carries `priorityPublicationCheckpoints`, a cumulative list of planned-file positions after each non-empty priority band.
- `WorkspaceAnalysisPlanService` owns checkpoint construction next to band construction, so publication policy consumes the prepared plan instead of recomputing current/open/dirty status inside `SymbolAnalyzer`.
- `WorkspaceSymbolAnalysisController` passes the checkpoint list into `SymbolAnalyzer`; `SymbolAnalyzer` clamps and deduplicates the list and publishes diagnostics-preserving snapshots whenever a checkpoint is crossed and at least one new publishable file has been updated.
- Dirty protected files still skip stale disk publication. Crossing a checkpoint that only contains protected files does not create a duplicate snapshot with no new symbols.
- Focused completion coverage verifies plan checkpoints (`1,2,3`) and a three-file staged publication flow: the first intermediate snapshot contains only the current-band module, the second contains current plus open-band modules, and the final snapshot contains the background module too.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, richer direct cancellation controls for long analysis runs, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 467 checks; direct `gui_smoke_test` run with 410 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Cancellation Checkpoint MVP

- Status: implemented in the current worktree.
- Scope: sixteenth usable Huge Workspace Mode milestone. It adds a deeper cancellation checkpoint around workspace result assembly and diagnostics publication. It does not claim to interrupt Slang's internal workspace symbol extraction yet.
- `WorkspaceAnalysisResult` now carries an explicit `cancelled` flag. `SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult` sets it when the cancel provider trips while file results are being assembled.
- Synchronous workspace analysis and asynchronous workspace analysis now treat canceled results as expired work: no partial symbol snapshot is published, diagnostics extraction is skipped when cancellation is observed before it starts, and watcher delivery emits the existing `workspaceAnalysisExpired` path.
- The async pipeline checks cancellation before symbol extraction, before result assembly, before diagnostics extraction, and after diagnostics extraction before publication, so later milestones can keep adding narrower interrupt points without changing the publication contract again.
- Focused completion coverage creates a two-file temporary workspace, trips cancellation during result assembly, and proves the run expires with zero completed symbols and no published workspace snapshot.
- Next Huge Workspace milestones: deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, richer direct cancellation controls for long analysis runs, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 472 checks; direct `gui_smoke_test` run with 410 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Direct Symbol Cancellation MVP

- Status: implemented in the current worktree.
- Scope: seventeenth usable Huge Workspace Mode milestone. It adds an explicit non-blocking workspace symbol cancellation route through scheduler/controller and progress visibility. It does not add a toolbar or global-control command yet and does not claim to interrupt Slang internals directly.
- `WorkspaceAnalysisRequestQueue::cancel()` clears active and pending requests while preserving active wait, pending wait, and discarded pending update count telemetry.
- `WorkspaceSymbolAnalysisController::cancelWorkspaceAnalysis()` uses queue cancellation, clears active project state, emits `workspaceSymbolAnalysisCancelled`, and expires the current async workspace run through `SymbolAnalyzer::expireWorkspaceAnalysis()` without waiting on the worker.
- `AnalysisScheduler::cancelWorkspaceAnalysis()` exposes the direct cancellation entrypoint, and `AnalysisProgressCoordinator` renders cancellation as warning-level Activity/Output plus status-bar feedback while setting the symbol-analysis cancellation flag used by the cancel provider.
- Focused completion coverage verifies queue cancellation clears active/pending state while preserving discarded telemetry. Focused GUI smoke coverage verifies Activity/status visibility and the cancellation flag.
- Next Huge Workspace milestones: UI command surface for direct cancellation if wanted, deeper interrupt points inside Slang-backed extraction where available, tiered symbol indexes, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `completion_test` and `gui_smoke_test`; direct `completion_test` run with 474 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Slang Boundary Cancellation MVP

- Status: implemented in the current worktree.
- Scope: eighteenth usable Huge Workspace Mode milestone. It lowers workspace symbol-analysis cancellation checks into Slang boundary code without claiming to preempt Slang internals while `fromFiles` or AST elaboration is actively running.
- `SlangManager::extractWorkspaceSymbolRecords()` and `SlangManager::extractWorkspaceDiagnostics()` now accept an optional cancel provider and check it during workspace file-list preparation, after Slang `fromFiles`, before collector/diagnostics work, and while appending diagnostics.
- `slang_symbols::collectSymbolRecords()` accepts the same optional cancel provider. Its definition pass, port-internal pass, and main AST visitor stop descending when cancellation is observed, and canceled collection clears partial records before returning to the analyzer.
- `SymbolAnalyzer` passes the active workspace cancel provider into Slang symbol and diagnostics extraction. Synchronous `analyzeProject()` now emits `workspaceAnalysisExpired` when cancellation is observed immediately after symbol extraction, matching the existing no-partial-publication contract.
- Focused completion coverage verifies canceled Slang workspace symbol extraction returns no partial records, canceled Slang workspace diagnostics returns no diagnostics from a known-bad fixture, and the analyzer's workspace cancellation path still expires before publication.
- Next Huge Workspace milestones: even narrower interrupt points where Slang exposes them, tiered symbol indexes, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 478 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Relationship Boundary Cancellation MVP

- Status: implemented in the current worktree.
- Scope: nineteenth usable Huge Workspace Mode milestone. It lowers workspace relationship-analysis cancellation checks into Slang boundary code and relationship visitor traversal without claiming to preempt Slang internals while `fromFiles` or AST elaboration is actively running.
- `SlangManager::extractWorkspaceRelationshipInfo()` now accepts an optional cancel provider and checks it during grouped-file setup, workspace path preparation, after Slang `fromFiles`, before/after compilation root access, during relationship visitor traversal, and while grouping extracted facts by source file.
- `SmartRelationshipBuilder::extractWorkspaceRelationshipInfo()` forwards its `isCancelled()` state to `SlangManager`, so `RelationshipAnalysisWorker` can stop earlier when workspace relationship analysis is cancelled before the per-file compute loop starts.
- Canceled workspace relationship extraction returns the existing normalized per-file buckets with empty `RelationshipExtractionInfo` payloads rather than leaking partial module, assignment, condition, timing, or call facts.
- Focused relationship coverage verifies a temporary two-file workspace extracts relationship facts normally, canceled Slang workspace relationship extraction returns empty facts after reaching the cancel boundary, and a cancelled `SmartRelationshipBuilder` forwards cancellation into workspace extraction.
- Next Huge Workspace milestones: even narrower interrupt points where Slang exposes them, tiered symbol indexes, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 772 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Relationship Cancelled Result Contract MVP

- Status: implemented in the current worktree.
- Scope: twentieth usable Huge Workspace Mode milestone. It closes the relationship-publication side of workspace cancellation. It does not add new UI controls or deeper Slang preemption; it makes cancelled relationship worker results explicit and non-publishable.
- `WorkspaceRelationshipAnalysisResult` now carries a `cancelled` flag. `RelationshipAnalysisWorker::analyzeWorkspace()` marks the result cancelled when cancellation is observed before extraction, after workspace relationship extraction, before/after per-file compute, or while assembling the workspace relationship snapshot.
- Cancelled worker results clear partial file relationships and keep `semanticSnapshot` on the base snapshot so no half-built relationship state is carried forward.
- `RelationshipAnalysisController::handleWorkspaceFinished()` emits `workspaceRelationshipAnalysisCancelled` for cancelled worker results before calling the publisher, and `RelationshipResultPublisher::applyWorkspaceResult()` refuses cancelled workspace results as a second guard.
- `RelationshipAnalysisController::requestWorkspaceAnalysis()` now resets relationship-builder cancellation before launching the async worker, keeping reset ownership at the controller boundary while the worker respects the active state it receives.
- Focused relationship coverage verifies a pre-cancelled worker result is marked cancelled, carries no partial relationships, preserves the base snapshot, and is rejected by `RelationshipResultPublisher`.
- Next Huge Workspace milestones: tiered symbol indexes, relationship-result telemetry for canceled/expired runs if needed, and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 774 checks; direct `gui_smoke_test` run with 412 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Relationship Result Telemetry MVP

- Status: implemented in the current worktree.
- Scope: twenty-first usable Huge Workspace Mode milestone. It makes workspace relationship result cost and yield visible without changing relationship extraction, publication, or cancellation semantics.
- `WorkspaceRelationshipAnalysisResult` now carries `processedFiles`, `relationshipCount`, and `elapsedMs` alongside `totalFiles` and `cancelled`.
- `RelationshipAnalysisWorker::analyzeWorkspace()` records elapsed time, increments processed-file count only for files that were read and relationship-processed, and counts publishable semantic relationships before building the merged relationship snapshot.
- Cancelled worker results keep the same telemetry fields while clearing partial `fileRelationships` and restoring the base snapshot, so later UI/reporting can distinguish zero work from partial work without publishing stale facts.
- `AnalysisProgressCoordinator::showRelationshipAnalysisFinished()` writes Activity/Output and status summaries with processed/total files, relationship count, and elapsed time.
- Next Huge Workspace milestones: tiered symbol indexes and eventually per-band diagnostics or relationship publication if those can be made semantically safe.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 777 checks; direct `gui_smoke_test` run with 414 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace File-Band Index MVP

- Status: implemented in the current worktree.
- Scope: twenty-second usable Huge Workspace Mode milestone and the first small tiered-index groundwork step. It does not change Slang extraction, symbol publication checkpoints, diagnostics publication, or relationship analysis.
- `WorkspaceAnalysisPlan` now carries `fileBandsByNormalizedPath`, a normalized file-to-band index keyed by planned workspace file path. `WorkspaceAnalysisPlan::bandForFile()` exposes the same lookup for callers that should not know how the index is keyed.
- `WorkspaceAnalysisPlanService` owns construction of the index next to current/dirty-open/clean-open/background band construction, so priority-tier ownership stays in the plan service.
- `AnalysisProgressCoordinator` now consumes the plan-owned file-band index for workspace progress labels instead of rebuilding a parallel map from the individual band lists.
- This is groundwork for tiered symbol indexes and per-band query/report policy; it intentionally leaves semantic snapshot content and diagnostics replacement semantics unchanged.
- Next Huge Workspace milestones: tier-aware symbol/query metadata, per-band diagnostics if semantically safe, and eventually richer tiered index publication that preserves dirty/open-file protection.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 498 checks; direct `gui_smoke_test` run with 416 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Band Summary Metadata MVP

- Status: implemented in the current worktree.
- Scope: twenty-third usable Huge Workspace Mode milestone and the next tier-aware metadata step after file-band indexing. It does not change Slang extraction, symbol publication checkpoints, diagnostics publication, or relationship analysis.
- `WorkspaceAnalysisPlan` now carries `bandSummaries` rows for the current, dirty-open, open, and background tiers. Each row includes the stable band label, display name, file count, priority flag, and cumulative publication checkpoint when the band is part of the priority segment.
- `WorkspaceAnalysisPlan::bandSummaryText()` provides a plan-owned Activity/status text fragment, with a fallback for tests or callers that construct plans manually.
- `WorkspaceAnalysisPlanService` derives `priorityPublicationCheckpoints` from the same band summary metadata, keeping checkpoint ownership next to tier construction instead of scattering count policy.
- `AnalysisProgressCoordinator` now renders the plan-owned band summary text rather than formatting tier counts from individual band lists.
- This is groundwork for tier-aware symbol/query metadata and richer tiered index publication while preserving dirty/open-file protection and current diagnostics semantics.
- Next Huge Workspace milestones: tier-aware symbol/query metadata, per-band diagnostics if semantically safe, and eventually richer tiered index publication that preserves dirty/open-file protection.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 509 checks; direct `gui_smoke_test` run with 416 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Tier-Aware Symbol Metadata MVP

- Status: implemented in the current worktree.
- Scope: twenty-fourth usable Huge Workspace Mode milestone and the first query-visible tier metadata step. It does not change Slang extraction, publication checkpoints, diagnostics replacement, relationship analysis, or UI ordering.
- `WorkspaceAnalysisPlan` now carries `fileBandMetadataByNormalizedPath`, mapping each planned workspace file to a stable band label, display name, priority flag, and publication checkpoint. `WorkspaceAnalysisPlan::bandMetadataForFile()` exposes that plan-owned lookup without making callers inspect map keys.
- `WorkspaceSymbolAnalysisController` converts the plan-owned file-band metadata into generic `SemanticAnalysisBandMetadata` and passes it to `SymbolAnalyzer` with the current workspace analysis request.
- `WorkspaceAnalysisResult` carries the file-band metadata for the same analysis generation, and `SymbolAnalyzer::publishWorkspaceAnalysisResult()` installs it in `SemanticIndex` before staged or final symbol publication.
- `SemanticIndex` annotates returned `SemanticSymbolRecord` objects and published `SemanticIndexSnapshot` records with the current/dirty-open/open/background analysis band for their source file. Protected dirty-open records that are preserved from open-document analysis still receive tier metadata at query/publication time.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService`; query code consumes metadata rather than recomputing priority tiers.
- Next Huge Workspace milestones: using symbol tier metadata for query ordering/report display where helpful, per-band diagnostics if semantically safe, and eventually richer tiered index publication that preserves dirty/open-file protection.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 516 checks; direct `gui_smoke_test` run with 417 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Tier-Aware Query Ordering MVP

- Status: implemented in the current worktree.
- Scope: twenty-fifth usable Huge Workspace Mode milestone and the first consumer of tier-aware symbol metadata. It does not change extraction, publication, diagnostics, relationship analysis, or UI rendering; it only improves service/query ordering when existing semantic match quality is otherwise equal.
- `semanticAnalysisBandSortPriority()` defines the shared ordering contract: `current`, `dirty-open`, `open`, `background`, then unknown/unbanded records.
- `SemanticIndex::searchSymbols()` uses analysis-band priority after match score and before file/line/name tie-breakers, so large-workspace searches keep current/open results prominent without hiding better textual matches.
- `SemanticIndexSnapshot::sortedDefinitionRecords()` and native fallback definition sorting use the same analysis-band priority after context/module/global definition scoring, preserving exact local context preference while improving equal-priority tie-breaks.
- `SemanticIndex` completion-record queries, generic completion-name lists, `CompletionSymbolQuery::namesFromRecords()`, and `CompletionSemanticQuery::typedSymbolRecords()` now sort by analysis-band priority and then case-insensitive name. Duplicate-name selection keeps the best available band for that name.
- Implementation remains no-regex and consumes the metadata produced by `WorkspaceAnalysisPlanService` through `SemanticIndex`; query code does not recompute workspace priority bands.
- Next Huge Workspace milestones: showing tier/report provenance where helpful, per-band diagnostics if semantically safe, and eventually richer tiered index publication that preserves dirty/open-file protection.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 519 checks; direct `gui_smoke_test` run with 417 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Analysis Band Report MVP

- Status: implemented in the current worktree.
- Scope: twenty-sixth usable Huge Workspace Mode milestone and a report/provenance visibility step. It does not change extraction order, symbol publication, diagnostics, relationship analysis, or query ranking; it exposes the tier metadata already produced by the analysis plan and consumed by query ordering.
- `SemanticAnalysisBandReport` summarizes returned semantic records by analysis band, including label, display name, priority flag, publication checkpoint, symbol count, file count, file list, total symbol count, total file count, and a compact summary string.
- `SemanticIndex::analysisBandReport()` and `SemanticIndexSnapshot::analysisBandReport()` provide the report from live native/snapshot records through the same record-native query boundary instead of making UI or completion code inspect maps or recompute priority tiers.
- `CompletionResult::SemanticCompletionItem`, `CompletionModel::CompletionItem`, and `CommandSymbolCompletionItem` now carry `analysisBand` plus `analysisBandDisplayName`; symbol completion tooltips append `band: <name>` when provenance is available.
- Implementation remains no-regex and keeps tier ownership in `WorkspaceAnalysisPlanService` / `SemanticIndex`; consumers receive provenance as data rather than policy.
- Next Huge Workspace milestones: per-band diagnostics if semantically safe, richer tiered index publication, and UI/report surfaces that consume `SemanticAnalysisBandReport` without recomputing priority policy.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 533 checks; direct `gui_smoke_test` run with 418 checks against `test_sv/new` plus `test_sv/test_symbols.sv`. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Analysis Band Activity Visibility MVP

- Status: implemented in the current worktree.
- Scope: twenty-seventh usable Huge Workspace Mode milestone and the first Activity/Output consumer of `SemanticAnalysisBandReport`. It does not change extraction order, staged publication, diagnostics, relationship analysis, or query ranking.
- `AnalysisProgressCoordinator::handleWorkspaceSymbolAnalysisFinished()` now preserves the existing status-bar message and relationship-stage startup, then appends a parsed-file/symbol Activity entry enriched with the analysis-band report for the finished project files.
- The report is generated from `ProjectSnapshot::systemVerilogFiles`, so Activity visibility stays scoped to the just-finished project instead of accidentally including unrelated native records left in `SemanticIndex`.
- `gui_smoke_test` covers the user-visible Activity path by publishing temporary current/background records, invoking the finish handler, and checking the logged `bands current ... background ...` summary while cleaning the temporary records and band metadata afterward.
- Implementation remains no-regex and keeps tier policy outside UI: the coordinator consumes `SemanticAnalysisBandReport` data rather than recomputing priority tiers.
- Next Huge Workspace milestones: per-band diagnostics if semantically safe, richer tiered index publication, and additional UI/report surfaces that consume `SemanticAnalysisBandReport` without recomputing priority policy.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 420 checks; focused build target `completion_test`; direct `completion_test` run with 533 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Report MVP

- Status: implemented in the current worktree.
- Scope: twenty-eighth usable Huge Workspace Mode milestone and a safe diagnostic-report groundwork step. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, sorting, filtering, or Problems UI rendering.
- `DiagnosticResult` now carries `analysisBand` and `analysisBandDisplayName` derived from `SemanticIndex::analysisBandForFile()` for the diagnostic file.
- `DiagnosticReport` now exposes `analysisBandCounts` and `analysisBandGroups`, grouping diagnostics by current/dirty-open/open/background/unbanded provenance with per-band diagnostic rows and severity counts.
- Unbanded diagnostics are grouped as `unbanded`, so external or non-workspace diagnostics remain visible without pretending to belong to a priority tier.
- `relationship_test` verifies current/background diagnostic grouping, band counts, severity counts, and row-level analysis-band metadata; `gui_smoke_test` verifies existing Problems-panel behavior remains stable.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; diagnostics consume band metadata as report data rather than recomputing workspace tiers.
- Next Huge Workspace milestones: UI/report surfaces that consume diagnostic band groups where useful, per-band diagnostics publication only if semantically safe, and richer tiered index publication that preserves dirty/open-file protection.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 782 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 420 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Activity Visibility MVP

- Status: implemented in the current worktree.
- Scope: twenty-ninth usable Huge Workspace Mode milestone and the first Activity/Output consumer of diagnostic analysis-band groups. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, Problems sorting/filtering/grouping, or per-band diagnostics publication.
- `DiagnosticReport::analysisBandSummaryText()` now formats the report-owned band summary with per-band diagnostic counts and severity mix, including the existing `unbanded` fallback for diagnostics outside known workspace tiers.
- `ProblemsPanelCoordinator` records a de-duplicated `Analyzer` Activity/Output entry when visible diagnostics have a non-empty report, using the service-owned summary instead of recomputing priority tiers or severity counts in UI code.
- Repeated refreshes with the same visible diagnostic summary do not spam Activity/Output; clearing diagnostics resets the last summary so a later diagnostic state can be reported again.
- `relationship_test` verifies the service-owned summary text for current/background diagnostic groups, and `gui_smoke_test` verifies the Problems-panel Activity path with temporary current/background diagnostic metadata.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; Problems UI consumes `DiagnosticReport` data rather than deriving scheduling policy.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional report surfaces that consume diagnostic band groups without recomputing priority policy.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 783 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 421 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Problems Column MVP

- Status: implemented in the current worktree.
- Scope: thirtieth usable Huge Workspace Mode milestone and the first direct Problems-panel diagnostic-band surface. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, sorting/filtering behavior, Activity logging semantics, or per-band diagnostics publication.
- `DiagnosticService::findDiagnostics()` now normalizes missing row-level band metadata to the existing `unbanded` fallback, so diagnostic rows have a stable band display name even for external or non-workspace diagnostics.
- `ProblemsPanelCoordinator` adds a `Band` column to diagnostic rows and file groups, rendering `DiagnosticResult::analysisBandDisplayName` from the service-owned report data while preserving existing navigation metadata on column 0.
- File groups use the first diagnostic row's band display, which is safe because groups are keyed by file and diagnostic band metadata is file-based.
- `gui_smoke_test` verifies the Band column header and rendered current/background rows through a temporary Problems panel, while existing Problems navigation and grouping smoke coverage continues to pass.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; Problems UI displays band data but does not derive scheduling policy.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional report surfaces that consume diagnostic band groups without recomputing priority policy.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 783 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 423 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Filter MVP

- Status: implemented in the current worktree.
- Scope: thirty-first usable Huge Workspace Mode milestone and the first query-level diagnostic-band narrowing path for Problems. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, diagnostic sorting, Activity logging semantics, or per-band diagnostics publication.
- `DiagnosticQuery` and `DiagnosticPanelQueryOptions` now carry `analysisBandLabel`, normalized through `DiagnosticService` alongside file and workspace filters.
- `DiagnosticService::findDiagnostics()` applies the band filter after row-level analysis-band normalization, so `unbanded` is filterable for external or non-workspace diagnostics and UI callers do not need special cases.
- `ProblemsPanelCoordinator` adds a `Diagnostic band` combo next to scope and severity with `All Bands`, `Current`, `Dirty Open`, `Open`, `Background`, and `Unbanded` choices; it passes the selected label into the service query instead of filtering rows locally.
- `relationship_test` verifies direct report filtering and panel-query propagation for current/background bands, and `gui_smoke_test` verifies the combo narrows a temporary Problems panel to the background diagnostic row.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; Problems UI requests a band, while `DiagnosticService` owns the report narrowing.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional report surfaces that consume diagnostic band groups without recomputing priority policy.
- Verification for this block: focused build target `relationship_test`; direct `relationship_test` run with 787 checks; focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 424 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Count Labels MVP

- Status: implemented in the current worktree.
- Scope: thirty-second usable Huge Workspace Mode milestone and a Problems-panel visibility refinement. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, diagnostic sorting, band filtering semantics, Activity logging semantics, or per-band diagnostics publication.
- `ProblemsPanelCoordinator` now refreshes each diagnostic-band combo label with counts from a service-owned `DiagnosticReport` computed for the current scope and severity filters.
- The count query intentionally clears only `analysisBandLabel`, so selecting `Background` still leaves labels such as `Current (2)` and `Background (1)` based on the unbanded count universe for the current scope/severity view.
- The combo stores fixed base labels in item data and blocks signals while rendering count text, so count updates do not corrupt filter data or trigger recursive Problems refreshes.
- `gui_smoke_test` verifies the rendered `All Bands` / `Current` / `Background` count labels and verifies that selecting the background band keeps the unfiltered per-band counts visible.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; Problems UI consumes `DiagnosticReport::analysisBandCounts` and does not derive scheduling policy.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional report surfaces that consume diagnostic band groups without recomputing priority policy.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 426 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Diagnostic Band Severity Tooltip MVP

- Status: implemented in the current worktree.
- Scope: thirty-third usable Huge Workspace Mode milestone and a Problems-panel diagnostic-band visibility refinement. It does not change Slang diagnostic extraction, diagnostics replacement, debounce/coalescing, diagnostic sorting, band filtering semantics, Activity logging semantics, or per-band diagnostics publication.
- `DiagnosticAnalysisBandGroup` now exposes a service-owned `summaryText()` so single-band display strings use the same count/severity mix logic as `DiagnosticReport::analysisBandSummaryText()`.
- `ProblemsPanelCoordinator` now updates each diagnostic-band combo item's tooltip/status/accessibility text from the unbanded count report computed for the current scope and severity filters.
- The tooltip query intentionally clears only `analysisBandLabel`, so selecting `Background` keeps `Current` and `Background` tooltips based on the unfiltered band universe for the current scope/severity view.
- `gui_smoke_test` verifies the all-band, current-band, and background-band severity tooltips and verifies that selecting the background band keeps the unfiltered per-band tooltip summaries visible.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; Problems UI consumes `DiagnosticReport::analysisBandGroups` and does not derive scheduling policy.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional report surfaces that consume diagnostic band groups without recomputing priority policy.
- Verification for this block: focused build target `gui_smoke_test`; direct `gui_smoke_test` run with 432 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Completion Band Summary Header MVP

- Status: implemented in the current worktree.
- Scope: thirty-fourth usable Huge Workspace Mode milestone and a completion-provenance visibility refinement. It does not change semantic extraction, completion query ordering, command completion behavior, diagnostic publication, or workspace scheduling.
- `CompletionResult` now exposes `analysisBandGroupCount()` and `analysisBandSummaryText()`, formatting result-owned current/dirty-open/open/background/unbanded completion provenance from the already-attached item metadata.
- `CompletionModel::updateCompletions()` prepends a non-selectable `:: COMPLETION BANDS - ... ::` header only when ordinary semantic completion items span multiple analysis bands, so single-band completion lists keep their existing one-row behavior.
- The completion popup consumes the result-owned summary instead of recomputing workspace priority tiers or inspecting scheduler state in UI code.
- `completion_test` verifies summary formatting for current/background completion items and verifies that the rendered header is non-selectable while the first selectable completion remains the first real semantic item.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; completion UI renders provenance data already present on `CompletionResult::SemanticCompletionItem`.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional completion/report surfaces that consume analysis-band provenance without recomputing priority policy.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 555 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

### Post-L: Huge Workspace Command Symbol Band Summary Header MVP

- Status: implemented in the current worktree.
- Scope: thirty-fifth usable Huge Workspace Mode milestone and a command-symbol completion provenance visibility refinement. It does not change semantic extraction, command-symbol query ordering, default insertion behavior, diagnostic publication, or workspace scheduling.
- `CompletionModel::updateSymbolRecordCompletions()` now builds a band summary from visible, real symbol rows after scoring/truncation, excluding the command description row and `[DEFAULT]` insertion row.
- When visible command-symbol rows span multiple analysis bands, the model inserts a non-selectable `:: COMMAND SYMBOL BANDS - ... ::` header after the default row, preserving the default row as the first selectable command-symbol item.
- The summary reuses `CompletionResult::analysisBandSummaryText()` so command-symbol completion renders existing current/dirty-open/open/background/unbanded provenance without recomputing workspace priority tiers.
- `completion_test` verifies current/background command-symbol rows render the summary header at row 2, keep it non-selectable, and leave the default row as the first selectable entry.
- Implementation remains no-regex and keeps priority policy in `WorkspaceAnalysisPlanService` / `SemanticIndex`; command-symbol UI consumes metadata already present on `SemanticSymbolRecord`.
- Next Huge Workspace milestones: per-band diagnostics publication only if semantically safe, richer tiered index publication, and additional completion/report surfaces that consume analysis-band provenance without recomputing priority policy.
- Verification for this block: focused build target `completion_test`; direct `completion_test` run with 556 checks. Final release verification for the commit also includes changed-file C++ regex API scan, full default CMake build, full `ctest --output-on-failure` 7/7, and `git diff --check`.

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
