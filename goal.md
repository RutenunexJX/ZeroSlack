# ZeroSlack Long-Term Goal Model

## Current Acceptance Repair (2026-08-04)

Overall status: **验收通过，P1 正确性修复完成**.

- Phase 0 — P1 Acceptance State Alignment: complete. The three documents now
  withdraw the prior acceptance claim before any product-code change.
- The Phase 0 checkpoint explicitly recorded “文档状态已对齐，代码修复尚未完成。”
  before product edits began. This is a historical checkpoint; the completed
  P1 repair and final 77/77 acceptance supersede it.
- Phase 1 — Keyword-Boundary Incremental Tree Repair: complete. Product fix,
  focused regressions, full build, and unfiltered acceptance all pass.
- 文档状态已更新为验收通过；P1 产品代码、回归与完整 77 项验收均已完成。
- Repair baseline and protection snapshot: `HEAD` and `origin/main` are both
  `73ddc5d0e3cb275bb291c9bfb8828d3fd8801dc6`. Protected untracked paths remain
  `current_app_signal_usage_hotspot_full_after.png` (141996 bytes, SHA-256
  `2FDBB5A46870821FF5927D26BE7B381D891DBBA7ABAA3A386910189F44B66A6F`),
  `test_sv/new/.zs` (8642 bytes, SHA-256
  `951DAE13441A96CC21063523180E3FA8EE1B37AC1D7C1C76259F1440F865F1C5`),
  `test_sv/huge_prj/.zs` (88254 bytes, SHA-256
  `FD279C386846C51F13C4DBA81378A4BD6748FFF274A9EA0AF9A7DEFA5EB26391`),
  and `dist/`. The two protected archives remain
  `ZeroSlack-0.1.0-win64.zip` (46192051 bytes, SHA-256
  `E3798348BBA07DB8E585C93892DD98D230EAB761F00D2A4F32D6AE3ECA65A9A1`)
  and `ZeroSlack-0.1.0-win64-20260727.zip` (46803753 bytes, SHA-256
  `2DBB6E28ECA2D4F50C00C02EC0F7A92CA42BB282B54236AD8B969FBBED693B74`).
- The registered CTest inventory contains 77 targets. The historical all-target
  build proves only compile/link. Historical excluded runs of 68/68 and 71/71
  remain partial records and are not used as complete acceptance evidence.
- The P1 root is fixed by deriving the fast-path exclusion set from Slang's
  IEEE 1800-2023 keyword table. Ordinary non-keyword identifier suffix edits
  still skip parsing; identifier-to-keyword and keyword-to-identifier edits
  synchronously take the existing authoritative Tree-sitter incremental parse.
- Current focused verification passes: `ts_doc_test` 0.20 seconds,
  `ts_large_file_semantics_test` 0.83 seconds, and
  `editor_incremental_test` 9.31 seconds. The semantic suite reports 26/26,
  including `alway -> always`, `logi -> logic`, reverse keyword exit, and the
  unchanged ordinary-identifier no-parse path.
- The complete serial build succeeds. Final unfiltered
  `ctest --output-on-failure -j1` passes 77/77 in 395.51 seconds, including
  `editor_incremental_test` in 13.64 seconds with every existing correctness
  and performance threshold unchanged.
- Pre-final history: one earlier full run produced 76/77 while the external
  `PassTheFear.exe` process was active, and an isolated rerun reproduced two
  latency-gate failures. That run was not accepted and no threshold was changed;
  the final unfiltered rerun above is the current acceptance result.
- Historical resolved blocker roots from the preceding repair:
  - Command Layer word-prefix matches did not distinguish exact word initials
    and retained weaker ranks, breaking required abbreviations and stable `go`
    ambiguity. One matcher now ranks exact initials and keeps only the strongest
    rank; no second command fact source was introduced.
  - The editor materialization assertion had included explicit cache-verification
    reads outside the edit path. The measured edit path is now isolated, while
    product-side identifier edits, transient candidate overlays, syntax lookup,
    folding, and occurrence updates remain incremental. The `0xc0000374` exit
    came from rebuilding `ExtraSelection` cursors during
    `QTextDocument::contentsChange`; presentation now refreshes after the
    transaction and selections are released while their document is alive.
  - Hotspot zoom reset emitted a synchronous zoom-state signal that overwrote
    the requested target before it was applied. The shared Registry/Focus route
    now preserves the target across reset and resynchronizes from the view.
  - The Hotspot panel test had no guaranteed diagnostic path before
    `QApplication`; it now installs an early stderr Qt message handler. With the
    shared zoom fix, the registered test completes every assertion normally.
- Historical consecutive target runs passed: `gui_smoke_test` 14.37/14.49/15.60
  seconds; `editor_incremental_test` 11.30/10.89/10.10 seconds;
  `graph_export_panel_integration_test` 0.32/0.32/0.32 seconds; and
  `signal_usage_hotspot_panel_test` 0.44/0.46/0.41 seconds.
- Historical required regressions passed: `expose_signal_to_top_gui_test` 1.17 seconds,
  `editor_structural_input_test` 0.32 seconds, and
  `editor_paste_transaction_test` 0.69 seconds. Added root checks also passed:
  `ts_large_file_semantics_test` 0.85 seconds and
  `action_registry_test` 0.09 seconds.
- Historical pre-P1 acceptance used `ctest --output-on-failure -j1` with no `-R`, `-E`,
  label filtering, exclusions, skipped tests, or relaxed thresholds: 77/77
  passed in 300.53 seconds. An earlier same-code full run passed 76/77 and
  transiently exceeded the existing visible-Wave 6/12 ms latency assertions;
  the isolated rerun measured p95 2.293 ms and max 2.640 ms, and the subsequent
  complete run passed the unchanged thresholds. That result predates the newly
  confirmed keyword-boundary defect and does not establish current acceptance.
- Phase 0 `git diff --check` passes. No commit or push was performed.

ZeroSlack is a SystemVerilog code editor and workspace browser. Long-term work
must improve daily RTL editing and understanding while preserving the service
boundary:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

## Goal Mode Rules

- Work only on the goal tracks listed in this file.
- Each milestone must be small, verifiable, and deliverable.
- Each completed milestone must update `readme.md`, `plan.md`, and `goal.md`.
- Each completed milestone must run verification appropriate to the changed
  scope.
- Do not add unlisted features to a milestone.
- Do not put semantic policy, workspace scans, or Slang execution in UI code.
- Do not restore qmake/pro/pri, SVLexer, old Tree-sitter symbol parsing, regex
  relationship analysis, or active `;:` commands.
- Huge Workspace work is status audit only unless the user explicitly changes
  the scope.

## Current Build Performance Goal Status (2026-07-20)

The local MinGW Debug link goal is measured but not complete. A representative
core mtime update rebuilt two core objects, one static archive, and all thirteen
dependent executables in 431.890 seconds. The observed ratios against the
818.9/1099.2-second GNU-ld baselines are 1.90x/2.54x, below the required 3x,
and the local linker remained GNU ld.

The accepted build-system scope is limited to stable inherited-PATH Ninja
launchers and a probed, Debug-only `ZEROSLACK_DEBUG_LINKER` LLD option with safe
GNU fallback. The only installed LLD is incompatible LLVM 7.0.1. Shared-core
and aggregated-test prototypes were rejected because their Qt DLL boundary or
test-initialization risks could not be proven safe without product/fixture
changes. Release configuration and ABI remain unchanged; Debug CTest passes
12/12 and demo passes hidden offscreen startup. Achieving and claiming 3x now
requires a compatible LLD host followed by a full Qt/Slang link benchmark and
the same regression suite.

## Current Goal State

Historical milestone record (2026-07-29): the eight requested product
interaction and architecture convergence items passed the then-current
regression and GUI/sample-workspace checks. This record does not establish the
current 77-target acceptance; that acceptance is established separately by the
2026-08-03 repair summary above.

Stage 0 is complete:

- verified `main` and `origin/main` at
  `7cb1f214022d7b4bd2ce20cdeb39a1aad4f873ce`
- verified the exact four protected untracked paths and excluded them from all
  writes and cleanup
- reviewed the current handoff, execution plan, goal model, and owners for
  Expose, action metadata, inline completion, editor modes, context menus,
  Insight panels, workspace persistence, and the two oversized coordinators
- passed the pre-change Debug CTest baseline: 25/25, 0 failed, 348.82 seconds
- established the migration ownership and test matrix in `readme.md` and
  `plan.md`

Stage 1 is complete:

- added the shared, revisioned `EditorActionContextService` without introducing
  a semantic fact store
- exposed workspace, active top/instance, module/package, syntax revision, and
  semantic current/stale/analyzing/unavailable state through a persistent main
  window context chip
- made unique Expose hierarchy binding independent of Files/Design navigation
  history and retained explicit candidate sets for multiple top/instance cases
- kept resolvable and hard-failure actions enterable, with visible selection or
  explanatory flows instead of disabled-action tooltip-only feedback
- retained `rtleditcore` as the sole Expose plan/preview/apply transaction path
- passed the test-first `expose_signal_to_top_gui_test`: 1/1, 0 failed,
  0.50 seconds

Stage 2 is complete:

- added one 76-descriptor `ActionRegistry` with stable ids, canonical names,
  categories, scopes, parameter models, requirements/failure text, recovery
  policy, aliases, and authoritative execution routes
- migrated F24, built-in inline semantic/template commands, the template
  catalog, static Global Control commands, Package Tools, source-symbol
  context actions, and Expose metadata to registry-backed adapters
- retained distinct trigger semantics while removing the former parallel
  action definitions
- made F24 help render the unified user-visible action catalog across all
  trigger surfaces
- preserved Package Tool structured slots and the Expose
  `rtledit.signal.exposeToTop` transaction route
- passed the test-first registry/catalog regressions and focused integration:
  `completion_test`, `action_registry_test`, `gui_smoke_test`, and
  `expose_signal_to_top_gui_test`, 4/4, 0 failed, 11.95 seconds

Stage 3 is complete:

- registered the audited free `;v` token as the semantic-only
  `completion.visibleSymbols` action and intentionally left `;;v` absent
- preserved every existing filtered `;cmd` and the explicit Tab-only
  activation contract; ordinary typing still does not open completion
- carried module and package identity in the saved abbreviation anchor so
  input and Backspace filter against the original semantic scope
- reused the existing `SemanticIndex` owner/import query to return local ports,
  signals, parameters, types, instances and subroutines plus unambiguous active
  package imports, while excluding unimported packages and foreign-module
  internals
- extended Slang-owned package metadata so package variables,
  functions/tasks, and existing type/value definitions participate in import
  visibility without a UI-side fact store
- made the registry regression fail before implementation, then passed
  `action_registry_test` 12/12, `completion_test` 842/842, and
  `gui_smoke_test`, including real Slang package metadata and explicit GUI Tab
  activation

Stage 4 is complete:

- replaced `EditorModeState` and migrated feature-active booleans with one
  authoritative `EditorModeController`
- declared owner, priority, co-existence, input capture, Esc behavior,
  presentation, and entry/exit reasons for inline/completion candidates,
  Template Slots, signal selection, column selection, virtual cursor, Fold
  Region, Fold Shelf, and source navigation
- routed actual Slot, signal, column/virtual, fold, candidate, and navigation
  lifecycle through the controller, including real cleanup callbacks rather
  than an outer compatibility shell
- added consistent tab switch, document close/identity, Global Control, and
  F24 stale exits while preserving right-click signal completion, Slot
  cycling, column/virtual selection, and Fold Shelf behavior
- published `EditorModeSnapshot` from the active editor and made the persistent
  Mode Chip consume the structured state instead of transient status text
- made the controller regression fail before implementation, then fixed the
  GUI-detected source-navigation input conflict and virtual-caret cleanup;
  the rebuilt `editor_mode_controller_test` and `gui_smoke_test` pass 2/2,
  0 failed, in 10.89 seconds
- recovered the protected root screenshot after a direct diagnostic invocation
  wrote it, restoring the deterministic 141996-byte artifact and original
  timestamp; redirected future GUI artifacts to `%TEMP%/zeroslack-gui-smoke`
  and reverified all four protected entries against the baseline

Stage 5 is complete:

- added `EditorContextMenuModel`, which combines Registry metadata, the shared
  `EditorActionContext`, and explicit runtime capabilities into one stable
  menu presentation
- expanded the single Registry from 77 to 96 descriptors so all standard,
  navigation, structural refactor, signal-selection, and formatter menu
  intents have canonical ids, labels, aliases, availability, and execution
  routes
- moved normal-menu ownership to `EditorCoordinator`; removed the second flat
  string/handler list from `EditorSourceNavigationUi`
- retained Undo/Redo/Cut/Copy/Paste/Select All in the leading block and grouped
  dynamic actions under Navigate, Inspect, Refactor, and Format
- omitted specialized actions that do not apply to the current object, kept
  resolvable actions clickable with reasons in visible labels, and exposed the
  reason in every shown hard-unavailable label
- kept Expose on its existing authoritative handler and `rtleditcore` path,
  and retained signal-selection right-click completion through the same
  Registry model
- made the test-first target fail at generation before the model existed, then
  passed `completion_test`, `action_registry_test`,
  `editor_context_menu_test`, `gui_smoke_test`, and
  `expose_signal_to_top_gui_test`: 5/5, 0 failed, 15.21 seconds
- reverified `git diff --check` and all four protected path sizes/timestamps

Stage 6 is complete:

- added one `InsightFocusController` and central Focus shell that reparents the
  exact RTL Insights, Signal Kernel, or Wave Preview panel between its Dock and
  the main editor area without duplicating widgets, scenes, reports, or models
- registered shared Fit, zoom, search, and Inspector adapters for FSM, Module
  Block Diagram, Signal Usage Hotspot, Signal Kernel Graph, and Wave Preview
- preserved graph/search/selection/transform/Inspector state across Back,
  re-entry, panel-local entry, menu entry, panel switching, and Return to Dock
- hid and restored the recorded surrounding Dock set so each large panel is at
  least 640x360 in the 1100x760 `test_sv/new` GUI regression
- prevented View aliases from revealing an empty focused Dock and made Reset
  Panel Layout return the exact widget before rebuilding the layout
- added an isolated temporary mirror of `test_sv/new` for GUI smoke so legacy
  session writes cannot touch the protected fixture
- restored a pre-isolation rewrite of protected `test_sv/new/.zs` exactly from
  checkpoint blob `38aa7c46a6cc62df3a9d0dc337ea76b6c128e5dc`
  (8642 bytes, original timestamp), then reverified all protected metadata and
  hash after repeated GUI runs
- passed `insight_focus_controller_test`, `relationship_test`,
  `fsm_ui_snapshot_test`, `gui_smoke_test`, `insight_visual_style_test`, and
  `signal_usage_hotspot_panel_test`: 6/6, 0 failed, 32.85 seconds

Stage 7 is complete:

- split workspace persistence into portable project semantics and local
  machine session state, without creating another semantic authority
- made `WorkspaceConfigurationService` atomically write relative include and
  ignored directories, extensions, defines, and top module to
  `.zeroslack/project.json`
- made `WorkspaceSessionStateService` persist tabs, cursor/scroll positions,
  window/Dock layout, and scan cache to a SHA-256 workspace-identity
  `QSettings` partition under the user's application-data location
- removed project configuration from session capture/restore and changed
  Save/Restore/Clear plus `ow s` labels, descriptions, and feedback to
  distinguish local state from portable configuration
- made local Clear suppress automatic recreation during the same workspace
  activation while retaining an explicit Save path
- implemented read-only legacy `.zs` configuration/session import; migration
  writes the separated destinations but never overwrites or deletes its source
- isolated GUI fixture workspaces and local settings stores so regression runs
  cannot write the protected repository fixtures
- made the persistence regression fail at compile time before the split API
  existed, then passed `completion_test`, `action_registry_test`,
  `workspace_persistence_test`, `gui_smoke_test`, and
  `global_control_ow_test`: 5/5, 0 failed, 105.59 seconds
- passed 23/23 persistence checks, including workspace relocation and
  byte/digest/timestamp preservation for both actual protected legacy files;
  reverified all four protected baselines and `git diff --check`

Stage 8 is complete:

- added dedicated `EditorTemplateSlotController`,
  `EditorColumnModeController`, and `EditorSignalSelectionController` modules;
  each owns its feature state and unified-mode exit lifecycle
- removed Slot, signal-selection, column-selection, and virtual-cursor raw
  state plus helper implementations from `MyCodeEditorState`
- routed Fold directly through `EditorFoldingController` and moved Source
  Navigation mode synchronization/exit ownership into
  `EditorSourceNavigationUi`
- split RTL Insights into one passive `RtlInsightsPanelViewState`, one
  `RtlInsightsGraphController`, one `RtlInsightsGraphSceneMapper`, and one
  `RtlInsightsPresenter`; retained only shell construction, wiring, Focus
  adapters, and compatibility delegates in the coordinator
- moved graph item classes, role constants, scene construction, inspector,
  selection/navigation, search, and report presentation to their owning
  modules and deleted the original duplicate definitions
- reduced `editorruntime.cpp` from 6874 to 4987 lines and
  `rtlinsightspanelcoordinator.cpp` from 4397 to 849 lines
- made the source/build-boundary regression fail at 5/27 before the split,
  then pass 27/27 with explicit CMake dependencies and 5000/900 line ceilings
- passed `completion_test`, `editor_mode_controller_test`,
  `controller_boundary_test`, `relationship_test`, `fsm_ui_snapshot_test`,
  `gui_smoke_test`, `insight_visual_style_test`, and
  `signal_usage_hotspot_panel_test`: 8/8, 0 failed, 33.47 seconds
- recorded 432.7 seconds for the affected MinGW Debug rebuild/relink without
  claiming a runtime-performance change; reverified all protected metadata
  and `git diff --check`

Stage 9 final acceptance is complete:

- the first complete run passed 30/31 and found a forbidden production
  `QRegularExpression` in Action Registry id validation; replaced it with an
  equivalent explicit ASCII scanner, added invalid-id cases, and retained the
  unchanged policy guard
- completed the MinGW Debug all-target build serially with exit code 0; a
  parallel attempt exhausted memory in simultaneous GNU ld links, so
  `--parallel 1` is the verified command on this host
- passed the complete CTest suite: 31/31, 0 failed, 230.07 seconds, including
  all six `components/rtleditcore` tests
- verified Files/Design context parity, unique and multiple-instance Expose,
  explicit `;cmd`/`;;cmd`/`;v`, F24, Global Control, Slot/Signal/Column/
  Virtual/Fold modes, grouped context menus, and FSM/Module Block/Usage
  Hotspot/Signal Kernel/Wave Focus View behavior through the focused and GUI
  regressions
- ran sample acceptance using `test_sv/new` and `test_sv/huge_prj`: GUI writes
  use a temporary mirror, Expose plans/applies on temporary copies, Global
  Control opens and analyzes both real roots, and persistence reads both real
  legacy files while proving their byte digest, size, and timestamp unchanged
- recorded final performance observations without claiming causal speedup:
  the pre-change 25-test suite took 348.82 seconds and the final 31-test suite
  took 230.07 seconds on a warm Debug build
- reverified the four protected metadata baselines, the protected
  `test_sv/new/.zs` clean-filter hash
  `38aa7c46a6cc62df3a9d0dc337ea76b6c128e5dc`, successful
  `git diff --check`, an empty index, branch `main`, and identical
  `HEAD`/`main`/`origin/main` at
  `7cb1f214022d7b4bd2ce20cdeb39a1aad4f873ce`
- left 66 task-owned modified paths, 2 intended deletions, and 31 task-owned
  new paths unstaged; the protected screenshot, `dist/`, and two `.zs` files
  remain separate pre-existing untracked content
- did not commit or push; detailed per-item evidence, complete file groups,
  commands, timings, GUI scope, and the sole host-linker limitation are in
  `readme.md`

Remaining stages: none for this goal. Independent control-side review remains
outside the execution-side scope.

Current product version baseline: `v0.1.0`. The root `VERSION` file is now the
single manual version source, CMake validates it as strict `X.Y.Z` before
`project()`, and `generated/version.h` is produced from `version.h.in` for
window title, status-bar, and tooltip display. Product versioning is decoupled
from dependency labels.

Inline command behavior has been updated to Tab-only abbreviation expansion:
typing `;cmd` / `;;cmd` text no longer enters command mode, starts the command
timer, or opens command candidates. Plain Tab parses only the registered suffix
ending at the cursor, supports arbitrary editable code positions, prefers
explicit `;;cmd` over `;cmd`, preserves only the abbreviation replacement
range, rejects comments/strings including cross-line block comments, and keeps
semantic completions scoped to the abbreviation anchor file/module/position.
After the first Tab opens multiple semantic candidates, identifier input and
Backspace update the abbreviation query and popup while retaining that saved
anchor. Unique results require Tab/Enter submission; zero-match results remain
open and recoverable with a non-selectable message, without an executable
`[DEFAULT]` item. Active inline popup sessions fail closed when the cursor
leaves the saved abbreviation end or a selection appears; cancellation hides
the popup and clears command highlight/include state without editing source.
Lexical context is checked by one state-machine scan, so closed URL strings and
closed block comments containing `//` do not block a later command on the same
line.
Ordinary code input never auto-opens completion: identifiers, `.`, `::`, macro
backtick text, `$`, and arbitrary two-character input all remain popup-free.
Only explicit command text followed by Tab enters a candidate session. The
semantic inline GUI path verifies live filtering, Backspace expansion,
zero-match recovery, anchor-module isolation, and explicit Tab/Enter activation
while preserving surrounding code and replacing only the complete
abbreviation. Inline semantic sessions suppress default declaration items. The
negative and explicit-Tab GUI cases are owned by `gui_smoke_test`, with
command/service cases in `completion_test`.

Current corrective milestone: workspace-open lifecycle, completion cleanup,
and effective-value unification are implemented in the working tree. The old
ordinary identifier completion state/timer/query/replacement path is deleted;
only explicit Tab command candidate sessions retain `QCompleter`.

All compile-time SystemVerilog effective values now originate in Slang
Compilation/AST/type data and flow through `SemanticSymbolPresentation`,
`EffectiveValueFact`, `SemanticIndex`, and `EffectiveValueService` before Hover
or Ghost consumes them. Package/compilation-unit values are static, real
instance values are keyed by exact hierarchy path, detached defaults remain
separate, and open unsaved documents participate through revisioned,
debounced, cancellable overlays. Exact declaration ranges are part of stable
identity. The service exposes unavailable/current/stale/error state, precise
  value/type/dimension/width data, scope or instance provenance, failure reason,
  and computation/document revisions. Ghost no longer contains a second
  arithmetic/concatenation/enum evaluator, and Wave Preview remains outside the
  compile-time value system. Formal-port annotations remain visible in a
  dedicated right-side lane and are deduplicated by declaration identity.
  Redundant direct parameter values are omitted only after Slang proves source
  and effective values equivalent; derived, coerced, and instance-different
  values remain. Known enum values display Slang decimal text while exact
  width/signed/X/Z binary semantics remain available.

`test_sv/new/PKG_global.sv` is not a valid-value fixture for every declaration:
`E_PRE_ASSERT` is used from line 550 and declared only at line 663. Slang
correctly reports an undeclared identifier for those forward enum references.
The unified value system publishes that error and never substitutes an
implicit increment or hand-evaluated literal; dedicated valid fixtures own the
package and general enum-expression value checks.

Dirty or cache-mismatched workspace symbol, effective-value, and diagnostic
analysis captures one immutable source transaction: the project file set plus
the exact text and revision of every open buffer. Clean indexed workspace files
open at semantic revision zero and do not launch an overlay or publish a new
snapshot. Any captured edit expires the worker and queues a restart from the
newest complete `DocumentModel` snapshot. Workspace epoch, request generation,
dependency/document revisions, and computation revision guard one atomic
GUI-thread publication. Staged/checkpoint/chunked publication is not part of
the architecture.

Automatic relationship analysis captures the semantic snapshot token emitted
by that transaction, uses the same snapshot source contents and symbol records,
and rejects stale request/project identities. It does not launch another
per-open-tab Slang symbol/value/diagnostic refresh.

Clean indexed files do not publish an overlay or rebuild Design. Dirty or
cache-mismatched publications remain handle-churn safe: authoritative
relationships are retained by stable identity, rebound to replacement handles,
and filtered for removed or changed endpoints. Navigation coalesces file/batch
completion by snapshot generation and rebuilds only when its stable Design
structural fingerprint changes; presentation-only publications merely advance
the cached generation. Editor revisions advance only for source-text changes,
so formatting/highlighting cannot stale Slang facts or remove Ghost annotations.

Problems `Current File` always follows the active document. File-specific
analysis notifications are invalidation hints only, and multiple different
file hints coalesce to a full refresh instead of selecting the last background
file.

The real `GlobalControlCoordinator -> FileCommandCoordinator ->
WorkspaceManager` `ow 1` route now has an integration regression covering
cancel, repeat, scan, symbol and automatic relationship completion, semantic
queries, and teardown with both worker families active. Shutdown broadcasts
cancellation before joining and finishes while the semantic runtime is alive;
Slang source-position cache destruction occurs before QtConcurrent worker exit.

The primary captured crash stack was
`SmartRelationshipBuilder::analysisCancelled -> cancelAnalysis ->
RelationshipAnalysisController::cancelWorkspaceAnalysis ->
AnalysisScheduler::~AnalysisScheduler -> MainWindow::~MainWindow`. Reverse
member destruction had released the relationship builder / semantic runtime
before scheduler teardown called it. A second captured failure ended in
`SourcePositionCache::~SourcePositionCache` during Qt TLS cleanup with
`0xfeeefeee`; explicit worker-scope cache destruction removes that late TLS
lifetime dependency.

Workspace scan callbacks are also treated as synchronous reentrancy boundaries.
`WorkspaceManager` validates its `QPointer`, scan generation, active path, and
workspace entry after start/progress/model/files/finish publication, preventing
a close or switch callback from publishing stale scan state.

Corrective coverage is split by ownership: `global_control_ow_test` exercises
the real `ow 1` coordinator path for selector cancel/repeat, `test_sv/new`,
`test_sv/huge_prj`, both analyses, semantic queries, and active-worker teardown;
`effective_value_test` covers package/compilation-unit/default/per-instance
  facts, general enum expressions, decimal/X/Z enum rendering, parameter
  source-value equivalence, wide/signed/X/Z/string/type/dimension values,
  same-name nested identity, Hover/Ghost, and unsaved overlay revision,
cancellation, atomic values, and diagnostics; `gui_smoke_test` covers ordinary
and backtick completion negatives, explicit Tab candidate behavior, scan
  reentrancy, embedded double-click popup lifetime/non-global flags and uniform
  editor-font inheritance, structured attribute-free ANSI/non-ANSI/interface
  port declarations, actual QTextLine-anchored FormalPort rendering across
  tabs, fixed/proportional fonts, horizontal scrolling, line-tail edits, long
  declarations, trailing whitespace and wrapping, and Design refresh
  coalescing. FormalPort now renders eight logical pixels after the live source
  line tail in the normal viewport; the fixed right-side lane and its viewport
  margin are removed;
  `relationship_test` retains real `PKG_global.sv` / `chl_ctrl.sv`
integration. The `ow 1` regression additionally opens analyzed `rtl_top.sv`
  through the real TabManager at semantic revision zero and verifies no
  open-tabs analysis, snapshot publication, or Design refresh while stable
  relationships, package values, semantic Ghost, port presentation, and nested
  Navigation data remain available. Final headless acceptance is complete: the
  final Debug all-target incremental build passed 17/17 steps in 1099.2 seconds,
  and complete CTest passed 12/12 in 130.60 seconds. No visible
`demo.exe` was launched after the user prohibited GUI interference; the real
coordinator route ran under Qt offscreen and remained alive for both projects.
The final static audit left both user `.zs` files untouched. `new/.zs` is the
user's 18:31:51 reproduction-session save (`9F0341AC...ACBC10`), while
`huge_prj/.zs` remains `CD8A81F2...21CF425`; headless validation did not
overwrite or restore either file.

Remaining scope is documented rather than hidden: dirty overlays invalidate
old relationship edges but do not yet launch an overlay-matched relationship
recompute; zero-symbol include/macro compilation inputs need complete
fingerprints; full engine-mirror replacement still runs on the GUI thread; and
the conservative source-text revision check copies a large editor buffer on
each `textChanged` event.

Previous milestone: RTL Insight core-view modernization is complete on top of
the UI theme and graph foundation. The current UI route keeps Qt Widgets, uses
`InsightVisualStyle` as the global theme layer, and uses `InsightGraphView` as
the shared graph view interaction layer.

Current baseline highlights:

- Insight UI v2 visual foundation is now the app theme foundation.
  `InsightVisualStyle` provides reusable light-theme tokens, role colors,
  selected/hover pens, heat intensity colors, compact control styling,
  panel/legend helpers, app shell/menu/toolbar/tab/side-rail/status/dock/input
  QSS builders, tree/list/table/splitter/scrollbar coverage, Global Control
  and Fold Shelf shell builders, and graph canvas node/edge/hover/selection
  tokens. Existing Insight panels continue to use this visual system for their
  insight surfaces.
- `InsightGraphView` provides the business-neutral graph interaction layer:
  themed canvas styling from `InsightVisualStyle`, pan/drag mode, wheel zoom
  with zoom limits, zoom in/out helpers, fit, center, reset, optional grid,
  empty-canvas selection clearing, zoom-change callbacks, and press/double-click
  hooks. Signal Kernel Graph, Signal Usage Hotspot track/matrix views, and the
  RTL Insights shared graph view behind State Transition Graph, FSM Graph, and
  Module Block Diagram are connected. Signal Journey and Clock/Reset are
  tree/report surfaces rather than graph views; Wave Preview remains a custom
  painted QWidget.
- Insight UI v2 convergence kept the old insight semantics intact: State
  Transition Graph still gates on structural next-state roles, Module Block
  Diagram still shows only module/instance containment, and Wave Preview remains
  a code-understanding sketch rather than a simulator. The completed pass added
  shared title/toolbar/canvas styling, graph search highlighting, visible
  empty/failure reasons, hover/selected graph feedback, and retained source
  navigation callbacks.
- Latest RTL Insight core-view modernization is complete: Module Block Diagram
  now has reference-aligned toolbar controls, nested module-only blocks, a right
  inspector, and an instances table; State Transition Graph keeps structural
  role gating while exposing signal/current/next controls, an inspector, and a
  transitions table; Wave Preview labels output as symbolic preview-only/no
  testbench and adds scope/clock/reset/filter/lane controls without simulation.
- Current execution session FSM drawing rewrite is active in the working tree:
  `fsmgraphlayout.h/.cpp` provides a pure Sugiyama layout function over
  `FsmGraph`, and RTL Insights maps its node rectangles, edge polylines, arrow
  angles, and compact `C#` labels to Qt scene items. The deleted
  `fsmdiagram*` path and coordinator-local router remain removed. FSM and State
  Transition Graph canvases render again while transition tables remain
  populated from `FsmGraphService` / `StateTransitionGraphService`.
  Structural next-state role gating, state/transition extraction,
  condition/source metadata, and dead/end-state table notes remain service
  owned. Latest repair adds dashed alias nodes for long return/forward edges,
  keeps alias nodes canonical-linked and out of FSM statistics, enforces
  top-down bottom-to-top ports, rounded orthogonal paths, grid-snapped columns,
  fixed layer heights, edge-attached `C#` labels, adaptive text-measured node
  widths, tighter spacing, subtitle-free alias nodes, transpose refinement, and
  layer-channel ordering. Layout invariants now cover rendered edge span after
  aliasing, alias/canonical consistency, port placement, label distance,
  collinear edge overlap, label/edge intersection, shared horizontal
  track-overlap, node text line-box bounds, and independent-edge crossings
  above 2. Snapshot metrics show 0 crossings for `elec_cfg_ns`, `phy_cfg_ns`,
  `phy_pass_thrg_cfg_ns`, and `vendor_ip_edma_ctx`; scene areas are 0.556,
  0.605, 0.516, and 0.525 of the fixed-width baseline; fallback aliases were
  not triggered. Verification passed for `relationship_test`,
  `fsm_ui_snapshot_test`, `gui_smoke_test`, `insight_visual_style_test`,
  `completion_test`, and `git diff --check`. Transition/label hover now exposes
  wrapped full-condition tooltips, while state hover links canonical nodes,
  aliases, and incident transitions without changing selection. Deterministic
  fuzz coverage runs 100 fixed seeds through the unchanged full invariant
  suite with no seed exceptions. Alias nodes are now constrained to exact
  same-name canonical copies; implicit endpoints such as `default` are compact
  dashed, self-canonical nodes and do not propagate selection/hover to unrelated
  states. Twenty-five fuzz graphs exercise this implicit-endpoint path.
  Reciprocal ordering uses a crossing-preserving order postprocess and
  layer/node minor-coordinate alignment. Adjacent canonical pairs remain
  separated arcs; aliased long reciprocal edges use short orthogonal routes.
  Crossing counts sample rendered reciprocal
  curves, and disconnected/unreachable components are packed into separate
  column regions before routing. Remaining risk: dense same-source
  fan-out can still occupy a wide channel, but current snapshots show no
  clipping, detached labels, or geometry overlap.
- Current execution session Phase B is complete in the working tree:
  Workspace Session State v1 writes and reads workspace-root `.zs` JSON for
  workspace configuration, relative workspace tabs with cursor/scroll/active
  state, main-window geometry/dock state, and scanned-file metadata. Restore is
  explicit through Global Control `ow s restore`; `ow s save` and `ow s clean`
  are also exposed without changing `ow 1`, `ow 2`, or `ow r`. Missing files
  are skipped, external config paths are reported, and full semantic cache
  snapshots remain intentionally out of v1. Acceptance fix: closing the active
  workspace saves `.zs` before closing its tabs so the saved tab list is not
  overwritten empty. Verification so far: Debug `completion_test` and
  `gui_smoke_test` passed.
- Signal Usage Hotspot is available from the editor source-symbol context action
  `Signal Usage Hotspot` and the RTL Insights `Usage Hotspot` action. It renders
  Track lanes from report items, Matrix heat cells from report summaries, role
  filters/search, item drill-down, inspector metadata, explicit empty/error
  states, and source reveal/flash navigation. Future polish can improve
  asynchronous progress and very-large-report virtualization.
- Most recent completed milestone: App Shell visual QA polish. Main window
  shell, common Qt Widgets controls, Global Control, and Fold Shelf active
  shell state use `InsightVisualStyle` token/QSS paths, and the main editor tab
  bar no longer carries a hard-coded `ModeManager` palette.
- Current redundancy cleanup baseline: Navigation UI is the Files/Design pane
  only; legacy Module/Symbol tab adapters, manager view branches, and
  SymbolOutline-row UI navigation wiring are removed. Service-owned module
  hierarchy and symbol outline queries remain semantic data APIs. Design child
  activation binds the editor to the exact active-top/instance path, Files or
  direct-open navigation returns it to an unbound default, and navigation
  history restores the recorded instance context.
- The prior recommendation to limit work to app-shell visual polish is
  superseded by the current corrective milestone above. Future changes must
  preserve the unified effective-value and analysis-lifecycle boundaries.
- Previous completed milestone: Signal Usage Hotspot v2 integration. The
  A-branch service/report API is connected to the B-branch visual helpers in a
  dual-mode panel with real `chl_ctrl.sv` / `mcs` coverage and enum-value
  hotspot filtering.
- Previous completed milestone: State Transition Graph real usability repair.
  FSM discovery handles structural `current <= #delay next` update pairs,
  including `#TP`, `# TP`, and `#(...)` controls common in the real corpus,
  while State Transition Graph remains gated to discovered next-state roles
  only. Current-state roles still reject with an explicit next-state selection
  reason.
- Verification for State Transition Graph real usability repair now rests on
  focused regression and GUI smoke coverage: `completion_test`,
  `relationship_test`, `gui_smoke_test`, `jump_test`,
  `insight_visual_style_test`, and feature-specific CTest guards.
- Corpus audit is retired as an acceptance signal. Future GUI-discovered
  defects should be reduced into small fixtures in the focused tests.
- Most recent completed milestone: import-aware SystemVerilog package symbol
  visibility. Unqualified package members are available to definition, hover,
  and explicit command semantic queries only from active `import pkg::*;`
  context; local/module symbols win over imports; ambiguous same-name imported
  package members do not produce a random jump.
- Recently completed workflow baselines also include user template JSON usage,
  user template JSON phase 1, explicit header/include and package import
  commands, semantic `;m` module instantiation Slot Mode, Macro / Define
  first-class semantics, Package Tools phase 1, References / Relationships
  workflow closure, and Fold Shelf persistence/restore/management.
- `;pk` remains the explicit package import insertion entry. Package Tools
  remain package-file editing tools, and `pkg::symbol` completion was not added.

Status history:

- FSM readability follow-up is complete. FSM graph edges show only dark bold
  unboxed `C#` condition IDs while the transitions table maps each ID to full
  condition/source metadata; graph bodies no longer draw current/next signal
  boxes; self-loops are compact curves; `chl_ctrl.sv` remains covered by
  body-fit and compact alias regressions without modifying the RTL fixture;
  aliases share canonical colors while remaining dashed; and pure self-loop
  dead/end states are called out in graph, inspector, and table surfaces.
  Verification passed from `build` for `completion_test`, `relationship_test`,
  `gui_smoke_test`, and `insight_visual_style_test`.
- RTL Insight core-view modernization is complete. Module Block Diagram row
  selection syncs graph and inspector state; State Transition regression
  coverage rejects `cs`/`ns` names without a structural current<=next FSM pair;
  Wave Preview coverage includes selected module/always static lane/event
  generation and the larger `cpld_preproc.sv` fixture without modifying user
  RTL files. Debug verification passed for `completion_test`,
  `relationship_test`, `gui_smoke_test`, `insight_visual_style_test`,
  `signal_usage_hotspot_panel_test`, plus `git diff --check`.
- Package/import semantic visibility repair is complete. `PackageVisible`
  package parameters, localparams, typedefs, enums, and structs are no longer
  treated as globally visible for unqualified lookup. Definition, hover, and
  explicit command semantic queries use the current file/scope import context,
  including
  `import pkg::*;`; local/module declarations take priority over imported
  package members for definition, hover, and explicit command queries; and conflicting same-name members from multiple imported
  packages are reported as ambiguous rather than resolved randomly. `;pk`
  remains an import insertion command only, Package Tools behavior is
  unchanged, and no ordinary `pkg::symbol` popup exists.
- Verification for package/import semantic visibility repair passed in the
  Debug build: `cmake --build . --target completion_test relationship_test
  gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Macro / Define first-class semantics are complete. Static `define` records
  enter the semantic index as Macro definitions; outline shows object-like and
  function-like macro names while keeping `ifdef` / `ifndef` / `else` /
  `endif` out of the main symbol list; `` `MACRO`` goto prefers current-file
  definitions and then workspace/include-visible definitions; hover shows
  definition location plus signature/body text; Find References returns the
  `define` row and all indexed backtick uses; undefined macro diagnostics are
  supplemented under the Semantic index owner; and inactive preprocessor
  branches are grayed conservatively from configured defines plus static
  current-file `define` / `undef` state.
- Macro / Define boundaries: no Vivado `.xpr` / Tcl parsing, no new define
  configuration UI, no complete SystemVerilog preprocessor or argument
  substitution engine, no formatter text mutation, and no ownership changes to
  Package Tools, Command Layer, Slot Mode, References, Relationships, or Problems
  workflows.
- Verification for Macro / Define first-class semantics passed: `cmake
  --build . --target completion_test relationship_test gui_smoke_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Macro / Define acceptance stabilization is complete. The GUI smoke
  reference/relationship dock regression now uses an isolated local semantic
  snapshot injected into `ReferenceService`, `RelationshipService`, and
  `HierarchyService`, resets dock filters at fixture entry/exit, and restores
  services to the global semantic index. This fixes CTest-order state leakage
  without weakening reference/relationship assertions or expanding the Macro /
  Define feature scope.
- Verification for the acceptance stabilization passed: `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`; `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed twice consecutively.
- `;m` module instantiation semantic completion is complete. Selecting a
  module through existing `;m` now emits a full named instantiation from
  semantic parameter and port records when available, omits `#(...)` for
  modules without parameters, falls back to the previous simple instantiation
  when parameter/port records are insufficient, and starts Slot Mode with
  instance, parameter value, then port connection slots. `;;m` remains the
  module definition skeleton path; no Package Tools phase 2, include/package/
  import workflow, new command, or Command Layer behavior was added.
- Verification for `;m` semantic instantiation passed: `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed.
- Header/include command convergence is complete. The old hidden `` `include
  `` + space entry is removed. `;h <query>` searches existing header/include
  candidates through the existing service-owned candidate/filter path and
  inserts a full SystemVerilog `` `include "..."`` statement. `;h -n <name>`
  creates a header before insertion, defaults suffix-less names to `.svh`,
  accepts `.vh` / `.svh`, prefers the current file directory, and refuses to
  overwrite existing files. `;;h` remains absent; no package/import completion,
  Command Layer command, or Global Control command was added.
- Verification for header/include convergence passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test`; `cmake --build .
  --target relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Package import explicit entry is complete. `;pk <query>` searches existing
  package semantic records through the completion service and inserts
  `import pkg_name::*;`. `;p` remains parameter completion, Command Layer
  `go package` remains package picker / package navigation, Package Tools remain package-file
  editing tools, and `;;pk` remains absent. No `pkg::symbol` completion,
  cross-file package management, Command Layer command, or Global Control command
  was added.
- Verification for package import explicit entry passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- User template JSON phase 1 is complete. `UserTemplateService` reads compact
  `;;` user templates from global `user_templates.json` and active-workspace
  `.zeroslack/user_templates.json`, with workspace records overriding global
  records by command token. Built-in templates and reserved `;;h` / `;;pk`
  holes cannot be overridden; invalid JSON, invalid commands, invalid slots,
  and conflicts are reported and ignored. Valid user templates enter the
  existing `;;cmd` completion path and start Slot Mode when slot metadata is
  present. No template GUI editor, import/export, macro recorder, new `;cmd`,
  Command Layer command, Global Control command, or Package Tools change was added.
- Verification for user template JSON phase 1 passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- User template JSON usage entry is complete. Tools / User Templates now has
  actions to open or create the global `user_templates.json`, open or create
  the active workspace `.zeroslack/user_templates.json`, and reload templates.
  Missing files are created with the empty legal skeleton only. Reload reports
  loaded/ignored counts in the status bar and lists invalid JSON, invalid
  commands, invalid slots, reserved tokens, and conflicts with file, command,
  field, and reason. No GUI template editor, import/export, variable system,
  macro recorder, new `;cmd`, new `;;cmd`, Command Layer, Global Control, or
  Package Tools feature was added.
- Verification for user template JSON usage entry passed in the Release build:
  `cmake --build . --target completion_test gui_smoke_test relationship_test`;
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.
- Verification Baseline Repair after Package Tools phase 1 is complete. This
  was baseline repair only, not Package Tools phase 2: Release
  `relationship_test` rebuild failures were traced to generated MinGW
  compile/link rules missing the compiler bin on `PATH`, and CMake now injects
  the detected compiler directory through `cmake -E env PATH=...` for generated
  compile and link rules. No `relationship_test` target was skipped and no
  assertions were weakened.
- Package Tools coverage is reinforced: `completion_test` verifies non-empty
  insert text and slot metadata for `parameter`, `localparam`, `typedef enum`,
  `typedef struct`, `typedef struct packed`, and `function`, plus exact
  `typedef struct packed` text and an exact six-tool count; `gui_smoke_test`
  verifies all six stable package-tool buttons exist, exactly six Package
  Tools buttons are present, and still covers one clicked insertion into Slot
  Mode. Existing `;;cmd` template behavior is unchanged.
- Verification for this repair passed: Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'` passed.
- Package Tools phase 1 is complete: when the active cursor is inside a
  parseable SystemVerilog package, the editor shows a lightweight Package Tools
  bar with `parameter`, `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function` insertion buttons. Insert positions
  are derived from Tree-sitter package structure, stay inside the current
  package, default before `endpackage`, append near same-kind direct package
  items when present, and reject module/interface/program scope with clear
  failure messages. Inserted templates start Slot Mode on all editable slots.
  Existing `;;cmd` behavior is unchanged; package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Focused verification for Package Tools phase 1 passed: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "completion_test|gui_smoke_test" --output-on-failure` passed.
- References / Relationships workflow closure is complete: source-symbol
  context actions are stable, References and Relationships panels keep query
  context and explicit empty reasons, result rows jump/copy through one
  validated path, graph entry points call only existing graphs, and navigation
  history is isolated per workspace.
- Acceptance repair for the workflow closure is complete: RTL Insights FSM
  Graph and State Transition Graph panels render the service report's
  state-register and next-state signal nodes, preserve existing transition
  edges, add the existing-data signal-flow edge, and let next-state signal
  nodes invoke the existing navigation handler. No FSM extraction semantics,
  State Transition trigger rules, package tools, Wave Preview behavior, or
  graph algorithms were added; Track 15 owns macro/define semantics.
- Final verification for the workflow closure passed: Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; Release `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed.
- Scoped named-action cleanup is complete: the obsolete named-action service,
  editor mode path, shortcut hooks, dispatcher path, tests, and user-facing
  docs were removed while preserving normal editor shortcuts, context menus,
  Command Layer, `;cmd`, and `;;cmd` boundaries.
- Focused verification for the cleanup: Release `completion_test` and
  `relationship_test` passed through `ctest`; Release `gui_smoke_test` target
  compiled and linked without launching the executable.
- Interactive RTL Insights graph rendering status: Module Block Diagram renders
  selectable QGraphics nodes/edges with service-backed navigation, and State
  Transition Graph / FSM Graph render service-backed Sugiyama FSM layouts.
- Focused verification for FSM rendering now covers pure layout invariants,
  GUI smoke rendering, selected-edge highlighting, and snapshot PNG output.
- RTL Insights FSM Graph rendering has been rewritten. `FsmGraphService` still
  filters out state-register candidates that lack parsed case-derived
  transitions, keeps real structural FSMs present, and excludes non-case
  enum/register noise.
- Focused verification for the FSM Graph repair: Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.
- Command Layer presentation is complete: its compact application-level panel
  shows the query, ranked canonical command names, descriptions, current
  selection, and non-modal failure reasons.
- Module Block Diagram container rendering and drill-down is complete: RTL
  Insights now renders the selected module as a large container on a grid-backed
  canvas, arranges child modules inside it, exposes `-` / `Fit` / `+` zoom
  controls alongside mouse-wheel zoom, and re-renders the diagram around a
  double-clicked module after navigation.
- Focused verification for the Module Block Diagram repair: Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` and `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.
- Command Layer editing integration is complete: `add signal`, `add parameter`,
  `add port`, `clear right`, and `select begin end` reuse the existing editor
  operations. Clear-RHS remains line-oriented, creates Slot Mode fill points,
  and reports failures without mutating the document.
- The persistent mode, toggle chord, editor badge, old short-code registry,
  parameter/signal declaration pickers, instance insertion, and continuous
  assignment insertion are removed. F24 now owns the held lifecycle, while
  ordinary backtick input is unaffected.
- Column Selection keeps visual-column behavior. Column Number Tool is outside
  Command Layer and remains available through Alt+C.
- Workspace Project Configuration And Diagnostics Workflow is complete:
  `WorkspaceConfigurationService` persists per-workspace include dirs,
  defines, ignored dirs, file extensions, and optional top module;
  `WorkspaceConfigurationDialog` provides the workspace-focused UI;
  `WorkspaceManager::setWorkspaceConfiguration()` applies settings through
  `ProjectModel` and existing ignored-directory validation; and workspace
  analysis keys include the configuration fields that affect Slang analysis.
- Diagnostics workflow completion: Problems rows show diagnostic owner,
  current-file diagnostic summary follows the active tab, diagnostics state
  displays current/stale/analyzing/background, Problems jumps validate target
  files/locations and flash the revealed line, and F8 / Shift+F8 use
  `DiagnosticNavigationService` to move next/previous by the Problems panel's
  scope and severity filters.
- Focused verification for the workspace configuration / diagnostics workflow:
  Release `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` passed. Release
  `ctest -R "^relationship_test$" --output-on-failure` was attempted and
  failed in existing RTL Insights FSM graph/panel checks, not in this
  milestone's workspace configuration or diagnostics workflow.
- Workspace cache repair after the workflow milestone is complete:
  `WorkspaceManager` now applies workspace configuration silently during
  workspace activation/cached restore, so cached workspace switching no longer
  emits `filesScanned` / `workspaceListChanged` or invalidates the Navigation
  design hierarchy cache. Explicit Workspace Configuration edits still notify
  and refresh file filtering. The Defines table no longer shows misleading
  Up/Down buttons because define order is not persisted.
- Focused verification for the cache repair: Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `ctest -R
  "^completion_test$" --output-on-failure` passed. Release `ctest -R
  "^gui_smoke_test$" --output-on-failure` initially still failed on existing
  VENDOR ctrl-click and Wave Preview checks, but the four workspace/navigation
  cache regression checks passed.
- GUI smoke baseline repair is complete: VENDOR ctrl-click, Wave Preview,
  RTL Insights, and Problems diagnostics failures were confirmed as current
  regression coverage rather than obsolete features. The fix stabilized fixture
  path lookup, Tree-sitter scope fallback for safe Wave Preview ranges, RTL
  Insights synthetic snapshot isolation, and the external-diagnostic
  preservation probe.
- Focused verification for the GUI smoke baseline repair: Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `ctest -R "^completion_test$" --output-on-failure` passed; Release
  `ctest -R "^gui_smoke_test$" --output-on-failure` passed twice.
- G0 Documentation And Goal Reset is complete and pushed in commit `bc2c059`.
- G4 Command Layer Refactor is complete: `commandlayercommandregistry` owns
  exactly ten canonical command records and deterministic ranked matching;
  `CommandLayerCoordinator` owns the application-level F24 lifecycle, panel,
  help, picker handoff, and dispatch; `CommandLayerService` owns module/package
  candidate shaping and module-relative line resolution.
- `g100`, `go100`, and `go 100` are parsed before fuzzy matching. All commands
  require Enter, and picker completion returns to search only while F24 remains
  held.
- G1.1 Editor Daily Action Inventory is complete: the baseline captured
  `Ctrl+F` Find, formatter context-menu actions, shortcut/context-menu
  metadata, and the missing daily action entry points.
- G1.2 Comment And Uncomment Entry Points is complete:
  active-line and selected-line line comments are available through `Ctrl+/`,
  `Ctrl+Shift+/`, and editor context-menu actions, without reviving active
  `;:` commands.
- Focused verification for G1.2: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.3 Indent And Unindent Entry Points is complete:
  active-line and selected-line indentation is available through `Ctrl+]`,
  `Ctrl+[`, and editor context-menu actions, without reviving active `;:`
  commands.
- Focused verification for G1.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.4 Replace And Goto Line Entry Points is complete:
  replace is available through `Ctrl+H` and editor context-menu action; goto
  line is available through `Ctrl+G` and editor context-menu action;
  non-dialog editor API coverage checks valid/invalid goto line, replace-next,
  selected replacement, replace-all, and replace-all undo behavior.
- Focused verification for G1.4: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G2.1 Slot Mode Model And Editor State is complete: Slot Mode ownership is
  documented as template-relative slot metadata from `CodeTemplateService`,
  insertion/start routing through `EditorCompletionWorkflow`, and editor-local
  session ownership in `MyCodeEditor` state; entry, Tab/Shift+Tab navigation,
  completion, Esc cancel, natural exit, stale-session invalidation, and future
  verification cases are defined without changing template behavior.
- Focused verification for G2.1: documentation inspection plus `git diff --check`.
- G2.2 Parameter Template Slot Mode is complete: `;;p` and `;;lp` produce
  ordered name/value slot metadata; parameter template activation starts Slot
  Mode through the existing completion workflow; `MyCodeEditor` state owns
  active slot ranges, highlighting, Tab/Shift+Tab navigation, Esc cancel,
  edit-driven range shifts, and cursor-outside stale exit; non-parameter
  template behavior is preserved. Entering the held Command Layer does not
  clear Slot Mode by itself.
- Focused verification for G2.2: Release `completion_test` passed with 625
  checks and 0 failures; Release `completion_test` and `gui_smoke_test` targets
  compile/link, with `gui_smoke_test` not launched.
- G2.3 Signal Template Slot Mode is complete: `;;l`, `;;w`, and `;;r` produce
  a signal-name slot; signal template activation reuses the existing Slot Mode
  session path; signal-name editing preserves packed/unpacked dimensions and
  Tab / Shift+Tab keep cycling until Esc exits Slot Mode. Parameter template
  behavior is preserved.
- Focused verification for G2.3: Release `completion_test` passed with 630
  checks and 0 failures; Release `completion_test` and `gui_smoke_test` targets
  compile/link, with `gui_smoke_test` not launched.
- Verification flow now includes checking for external Windows application-error
  or memory-read dialogs when CTest appears stalled.
- G3.1 Clear-RHS Report/Service Design is complete:
  `RtlBatchEditService::planClearAssignmentRhs` now returns a non-mutating
  selected-text report with replacement text, per-assignment edit metadata,
  document-relative RHS offsets, zero-length `rhsN` template slots, and failure
  reasons. Supported first-pass forms are complete selected blocking,
  nonblocking, and continuous `assign` statements. Unsupported selections fail
  without mutation for empty selection, incomplete statement, declaration
  initializer, control-flow statement, macro statement, ambiguous top-level
  assignment, unmatched delimiter, unterminated string, or unterminated comment.
- G3.2 Clear-RHS Editor Command is complete:
  `MyCodeEditor::clearSelectedAssignmentRhs` routes selected text through
  `RtlBatchEditService`, applies successful reports in one undoable edit block,
  leaves the replacement range selected, and reports validation failures
  without text mutation. The obsolete named-action layer has been removed.
- G3.3 Clear-RHS Slot Mode Integration is complete:
  successful clear-RHS execution starts Slot Mode from the
  `RtlClearAssignmentRhsReport` `rhsN` template-slot metadata while preserving
  one undoable text replacement. G3.2 failure behavior is unchanged; Slot Mode
  start, slot editing, Tab / Shift+Tab cycling, Esc exit, undo restore, and
  declaration rejection are covered by focused tests.
- G5.1 Workspace Open/Recent Behavior Audit is complete:
  `WorkspaceManager` owns workspace open/close/switch, alias rename, cached
  scanned-file restoration, directory scan lifecycle, file watching, and recent
  workspace persistence. Recent entries are stored through `QSettings`,
  normalized, deduplicated, capped at 20, and exposed through
  `recentWorkspaceEntries()`. Global Control currently displays `ow 1`, `ow 2`,
  and numeric `ow <num>` only; `ow r` is hidden by `GlobalControlService` but
  still handled internally by `MainWindow` through the existing recent
  workspaces dialog. `ProjectModel` already supports `ignoredPaths` filtering,
  but there is no workspace-owned service/model path to set ignored directories.
- G5.2 Ignored-Directory Model/Service Path is complete:
  `WorkspaceIgnoreService` validates and normalizes ignored-directory requests,
  while `WorkspaceManager::setIgnoredDirectories()` applies them through
  `ProjectModel`, refreshes current file lists, updates workspace entry state,
  and preserves per-workspace ignored directories across cached switches.
  Cached restore keeps raw scanned files so clearing ignores can reveal
  previously hidden files again. No ignored-directory UI or `ow r` work was
  added in this milestone.
- Focused verification for G5.2: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `completion_test` passed directly with 669 checks and 0 failures; `ctest -R
  "^completion_test$"` passed.
- G5.3 Restore ow r Global Control Command is complete:
  `GlobalControlService` displays `ow r` as an `ow` domain child command, exact
  `ow r` queries return the recent-workspaces command instead of the numeric
  count hint, and dispatch continues through the existing `MainWindow` recent
  workspaces dialog path. No session restore, include dirs/defines UI, recent
  files, or broader workspace UX was added.
- Focused verification for G5.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; Release
  `completion_test` passed directly with 672 checks and 0 failures; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G6.1 Completion Command Responsibility Audit is complete:
  `;cmd` is documented as inline semantic command completion owned by
  `InlineCommandMode`, `CompletionCommandMode`, `CompletionService`, and
  `CompletionSemanticQuery`; `;;cmd` is documented as template expansion owned
  by `CodeTemplateService` and applied through `EditorCompletionWorkflow` /
  Slot Mode; the obsolete named-action layer is documented as removed;
  Command Layer and Global Control are documented as separate command surfaces with
  separate registries, services, and coordinators.
- G6.1 conflict boundaries are documented: do not revive `;:cmd`, keep `;cmd`
  semantic, keep `;;cmd` template-only, keep Command Layer application-level,
  and keep Global
  Control app/workspace/global.
- Focused verification for G6.1: documentation inspection plus
  `git diff --check`.
- G6.5 Tab-only inline abbreviation expansion is complete:
  `inlinecommandmode` now exposes a suffix parser for registered abbreviations;
  ordinary text changes do not enter any completion path;
  `EditorCompletionWorkflow` owns only explicit Tab sessions with
  saved absolute range and anchor context; popup Esc cancellation preserves the
  original abbreviation; unique candidates replace directly; multi-candidate
  results use the existing popup; Slot Mode, Column Mode, existing popup Tab,
  bracket-range Tab, and normal Tab keep their priority.
- Focused verification for G6.5 passed in the Debug build:
  `completion_test`, `gui_smoke_test`, `relationship_test`, and
  `insight_visual_style_test`.
- G6.6 Inline abbreviation acceptance hardening is complete:
  `EditorCompletionWorkflow::handleCursorPositionChanged()` cancels active
  inline sessions when the cursor no longer equals the saved replacement end or
  text is selected; `inlineAbbreviationSessionValid()` repeats the same check
  before replacement. `InlineCommandMode` removed the raw `indexOf("//")`
  guard and relies on a single lexical state machine for strings, line
  comments, and block comments. Regression coverage includes multi-candidate
  popup cursor movement, ordinary Tab after cancel, cross-line block comments,
  URL strings, closed block comments containing `//`, real line comments, and
  commands inside strings.
- Focused verification for G6.6 passed in the Debug build:
  `completion_test`, `gui_smoke_test`, `relationship_test`, and
  `insight_visual_style_test`.
- G6.2 User Template Storage/Query Model is complete:
  `UserTemplateService` now owns validation, persistence, reload, exact-token
  query, add/update, remove, and clear operations for compact `;;` user
  template records. Records are maintained in JSON files: a global
  `user_templates.json` and an optional workspace
  `.zeroslack/user_templates.json`. The service preserves
  `CodeTemplateSlotList` metadata, exposes `CodeTemplateItem`-compatible query
  results, and reports invalid JSON, invalid commands, invalid slots, and
  conflicts without mutating built-in templates.
- Focused verification for G6.2: Release `completion_test` and
  `gui_smoke_test` passed through `ctest -R
  "^(completion_test|gui_smoke_test)$" --output-on-failure`.
- G6.3 Custom Abbreviation Resolution is complete:
  `CustomAbbreviationService` now owns validation, persistence, reload, prefix
  query, intent-scoped query, exact resolution, add/update, remove, and clear
  operations for compact custom abbreviations. Records persist under
  `QSettings` `customAbbreviations/items`, reject duplicate aliases, invalid
  command tokens, command-surface marker aliases, and reserved `;:` action
  tokens, and resolve only to existing `;cmd` semantic command tokens or
  `;;cmd` template command tokens. Built-in `;cmd` / `;;cmd` behavior remains
  unchanged; custom abbreviations are not Command Layer or Global Control commands.
- Focused verification for G6.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G6.4 Integrate User Templates With Slot Mode is complete:
  `CompletionService` now consumes service-owned `UserTemplateService` records
  in the inline `;;cmd` template path. Normal use falls back to the singleton
  user-template service; tests can inject global/workspace JSON paths. Compact user
  `;;token ` commands are recognized as `CodeTemplate` intent without changing
  `;cmd`, Command Layer, Global Control, or custom abbreviation namespaces. Built-in
  templates keep priority, workspace user templates override global user
  templates with the same command token, and reserved `;;h` / `;;pk` holes stay
  unavailable in this phase. Activation preserves insert text, primary
  selection, and `CodeTemplateSlotList` metadata through the existing
  `EditorCompletionWorkflow` / `MyCodeEditor` Slot Mode path.
- Focused verification for G6.4: Release `completion_test` and
  `gui_smoke_test` passed through `ctest -R
  "^(completion_test|gui_smoke_test)$" --output-on-failure`.
- G7.1 Fold Shelf Persistence Schema And Ownership is complete:
  existing owners are documented. `FoldBlockShelfModel` owns the in-memory
  shelf item list and add/consume/remove/clear lifecycle; `FoldShelfItem`
  carries id, alias, text, source file/module, source line range, line count,
  origin kind, consumed, and stale; MIME helpers are drag/drop transport only;
  `FoldBlockShelfPanel` renders and manages preview/delete/drop/drag UI;
  `EditorFoldingController` owns shelf mode, custom fold extraction, source
  deletion, and insertion; `MainWindow` wires dock/model visibility and restore
  dispatch. The durable schema is versioned under `foldShelf/v1/items`, scoped
  by normalized workspace root, and persists the current item fields with
  normalized `sourceFile`. Persistence policy belongs in the model/service
  layer, not the panel. G7.2 same-file restore requirements are documented
  without implementing restore behavior in G7.1.
- Focused verification for G7.1: source inspection of
  `foldblockshelfmodel`, `foldblockshelfpanel`, `editorfolding`,
  `mainwindow`, and existing Fold Shelf tests, plus `git diff --check`.
- G7.2 Persist And Restore Same-File Fold Shelf Items is complete:
  `FoldShelfPersistenceService` owns versioned QSettings-backed storage under
  `foldShelf/v1/workspaces/<scope>/items`, scoped by normalized workspace root
  and normalized source paths. `FoldBlockShelfModel` attaches the persistence
  service and workspace root, reloads persisted items on root changes, and
  persists add, consume, stale mark, remove, and clear mutations.
  `MainWindow` wires the singleton service into the model, updates model scope
  on workspace activation/close, and marks items stale when same-file restore
  fails because the source file is unavailable, cannot be opened, or cannot be
  inserted at the recorded location. Successful restore removes the item only
  after insertion. `FoldBlockShelfPanel` remains storage-policy free.
- Focused verification for G7.2: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G7.3 Restore Fold Shelf Items Across Files is complete:
  `FoldShelfRestoreService` owns explicit active-editor restore reports,
  validates model/item/editor availability, restores selected persisted shelf
  items through the existing editor fold-shelf insertion API, and consumes or
  removes items only after successful insertion. Missing active editor or failed
  insertion marks the item stale with a clear failure reason. `FoldBlockShelfPanel`
  stays storage-policy free and only emits restore requests; `MainWindow` wires
  the selected shelf item to the active editor/cursor line.
- Focused verification for G7.3: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.
- G7.4 Add Fold Shelf Rename/Search/Clean Management Actions is complete:
  `FoldBlockShelfModel` owns rename, query/filter, and stale/consumed cleanup
  behavior. Rename trims aliases, rejects blank aliases, persists successful
  changes, and emits the existing change signal. Filtering reads only shelf
  item data, and clean management removes stale or consumed items through an
  explicit model mutation. `FoldBlockShelfPanel` provides the search field and
  Rename / Clean Stale/Consumed buttons while remaining a model consumer.
- Focused verification for G7.4: `git diff --check`; Release
  `completion_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G8.1 Add Signal Kernel Graph Fanout Grouping Reports is complete:
  `SignalKernelGraphReport` now carries service-owned high-fanout grouping
  metadata for dense input/output sides while preserving raw inputs, outputs,
  edges, and module groups. `SignalKernelGraphFanoutGroup` records group id,
  role, input lane, module, display name, grouped node ids, per-group count,
  total side count, cross-module state, and high-fanout state. Grouping is
  emitted only when a graph side reaches the service-owned threshold; UI
  collapse/expand, filtering UI, and graph search were not added.
- Focused verification for G8.1: `git diff --check`; Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G8.2 Render Collapsible Signal Kernel Graph Fanout Groups is complete:
  `SignalKernelGraphPanelCoordinator` renders G8.1 fanout groups as
  collapsible summaries. New group keys default to collapsed; collapsed groups
  hide raw nodes, render a summary item, and route grouped edges through that
  summary while deduplicating only collapsed group edges. Expanded groups show
  raw nodes with their existing preview, navigation, and rebase handlers plus a
  clickable header to collapse again. Collapse/expand state stays in the panel.
- Focused verification for G8.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G8.3 Add Signal Kernel Graph Filtering And Search is complete:
  `SignalKernelGraphPanelCoordinator` now exposes graph search plus
  input/output/cross-module filters above the graph. Filtering rebuilds the
  visible graph from existing `SignalKernelGraphReport` node data, preserves
  fanout collapse/expand state, hides groups with no visible members, and keeps
  raw visible-node preview, navigation, and rebase behavior. Graph search
  highlights/focuses visible node matches and collapsed fanout-group matches
  without expanding groups implicitly. No workspace scan or UI-side Slang work
  was added.
- Focused verification for G8.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.1 Selected-Always Wave Preview Entry And Scoped Report is complete:
  `TSDocument` now exposes current/selected `always_construct` scope targets and
  rejects selections spanning multiple `always` blocks. `MyCodeEditor` exposes
  that editor-local target to `MainWindow`, and the Wave Preview dock refreshes
  from the selected/current `always` scope only. The report still flows through
  existing `WavePreviewQuery` / `WavePreviewService` scoped fields; no simulator
  behavior, selected-module entry, workspace scan, or UI-side Slang work was
  added.
- Focused verification for G9.1: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.2 Selected-Module Wave Preview Entry And Scoped Report is complete:
  `TSDocument` now exposes current/selected module/interface/program scope
  targets and rejects selections spanning multiple RTL containers. `MyCodeEditor`
  exposes that editor-local target to `MainWindow`; Wave Preview uses selected
  `always` scope first and falls back to selected/current module scope only when
  no `always` scope is active. The report still flows through existing
  `WavePreviewQuery` / `WavePreviewService` scoped fields; no simulator
  behavior, workspace scan, UI-side Slang work, or sketch readability polish was
  added.
- Focused verification for G9.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G9.3 Wave Preview Sketch Readability Polish is complete:
  `WavePreviewPanelCoordinator` renders compact canvas legends for trace
  sketches and assign/blocking/nonblocking code sketches. The Wave Preview tree
  adds Scope and Legend overview rows ahead of detailed trace, warning,
  activity, lane, and event rows. The selected-`always` and selected/current
  module entry behavior is preserved, and the UI remains a scoped report
  consumer with no workspace scan, UI-side Slang work, or simulator behavior.
- Focused verification for G9.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G10.1 State Transition Trigger Gating is complete and now structural:
  `StateTransitionTriggerService` asks `FsmGraphService` for the selected
  symbol's discovered FSM role. `EditorSourceNavigationQuery` enables and
  dispatches `Show State Transition Graph` only for selected next-state roles;
  selected current-state roles, ordinary symbols, and missing module context do
  not dispatch. Names such as `ns`, `next_state`, `cs`, and `current_state` are
  examples/display hints rather than gates. Accepted requests route through
  `EditorCoordinator` and `SemanticPanelRefreshCoordinator` to the existing RTL
  Insights FSM graph path.
- Focused verification for G10.1: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^completion_test$"` and `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G10.2 State Transition Service-Owned Report is complete:
  `StateTransitionGraphService` validates accepted requests through
  `StateTransitionTriggerService`, builds module FSM graph data through
  `FsmGraphService`, and filters the result to the selected next-state signal.
  Accepted structural next-state role reports expose selected signal, module,
  state count, transition count, and the matching `FsmGraph`; rejected
  current-state roles, no-FSM modules, and symbols without a matching
  next-state graph return service-owned failure reasons. The existing
  state-transition UI route consumes the new service report through RTL
  Insights without moving extraction or filtering into UI. No workspace scan or
  UI-side Slang work was added.
- Focused verification for G10.2: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` and `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G10.3 State Transition Graph UI And Navigation Evidence is superseded by the
  new Sugiyama FSM renderer: RTL Insights now renders graph scene items from
  `fsmgraphlayout` and keeps controls plus transition-table data from the
  service-owned `StateTransitionGraphReport`. G10.1 gating and G10.2 selected
  next-state filtering are preserved; extraction and report shaping remain
  outside UI code.
- Focused verification for G10.3: `git diff --check`; Release
  `completion_test`, `relationship_test`, and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` and `ctest -R
  "^completion_test$"` passed. `gui_smoke_test` was not launched.

- G11.1 Module Block Diagram Service Report is complete:
  `ModuleBlockDiagramService` owns selected-module containment report shaping
  from indexed module/instance symbols plus `INSTANTIATES` relationships. The
  report returns module definition nodes, child module type names,
  instance names, instantiation edges, module definition links, instance
  declaration links, and unresolved/blackbox reasons; signal, interface, and
  non-instance relationships are filtered before UI consumption.
- Focused verification for G11.1: `git diff --check`; Release
  `relationship_test` target compile/link; `ctest -R "^relationship_test$"`
  passed.

- G11.2 Module Block Diagram Rendering is complete:
  RTL Insights renders `ModuleBlockDiagramReport` as a module-only interactive
  graph scene. The panel action renders the active module, and editor
  source-symbol routing can render selected module names as the diagram root.
  The rendered nodes and edges come from the service report, show
  module containment only, include module type plus instance names,
  keep unresolved module types visible as blackbox nodes with reasons, and do
  not show signal nodes. No UI workspace scan, UI-side Slang work, or new
  relationship extraction was added.
- Focused verification for G11.2: `git diff --check`; Release
  `relationship_test` and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` passed. `gui_smoke_test` was not launched.

- G11.3 Module Block Diagram Navigation Evidence is complete:
  rendered top-module and child-module graph nodes carry report-provided module
  definition links into the existing RTL Insights navigation path; instantiation
  edges carry child module definition links. Focused panel coverage invokes
  graph-element navigation for root and child module nodes and verifies that
  the navigation handler receives the
  corresponding module definition file, line, and column. G11.1 report
  ownership and G11.2 module-only rendering are preserved; no signal rendering,
  UI workspace scan, UI-side Slang work, or new relationship extraction was
  added.
- Focused verification for G11.3: `git diff --check`; Release
  `relationship_test` target compile/link; `ctest -R "^relationship_test$"`
  passed.

- HWA.1 Huge Workspace Owner And Verification Inventory is complete:
  the original audit established the planning, queue, analyzer, semantic-index,
  relationship, and Activity owner boundaries. Its protected-file and staged
  publication description is superseded by the current immutable all-open-buffer
  transaction: `WorkspaceSymbolAnalysisController` captures text/revisions,
  `SymbolAnalyzer` cancels/restarts and atomically publishes symbols, Slang
  value facts, and diagnostics, and relationship analysis consumes that
  published snapshot token. Analysis bands remain query/telemetry metadata.
- Existing HWA verification anchors are documented as `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`.
  No Huge Workspace UX feature was added in HWA.1.
- Focused verification for HWA.1: documentation inspection plus
  `git diff --check`.

- HWA.2 Huge Workspace Safe Verification is complete:
  the historical audit path was Release compile/link of the HWA-related
  test/harness targets rather than launching GUI executables in an environment
  that has recently shown external Windows application-error dialogs.
  `completion_test`, `relationship_test`, `large_file_perf_test`, and
  `relationship_perf_test` built successfully in the Release CMake build
  directory, and artifact inspection confirmed all four executables exist.
  `ctest` was not run in that audit milestone. This is not the acceptance
  status of the current corrective work.
- Focused verification for HWA.2: Release build of `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`;
  documentation inspection plus `git diff --check`.

- HWA.3 Huge Workspace Confirmed Status And Gaps is complete:
  the Huge Workspace docs now state that owner boundaries exist for immutable
  overlay capture, request coalescing, cancellation/restart, atomic
  symbol/value/diagnostic publication, snapshot-consistent relationship work,
  Activity telemetry, and semantic snapshot query exposure. They also state
  the remaining audit gaps: HWA.2 did not run test
  executables, full `ctest` was not run during that historical audit,
  `relationship_perf_test` was not freshly executed against
  `test_sv/huge_prj`, Activity telemetry was not revalidated in a live GUI
  session, and the prior Release `huge_prj` timings remain reference points
  rather than refreshed HWA.3 measurements. No Huge Workspace UX or behavior
  change was added.
- Focused verification for HWA.3: documentation inspection plus
  `git diff --check`.

- Post-HWA acceptance baseline repair is complete:
  `completion_test` source-symbol context menu assertions now match the current
  five-action menu after Module Block Diagram was added. The repaired baseline
  verifies ordinary signals keep Module Block Diagram disabled, selected module
  names enable it and carry the expected action/file/module payload, `ns` and
  `next_state` still enable State Transition Graph, and `cs` /
  `current_state` still reject it. No product feature behavior was changed.
- Focused verification for the baseline repair: Release `ctest -R
  "^completion_test$" --output-on-failure` and Release `ctest -R
  "^relationship_test$" --output-on-failure` both passed.

## Track 1: Editor Daily Operations Completion

Goal: provide practical entry points for daily editing actions.

Allowed actions:

- replace
- goto
- comment
- uncomment
- indent
- unindent

Milestones:

- G1.1 Inventory current commands/actions and document missing entry points.
- G1.2 Deliver one coherent action family with tests.
- G1.3 Repeat until all listed daily actions have usable entry points.
- G1.4 Replace and goto line entry points.

## Track 2: Slot Mode After Template Insertion

Goal: after `;;cmd` insertion, users can jump through editable slots instead of
manually moving the cursor.

Milestones:

- G2.1 Define slot model, editor state, cancel/exit behavior, and verification
  cases.
- G2.2 Enable slot mode for the parameter declaration template family.
- G2.3 Expand slot mode to signal declaration template families.

## Track 3: Batch RTL Edit Actions

Goal: add focused RTL batch edits that feed slot mode.

First target:

- clear assignment RHS expressions and create fill points
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`

Milestones:

- G3.1 Design the clear-RHS report/service path.
  (complete: non-mutating `RtlBatchEditService` report, failure reasons, and
  fill-slot metadata)
- G3.2 Implement the editor-local clear-RHS command for selected assignments.
  (complete: `MyCodeEditor::clearSelectedAssignmentRhs` applies the service
  report in one undoable edit block and preserves failure reasons)
- G3.3 Connect clear-RHS output to slot mode.
  (complete: successful clear-RHS execution starts Slot Mode on `rhsN` fill
  slots)
- G3.4 Expose clear-RHS as Command Layer command `clear right`.
  (complete: `clear right` dispatches through the editor-local clear-RHS path, supports
  selected/current assignment cleanup, starts Slot Mode, and keeps failures
  non-modal)
- G3.5 Make clear-RHS line-oriented for partial selections.
  (complete: selected text expands to touched complete lines before RHS
  cleanup, preserving source-order slot flow)

## Track 4: Command Layer Refactor

Goal: provide a transient application-level command surface without persistent
editor mode state or short-code registration.

Implemented scope:

- F24 press enters Command Layer and F24 release exits unfinished search;
  auto-repeat is ignored and application deactivation prevents stuck held state.
- The registry contains exactly `go <number>`, `go module`, `go package`,
  `go endmodule`, `add signal`, `add parameter`, `add port`, `clear right`,
  `select begin end`, and `help`.
- Matching ignores spaces and case, ranks exact/prefix/word abbreviation/
  subsequence results deterministically, exposes ambiguity for Up/Down
  selection, and never auto-executes a unique result.
- `g100`, `go100`, and `go 100` resolve module-relative lines before fuzzy
  matching and reject invalid or zero line numbers.
- Module/package pickers take keyboard ownership. F24 release leaves an open
  picker intact; completion returns to search only while F24 is held.
- Editing commands reuse existing editor operations. `go endmodule` is
  navigation-only, and help lists all ten canonical forms without editing.
- The old toggle chord, persistent mode state, short-code registry/prefixes,
  command-only parameter/signal pickers, instance insertion, continuous-assign
  insertion, and editor badge are removed.
- Column Number Tool is not registered in Command Layer and remains available
  through Alt+C.

## Track 5: Limited Workspace Workflow Additions

Goal: add only the explicitly allowed workspace workflow pieces.

Allowed work:

- ignored directories
- Global Control `ow r` recent workspace command

Not allowed in this track:

- session restore
- include dirs/defines configuration UI
- recent files
- broad workspace UX expansion

Milestones:

- G5.1 Audit current workspace open/recent behavior.
  (complete: owners, current `ow` behavior, hidden `ow r` compatibility, recent
  persistence, and ignored-path baseline documented)
- G5.2 Add ignored-directory model/service support.
  (complete: service validation plus workspace manager state path)
- G5.3 Restore `ow r` as a displayed Global Control child command.
  (complete: visible `ow r` child command plus existing recent dialog routing)

## Track 6: Completion / `;cmd` / `;;cmd`

Goal: support user customization and clarify command responsibilities.

Allowed work:

- user templates
- custom abbreviations
- slot mode integration
- clear boundary between `;cmd`, `;;cmd`, and Command Layer

Milestones:

- G6.1 Document current responsibilities and conflict boundaries.
  (complete: command surface ownership and conflict boundaries documented)
- G6.2 Add user-template storage/query model.
  (complete: `UserTemplateService` storage/query model)
- G6.3 Add custom abbreviation resolution.
- G6.4 Integrate user templates with slot mode.

## Track 7: Fold / Fold Shelf

Goal: make Fold Shelf durable across work sessions and files.

Allowed work:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

Milestones:

- G7.1 Define persistence schema and ownership.
- G7.2 Persist/restore same-file shelf items.
- G7.3 Restore shelf items across files.
- G7.4 Add rename/search/clean management actions.

## Track 8: Signal Kernel Graph

Goal: make dense graphs readable.

Allowed work:

- high fanout collapse/grouping
- filtering
- graph search

Milestones:

- G8.1 Add service/report grouping for high fanout.
  (complete: service/report metadata)
- G8.2 Render collapsible fanout groups.
  (complete: panel collapse/expand)
- G8.3 Add filter and in-graph search.
  (complete: panel filters and graph search)

## Track 9: Wave Preview

Goal: show waveform sketches for selected RTL context only.

Allowed work:

- selected `always` block sketch
- selected module sketch
- readability polish for the sketch

Not allowed:

- simulator behavior
- timing-accurate verification
- waveform database import
- testbench execution

Milestones:

- G9.1 Selected-`always` entry and scoped report.
  (complete: editor Tree-sitter scope target plus scoped service report)
- G9.2 Selected-module entry and scoped report.
  (complete: module fallback scope target plus scoped service report)
- G9.3 Sketch readability polish.
  (complete: canvas legend plus Scope/Legend overview rows)

## Track 10: State Transition Graph

Goal: show a state transition graph only when the selected symbol is the
discovered next-state role of a structural FSM pair.

Rules:

- Structural FSM discovery owns current/next role classification.
- Selected discovered next-state roles trigger.
- Selected discovered current-state roles do not trigger and should prompt for
  the next-state role.
- Names such as `ns`, `next_state`, `cs`, and `current_state` are examples or
  display hints only.
- Current UI presentation is controls plus transition table plus the shared
  Sugiyama FSM canvas renderer.

Milestones:

- G10.1 Trigger gating and tests for structural FSM roles.
  (complete: service-owned structural role gate plus editor source-symbol
  action)
- G10.2 Service-owned transition extraction/report.
  (complete: selected next-state report service and filtered FSM graph data)
- G10.3 Graph UI rendering and navigation evidence.
  (superseded by the new `fsmgraphlayout` renderer; the previous
  coordinator-local and `fsmdiagram*` rendering paths remain deleted)
- G10.4 Complex path layout and alias-state semantics.
  (removed/superseded: path-centric layout, branch lanes, alias-heavy nodes,
  flowBreak, and router-led behavior remain deleted)
- G10.5 FSM graph hover linkage and deterministic fuzz verification.
  (complete: edge/label condition tooltip and hover, canonical/alias/incident
  edge state hover, selected-over-hover priority, dedicated hover snapshots,
  implicit-state identity isolation, and 100 fixed-seed random FSMs including
  25 implicit-endpoint cases running the unchanged global layout invariants;
  all focused and integration suites pass)

## Track 11: Module Block Diagram

Goal: from a selected module name, show a module-only block diagram rooted at
that module.

Rules:

- show module instance and wrapping relationships only
- filter interface declarations, interface instances, and interface-typed
  unresolved children from user-facing module views
- do not show signals
- clicking the root module block jumps to the module definition
- clicking a child module/instance block jumps to the instance declaration and
  drills into the child module when its definition is resolved
- unresolved/blackbox instances remain visible with an explicit reason and jump
  to their instance declaration

Milestones:

- G11.1 Service report for module containment from selected module.
  (complete: `ModuleBlockDiagramService` owns the module-only containment
  report, definition links, instance links, instance names, and blackbox
  reasons)
- G11.2 Module-only block diagram rendering.
  (complete: RTL Insights renders `ModuleBlockDiagramReport` for active or
  selected modules, including blackbox instance nodes)
- G11.3 Click navigation to module definitions.
  (complete: focused tests verify root definition navigation, child instance
  declaration navigation, resolved child drill-down, and unresolved blackbox
  instance navigation)
- G11.4 Real usability regression coverage.
  (complete: Module Block Diagram behavior is covered by focused GUI/service
  regressions; future diagram issues should become compact fixtures rather than
  broad corpus sweeps)
- G11.5 Interface filtering and compact wrapped layout.
  (complete: module block reports reject interface roots/filter interface
  children, Design hierarchy hides interface instances, and sibling module
  blocks wrap across columns)

## Track 12: Workspace Project Configuration And Diagnostics Workflow

Goal: make workspace-level project configuration explicit and make diagnostics
usable for day-to-day navigation.

Allowed work:

- include dirs
- defines
- ignored dirs
- file extensions
- optional top module / active top
- next/previous diagnostic navigation
- Problems jump stability, flash, owner, status, and current-file summary

Not allowed in this track:

- session restore
- recent files
- new diagnostic engines beyond the existing Slang and Semantic index paths
- UI-side Slang execution
- UI-side workspace scanning
- broad workspace UX expansion

Milestones:

- G12.1 Workspace Configuration Service And Dialog.
  (complete: per-workspace persisted include dirs, defines, ignored dirs, file
  extensions, and top module with a focused dialog)
- G12.2 Diagnostics Navigation And Problems Status.
  (complete: next/previous diagnostic navigation, owner display, current-file
  summary, diagnostics state, validated jumps, and flash)
- G12.3 Verification And Documentation.
  (complete: docs updated and focused Release verification recorded)

## Track 13: References / Relationships Workflow Closure

Goal: make source-symbol navigation, references, relationship rows, and graph
result jumps easy to find, jump from, and return from.

Allowed work:

- source-symbol right-click workflow organization
- References result grouping, query context, empty-state reasons, copying, and
  jumps
- Relationships result source jumps and existing graph entry points
- unified navigation path with Back/Forward history isolation per workspace
- status-bar or panel feedback for jump failures

Not allowed:

- package tools
- macro/define semantics, which is owned by Track 15
- Wave Preview expansion
- new graph types
- complex graph algorithms
- expanded State Transition Graph behavior beyond structural FSM role gating

Milestones:

- G13.1 Unified source-symbol context menu.
  (complete: Go to Definition, Find References, Show Relationships, Signal
  Kernel Graph, State Transition Graph, and Module Block Diagram are always
  present; disabled entries expose reasons; Module Block Diagram is limited to
  existing module definitions; discovered current-state roles remain
  rejected)
- G13.2 References panel workflow closure.
  (complete: query context, empty reasons, click/activated jump with flash,
  and copy path or file:line)
- G13.3 Relationships panel workflow closure.
  (complete: source-code jumps for every result row, instance/module-definition
  jumps where existing data supports them, signal role jumps from existing
  relationship data, and only existing graph entry points)
- G13.4 Unified navigation failure feedback and history isolation.
  (complete: References, Relationships, and graph jumps share one validated
  navigation path; Back/Forward history is isolated by workspace; failures are
  visible)
- G13.5 Verification and documentation.
  (complete: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release acceptance CTest
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed)

## Track 14: Package Tools

Goal: give SystemVerilog package scopes a lightweight package-only insertion
surface for common definitions.

Allowed work:

- package-scope detection from Tree-sitter editor structure
- package-only insertion buttons for common definition templates
- same-kind direct package item append anchors
- default insertion before `endpackage`
- Slot Mode after insertion
- clear failure reasons when the current package cannot be determined

Not allowed:

- package/import semantic interpretation
- macro/define handling, which is owned by Track 15
- cross-file package management
- automatic package-wide sorting
- new graphs or insight panels
- changing existing `;;cmd` template behavior

Milestones:

- G14.1 Package Tools first phase.
  (complete: `parameter`, `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function` buttons insert package-only
  templates inside the current package and start Slot Mode)
- G14.1a Verification Baseline Repair.
  (complete: Release `relationship_test` rebuild baseline repaired and all six
  Package Tools phase-1 templates/buttons covered without Package Tools phase
  2, package/import semantics, cross-file package management, package sorting,
  graph work, or `;;cmd` changes; Track 15 owns macro/define semantics)
- G14.2 Future Package Tools expansion.
  (not started: additional package templates, semantic package handling,
  package sorting, or cross-file workflows require a new scoped request)

## Track 15: Macro / Define Semantic Workflow

Goal: make common SystemVerilog macro and preprocessor-branch workflows usable
without turning ZeroSlack into a full preprocessor.

Allowed work:

- static `define` symbol records and outline rows
- goto definition, hover, and Find References for `` `MACRO`` uses
- clearer undefined macro diagnostics as Semantic index supplements to Slang
- conservative inactive `ifdef` / `ifndef` / `elsif` / `else` / `endif`
  branch decorations using workspace configured defines and static current-file
  `define` / `undef` state

Not allowed:

- Vivado `.xpr` / Tcl parsing
- new define configuration UI
- complete SystemVerilog macro expansion or argument substitution
- formatter-visible text mutation
- changing Package Tools, Command Layer, Slot Mode, diagnostics, references, or
  relationship ownership boundaries

Milestones:

- G15.1 Macro / Define First-Class Semantics.
  (complete: outline, goto definition, hover, references, supplemental
  undefined diagnostics, and inactive branch gray decorations are available
  with conservative static behavior)
- G15.1a Acceptance Stabilization.
  (complete: GUI smoke reference/relationship dock regression now isolates
  semantic services and panel filters; Release acceptance CTest passed twice
  consecutively)

## Control-Side Acceptance Remediation Record

This record does not create a new goal or feature track. The 2026-07-30 P1
acceptance blocker for the editor action-context chip is complete:

- cursor/document refresh no longer materializes `ProjectSnapshot` or
  traverses, normalizes, sorts, or joins the workspace file table;
- workspace root, configured top, normalized file scope, stable revision, and
  deterministic identity are cached from authoritative `projectChanged`
  publication and rebuilt once per new project revision;
- the status chip and editor right-click workflow resolve through the same
  `EditorActionContextService`, while `ProjectModel`, `SemanticIndex`, and
  `HierarchyService` remain authoritative;
- identical chip presentation performs no QWidget property or style writes;
- the production-wired hot-path regression records `128/128/128/896` before
  and `0/0/0/0` after for snapshot materialization, normalization, sorting,
  and chip writes across 128 unchanged refreshes;
- affected tests, `editor_incremental_test`, `gui_smoke_test`, and full Release
  CTest passed; full CTest result is 31/31 in 148.87 seconds;
- all four protected artifacts remained byte-identical and untracked.

## Huge Workspace Status Audit

Huge Workspace is not an active feature expansion track in this goal model.
Only audit and document existing strategy status.

Audit topics:

- immutable project/open-buffer snapshot capture
- analysis bands and query ordering
- stale request coalescing, expiration, cancellation, and restart
- atomic symbol/effective-value/diagnostic publication
- relationship reuse of the published semantic snapshot token
- Activity telemetry
- Release `huge_prj` performance references

Milestones:

- HWA.1 Inventory owner classes, current behavior, and existing verification.
  (complete: owners and existing harnesses documented)
- HWA.2 Run safe verification or compile relevant targets.
  (complete: Release build of related targets passed)
- HWA.3 Update docs with confirmed status and open gaps.
  (complete: confirmed status and open audit gaps documented)

## Functional Corpus Audit

Retired. `corpus_audit_test` and `test_sv/corpus_audit_report.*` are no longer
maintained because broad corpus sweeps were expensive and produced weak
acceptance signals. Use targeted regression fixtures, GUI smoke, and
feature-specific tests for future coverage.

## Full Feature Audit

Goal: retired. Feature ownership coverage now belongs in focused regression
targets, GUI smoke, and feature-specific guards.

Milestones:

- FFA.1 Feature inventory and matrix.
  (retired: the former generated inventory and report files have been removed.)
- FFA.2 Full recursive corpus integration.
  (retired: full-feature no longer consumes `corpus_audit_test` output; it now
  records focused regression and GUI-smoke ownership.)
- FFA.3 Known issue triage without broad refactor.
  (complete: current matrix generated 2026-07-01T18:25:42Z UTC is 23 pass / 0
  fail / 0 skipped / 0 known-issue. Signal Kernel Graph budget limits are now
  expressed as deterministic case-level skipped reasons; other expensive RTL
  sweeps are likewise bounded and remain pass.)
- FFA.4 Verification and documentation.
  (retired: focused regression CTest runs now cover current executable targets
  and feature-specific guards.)
- FFA.5 Acceptance repair after independent rerun.
  (complete: stale fast-regression fixtures were repaired instead of
  converting failures to known issues. FSM assertions now use structural
  current<=next fixtures, GUI FSM graph smoke includes the clocked
  `state_q <= state_d` update required by structural discovery, `jump_test`
  resolves `test_sv/new` from the source tree when launched from the build
  directory, and package member definition checks provide explicit
  `import snap_pkg::*;` context. The former generated inventory target has
  since been removed.)

## Completion Order

Preferred starting order:

1. G0 Documentation And Goal Reset.
2. G4 Command Layer Refactor.
3. G1 Editor Daily Operations Completion.
4. G2 Slot Mode After Template Insertion.
5. G3 Batch RTL Edit Actions.

The remaining tracks may be pulled forward only when the user explicitly asks or
when a milestone naturally depends on them. Do not combine unrelated tracks in
one milestone.

## 2026-08-02 Historical A-G Implementation And Partial Acceptance Record

Status: the A-G product batches were implemented, but complete acceptance was
not established. Six targets were omitted after a historical
three-reproduction policy; omitted targets were not passed tests, and that
policy is superseded by the current acceptance repair.

- E acceptance migrated the Fold Shelf preview, workspace file-operation dry-run
  plan, and Expose Signal to Top proposal/diff from preview dialogs to the shared
  embedded `EditorHoverPopup` Peek surface. The Expose dialog implementation was
  removed, and the source inventory now names `exposesignaltotoppreview.cpp/.h`.
- Focused verification passed: `unified_peek_container_test` 17/17 checks,
  `workspace_file_operation_test` 38/38 checks, and
  `expose_signal_to_top_gui_test` 56/56 checks. The related five-target CTest
  group passed 5/5 in 7.36 seconds.
- A subsequent F audit closed the workspace file-operation
  preview-to-apply revision gap. File mutation plans now carry a SHA-256
  content snapshot and deterministic revision token through Action Registry
  dry-run output; Apply rejects a changed token before any filesystem write.
  The focused test proves rejection even when source size and modification
  time are restored after a content change. Its second minimal run passed
  36/36 checks in 0.39 seconds; the first run exposed and then corrected a
  read-only timestamp-restoration test fixture. A subsequent performance guard
  confines full-content hashing to Rename/Delete; Copy Full Path and Reveal
  retain lightweight metadata tokens. The final focused run passed 38/38
  checks in 0.38 seconds after a 9-step serial build completed in 728.1
  seconds.
- A/D continuation removed the generated `.ui` shadow actions that bypassed
  Action Registry, including legacy Cut/Copy/Paste/Undo/Redo and the global
  Esc action. New File, Open File, Save, Save As, and Open Workspace now have
  canonical IDs, execution routes, F24 commands, Action Catalog entries,
  effective-shortcut adapters, and MainWindow execution through
  `executeAction`. The reusable `FileCommandCoordinator` remains the route
  implementation. `action_registry_test` passed in 0.12 seconds after a
  19.9-second serial build; `expose_signal_to_top_gui_test` passed in 0.86
  seconds after a 34-step serial build completed in 527.3 seconds.
- D continuation routes Replace and all nine retained context-menu formatter
  actions through the MainWindow Action host instead of invoking editor
  methods directly. Context QAction adapters now expose both canonical
  `actionId` and `executionRoute`; successful context execution enters the
  application history and Command Mode `repeat action` replays it against the
  current editor. The serial target build passed 7/7 steps in 426.8 seconds,
  `Create Signal Definition` and `Edit Instance Slots` now use the same route
  with an explicit right-click cursor invocation. Their recorded invocation
  discards the stale cursor parameter after success so `repeat action`
  resolves the current cursor. The latest serial target build passed 8/8
  steps in 514.7 seconds. Source-symbol F12/Insight requests and the four
  retained Insight context actions now also enter Registry execution with a
  transient cursor parameter; MainWindow delegates the canonical source
  routes back to EditorCoordinator and records only successful panel or
  navigation execution. The latest source-action build passed 8/8 steps in
  743.9 seconds. `refactor.exposeSignalToTop` now also dispatches through its
  canonical `rtledit.signal.exposeToTop` route, records the accepted final port
  and hierarchy binding, and uses remembered parameters on repeat. High-risk
  repeat enters a dedicated plan-only embedded Peek: it supports live replan
  but exposes Close instead of Apply and never calls the transaction apply
  path. The latest serial build passed 9/9 steps in 785.5 seconds, and the
  final verbose `expose_signal_to_top_gui_test` run passed 74/74 checks in 1.04
  seconds. The separate signal-selection context menu now emits a canonical
  Registry request for `refactor.createAssignmentQueue` instead of directly
  editing; MainWindow executes the routed queue transaction and records it in
  history while the editor retains a no-host fallback. Its 50-step dependency
  rebuild passed in 640.6 seconds, and the final GUI run passed 78/78 checks in
  0.99 seconds. Comment/Uncomment and Indent/Unindent no longer use hard-coded
  Ctrl key checks: their four shortcuts are canonical Registry surfaces, the
  editor keyboard path publishes the same request used by the context menu,
  and standalone editors retain a local fallback. `action_registry_test`
  passed 54/54 checks in 0.10 seconds after a 4-step, 6.5-second build; the GUI
  target build passed 8/8 in 497.0 seconds and its final run passed 80/80
  checks in 1.10 seconds. Ctrl+D and Ctrl+Shift+L now publish the same
  occurrence-selection Actions before using the standalone-editor fallback;
  the GUI regression preserves the established two-step Ctrl+D contract and
  passed 82/82 checks in 1.40 seconds after a 424.6-second relink. Alt+F7,
  Shift+Alt+F7, Alt+F8, Shift+Alt+F8, Ctrl+Shift+J, and Ctrl+Shift+K now also
  execute their canonical structural-navigation or line-operation routes in
  MainWindow and enter application history. The product rebuild passed 7/7
  steps in 552.2 seconds; the corrected real-document GUI regression passed
  85/85 checks in 1.24 seconds after a 369.1-second test-only relink. Standard
  Undo/Redo/Cut/Copy/Paste/Select All plus Ctrl+F/G/H now also resolve their
  effective Registry shortcuts and execute through MainWindow. Clipboard
  execution preserves multi-cursor, column-selection, line-copy/cut, and
  standalone-editor ownership. Interactive Go to Line records its resolved
  line parameter for repeat, while intentionally hidden standard context-menu
  entries remain hidden. `action_registry_test` passed in 0.13 seconds after a
  4-step, 7.0-second build. The dependency rebuild passed 51/51 steps in 791.9
  seconds; after correcting the hidden-entry GUI expectation, the test-only
  relink passed 3/3 in 538.9 seconds and the GUI run passed 89/89 checks in
  1.35 seconds. The final default-shortcut audit routed Ctrl+Shift+I through
  `format.document` and replaced physical F12/Ctrl+R checks with effective
  Registry bindings. A standalone editor regression overrides them to
  Ctrl+F12/Ctrl+Alt+R and proves the defaults stop firing while the new chords
  dispatch. The serial build passed 8/8 steps in 771.9 seconds and the GUI run
  passed 91/91 checks in 1.18 seconds. Additional hosted-mode coverage proves
  Ctrl+C keeps multi-cursor distributed selection and column-selection row
  slicing while both execute `edit.copy` through Registry history. After a
  375.4-second test-only relink, the final GUI run passed 93/93 checks in 1.47
  seconds.
- The reusable clipboard, line-operation, and scoped-occurrence Action
  executors now live in `editorruntimecommands.cpp`; key release ownership
  moved with the command path. This restores the runtime shell boundary from
  5246 to 4995 lines without changing behavior. `controller_boundary_test`
  now verifies both the size limit and implementation ownership and passed in
  0.10 seconds. The serial GUI dependency rebuild passed 6/6 steps in 536.7
  seconds, and `expose_signal_to_top_gui_test` retained 93/93 passing checks in
  1.28 seconds.
- Navigation panel Ctrl+1 is now the default shortcut of
  `view.navigation.toggle`; the independent `QShortcut` bypass was removed.
  The real menu adapter toggles `navigationDock` and records Action history.
  `action_registry_test` passed in 0.13 seconds, the serial GUI build passed
  8/8 steps in 524.5 seconds, and the GUI test passed 94/94 checks in 1.34
  seconds.
- Ctrl+W, Ctrl+E, and Ctrl+Q now resolve Registry-owned structural-selection
  and selected-symbol navigation Actions. Their reusable algorithms moved to
  `EditorSelection`, while MainWindow shortcuts and Command Mode share the
  same execution routes and history. `editorruntime.cpp` is now 4814 lines.
  `action_registry_test` passed 56/56 checks in 0.12 seconds and
  `controller_boundary_test` passed in 0.11 seconds. The product GUI build
  passed 67/67 steps in 515.6 seconds; after restoring the existing formatter
  fixture cursor, the 3-step test relink passed in 381.4 seconds and the final
  GUI run passed 98/98 checks in 1.31 seconds.
- Alt+Up/Down logical-line movement now belongs to
  `EditorLineOperationController` and canonical `edit.moveLinesUp/Down`
  Actions. The controller preserves touched-line boundary semantics,
  cursor/selection position, column-mode exclusion, and one undo transaction;
  shortcuts and Command Mode share the same routes. The runtime shell is now
  4635 lines. `editor_line_operation_test` passed 41/41 checks in 0.13 seconds,
  `action_registry_test` passed 58/58 in 0.13 seconds, and
  `controller_boundary_test` passed in 0.12 seconds. The serial GUI build
  completed 67 executed steps in 539.3 seconds and the GUI run passed 100/100
  checks in 1.43 seconds.
- Global Control invocation now uses the non-menu
  `view.globalControl` Action. Its event filter resolves the effective
  Registry shortcut, requests MainWindow execution, exits editor modes through
  the existing opening hook, and retains a standalone fallback. The Registry
  test passed 59/59 checks in 0.13 seconds. A 36-step serial GUI rebuild passed
  in 503.2 seconds; the GUI test overrides Ctrl+Space with Ctrl+Alt+Space,
  proves the old chord inactive, and passes 101/101 checks in 1.40 seconds with
  the canonical history id.
- Command Mode hold entry now uses the non-menu, non-repeatable
  `view.commandMode` Action. The application event filter resolves its
  effective single-stroke shortcut instead of testing physical F24; invalid
  multi-stroke overrides are rejected, and panel/search-help text shows the
  active binding. The local Action host owns `ui.commandMode.show`, preserving
  completion-popup replacement and press/release input ownership without
  displacing Repeat Last Action history. `action_registry_test` passed 61/61
  in 0.13 seconds and `controller_boundary_test` passed in 0.11 seconds. An
  initial parallel build had one unrelated compiler process exit without a
  diagnostic; explicit `--parallel 1` rebuilt and linked 5/5 steps in 549.9
  seconds. The GUI override regression passed 102/102 in 1.47 seconds, and the
  serial `completion_test` rebuild passed 2/2 in 436.3 seconds with its test
  passing in 2.44 seconds.
- The Column Number Tool now enters the non-menu, non-repeatable
  `insert.columnNumbers` Action. Its Alt+C application event path resolves the
  effective Registry shortcut, Command Mode uses the same
  `editor.columnNumbers.show` route, and the route validates the active column
  selection before presenting the existing interactive panel. The old
  physical-key branch is removed. `action_registry_test` and
  `controller_boundary_test` passed in 0.12 and 0.10 seconds. The explicit
  single-thread GUI rebuild completed 36 executed steps in 675.5 seconds; the
  GUI run passed 102 checks in 1.43 seconds, including old-chord deactivation,
  Ctrl+Alt+C override activation, panel Action identity, and repeat-history
  isolation.
- Fold Shelf list deletion now enters the non-menu, non-repeatable
  `fold.shelf.deleteSelected` Action. The list resolves the effective Registry
  shortcut instead of testing physical Delete, MainWindow owns the guarded
  delete route, and Command Mode shares it while the panel retains a reusable
  standalone fallback. `action_registry_test` and `controller_boundary_test`
  passed in 0.12 and 0.10 seconds. The explicit single-thread product/test
  build completed 38 executed steps in 649.5 seconds;
  `unified_peek_container_test` passed 18/18 checks in 0.19 seconds, proving
  old-key deactivation and Ctrl+Delete override activation on a real Fold
  Shelf list and model.
- Editor Tab context commands now consume a 12-entry
  `ActionSurface::TabContextMenu` catalog instead of private labels and an
  `EditorTabCommand` enum. Close variants, reopen, Duplicate View, four-way
  split, Merge Group, and Lock/Unlock publish canonical Action ids;
  TabManager establishes the clicked group/tab context and MainWindow owns the
  shared execution route. Review also found that copied view state could reuse
  an attached `viewId`; `SharedDocument::attachView` now regenerates identity
  collisions centrally, preserving per-view lock, cursor/scroll persistence,
  and split layout identity while retaining one Document and undo stack.
  Modified files are `actionregistry.h/.cpp`,
  `editorsplitcontroller.h/.cpp`, `tabmanager.h/.cpp`, `mainwindow.cpp`,
  `shareddocument.cpp`, `CMakeLists.txt`, and the four focused tests. Registry,
  boundary, and split-controller tests passed 1/1 in 0.10, 0.08, and 0.15
  seconds. The first shared-document run exposed the identity collision (66/69
  checks); after the product fix, its second minimal run passed all 70 checks
  in 0.69 seconds. Explicit serial builds took 27.97, 2.56, 655.35, and 555.10
  seconds for the final affected targets. Both GUI-focused targets used the
  registered offscreen platform; no capped GUI target was rerun.
- Navigation design-hierarchy context commands now use three non-repeatable
  Registry Actions: instantiation navigation, module-definition navigation,
  and Set Design Top. `navigationmanagerconnections.cpp` materializes the
  design-tree menu from Registry labels/routes and preserves the complete
  hierarchy instance context; `navigationmanagerfileoperations.cpp` also
  reuses the Set Design Top descriptor for single-module and dynamic
  multi-module file-menu choices. Connecting to an already-open workspace now
  synchronizes Navigation context before availability evaluation. Modified
  files are `actionregistry.h/.cpp`, `navigationmanager.h/.cpp`,
  `navigationmanagerconnections.cpp`, `navigationmanagerfileoperations.cpp`,
  and three focused tests. `action_registry_test`,
  `controller_boundary_test`, and `workspace_file_operation_test` passed 1/1
  in 0.10, 0.08, and 0.43 seconds; the final core/test serial build completed
  39 executed steps in 744.12 seconds. The offscreen workflow retained all
  existing file-operation preview and revision-check regressions, and no
  capped target was run.
- Bottom-panel Tab context actions now use a dedicated
  `ActionSurface::PanelContextMenu`. Pin/Unpin and Close labels/routes derive
  from the existing bottom-panel descriptors, Command Mode receives the same
  canonical ids, and the clicked `panelId` travels through ActionInvocation so
  MainWindow acts on the right-clicked page rather than whichever page was
  previously active. Pinned pages expose a dynamic Unpin label and reject
  Close before host dispatch; both actions are non-repeatable. Modified files
  are `actionregistry.h/.cpp`, `panellayoutcontroller.h/.cpp`,
  `mainwindow.cpp`, `CMakeLists.txt`, and three focused tests.
  `action_registry_test` passed in 0.11 seconds, the first panel test run
  exposed only test-fixture active-tab pollution (36/39), and the corrected
  second minimal run passed 39/39 in 0.11 seconds. `controller_boundary_test`
  passed in 0.08 seconds, and the final serial `zeroslack_core` build passed
  37/37 steps in 203.11 seconds. No capped target was run.
- RTL Insights Jump, Focus, and Set Top now use three non-repeatable
  `ActionSurface::GraphPanel` descriptors. The More menu, Inspector buttons,
  and module-block Set Top button share Registry labels, metadata, availability,
  execution routes, and failure reporting; selection enablement is synchronized
  by the graph scene mapper. Execution is isolated in
  `rtlinsightspanelactions.cpp`, while the coordinator remains 834 lines.
  Modified files are `actionregistry.h/.cpp`,
  `rtlinsightspanelcoordinator.h/.cpp`, `rtlinsightspanelactions.cpp`,
  `rtlinsightspanelviewstate.h`, `rtlinsightsgraphscenemapper.cpp`,
  `rtlinsightspaneltestapi.cpp`, `CMakeLists.txt`, and four focused tests.
  `action_registry_test`, `controller_boundary_test`,
  `rtl_insight_linkage_test`, `relationship_test`, and
  `graph_export_panel_integration_test` passed 1/1 in 0.09, 0.07, 0.31,
  15.92, and 0.37 seconds. Their serial builds completed in 23.4, 2.7,
  564.9, 368.6, and 354.5 seconds; the export target had first reached its
  300.3-second tool limit while compiling and then completed without a source
  diagnostic under the extended limit. No capped target was run.
- Signal Usage Hotspot Fit, Zoom In/Out, Center Current, and Reset Layout now
  use five non-repeatable GraphPanel descriptors. The Track context menu,
  panel button metadata, and Focus View adapters share the same action ids and
  `executeAction` host; graph-content and Center-current availability propagate
  to bound controls. Modified files are `actionregistry.h/.cpp`,
  `signalusagehotspotpanel.h/.cpp`, `signalusagehotspotpanelactions.cpp`,
  `CMakeLists.txt`, and three focused tests. `action_registry_test` and
  `controller_boundary_test` passed 1/1 in 0.09 and 0.07 seconds after 23.7-
  and 2.9-second serial builds. The panel/core target compiled and linked 46
  steps in 559.0 seconds. `signal_usage_hotspot_panel_test` then consumed its
  three-run cap: two CTest starts exited before producing test output in 0.38
  and 0.31 seconds, and a GDB run showed Windows startup status `0xc0000135`
  before `main`, with no stack. Its direct and transitive DLL list matches
  passing GUI targets and all listed DLLs exist. The independent
  `graph_export_panel_integration_test` started normally: its three runs passed
  33/34, 38/39, and 38/39 checks; every graph export and Reset, Zoom Out,
  Center, Fit, metadata, and history check passed. The sole failure was a test
  calling a deliberately hidden panel `QPushButton`; the real Focus entry uses
  `focusZoomIn()`. The future assertion now calls that production adapter, but
  neither capped target was rerun. Serial integration builds took 348.6,
  345.8, and 433.5 seconds.
- Insight Focus Fit, Zoom Out, and Zoom In now consume the same non-repeatable
  GraphPanel descriptors as the embedded graph panels. Toolbar labels,
  tooltips, route metadata, availability, and dispatch are owned by
  `ActionRegistry`; active Focus registrations still supply the concrete view
  callbacks. Execution was separated into
  `insightfocuscontrolleractions.cpp`, leaving the Focus shell responsible for
  panel ownership and lifecycle. Modified files are
  `insightfocuscontroller.h/.cpp`, `insightfocuscontrolleractions.cpp`,
  `CMakeLists.txt`, and two focused tests. The serial
  `insight_focus_controller_test` build completed in 14.2 seconds and the test
  passed 1/1 in 0.13 seconds; `controller_boundary_test` built in 2.9 seconds
  and passed 1/1 in 0.10 seconds. No capped target was run.
- The historical A-G build on 2026-08-03 built every Debug target serially. The
  first pass exposed one deterministic stale test adapter in
  `test_sv/gui_smoke_test.cpp`: its one-argument registered-Action callback no
  longer matched the product `(actionId, parameters)` contract. The fixture
  now captures both values and verifies the Rename request carries an empty
  parameter map. The restarted build completed all remaining targets; after a
  session continuation, Ninja reported only ten pending links and those 10/10
  completed in 3333 seconds. All 77 CTest executables therefore compile and
  link, including the nine execution-capped targets.
- A historical partial suite excluded nine targets by exact name and passed the
  remaining 68/68 in 199.58 seconds. This result does not establish complete
  77-target acceptance. The partial run included
  `graph_export_service_test` 0.14 seconds,
  `editor_visible_region_perf_test` 4.40 seconds,
  `rtl_insight_linkage_test` 1.88 seconds,
  `insight_focus_controller_test` 0.65 seconds,
  `panel_layout_controller_test` 0.61 seconds,
  `analysis_scheduler_test` 4.91 seconds, and all seven `rtleditcore` tests.
- Real-project checks passed within that partial run:
  `large_file_perf_test` exercised `test_sv/new` in 7.96 seconds,
  `relationship_perf_test` exercised `test_sv/huge_prj` in 93.66 seconds,
  and `expose_signal_to_top_fixture_test` checked both roots in 4.95 seconds.
  The protected files remain byte-identical:
  `test_sv/new/.zs` is 8642 bytes, timestamp 2026-07-26 22:39:01, SHA-256
  `951DAE13441A96CC21063523180E3FA8EE1B37AC1D7C1C76259F1440F865F1C5`;
  `test_sv/huge_prj/.zs` is 88254 bytes, timestamp 2026-07-25 00:19:56,
  SHA-256
  `FD279C386846C51F13C4DBA81378A4BD6748FFF274A9EA0AF9A7DEFA5EB26391`.
- Offscreen GUI evidence includes seven fresh, nonblank FSM PNGs under
  `artifacts/ui/fsm`; visual inspection of `fsm_selected_transition.png`
  confirms distinct selected-transition and alias/implicit-state rendering.
  The retained `current_signal_usage_hotspot_after.png` confirms readable
  Track/Matrix layout, counts, selection, cell details, source links, zoom,
  Fit, and Export controls. `git diff --check` passed apart from line-ending
  advisories, the status audit found zero temporary log/program entries, and
  no build or test process remains.
- A historical Debug serial incremental build passed 108/108 Ninja steps in
  13,808.5 seconds. A partial CTest run excluded six entries and passed the
  remaining 71/71 in 161.43 seconds; this is not complete acceptance.
- Real-project checks passed in that partial run: `large_file_perf_test`
  exercised `test_sv/new` in 8.33 seconds, `relationship_perf_test` exercised
  `test_sv/huge_prj` in 99.16 seconds, and
  `expose_signal_to_top_fixture_test` checked both projects in 4.24 seconds.
  The protected `.zs` lengths and timestamps remain 8642 bytes / 2026-07-26
  22:39:01 and 88254 bytes / 2026-07-25 00:19:56 respectively.
- `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'` passed. No obsolete
  `ExposeSignalToTopDialog` name, temporary test/log artifact, or residual
  test/build process remains.

### Historical Three-Reproduction Test Record

Each entry below records three historical execution attempts. The record proves
compile/link status and neighboring coverage only; it does not convert an
omitted or failing target into a pass. The former cap was superseded; the
required registered-environment reruns and final unfiltered suite are recorded
in the current repair summary above.

- `gui_smoke_test`: test scope `test_sv/gui_smoke_test.cpp:1` through 15740,
  with the integration main beginning at line 12237; product scope is
  `mainwindow.cpp`, `mycodeeditor.cpp`, workspace/session coordinators, and
  semantic panels. Reproduction is the CMake-registered offscreen test with
  `test_sv/new` and `test_sv/test_symbols.sv` arguments
  (`CMakeLists.txt:1507`).
- `editor_incremental_test`: test scope
  `test_sv/editor_incremental_test.cpp:1` through 2348, main at line 2049;
  product scope is `tabmanager.cpp`, `mycodeeditor.cpp`, `tsdocument.cpp`,
  `symbolanalyzerincremental.cpp`, and incremental-analysis services.
  Reproduction is the offscreen CMake test with `rtl_top.sv` and
  `test_sv/huge_prj` (`CMakeLists.txt:1576`).
- `editor_structural_input_test`: test scope
  `test_sv/editor_structural_input_test.cpp:125` through 533; product scope is
  `editorstructuralinputcontroller.cpp`, `mycodeeditor.cpp`, editor syntax, and
  completion workflow. Reproduction is the no-argument offscreen CMake test
  (`CMakeLists.txt:1585`).
- `editor_paste_transaction_test`: test scope
  `test_sv/editor_paste_transaction_test.cpp:56` through 259; product scope is
  `mycodeeditor.cpp`, document transactions, and shared-document view updates.
  Reproduction is the no-argument offscreen CMake test
  (`CMakeLists.txt:1606`).
- `global_control_ow_test`: test scope
  `test_sv/global_control_ow_test.cpp:418` through 967; product scope is
  `mainwindow.cpp`, Global Control/File Command coordination,
  `workspacemanager.cpp`, and analysis scheduling. Reproduction is the
  offscreen CMake test with both real project roots (`CMakeLists.txt:1544`).
- `editor_multicursor_controller_test`: test scope
  `test_sv/editor_multicursor_controller_test.cpp:75` through 963; product
  scope is `editormulticursorcontroller.cpp`, virtual-column ownership,
  `mycodeeditor.cpp`, and selection/mode coordination. Reproduction is the
  no-argument offscreen CMake test (`CMakeLists.txt:1599`).
- `expose_signal_to_top_gui_test` Insight Focus Escape experiment: the stable
  target first passed the Column Number Tool stage with 102 checks in 1.43
  seconds. Three later minimal runs against a proposed Registry migration all
  reproduced the same failure: the signal-kernel Action entered Focus View,
  and the leave QAction was present, enabled, and carried the effective Escape
  sequence, but the offscreen key event did not trigger
  `view.insightFocus.leave`; the following assignment-queue failure was a
  consequence of the still-active Focus View. Explicit page focus did not
  change the result. Source scope is `mainwindow.cpp` View-menu QAction setup
  and route execution plus `insightfocuscontroller.cpp` focus-page input
  ownership; reproduction is the CMake-registered offscreen GUI test
  (`CMakeLists.txt:1524`). The unverified migration and transient checks were
  removed, restoring the previously verified local Escape behavior. The target
  was included in the final required regression run and passed in 1.17 seconds.
- `signal_usage_hotspot_panel_test`: the affected 46-step core/test target
  compiled and linked in 559.0 seconds. Two CTest runs then exited in 0.38 and
  0.31 seconds without entering any logged assertion. A third GDB run exited
  during process startup with Windows status `0xc0000135` and no stack. Static
  dependency inspection shows the same Qt6Core/Gui/Svg/Widgets and MinGW DLL
  set as passing `rtl_insight_linkage_test`; every listed DLL exists in the
  configured runtime paths. Source scope is
  `test_sv/signal_usage_hotspot_panel_test.cpp`,
  `signalusagehotspotpanel.cpp`, and `signalusagehotspotpanelactions.cpp`. The
  current repair added early diagnostics, fixed the shared zoom-state route,
  and completed three registered passes in 0.44, 0.46, and 0.41 seconds.
- `graph_export_panel_integration_test`: after adding focused Hotspot graph-view
  checks, three runs passed 33/34, 38/39, and 38/39 checks. All five export
  surfaces, Registry metadata, availability, structured failures, Reset,
  Zoom Out, Center, Fit, and repeat-history checks passed every applicable
  run. The only repeatable failure was `QPushButton::click()` on a control that
  product layout deliberately hides; the real Focus toolbar invokes
  `focusZoomIn()`. The future test now calls that production adapter, but the
  target was not run a fourth time. Source scope is
  `test_sv/graph_export_panel_integration_test.cpp` and the Hotspot panel Action
  binding. The current repair fixed the shared zoom-state route and completed
  three registered passes in 0.32, 0.32, and 0.32 seconds.
