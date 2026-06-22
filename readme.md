# ZeroSlack Handoff

ZeroSlack is a lightweight SystemVerilog workspace browser and editor.

```text
Tree-sitter live syntax and editor scope
Slang semantic facts
ProjectModel / DocumentModel / AnalysisScheduler
SemanticIndex / SemanticIndexSnapshot
Query Services and feature services
Thin UI consumers
```

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Never discard user changes or use destructive git commands unless explicitly requested.
- Do not touch user dirty RTL fixture files.
- Do not push unless explicitly asked.
- Keep local commits coherent, concise, and architecture-oriented.
- Keep source, tests, UI strings, CMake, and these docs English / ASCII.

## Latest Strategy

- Phase E semantic data model hardening is complete on this branch.
- Phase F0 Legacy Field Retirement is complete on this branch.
- Phase F1-F3 semantic migration is complete on this branch, including record-first relationship/reference/report paths and the post-F3 release gate.
- Phase G Complete Legacy Field Deletion is complete on this branch, including the post-G release gate.
- Phase H Semantic Core Slimdown is complete on this branch: legacy type adapters, redundant APIs, and compatibility logic were removed from semantic core contracts or tightened behind guarded transition boundaries.
- Phase I Semantic Store Native / Collector Native is complete on this branch: the legacy collector/store body has been replaced by semantic-native collection and storage.
- Phase J Test Fixture Native Cleanup is complete: tracked tests use semantic-native fixture builders, `test_sv/syminfo*` and the reverse fixture adapter have been deleted, and the final zero target scans repo source except docs and guard definitions.
- I0 is complete: `legacy_field_policy_guard.ctest` now defines the Phase I zero-legacy target terms and an opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET` final scan for I5.
- G0 is complete: `SemanticRelationshipResult` no longer carries legacy `fromSymbol` / `toSymbol` endpoint payloads; relationship consumers use endpoint records and stable keys.
- G1 is complete: `SemanticDefinitionResult` no longer carries legacy `symbol` payloads; consumers use `symbolRecord` / `symbolStableKey`.
- G2 is complete: `SemanticIndex` / `SemanticIndexSnapshot` no longer expose the retired `SymbolInfo` public APIs such as `getSymbols`, `getSymbolsByType`, `getSymbolByStableKey`, or `findDefinitions`.
- G3 is complete: direct `symbolId`, `symbolType`, `moduleScope`, `dataType`, and `sym_type_e` product-facing use has been removed or isolated to collector/import, taxonomy, completion compatibility, snapshot-local, and guarded adapter boundaries.
- The Phase G release gate passed locally with full Ninja and full CTest.
- The Phase H release gate passed locally with full Ninja, full CTest, `legacy_field_policy_guard`, static legacy API scans, docs consistency review, and forbidden-file regression checks.
- Completion compatibility, snapshot/store `SymbolInfo` carriers, feature-service internal helpers, `syminfo` legacy queries, and collector/import adapter surfaces were tightened during Phase H and later retired by Phase I/J cleanup.
- Phase I progress tracking used per-subphase accounting. Current subphase: I5 complete.
- I1 first block is complete: Slang collection now exposes semantic-native record APIs and `SymbolAnalyzer` consumes `extractSymbolRecords` / `extractWorkspaceSymbolRecords` directly instead of converting collector symbols in the analyzer layer.
- I1 second block is complete: `slangsymbolcollector` now builds `SemanticSymbolRecord` as the primary collector output.
- I1 is complete: `collectSymbols`, `extractSymbols`, and `extractWorkspaceSymbols` are deleted from the tracked collector/manager/test source, leaving Slang symbol extraction record-native.
- I2 first block is complete: `SemanticIndex` now owns a semantic-native record/content store with local handles and stable-key indexes.
- I2 second block is complete: native file content state now drives `contentAffectsSymbols`, and native records/content override older snapshot entries after file replacement.
- I2 third block is complete: native file coverage now prevents stale snapshot or `sym_list` fallback records from resurfacing after a file is replaced with zero semantic symbols.
- I2 fourth block is complete: `SemanticIndex::refreshStructTypedefEnumForFile` is native-only, and the redundant `sym_list` refresh API is deleted and guarded against returning.
- I2 fifth block is complete: `SemanticIndex::getSymbolRecords` no longer reads legacy `sym_list` records through the adapter; record queries merge only native store and snapshot data.
- I2 sixth block is complete: `SemanticIndex` cached-content and content-change queries no longer fall back to `sym_list`; missing native state conservatively requests analysis.
- I2 is complete: `SemanticIndex` no longer reads legacy `sym_list` records through the adapter, and the former I3 write mirror has been removed.
- I3 is complete: scope rebuild, module containment, relationship containment, and module lookup have moved away from the legacy relationship mirror.
- I3 first block is complete: `SmartRelationshipBuilder` consumes semantic records through a `SemanticIndex` record provider instead of reading records back from `sym_list` or the collector adapter.
- I3 second block is complete: `SymbolRelationshipEngine` file rebuild paths consume semantic records through a provider and no longer expose the legacy `symbols()` DB-to-record accessor.
- I3 third block is complete: `sym_list` relationship rebuild and scope refresh now forward containment work to the semantic-record relationship engine instead of adding CONTAINS edges from legacy fields.
- I3 fourth block is complete: legacy `sym_list` current-module lookup and module-scope auto-inference are deleted; module lookup stays on semantic-record `SemanticIndex` paths.
- I3 fifth block is complete: `SymbolRelationshipEngine` no longer accepts, stores, or exposes a `sym_list` database pointer; relationship engine files are guarded against reintroducing `sym_list`.
- I3 sixth block is complete: `SemanticIndex` now owns the attached relationship engine pointer for relationship queries, completion relationship facts, and snapshot publication instead of fetching it from `sym_list`.
- I3 seventh block is complete: `sym_list` no longer stores, exposes, or forwards a relationship engine; `SemanticIndex` directly rebuilds native relationship facts when records change or an engine is attached.
- I3 eighth block is complete: `SemanticIndex` no longer mirrors semantic records back into `sym_list`, and the reverse relationship/scope mirror conversion is deleted and guarded against returning.
- I4 is complete: remaining legacy carrier APIs and compatibility taxonomy surface have been deleted or isolated now that semantic store, scope, and relationship paths no longer depend on them.
- I4 first block is complete: the unused `semanticcollectoradapter` source/header have been removed from CMake and guarded as forbidden legacy collector adapter files.
- I4 second block is complete: `SemanticIndex` no longer exposes or stores a legacy `sym_list` database injection/access API.
- I4 third block is complete: dead `sym_list` symbol database mutator/accessor/index/scope-tree storage has been deleted, including `syminfoindex.cpp`, `syminfoscope.cpp`, and `scope_tree.h`.
- I4 fourth block is complete: native taxonomy metadata checks no longer route through the retired private raw-kind bridge.
- I4 fifth block is complete: unused legacy metadata write-back helpers have been deleted from the taxonomy compatibility surface.
- I4 sixth block is complete: legacy `SymbolInfo` owner/visibility taxonomy overloads have been removed from the public compatibility header; callers verify the semantic metadata contract instead.
- I4 seventh block is complete: legacy `SymbolInfo` interface helper and declaration-helper overloads have been removed from the public taxonomy compatibility header.
- I4 eighth block is complete: legacy `SymbolInfo` scope/visibility helper APIs have been deleted; definition visibility callers use the native semantic metadata overload.
- I4 ninth block is complete: legacy taxonomy no longer exposes `packageScopeNames(QList<SymbolInfo>)`; tests compute package scopes locally before building semantic records.
- I4 tenth block is complete: legacy completion type matching compatibility APIs have been deleted in favor of `SemanticCompletionKind` metadata matching.
- I4 eleventh block is complete: legacy outline grouping compatibility APIs have been deleted; outline checks now use native semantic metadata and raw collector kind directly.
- I4 twelfth block is complete: legacy raw collector kind round-trip helpers have been removed from the public taxonomy compatibility header.
- I4 thirteenth block is complete: tracked tests no longer include the removed `semanticcollectoradapter` header; legacy fixture-to-record conversion is isolated in a test fixture helper and guarded against adapter include regressions.
- I4 fourteenth block is complete: direct `symboltaxonomylegacy.h` includes in tracked tests are isolated to the legacy fixture conversion helper and guarded against spreading back into test bodies.
- I4 fifteenth block is complete: global `symboltaxonomylegacy.h` includes are guarded to remain isolated to `symboltaxonomy.cpp` and the test fixture conversion helper.
- I5 is complete: the Phase I release gate passed with full Ninja, full CTest, normal `legacy_field_policy_guard.ctest`, opt-in `ZEROSLACK_PHASE_I_ZERO_TARGET`, static core legacy scans, and `git diff --check`.
- I5 first block is complete: lowercase `rawCollectorKind` / `requestedRawCollectorKind` field and helper names were renamed to semantic-native `collectorKind` / `CollectorKind` naming, and the guard continues to reserve the raw names as retired compatibility terms.
- I5 second block is complete: `symboltaxonomylegacy.h` and the production `sym_type_e` taxonomy overloads were deleted; tracked tests moved through a temporary fixture-local adapter before Phase J removed it.
- I5 third block is complete: root/core `syminfo*` carrier files moved into `test_sv` fixture scope, core CMake stopped building them, and Phase J later deleted the fixture carrier entirely.
- I5 fourth block is complete: stale core `syminfo` includes and `SymbolInfo` naming were removed, tracked tests were migrated to fixture-local/native semantic helpers, GUI smoke fixtures synchronize native records before snapshot-only checks, and the final Phase I release gate passed.
- J0 is complete: `legacy_field_policy_guard.ctest` defines the repo-wide fixture cleanup allowlist and guards new `sym_list` / `syminfo` legacy terms outside the current cleanup scope.
- J1 is complete: semantic-native test record builders cover metadata, owners, type info, local handles, stable keys, and relationship endpoints.
- J2 is complete: completion and jump tests use semantic-native records instead of hand-authored `sym_list::SymbolInfo` fixture data.
- J3 is complete: relationship and GUI smoke tests use semantic-native records and no longer include or populate the fixture-only `sym_list` carrier.
- J4 is complete: `test_sv/syminfo*` files and the reverse fixture adapter in `semantic_fixture_records.h` have been deleted, CMake no longer builds fixture carrier sources, and full Ninja/full CTest/guard checks passed.
- J5 is complete: the final zero target now scans first-party repo source, including tracked test fixtures, while excluding docs and guard definitions; full Ninja, full CTest, normal guard, final zero-target guard, static scans, and `git diff --check` passed.
- Phase J progress tracking used per-subphase accounting. Current subphase: J5 complete.
- Post-J editor navigation, responsiveness, and completion trigger fixes are committed through `d7c72d5`: completion popup sizing now uses real item text width, interface definitions preserve cross-file header locations, instance named port clicks jump to child port declarations while actual signal clicks stay local, Navigation avoids root-cause synchronous rebuilds by caching hierarchy inputs and batching tree population without timer-delay workarounds, normal completion triggers from identifier prefixes or strong contexts without space/semicolon dependence, semantic command completion uses `;cmd` plus Space, code templates use `;;cmd` plus Space, editor action commands reserve the `;:cmd` namespace, `;?` / `;;?` / `;:?` show scoped help, and Double Shift opens the ZeroSlack Global Control overlay from editor or non-editor focus.
- Phase K Follow-up Editor Structure Features is complete. Current subphase: K3 Fold Block Shelf complete, 0% remaining.
- Phase K delivered K1 Design Hierarchy with right-click Design Top and Files dimming, K2 Tree-sitter-backed Code Folding plus Custom Folding through the `;:fd` action completion, and K3 Fold Block Shelf through `;:fds`.
- Phase K verification passed with focused Ninja targets, `completion_test` with 315 checks, `gui_smoke_test` with 331 checks against `test_sv/new` plus `test_sv/test_symbols.sv`, `git diff --check`, normal `legacy_field_policy_guard`, and opt-in `ZEROSLACK_PHASE_J_ZERO_TARGET`.
- Phase L Regex Logic Native Cleanup is complete. Current subphase: L6 Final Regex Zero Target complete, 0% remaining.
- Phase L removed regex-driven logic from first-party production semantic/editor feature paths, replacing it with deterministic SV token helpers while preserving semantic record and `SemanticIndex` data flow.
- Phase L verification passed with full CMake/Ninja build, full CTest 7/7, focused `completion_test` with 321 checks, `jump_test` with 115 checks, `gui_smoke_test` with 331 checks, normal `legacy_field_policy_guard`, opt-in `ZEROSLACK_PHASE_L_ZERO_TARGET`, static production regex API scan, and `git diff --check`.
- `SemanticIndexSnapshot` is the intended single UI query truth.
- Do not add feature-specific workarounds in UI, scheduler, or analyzer code.
- After Phase J, keep new semantic/test work record-native and keep the repo-source zero target passing.

## Phase K: Follow-up Editor Structure Features

- Status: complete.
- K1 Design Hierarchy view is implemented: Navigation has a Design/Hierarchy tab without a Top dropdown; Design Top is set from right-click menus on Files, Module Browser, or Design Hierarchy nodes; nodes render as `instance_name : module_type`; instance double-clicks navigate to instantiation sites; module-definition navigation is available from context menus; reports are cached by snapshot generation plus selected top; Files view dims entries outside the selected hierarchy without hiding them.
- K2 Code Folding and Custom Folding is implemented: SystemVerilog folding ranges come from Tree-sitter syntax nodes, custom `// fold <alias>` / `// endfold` ranges are parsed from Tree-sitter comment nodes, gutter fold controls collapse/expand regions, and selecting `fd` from the `;:fd` action completion enters Fold Region Mark Mode to insert custom markers as one undo block. Bare `;:fd` shows `fd` and `fds` action candidates instead of executing immediately.
- K3 Fold Block Shelf is implemented: `;:fds` enters Fold Shelf mode and opens the shelf dock; custom fold blocks highlight on hover with a clear bottom boundary, move or Ctrl-copy into the shelf through structured MIME data, shelf items can be consumed or Ctrl-copied back into editors at line boundaries, double-click preview is read-only, moved item deletion offers restore/delete/cancel, and shelf operations log to Activity/Output.
- Editor action commands now use the `;:` completion namespace: typing `;:` opens editor actions, typing `;:fd` filters to `fd` / `fds`, and execution happens only after a candidate is accepted.
- K1-K3 must not use regex for structure or custom fold parsing. Custom fold and shelf behavior must reuse Tree-sitter/comment-node parsing and the folding range model.
- Opening files must not rebuild heavy hierarchy reports, run RTL Insights reports, scan workspace files from UI, or run Slang directly from UI.
- Activity/Output should record hierarchy builds, Design Top changes, fold/shelf moves, copies, inserts, restores, and warnings without adding long-lived scattered perflog.

## Phase K Goal Mode

- Track Phase K by feature subphase, not by the whole phase.
- K1, K2, and K3 are each their own 100% unit.
- Current subphase: K3 Fold Block Shelf complete, 0% remaining.
- At the end of every work turn, report the current Phase K subphase and the remaining percentage for that subphase.
- Move to the next subphase only after the current feature is implemented, tested, docs are current, and any coherent local commit requested for that block is complete.
- Phase K is complete only when K1-K3 are implemented and verified, docs are current, `gui_smoke_test` passes for affected UI flows, service tests pass where added, `legacy_field_policy_guard` / final zero-target scans still pass when relevant, and `git diff --check` is clean.

## Phase L: Regex Logic Native Cleanup

- Status: complete.
- Goal achieved: first-party production regex logic was removed from semantic, editor, completion, navigation, scheduling, and RTL Insights feature paths without restoring old parsers or moving policy into UI.
- L0 is complete: the repo-wide regex inventory is documented, normal guard blocks new first-party production regex API use outside the cleanup allowlist, and the optional `ZEROSLACK_PHASE_L_ZERO_TARGET` final scan is defined for L6.
- L1 is complete: low-risk identifier checks, word-boundary keyword checks, and whitespace normalization use deterministic token helpers instead of `QRegularExpression`.
- L2 is complete: completion context extraction no longer uses `QRegularExpression`; struct member, enum, assignment, and module-instantiation context parsing uses deterministic token helpers with focused completion coverage.
- L3 is complete: module range, include, and import context logic no longer uses regex; module-context include/import extraction uses deterministic token parsing while preserving `SemanticIndex` record flow.
- L4 is complete: FSM transition extraction no longer uses raw-file regex scans; case selectors, labels, conditions, assignments, and state references are parsed by deterministic token helpers plus semantic state records.
- L5 is complete: open-document structural scheduling checks no longer use keyword regex and now use deterministic token-boundary matching for the existing lightweight structural keyword policy.
- L6 is complete: the final zero target rejects regex APIs in first-party source and tests outside docs and guard definitions, and verification passed with full build, full CTest, focused tests, guard scans, static scans, and `git diff --check`.
- Phase L must not remove guard regex definitions themselves; those are the enforcement mechanism, not product logic.

## Phase L Goal Mode

- Track Phase L by cleanup subphase, not by the whole phase.
- L0 through L6 are each their own 100% unit.
- Current subphase: L6 Final Regex Zero Target complete, 0% remaining.
- At the end of every work turn, report the current Phase L subphase and the remaining percentage for that subphase.
- Move to the next subphase only after the current regex cleanup block is implemented or documented as complete, verified, docs are current, and any requested coherent local commit is complete.
- Phase L is complete: L0-L6 are complete, docs are current, first-party production code no longer uses regex for semantic/editor feature logic, guard exceptions are limited to docs and guard definitions, focused tests and affected GUI smoke tests pass, full Ninja/full CTest passed, and `git diff --check` is clean.

## Feature Expansion Guardrails

These constraints are mandatory for every new feature.

- New semantic or RTL behavior must flow through `ProjectModel` / `DocumentModel` / `SemanticIndexSnapshot -> Query Service or feature service -> report/model -> UI render`.
- UI code must render service/model output only; it must not run Slang, scan workspace files, own semantic policy, or patch around missing semantic data.
- `AnalysisScheduler` and `SymbolAnalyzer` must stay limited to timing, lifecycle, extraction, publication, and refresh routing; they must not own feature policy.
- New tests and fixtures must use semantic-native records/builders and must not reintroduce `sym_list`, `syminfo`, reverse adapters, legacy taxonomy headers, or legacy field/API names.
- New feature reports must expose stable keys, semantic records or typed report rows, owner/type/source-role metadata, navigation payloads, evidence, confidence, and not-found reasons where relevant.
- Real workspace fixtures, especially `test_sv/new`, must cover behavior that depends on package/import/include/interface/modport/cross-file semantics.
- `legacy_field_policy_guard.ctest` and the final repo-source zero target must keep passing after every feature change.
- Feature work must be rejected or redesigned if it requires qmake/pro/pri, `.claude`, SVLexer, the old Tree-sitter symbol parser, direct UI Slang execution, UI workspace scans, regex relationship analysis, long-lived scattered perflog, or legacy carrier/API resurrection.

## Current Architecture

- `ProjectModel` owns workspace root, SV files, include dirs, defines, top, and ignored paths.
- `DocumentModel` owns open document identity, text snapshots, versions, dirty/saved state, cursor, live module names, and registry-backed text queries.
- `AnalysisScheduler` owns analysis timing, debounce/cancel policy, refresh requests, relationship work, and lifecycle routing; it does not own feature policy.
- `SemanticIndexSnapshot` should be the single query truth for UI-facing semantic reads.
- Query services and feature services own feature-specific semantic reads and report shaping.
- UI code renders service/model output and must not run Slang or scan workspace files directly.
- `MyCodeEditor` owns editor event flow and stable public adapters; focused editor subsystem files own gutter, selection/highlight, geometry, cursor navigation, file identity, syntax state, runtime, completion, hover, and source navigation.
- Inline commands are layered by intent: `CompletionService` owns semantic `;cmd` behavior, `CodeTemplateService` owns `;;cmd` template expansion, and the `;:cmd` action namespace is reserved for editor actions.
- Global Control search is coordinated through a global event filter plus `GlobalControlService`; it reads ProjectModel files, SemanticIndexSnapshot symbols, CodeTemplateService templates, and existing coordinators for dispatch rather than scanning files or running analysis in UI.
