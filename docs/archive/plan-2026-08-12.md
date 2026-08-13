# ZeroSlack Development Plan

## Global Theme and Temporary Editor Drawer (2026-08-11)

Status: **控制侧已独立验收通过**. No commit or push is part
of this handoff.

- Phase 1, centralized theme: complete. Settings Center/QSettings owns
  persistence under `appearance.theme`; `ApplicationThemeManager` owns only
  the live Light or Dark mode, application palette/QSS, and ordered pre-change/
  post-change signals. `InsightVisualStyle` supplies all mode-dependent tokens.
  Settings > Appearance applies the selection immediately and defaults an
  unset profile to Light without rebuilding editor or workspace state.
- Phase 2, unified target and controller: complete. `EditorLocation` carries
  stable document identity, path, line/column, optional selection, symbol, and
  source-link identity. One `TemporaryEditorDrawerController` owns target
  replacement, Back/Forward history, search, and `Hidden`/`Floating`/
  `EdgeStowed` state. Pin independently disables automatic collapse and is not
  persisted.
- Phase 3, shared auxiliary View: complete. `TabManager` binds the drawer
  editor to the authoritative `SharedDocument`/`QTextDocument`; no temporary
  tab, second buffer, external-change path, or save authority is introduced.
  Cursor, selection, scroll, folding, and history are View-local, and the
  editor split tree is not changed. Each editor owns an `EditorViewProjection`
  that maps collapsed source ranges into per-View painting, geometry, hit
  testing, navigation, and scrolling without mutating shared `QTextBlock`
  visibility or line counts. Fold endpoints use insertion-aware `QTextCursor`
  anchors so edits remap safely, and drawer Back/Forward entries restore their
  own fold sets.
- Phase 4, overlay interaction: complete. The editor-host child overlay can be
  moved, resized, or stowed at any of four edges; only its labeled handle
  expands it. Delayed collapse is guarded during focus, selection, dragging,
  editing, completion, menu, and popup interaction. Back, Forward, Pin, Close,
  and selectable target search are in the title bar. The search provider uses
  cached workspace-file and `SearchService` semantic catalogs, refreshes files
  on `filesScanned`, performs in-memory fuzzy ranking, preserves duplicate-name
  candidates with disambiguation, and exposes keyboard and mouse selection in
  a themed child popup. Current editor, Tab, Files, Design, definition preview,
  Scoped Search, and RTL source-linked graph entry points use the same Action
  route. Parent resize and ordinary interaction remain locally filtered; the
  application-wide filter is installed only for popup/menu or active-drag
  protection and is removed for Hidden and idle states.
- Phase 5, execution-side verification: complete. Added or extended coverage
  includes `editor_view_local_folding_test`,
  `temporary_editor_drawer_event_filter_test`,
  `temporary_editor_drawer_search_test`, and the existing drawer, shared
  document, theme, search, editor-input, performance, and GUI smoke targets.
  The final affected `jump_test`/`insight_visual_style_test`/
  `shared_core_runtime_test` group passed 3/3. Shared-Debug unfiltered serial
  CTest passed 82/82 with zero failures in 380.30 seconds wall-clock; the
  control side independently accepted all three repairs and the shared DLL
  boundary.

## Default Shared-Core Build Strategy (2026-08-12)

Status: **执行侧实现及测试完成，等待控制侧验收**. The implementation,
representative checks, complete build, and unfiltered serial CTest run are
recorded below.

- Require CMake 3.27 or newer and keep `ZEROSLACK_SHARED_CORE` `ON` by default.
  Debug, Release, tests, and subsequent acceptance therefore use the shared
  core. Development and Debug acceptance use the existing Shared-Debug build
  directory. Retain explicit `-DZEROSLACK_SHARED_CORE=OFF` only as a
  compatibility/comparison route; never select it as an implicit fallback.
- Guard this contract with a source-level CMake policy test. Verify it with two
  clean configure-only probes: default configuration must produce an `ON`
  cache and DLL edge; explicit `OFF` must produce a static archive edge.
- Build `zeroslack_core` as a DLL in the default graph. Mark public Qt and
  C++ boundary types with `ZEROSLACK_API`; on MinGW, generate and validate an
  explicit filtered `.def` from core and Tree-sitter objects before the DLL
  link. Keep `shared_core_runtime_test` as the meta-object, signal, RTTI, and
  editor-boundary guard.
- Keep Slang non-transitive from the shared core by using its existing target
  only as a compile requirement for consumers. Link Slang explicitly from the
  tests that directly use it, and link the existing Tree-sitter object target
  explicitly from the two tests that call its C API. Do not create alternate
  semantic, syntax, or document fact stores.
- Serialize memory-heavy Ninja link edges through the configurable
  `ZEROSLACK_LINK_JOBS` pool, defaulting to one for this MinGW host.
- The latest core DLL edge is 389.726 seconds. Static-Debug core
  consumers are N=53 with median 349.730 seconds, P75 354.114, maximum
  406.279, and cumulative 4.851 hours. Shared-Debug core consumers are N=54
  with median 0.675 seconds, P75 1.306, maximum 205.073, and cumulative
  279.092 seconds. Excluding the sole direct-Slang core-consumer outlier
  `effective_value_test`, the Shared-Debug N=53 values are median 0.670
  seconds, P75 1.201, maximum 16.675, and cumulative 74.019 seconds.
- Representative current links are 0.670 seconds
  (`temporary_editor_drawer_test`), 0.748
  (`temporary_editor_drawer_search_test`), 0.304
  (`temporary_editor_drawer_event_filter_test`), 0.324
  (`editor_view_local_folding_test`), and 16.675 (`gui_smoke_test`). The final
  affected group passed 3/3, and Shared-Debug unfiltered serial CTest passed
  82/82 with zero failures in 380.30 seconds wall-clock. No Release packaging
  result is inferred from the verified Shared-Debug measurements.
- The default-policy rebuild completed successfully, its immediate repeat was
  a no-op, and the focused CMake/runtime policy group passed 5/5. Because this
  step changes configuration defaults and documentation only, it does not
  repeat the accepted 82/82 runtime suite.

## Editor Interaction Repair (2026-08-10)

Status: complete.

- Phase A: protected-worktree baseline and four regression groups completed.
- Phase B: scoped editor surface, stable gutter geometry, maximum-zoom line
  number fit, and explicit-only virtual-column entry completed.
- Phase C: manual bottom-dock resize and explicit Collapse/Expand unified in
  the persisted `PanelLayoutState` state machine completed.
- Phase D: reusable five-zone drop-preview overlay, labeled drag pixmap,
  cancellation cleanup, and unique-View drag identity completed.
- Phase E: affected targets built successfully; the required seven-test CTest
  set passes 7/7, and six real-main-window screenshots were inspected.
- Phase F: this focused handoff update completed. No legacy text authorizes an
  ordinary click or ordinary Right key to enter a virtual column.

## Historical Keyword-Boundary Repair Record (2026-08-04)

Current project status: **控制侧已独立验收通过**. The
following bullets record an earlier keyword-boundary repair only and do not
state the acceptance status of the current theme/drawer work.

- Phase 0 — P1 Acceptance State Alignment: complete. The three documents now
  withdraw the prior acceptance claim before any product-code change.
- Phase 1 — Keyword-Boundary Incremental Tree Repair: historically complete.
  Its product fix, focused regressions, full build, and unfiltered test run were
  recorded as passing at that checkpoint.
- Repair baseline: `73ddc5d0e3cb275bb291c9bfb8828d3fd8801dc6`.
- Historical implementation commit: `8308b5a9820ac9ad4fd852348ad8fe17f9b120ef`.
  This documentation-only follow-up records that result and does not claim a
  later documentation commit as the current `HEAD`.
- The protected untracked screenshot, `dist/`, and both protected `.zs` paths
  remained outside all edits and cleanup.
- The final Shared-Debug unfiltered CTest run for the current work completed
  82/82 with zero failures in 380.30 seconds wall-clock. The historical P1
  all-target build and 77-target runs prove only their recorded baseline, while
  excluded runs of 68/68 and 71/71 remain partial historical records.
- The P1 root is fixed by deriving the fast-path exclusion set from Slang's
  IEEE 1800-2023 keyword table. Ordinary non-keyword identifier suffix edits
  still skip parsing; identifier-to-keyword and keyword-to-identifier edits
  synchronously take the existing authoritative Tree-sitter incremental parse.
- Historical focused verification recorded: `ts_doc_test` 0.20 seconds,
  `ts_large_file_semantics_test` 0.83 seconds, and
  `editor_incremental_test` 9.31 seconds. The semantic suite reports 26/26,
  including `alway -> always`, `logi -> logic`, reverse keyword exit, and the
  unchanged ordinary-identifier no-parse path.
- Historical execution-side verification: the complete serial build succeeded,
  and its
  final unfiltered `ctest --output-on-failure -j1` passed 77/77 in 395.51
  seconds, including `editor_incremental_test` in 13.64 seconds with every
  existing correctness and performance threshold unchanged.
- Historical control-side verification: `ts_doc_test` passed in 0.74 seconds,
  `ts_large_file_semantics_test` in 1.36 seconds, and
  `editor_incremental_test` in 12.01 seconds. The subsequent unfiltered serial
  CTest run passed 77/77 in 412.70 seconds, including the corresponding three
  targets.
- Pre-final history: one earlier full run produced 76/77 while the external
  `PassTheFear.exe` process was active, and an isolated rerun reproduced two
  latency-gate failures. That run was not accepted and no threshold was changed;
  the final unfiltered rerun above is the recorded result for that historical
  keyword-boundary repair only.
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

Use `readme.md` for the current handoff baseline and `goal.md` for the active
long-term goal model. This file defines how to execute work.

## Current Identity

ZeroSlack is a SystemVerilog code editor and workspace browser. It should help
users read, navigate, edit, and understand RTL projects without turning UI code
into an analyzer.

## Active Interaction Convergence Plan (2026-07-29)

Stages 0 through 8 are complete on `main` at
`7cb1f214022d7b4bd2ce20cdeb39a1aad4f873ce`. The Debug baseline passed all
25 CTest targets in 348.82 seconds. The four pre-existing untracked/protected
paths recorded in `readme.md` currently match their baseline metadata.

Stage 1 delivered the shared `EditorActionContextService`, persistent context
chip, Tree-sitter package context, history-independent unique hierarchy
resolution, explicit multiple-instance/top selection, and visible/actionable
Expose failure paths while retaining the existing `rtleditcore`
plan/preview/apply transaction. Its test-first regression now passes 1/1 in
0.50 seconds.

Stage 2 replaced the F24-owned metadata table with a 76-descriptor
`ActionRegistry`. F24, built-in inline semantic/template commands, templates,
Global Control, Package Tools, source-symbol actions, and Expose now consume
registry ids, names, aliases, availability metadata, and execution routes.
F24 help renders the unified catalog. The test-first registry and catalog
regressions plus completion/GUI/Expose integration pass 4/4 in 11.95 seconds.

Stage 3 added the registry-backed semantic-only `;v` action without creating
`;;v`. The saved explicit Tab anchor now carries module and package identity;
the existing semantic index returns current-owner definitions plus
unambiguous active-import members and excludes foreign-module internals.
Package variables/functions/tasks are correctly marked package-visible by the
Slang record taxonomy. Test-first coverage passes
`action_registry_test` 12/12, `completion_test` 842/842, and
`gui_smoke_test`.

Stage 4 replaced `EditorModeState` and the migrated active booleans with an
authoritative `EditorModeController`. Nine runtime modes now declare
co-existence, input ownership, priority, Esc behavior, stale-exit reasons, and
persistent presentation. Slot, signal, column/virtual, fold, candidate, and
source-navigation runtimes consume this state directly; tab/document/control
transitions exit through the same controller. A structured snapshot drives
the persistent Mode Chip. The test-first controller and GUI regressions pass
2/2 in 10.89 seconds after fixing source-navigation ownership and virtual
caret cleanup.

A direct GUI diagnostic invocation temporarily wrote the protected root
screenshot. The deterministic 141996-byte historical artifact and original
timestamp were restored, all four protected entries now match the baseline,
and smoke-test artifacts were redirected to `%TEMP%/zeroslack-gui-smoke`.

Stage 5 added `EditorContextMenuModel` and expanded the Registry from 77 to
96 descriptors so every editor-menu leaf has one id, canonical label,
availability/recovery policy, execution route, and ContextMenu alias. The
normal menu is now built once by `EditorCoordinator` from the same
`EditorActionContext` as the status chip. Standard actions lead; Navigate,
Inspect, Refactor, and Format follow in stable groups. Irrelevant specialized
actions are omitted, recoverable actions stay clickable with visible reasons,
and shown hard failures include visible reasons. The test-first menu model and
focused integration pass 5/5 in 15.21 seconds.

Stage 6 added one `InsightFocusController` and central Focus shell. It moves
the exact RTL Insights, Signal Kernel, or Wave Preview widget out of its Dock,
hides/restores surrounding Docks for readable 1100x760 geometry, and routes
Fit, zoom, search, Inspector, Back, Return to Dock, menu aliases, and
panel-local entry through the existing presenter state. GUI tests verify FSM,
module block, hotspot, kernel, and wave widget identity and state retention.
The focused suite passes 6/6 in 32.85 seconds. A legacy `.zs` write observed
before test isolation was restored exactly from checkpoint blob
`38aa7c46a6cc62df3a9d0dc337ea76b6c128e5dc`; GUI smoke now runs on a temporary
mirror of `test_sv/new`, and all protected metadata matches baseline. Stage 7
then separated portable project semantics from local session state.

Stage 7 writes only relative include/ignore paths, extensions, defines, and top
module to atomic `.zeroslack/project.json`. Tabs, cursor/scroll positions,
window/Dock state, and scan cache live in a SHA-256 workspace-identity
`QSettings` partition. Save/Restore/Clear and `ow s` now explicitly describe
local state, while project configuration has separate storage and wording.
Legacy `.zs` is imported read-only into the two destinations without deleting
or overwriting it. The test-first target failed before the split APIs existed;
the final five-target suite passes 5/5 in 105.59 seconds, and its 23/23
persistence checks preserve the exact bytes and timestamps of both protected
real `.zs` fixtures.

Stage 8 introduced dedicated Slot, Column/Virtual Cursor, and Signal Selection
controllers and bound Source Navigation directly to the unified mode
controller; Fold already owns its feature state and is now called directly.
`MyCodeEditorState` contains composition/shared services rather than migrated
mode state. RTL Insights now composes a passive single view state, graph
controller, scene mapper, and presenter; its coordinator retains only shell
construction, wiring, Focus adapters, and compatibility delegates. Source
ceilings decreased from 6874/4397 to 4987/849 lines. The test-first boundary
guard moved from 5/27 to 27/27, and eight behavior targets pass 8/8 in 33.47
seconds.

Historical eight-item verification record: the first full run passed 30/31 and
identified one forbidden production `QRegularExpression` in Action Registry validation.
It was replaced by an explicit ASCII scanner, invalid-id coverage was added,
and the unchanged policy guard then passed. A serial MinGW Debug all-target
build completed with exit code 0, followed by 31/31 CTest targets in 230.07
seconds, including all `components/rtleditcore` tests. GUI tests used Qt
offscreen and a temporary `test_sv/new` mirror; Expose, persistence, Global
Control, and performance tests consumed both real sample roots through
read-only or copied paths. Files/Design parity, unique/multiple Expose,
`;cmd`/`;;cmd`/`;v`, F24, Global Control, all editor modes, the grouped context
menu, all five Focus views, legacy import, and workspace relocation have
direct regression coverage. The actual protected `.zs` files retained their
byte digest, size, and timestamp. All four protected path metadata baselines
match, `git diff --check` succeeds, the index is empty, and no commit or push
was performed.

Implementation order and ownership:

1. Complete: add a revisioned `EditorActionContext` service and persistent context strip;
   resolve Expose hierarchy candidates from the current semantic snapshot,
   using the tab's binding only as an explicit choice rather than navigation
   history. Unique candidates bind automatically; multiple candidates enter a
   picker/preview flow.
2. Complete: replace the F24-only metadata table with one `ActionRegistry`. Migrate
   F24, source-symbol actions, Expose, Package Tools, overlapping templates and
   edit actions to descriptors/adapters without retaining parallel metadata.
3. Complete: register `;v` as the audited free token for visible symbols. It is Tab-only,
   uses the saved abbreviation anchor, and queries only the current
   module/package plus unambiguous imports; instance declarations remain
   visible without exposing foreign-module internals.
4. Complete: replace `EditorModeState` with the authoritative
   `EditorModeController`; migrate Slot, signal selection, column/virtual
   cursor, fold, candidate, and source-navigation lifecycle/input policy.
   Highest-priority Esc and tab/document/global-control stale exits now come
   from controller declarations and a structured snapshot drives the Mode
   Chip.
5. Complete: build the context menu from registry categories: standard edit actions stay
   in the familiar leading block; dynamic actions are grouped into Navigate,
   Inspect, Refactor, and Format. A context-resolvable action stays clickable
   and opens its resolution flow; a shown hard failure includes visible reason
   text.
6. Complete: add a central Insight Focus host that temporarily owns the same Dock panel
   widget. Return-to-editor, Fit, zoom, search, and Inspector reuse existing
   controls and state. No cloned scenes or reports are permitted.
7. Complete: persist portable semantics to `.zeroslack/project.json` with relative paths.
   Persist local session state under stable workspace identity in `QSettings`.
   Import legacy `.zs` read-only, preserving the source file.
8. Complete: split editor and RTL Insight coordination into controller/presenter/view
   state modules, delete migrated duplicate state/helpers, and enforce the
   boundaries through CMake dependencies and focused tests.
9. Complete: build all Debug targets serially, pass the complete 31-target
   CTest suite, audit both sample workspaces and all GUI interaction groups,
   verify protected paths and Git state, and record the final handoff in all
   three project documents.

Test-first matrix:

| Stage | Failing regression added before implementation | Acceptance |
| --- | --- | --- |
| 1 | unbound unique Expose; ambiguous candidate flow; context strip states | same visible code/selection gives the same candidate set from Files and Design |
| 2 | descriptor uniqueness, alias parity, canonical failure text, handler id | one stable id and one handler/plan path per intent |
| 3 | `;v` audit, local/port/import candidates, cross-module negative, filtering | explicit Tab only; Backspace and input retain anchor |
| 4 | pairwise mode conflict matrix, priority Esc, tab/close/control stale exits | controller owns all input decisions and Mode Chip text |
| 5 | menu group/object names, relevant-action filtering, clickable resolution | no disabled-tooltip-only recovery path |
| 6 | panel widget identity across Dock/Focus, transform/selection/search retention | first-open geometry is readable at typical desktop size |
| 7 | project move, local-state isolation, legacy `.zs` preservation/import | no local UI/cache data enters the workspace project file |
| 8 | module ownership/source guards plus focused runtime behavior | large files shrink and duplicate state/helpers are removed |

No behavior assertion or performance threshold may be weakened. Each behavior
change starts with a regression that fails for the intended missing behavior,
then the affected targets are built and run before the next stage. Full Ninja,
full CTest including `components/rtleditcore`, `test_sv/new`, and
`test_sv/huge_prj` acceptance were completed at the end.

Final command/results:

```text
cmake --build build/Desktop_Qt_6_10_2_MinGW_64_bit-Debug --parallel 1
  exit 0
ctest --test-dir build/Desktop_Qt_6_10_2_MinGW_64_bit-Debug --output-on-failure
  31/31 passed, 0 failed, 230.07 sec
git diff --check
  exit 0; Windows line-ending and sandbox user-ignore access advisories only
```

The current host cannot safely run simultaneous large GNU ld links: a
parallel relink exhausted memory, while serial linking completed without a
compiler or linker error. This remains an environment/resource limitation;
it does not alter product behavior or the acceptance result.

## Historical Build-System Performance Milestone (2026-07-20; superseded 2026-08-12)

The MinGW Debug link audit was complete at that checkpoint.
`libzeroslack_core.a` fed thirteen
independent executable links, so a core update necessarily relinks demo plus
twelve tests in that static graph. The final local GNU-ld measurement
was 431.890 seconds for two core compilations, one archive update, and thirteen
links. That is 1.90x/2.54x against the recorded 818.9/1099.2-second baselines,
not the required 3x; the linker was unchanged, so no causal speedup is claimed.

- Retained: stable run-time PATH launchers for compile/link/custom Ninja rules,
  plus `ZEROSLACK_DEBUG_LINKER=AUTO|GNU|LLD`, an optional LLD path, a real
  GCC-driver link probe, explicit linker logging, safe GNU fallback, and
  Debug-only LLD flags. At that checkpoint Release stayed on the existing
  MinGW ABI and GNU ld; this is not the current default linkage.
- LLD: expected high benefit and low source maintenance, but the host's only
  LLVM 7.0.1 linker is incompatible with GCC 13/Qt/Slang. The option remains
  available for a compatible host, where a full link and CTest are still
  required before adoption is considered measured.
- Shared core: the historical prototype demonstrated very small downstream
  test links but failed at a Qt meta-object/data boundary because its DLL
  interface was not annotated. The 2026-08-12 default Shared-Debug
  implementation supersedes this rejection with explicit Qt/C++ exports, a
  generated MinGW `.def`, and a runtime boundary test.
- Shared/aggregated test runtime: rejected because renamed entry points,
  COFF/COMDAT coalescing, and all-test global initialization add maintenance and
  fixture-isolation risk. Combining tests in one process was not attempted.
- Smaller test-specific core libraries: rejected because roughly 180 core
  sources are tightly coupled and most heavy tests span semantic and UI layers;
  maintaining per-test partitions would be costly and fragile.
- Debug/link flags: split DWARF produced invalid PE images with this toolchain,
  `-gz` is unsupported, removing Debug information is out of scope, and a
  single representative GNU link already took 250.436 seconds, so job-pool
  tuning alone cannot meet the threshold.

Verification completed with a Debug no-op, all thirteen executable links,
final CTest 12/12 in 143.40 seconds, an offscreen demo startup, unchanged demo DLL
imports, and successful Release regeneration with no LLD flags. The project has
no CPack/install/package target. These statements describe the 2026-07-20
static/LLD experiment; they do not describe the current default Shared-Debug
graph, whose final unfiltered serial CTest completed 82/82 with zero failures
in 380.30 seconds wall-clock.

## Current Execution Baseline

- Version management baseline is now SemVer-based with root `VERSION` as the
  only manually maintained product version source. Current baseline:
  `v0.1.0`. CMake configures `generated/version.h`; version numbers are no
  longer hand-maintained in a source-tree header or coupled to dependency
  version labels.
- Inline command matching is now Tab-only. Built-in `;cmd` and user/built-in
  `;;cmd` abbreviations can appear at arbitrary editable code positions, but
  typing them does not enter command mode or open command candidates. Plain
  Tab parses only the registered suffix ending at the cursor, prefers `;;cmd`
  over `;cmd`, preserves replacement range from the matched semicolon to the
  cursor, rejects comments/strings including cross-line block comments, and
  scopes semantic queries by the abbreviation anchor file/module/position.
  Multi-candidate semantic sessions accept identifier input and Backspace after
  the first Tab, update the abbreviation query and popup in place, and retain
  the original semantic anchor. Filtering to one or zero candidates does not
  auto-submit; zero matches remain recoverable through a non-selectable row and
  do not expose a default declaration item. Active inline popup sessions cancel
  on cursor movement away from the abbreviation end or on selection, preserving
  source text and clearing popup / command highlight state. The comment/string
  guard no longer treats `//` inside a closed string or closed block comment as
  a live line comment.
- The ordinary identifier production-completion path is deleted. Member access
  `.`, package scope `::`, backtick macro text, `$`, and arbitrary two-character
  input never open a popup. GUI regression verifies that
  explicit `;l<Tab>` opens all anchor-module logic,
  incremental filtering and Backspace keep the popup active, zero matches can
  recover, and Tab/Enter replace only the complete abbreviation. Inline
  semantic sessions suppress `[DEFAULT]` items entirely. The negative and
  explicit-Tab GUI cases live in `gui_smoke_test`; command/service cases remain
  in `completion_test`.
- Corrective completion cleanup is complete in the working tree: the former
  `textChanged -> completion state -> 0 ms timer -> editor word query` chain,
  its activation/replacement modes, ordinary completion model/service entry
  points, and tests that existed only for that path are deleted. Explicit
  `;cmd`, `;;cmd`, `;h`, package import, and user-template candidate sessions
  remain Tab-only.
- Unified effective-value architecture is now the execution baseline:

```text
ProjectSnapshot + immutable all-open-buffer text/revisions
  -> cancellable SymbolAnalyzer symbol/value/diagnostic transaction
  -> Slang AST ConstantValue / Type extraction
  -> SemanticSymbolPresentation + EffectiveValueFact
  -> one revision-checked SemanticIndex + EffectiveValueService publication
  -> captured SemanticSnapshotToken
  -> relationship worker over the same snapshot source and symbol records
```

  Package and compilation-unit traversal is independent of module instances;
  actual instance maps and detached declaration defaults are stored separately.
  Exact declaration ranges participate in stable keys and presentation merge
  identity. `EffectiveValueService` exposes unavailable/current/stale/error
  state, exact value/type/dimension/width data, scope/instance provenance,
  failure reason, and computation/document revisions. Ghost only locates
  display anchors and formats already-evaluated facts; Wave Preview keeps its
  separate runtime trace evaluator. FormalPort declarations are viewport
  overlays anchored eight logical pixels after the last visible source
  character. `EditorDocumentGeometry` derives the tail and baseline from the
  live `QTextBlock::layout()` / `QTextLine`, so tabs, proportional fonts,
  wrapping, DPI, line-tail edits, and horizontal scrolling share Qt's real
  layout geometry; no right-side lane or viewport margin remains. Long Ghost
  text is clipped normally by the viewport instead of being relocated or
  discarded. Slang port source presentation prints only the structured port
  header/declarator children (or the explicit-ANSI core fields), excluding
  `AttributeInstanceSyntax`, comments, disabled text, and preprocessor
  directives without regex cleanup. Slang-provided source/effective-value
  equivalence suppresses only redundant direct parameter values;
  derived/coerced/instance-different values remain visible. Known enum values
  display Slang decimal text while lossless binary `valueText` and X/Z
  rendering remain authoritative.
- The pinned double-click symbol popup is an embedded editor child and every
  text label inherits the active editor font family, size, style hint, and
  pitch request. Title emphasis changes weight only. Popup content remains
  plain text, ordinary mouse hover remains disabled, and no tooltip or
  global-always-on-top window flag is introduced.
- Treat invalid source as an error, not as an invitation to synthesize a value.
  In `test_sv/new/PKG_global.sv`, `E_PRE_ASSERT` is referenced from line 550
  before its declaration at line 663. Slang's undeclared-identifier diagnostic
  is the required result for those enum expressions; no implicit-increment or
  hand-evaluated fallback may replace it. Dedicated valid fixtures cover
  package constants and the general enum-expression categories.
- Every dirty or cache-mismatched workspace/open-document semantic worker owns
  one immutable overlay set. A clean workspace file whose loaded text equals
  indexed content establishes revision zero and requests presentation refresh
  only. Any captured edit cancels/expires its worker and coalesces a restart from
  the latest complete `DocumentModel` snapshot. Workspace epoch, request
  generation, dependency/document revisions, and effective-value computation
  revision gate one GUI-thread publication. Dirty files replace disk content;
  staged, checkpoint, and chunked publication are no longer current behavior.
- Automatic relationship analysis consumes the semantic snapshot token
  published by that transaction, reads source from the same captured snapshot,
  and rejects stale request/project identities. It does not run a duplicate
  per-open-tab Slang symbol/value/diagnostic refresh before relationship work.
- Clean indexed opens must not publish an overlay: preserve revision zero, the
  current snapshot, relationships, and Design cache. Dirty or cache-mismatched
  publications preserve authoritative relationships by stable source key and
  rebind them to current handles. Navigation processes at most one refresh per
  snapshot generation and rebuilds its widget only when the Design structural
  fingerprint changes. The real `rtl_top.sv` regression asserts zero file
  analysis start/finish events, unchanged snapshot generation, zero Design
  refreshes, and preserved Ghost/relationship/hierarchy results.
- Semantic document revision is source-content-only. Formatting and syntax
  highlighting must not advance it; real text edits must. Document snapshots,
  overlay analysis, Hover/Ghost queries, and published effective-value facts
  use the same token. Semantic publication and tab/file/instance-context
  changes refresh Ghost without weakening stale-result rejection.
- Problems `Current File` is active-document state, never the most recently
  analyzed file. Diagnostics refresh requests treat file names as invalidation
  hints, promote multiple different files to full scope, and retain one-file
  scope only for repeated requests for the same file.
- The `ow 1` lifecycle repair is part of the current execution baseline. The
  primary captured stack was `SmartRelationshipBuilder::analysisCancelled ->
  cancelAnalysis -> RelationshipAnalysisController::cancelWorkspaceAnalysis ->
  AnalysisScheduler::~AnalysisScheduler -> MainWindow::~MainWindow`: reverse
  member destruction had released the semantic runtime / relationship builder
  before scheduler teardown used it. The scheduler now broadcasts cancellation
  to all worker families before joining any future and joins while the runtime
  remains alive. A second captured stack ended in
  `SourcePositionCache::~SourcePositionCache` during Qt TLS cleanup with
  `0xfeeefeee`; Slang worker caches are now explicitly destroyed inside the
  collector/presentation scope before thread exit.
- `WorkspaceManager` also treats scan signals as synchronous reentrancy points.
  Scan generation, manager `QPointer`, active path, and entry identity are
  checked after start/progress/model/files/finish publication, so a callback
  that closes or switches a workspace cannot resume and publish stale results.
- `global_control_ow_test` drives the real Global Control coordinator with
  injected directory and alias selectors through `FileCommandCoordinator` and
  `WorkspaceManager`. It covers selector cancellation, repeat open, scan,
  symbol analysis, automatic relationship analysis, semantic queryability, and
  active-worker teardown for `test_sv/new` and `test_sv/huge_prj`.
  `effective_value_test` covers package/compilation-unit/default/instance
  values, general enum expressions, decimal/X/Z enum rendering, parameter
  source-value equivalence, wide/four-state data, type dimensions, exact-range
  identity, Hover/Ghost consumers, and unsaved overlay
  revision/cancellation/atomic diagnostic publication. `gui_smoke_test` covers
  completion negatives and explicit Tab sessions, scan reentrancy, embedded
  double-click popup lifetime and uniform text font, structured attribute-free
  port declarations, actual FormalPort line-tail geometry/rendering across
  tabs, proportional and fixed fonts, horizontal scrolling, line edits, long
  declarations, trailing whitespace and wrapping, and Design structural
  refresh coalescing;
  `relationship_test` retains real `PKG_global.sv` / `chl_ctrl.sv` coverage.
  The real `ow 1` test also opens analyzed `rtl_top.sv` at semantic revision
  zero and verifies no file analysis, snapshot publication, or Design refresh
  while relationships, values, Ghost, port presentation, and nested Navigation
  data remain queryable. This corrective milestone passed final headless
  acceptance: final Debug all-target incremental build 17/17 steps in 1099.2
  seconds and complete CTest 12/12 in 130.60 seconds. No
  visible `demo.exe` was launched after the user prohibited GUI interference;
  all Qt validation used offscreen mode. The final static audit left both user
  `.zs` files untouched: `new/.zs` is the user's 18:31:51 reproduction-session
  save (`9F0341AC...ACBC10`) and `huge_prj/.zs` remains
  `CD8A81F2...21CF425`; neither was overwritten or restored here.
- Follow-up relationship work: schedule relationship recomputation from the
  same dirty-overlay token after invalidation; fingerprint zero-symbol include,
  macro, define, and include-order inputs; move or optimize full engine-mirror
  construction so `huge_prj` publication has a measurable UI latency budget.
- Follow-up revision performance: replace the current full-text comparison on
  `textChanged` with a correct incremental source-text cache. Do not count raw
  Qt `contentsChange` events because formatting emits them too.
- GraphCanvas Phase 2 and the App Shell modernization pass of the current UI
  route are complete. Settings Center/QSettings persists `appearance.theme`;
  `ApplicationThemeManager` owns the live Light/Dark mode, application
  palette/QSS, and ordered pre/post change signals; `InsightVisualStyle` is the shared
  dual-theme token layer for menu/status/tab/dock/sidebar, toolbar, splitter,
  common
  input, tree/list/table, scrollbar, Global Control, Fold Shelf shell state,
  and graph tokens. `InsightGraphView` is the reusable Qt Widgets graph view
  foundation for canvas theme application, pan/drag, wheel zoom, fit, center,
  reset, optional grid, and business-neutral mouse hooks.
- Navigation UI is now the current Files/Design surface only. Legacy
  Module/Symbol tab widgets, manager view branches, tree adapters, and
  SymbolOutline-row navigation wiring remain removed; service-level module
  hierarchy and symbol outline queries are retained as semantic data APIs.
  Design child activation records the exact active-top and instance path in the
  editor context; Files/direct-open paths explicitly unbind it, and navigation
  history restores the saved context.
- Verification baseline for this repair is now focused regression plus GUI
  smoke: `completion_test`, `relationship_test`, `gui_smoke_test`,
  `jump_test`, `insight_visual_style_test`, and feature-specific CTest guards.
- Corpus audit is retired as an acceptance signal. GUI-found issues should be
  reduced to small fixtures in the focused tests instead of broad corpus
  sweeps.
- GraphCanvas connection status: Signal Kernel Graph uses `InsightGraphView`
  for themed canvas, wheel zoom, pan, right-click preview, and double-click
  navigation hooks. Signal Usage Hotspot uses it for Track and Matrix surfaces,
  including track zoom/fitting/centering while preserving the existing layout
  persistence. RTL Insights now uses it for the shared graph view behind State
  Transition Graph, FSM Graph, and Module Block Diagram. Signal Journey and
  Clock/Reset stay on tree/report surfaces; Wave Preview stays on its custom
  painted QWidget.
- The remaining UI acceptance work is the current Light/Dark automated and
  screenshot QA plus small app-shell polish if a concrete regression appears.
  The earlier follow-up QA polish closed the hard-coded main editor tab bar
  style path by routing it through `InsightVisualStyle`. Do not
  broaden UI polish into semantic analysis, hotspot data, FSM discovery, module
  block extraction, Wave Preview business logic, or workspace scanning.
- Insight UI v2 visual foundation remains established through shared
  `InsightVisualStyle` Qt helpers. Signal Kernel Graph, Signal Usage Hotspot,
  State Transition Graph, Module Block Diagram, and Wave Preview consume these
  color, pen, font, panel, search, segmented-control, legend, heat, shell, and
  graph helpers instead of adding panel-local palettes.
- Signal Usage Hotspot opens from the editor source-symbol context action
  `Signal Usage Hotspot` and from RTL Insights `Usage Hotspot`. Track mode uses
  report lanes/items with role-colored blocks; Matrix mode uses report matrix
  cells and item drill-down. Known follow-up space: richer async progress and
  deeper virtualization for very large hotspot reports.
- Current RTL readability repair: Module Block Diagram is module-only for
  UI/report consumption, filters interface declarations/instances, wraps
  sibling child modules into tighter containers, and preserves readable
  fill/border/text on hover and selection. Design hierarchy defaults to hiding
  interface/interface-instance nodes. FSM drawing now uses the new
  `fsmgraphlayout` Sugiyama module rather than the deleted `fsmdiagram*`
  model/layout/router/renderer path.
- Current FSM drawing rewrite is present in the working tree: `fsmgraphlayout`
  is a pure layout module that consumes `FsmGraph` and returns node rectangles,
  edge polylines, arrow angles, and compact `C#` label anchors. RTL Insights
  maps that geometry to Qt scene items for FSM Graph and State Transition Graph
  without restoring coordinator-local layout/routing logic. The snapshot target
  `fsm_ui_snapshot_test` writes review PNGs under `artifacts/ui/fsm/`.
  The latest compacting repair keeps the Sugiyama skeleton and adds
  text-measured node widths, tighter default spacing, subtitle-free alias
  nodes, transpose refinement, layer-channel ordering, and a bounded
  fallback-alias rule for edges that still cross multiple independent edges.
  Alias nodes are dashed, canonical-linked, and do not change service-owned FSM
  counts. Implicit transition endpoints are separate self-canonical nodes with
  an `implicitState` style marker; alias identity is now restricted to exact
  same-name visual copies. Top-down non-self edges leave source bottoms, enter target tops, use
  rounded orthogonal paths, snap columns to a grid, and keep `C#` labels next to
  owned vertical segments. `relationship_test` now rejects over-budget rendered
  span, invalid alias metadata, wrong ports, detached labels, collinear edge
  overlap longer than 2 px, label/edge intersections, shared horizontal
  channel-track overlap, cropped node text line boxes, and independent-edge
  crossings above 2. Snapshot metrics currently show 0 crossings for
  `elec_cfg_ns`, `phy_cfg_ns`, `phy_pass_thrg_cfg_ns`, and
  `vendor_ip_edma_ctx`; scene areas are 0.556, 0.605, 0.516, and 0.525 of the
  previous fixed-width baseline, with no fallback aliases triggered.
  Current interaction work adds condition tooltips on transitions, linked
  edge/label hover, canonical-plus-alias state hover, incident-edge propagation,
  and selected-over-hover priority. The snapshot harness now emits dedicated
  state-hover and edge-hover review images.
  The state-hover case selects the largest alias family (`S_IDLE`: one
  canonical plus two aliases), verifies all six incident transitions, and
  verifies that the independent `default` state is not highlighted. Direct
  `default` selection selects only that node; direct hover highlights that node
  and its actual incident transition.
  Current pure-layout verification includes 100 deterministic random FSMs with
  5-50 states and mixed back edges, self-loops, reciprocal pairs, long names,
  and unreachable components. All generated graphs pass the unchanged global
  invariants; failures exposed during implementation were repaired in generic
  Bezier collision sampling, reciprocal obstacle clearance, channel tracks,
  weak-component packing, and local label placement. The crossing metric now
  samples rendered quadratic/cubic curves at 96 steps and limits shared-endpoint
  exemption to the 12 px port neighborhood; all four review fixtures report 0
  under that stricter definition. Reciprocal ordering now uses a
  crossing-preserving order postprocess followed by layer/node coordinate
  alignment. Aliased long reciprocal edges use short orthogonal routes, while
  adjacent canonical pairs remain separated arcs. Deterministic seeds divisible by four
  also include an endpoint absent from `stateRows`.
  Verification passed for `relationship_test`, `fsm_ui_snapshot_test`,
  `gui_smoke_test`, `insight_visual_style_test`, `completion_test`, and
  `git diff --check`.
- Current Workspace Session State v1 follow-up: `.zs` versioned JSON save/load
  is implemented through `WorkspaceSessionStateService`, explicit Global
  Control commands `ow s save` / `ow s restore` / `ow s clean`, TabManager
  cursor/scroll/active-tab restore, WorkspaceManager scanned-file restore, and
  main-window geometry/dock state persistence. The active-workspace close path
  now saves the session before closing that workspace's tabs, with GUI smoke
  coverage proving `.zs` keeps the open tab. Remaining risk is v1 scope:
  semantic index snapshots are intentionally not hard-restored, and layout
  restore falls back to the default dock layout on Qt state mismatch.
- The current completed baseline includes import-aware package member
  visibility for unqualified semantic lookup, definition, and hover; user template
  JSON usage actions; explicit header/include and package import commands;
  semantic `;m` module instantiation Slot Mode; Macro / Define semantics;
  Package Tools phase 1; References / Relationships workflow closure; and Fold
  Shelf persistence/restore/management.
- `;pk` remains an explicit import insertion command. Package member lookup is
  import-aware, and Package Tools remain package-file editing tools.

## Non-Negotiable Architecture

- Use Qt 6, CMake, and Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old
  Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship
  analysis, or long-lived scattered perflog probes.
- Do not revive `;:cmd` or introduce active `;:` behavior.
- UI code must not run Slang, scan workspace files, or own semantic policy.
- New semantic/RTL behavior must flow through:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

- Tree-sitter may be used for editor-local syntax structure and low-latency
  insertion/navigation targets.
- Slang-backed semantic truth must enter UI features through semantic records,
  snapshots, query services, or feature services.
- Keep command routing separate:
  - Global Control: app/workspace/global domains.
  - Command Layer: current-editor local commands.
  - `;cmd`: semantic command completion.
  - `;;cmd`: code template expansion.

## Worktree Policy

- Start every turn by checking the current worktree state.
- Never revert user changes unless explicitly asked.
- If unrelated dirty files exist, leave them alone and stage only the files for
  the current milestone.
- Documentation cleanup may be committed separately from implementation changes.
- Do not push unless the current task owner explicitly asks for it.

## Active Long-Term Scope

Only these long-term tracks are allowed. Do not add extra tracks without an
explicit user request.

### 1. Editor Daily Operations Completion

Goal: make common editing actions easy to reach and consistent.

Allowed scope:

- replace
- goto
- comment
- uncomment
- indent
- unindent
- clear entry points for existing partial command/action paths

First milestones:

- M1.1 inventory existing actions and shortcuts, document gaps, no behavior
  change
  (complete: baseline captured for find, formatter, shortcut/context-menu
  entries, and missing daily action entry points)
- M1.2 add reliable entry points for one action family, with focused editor
  tests
  (complete: comment/uncomment are available through `Ctrl+/`,
  `Ctrl+Shift+/`, and editor context-menu actions)
- M1.3 repeat per action family until all listed actions have usable entries
  (complete: indent/unindent are available through `Ctrl+]`, `Ctrl+[`, editor
  context-menu actions)
- M1.4 add reliable replace and goto line entry points, with focused editor
  tests
  (complete: replace uses `Ctrl+H` and context menu; goto line uses `Ctrl+G`
  and context menu)

### 2. Slot Mode After Template Insertion

Goal: after `;;cmd` template insertion, users can fill multiple editable points
without manually moving the cursor.

Allowed scope:

- editable slots in inserted templates
- jump between slots
- exit/cancel behavior
- integration with `;;cmd`

First milestones:

- M2.1 define slot model and editor state without changing templates
- M2.2 enable slots for the parameter declaration template family
- M2.3 expand slots to existing signal declaration template families
- M2.4 make Slot Mode visible and cyclic
  (complete: all slots blink, active slot is emphasized, status reports
  `SLOT n/m`, and Tab / Shift+Tab wrap without implicit exit)

M2.1 design contract:

- Slot metadata belongs to template expansion data as ranges relative to the
  inserted text. Existing `selectionStart` / `selectionLength` is the
  compatibility path for a single primary slot.
- `EditorCompletionWorkflow` applies template insertion and starts Slot Mode
  after activation; `MyCodeEditor` editor state owns active slot ranges,
  highlight rendering, navigation, and stale-session invalidation.
- Tab advances, Shift+Tab goes backward, both directions wrap, and Esc exits
  Slot Mode without reverting inserted text. Tab / Shift+Tab never complete
  Slot Mode implicitly.
- Slot Mode exits when the cursor leaves the session, the document edit stream
  makes slot ranges unsafe, the tab changes/closes, or Global Control starts.
  Command Layer toggle does not clear Slot Mode by itself.
- First implementation target is `;;p` / `;;lp` because parameter declarations
  naturally have at least name and value fill points.
- Focused verification cases for implementation: activation selects the first
  slot, Tab advances, Shift+Tab returns, Tab and Shift+Tab wrap, Esc exits
  without reverting inserted text, editing a slot keeps later ranges correct,
  moving the cursor outside the session exits, and undo that removes the
  inserted template clears the session safely.

M2.2 implementation status:

- Complete: `;;p` and `;;lp` produce name/value slot metadata.
- Complete: parameter template activation starts editor-local Slot Mode through
  the completion workflow.
- Complete: Slot Mode owns ordered ranges, highlights slots, supports Tab,
  Shift+Tab, explicit Esc exit, name-edit range shifting, and cursor-outside
  stale exit.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M2.3 implementation status:

- Complete: `;;l`, `;;w`, and `;;r` produce a signal-name slot.
- Complete: signal template activation reuses the existing editor-local Slot
  Mode path from parameter templates.
- Complete: signal name editing preserves packed/unpacked dimensions; Tab keeps
  cycling the signal slot until Esc exits Slot Mode.
- Verification: Release `completion_test` passed; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M2.4 implementation status:

- Complete: Slot Mode paints every slot with a weak blinking highlight and a
  stronger active-slot highlight, including zero-length RHS fill points.
- Complete: status messages report `SLOT n/m`.
- Complete: Tab and Shift+Tab wrap between slots and are captured by the Slot
  handler, including Qt Backtab events, without inserting tab characters.
- Complete: Slot Mode now exits through explicit Esc or stale-session actions,
  not by reaching the final slot with Tab.
- Verification: Release `completion_test` covers Slot Mode blink state, Tab
  wrap, Shift+Tab wrap, Backtab capture without text mutation, and Esc exit.

### 3. Batch RTL Edit Actions

Goal: support focused RTL batch edits that reduce repetitive cleanup.

Allowed scope:

- first target: clear assignment RHS and create fill slots
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`
- slot flow after the batch edit

First milestones:

- M3.1 service/report design for selected assignment cleanup
  (complete: `RtlBatchEditService` plans selected-text RHS clearing without
  mutating editor text)
- M3.2 editor command for clear-RHS on selected assignments
  (complete: `MyCodeEditor::clearSelectedAssignmentRhs` applies the report in
  one undoable edit block)
- M3.3 connect result to slot mode
  (complete: successful clear-RHS reports start Slot Mode on `rhsN` fill slots)
- M3.4 expose clear-RHS as a Command Layer command
  (complete: `clear right` clears the selected/current assignment RHS and starts Slot
  Mode through the editor-local clear-RHS path)
- M3.5 make clear-RHS line-oriented for selections
  (complete: selected text expands to touched complete lines before planning
  assignment RHS cleanup)

M3.1 implementation status:

- Complete: `RtlBatchEditService::planClearAssignmentRhs` returns a
  `RtlClearAssignmentRhsReport` containing replacement text, edit metadata,
  zero-length `rhsN` template slots, and validation failure reasons.
- Complete: the service supports complete selected blocking assignments,
  nonblocking assignments, and continuous `assign` statements, while preserving
  surrounding whitespace/comments.
- Complete: unsupported selections fail without mutation for empty selection,
  incomplete statements, declaration initializers, control-flow statements,
  macro statements, ambiguous top-level assignments, unmatched delimiters,
  unterminated strings, and unterminated comments.
- Complete: focused verification covers ready multi-assignment cleanup, slot
  metadata, document-relative edit offsets, declaration rejection, incomplete
  statement rejection, and empty selection reporting.
- Not done in M3.1: editor command wiring, undo block application, and Slot Mode
  entry after replacement. Those stay in M3.2/M3.3.
- The future implementation must remain editor-local or service-owned and must
  not scan the workspace, run Slang from UI, or use regex as SystemVerilog
  semantic analysis.

M3.2 implementation status:

- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` routes the current
  selection through `RtlBatchEditService`.
- Complete: successful reports are applied in one undoable edit block, and the
  replacement range remains selected.
- Complete: validation failures are surfaced through editor status feedback
  without mutating text.
- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` exposes the editor-local
  action without the obsolete named-action layer.
- Not done in M3.2: Slot Mode entry after replacement. That stays in M3.3.
- Verification: Release `completion_test` covers selection application, undo
  restore, and declaration rejection; Release `completion_test` and
  `gui_smoke_test` targets compile/link.

M3.3 implementation status:

- Complete: successful clear-RHS editor-action execution starts Slot Mode using
  the `RtlClearAssignmentRhsReport` template-slot metadata.
- Complete: the text replacement itself remains one undoable edit block.
- Complete: G3.2 failure behavior is unchanged: unsupported selections do not
  mutate text and surface service-owned failure reasons.
- Complete: Tab advances between cleared RHS slots, Esc exits Slot Mode, and
  slot edits shift later RHS ranges through the existing Slot Mode state.
- Verification: Release `completion_test` covers Slot Mode start, slot editing,
  Tab advance, Esc exit, undo restore, and declaration rejection; Release
  `completion_test` and `gui_smoke_test` targets compile/link.

M3.4 implementation status:

- Complete: the Command Layer registry exposes `clear right` as the canonical
  clear-RHS command.
- Complete: `CommandLayerCoordinator` dispatches `clear right` through
  `MyCodeEditor::clearSelectedAssignmentRhs` and leaves failures visible in
  the held Command Layer with a non-modal `No assignment RHS found` message.
- Complete: `MyCodeEditor::clearSelectedAssignmentRhs` also supports the
  no-selection case by locating the current assignment statement and still
  applying the existing `RtlBatchEditService` report path.
- Complete: successful `clear right` execution starts Slot Mode on the cleared
  RHS slots; Command Layer continues only while F24 remains held.
- Verification: Release `completion_test` covers selection cleanup,
  current-assignment cleanup, failure without mutation, undo restore, and Slot
  Mode slot order. Release `gui_smoke_test` target compile/link passed and the
  launched smoke output shows all new `clear right` checks passing, but the full
  monolithic GUI smoke baseline still fails on unrelated existing checks.

M3.5 implementation status:

- Complete: when `clearSelectedAssignmentRhs` sees a selection, it expands the
  selection to the complete line range touched by the selection before passing
  text to `RtlBatchEditService`.
- Complete: partial selections on assignment lines no longer need to include
  the full assignment or RHS; no-selection behavior still targets the current
  assignment statement.
- Complete: Slot Mode slot ordering remains source ordered after line-oriented
  cleanup.
- Verification: Release `completion_test` covers partial multi-line selection
  cleanup, current-assignment cleanup, no-RHS failure without mutation, slot
  ordering, and undo restore. Release `gui_smoke_test` target compile/link
  passed and the launched smoke output shows the new
  `select begin end` then `clear right` flow
  passing, while the full smoke baseline still fails on unrelated existing
  checks.

### 4. Command Layer Refactor

Goal: provide a transient application-level command surface without persistent
editor mode state or short-code registration.

Implemented scope:

- F24 key press enters search, F24 key release exits unfinished search, and
  auto-repeat events do not change lifecycle state. Application deactivation
  clears held state so search cannot remain stuck.
- The canonical registry contains exactly `go <number>`, `go module`,
  `go package`, `go endmodule`, `add signal`, `add parameter`, `add port`,
  `clear right`, `select begin end`, and `help`. Metadata is limited to
  canonical name, description, input kind, and execution id.
- Matching ignores spaces and case. Ranking is exact, full prefix, ordered word
  abbreviation, then ordered character subsequence, with deterministic
  skipped-character and registry-order tie breaks. Enter always commits the
  selected candidate; unique matches never auto-execute.
- Module-relative navigation prioritizes `g100`, `go100`, and `go 100` parsing
  before fuzzy matching and rejects zero, negative, overflow, and out-of-range
  lines with visible reasons.
- `CommandLayerCoordinator` owns F24 state, query, selection, panel, help,
  secondary picker handoff, and dispatch. `CommandLayerService` retains only
  module/package picker shaping and relative-line resolution.
- Module/package pickers take keyboard ownership after opening. F24 release
  records the held-state change without closing the picker; picker completion
  returns to search only if F24 is still held.
- Editing commands reuse the existing add-port, add-signal, add-parameter,
  clear-RHS, and begin/end selection paths. `go endmodule` is navigation-only
  and does not insert or replace text.
- The former parameter picker, signal declaration picker, instance insertion,
  continuous-assign insertion, persistent editor mode, toggle chord, editor
  badge, old prefix records, and automatic unique-prefix dispatch are removed.
- Column Number Tool remains independent of Command Layer and continues to open
  through Alt+C with its existing formatting/inference model and one-edit
  application behavior.
- Verification covers lifecycle, auto-repeat, ordinary backtick input,
  continuous execution, all required abbreviations, ranking and selection,
  line forms and failures, ten-command help, removed registry entries,
  module/package picker handoff, edit/navigation behavior, and Alt+C.

Column Selection visual-column repair status:

- Complete: column mode stores and edits by visual column, converting through
  the editor tab width for selection highlight, copy/cut/paste, delete,
  backspace, printable input, navigation, and mouse selection.
- Complete: column-mode Tab and Shift+Tab are captured by the column handler.
  Tab inserts spaces to the next visual tab stop; Shift+Tab performs visual
  outdent and does not leak to normal editor indentation.
- Verification: Release `gui_smoke_test` target compile/link passed; launched
  smoke output shows the new tab-containing column selection, column input, and
  Tab/Shift+Tab checks passing.

### 5. Limited Workspace Workflow Additions

Goal: improve workspace workflow only in the explicitly allowed ways.

Allowed scope:

- ignored directories
- restore Global Control `ow r` for recent workspaces

Not in scope:

- session restore
- include dirs/defines configuration UI in this track; that belongs to Track 12
- recent files
- broad workspace UX expansion

First milestones:

- M5.1 document and audit current workspace open/recent behavior
  (complete: owner classes, existing recent-workspace persistence, current
  Global Control `ow` behavior, hidden `ow r` compatibility, and ignored-path
  baseline are documented)
- M5.2 add ignored-directory model/service path
  (complete: `WorkspaceIgnoreService` and `WorkspaceManager` now provide the
  workspace-owned path for active ignored directories)
- M5.3 restore `ow r` as a displayed Global Control child command
  (complete: `ow r` is displayed in the `ow` domain and routes to the existing
  recent-workspaces dialog)

M5.1 audit status:

- Complete: `WorkspaceManager` owns open/close/switch, aliases, cached scanned
  files, directory scan lifecycle, file watching, and recent-workspace storage.
- Complete: `WorkspaceEntry` already caches scanned files and `scanComplete`;
  switching to a scanned workspace restores cached files without a new scan.
- Complete: recent workspaces are persisted with `QSettings`, deduplicated,
  capped at 20, and exposed through `recentWorkspaceEntries()`.
- Complete: Global Control currently shows root domains only, and the `ow`
  domain displays `ow 1`, `ow 2`, and parameterized `ow <num>` behavior.
- Complete: `ow r` is hidden from `GlobalControlService`, but `MainWindow`
  still has an internal `ow r` handler and recent-workspaces dialog.
- Complete: `ProjectModel` already filters `ignoredPaths`, but no workspace
  workflow/service path sets ignored directories yet.
- Verification: documentation inspection plus `git diff --check`.

M5.2 implementation status:

- Complete: `WorkspaceIgnoreService` validates ignored-directory requests for a
  workspace, normalizes relative/absolute paths, deduplicates them, and rejects
  the workspace root, outside paths, and existing non-directory paths.
- Complete: `WorkspaceManager::setIgnoredDirectories()` applies the normalized
  ignored directories through `ProjectModel::setIgnoredPaths()`, refreshes
  current file lists from the model, updates file watching, and emits existing
  workspace/file-list signals.
- Complete: each open `WorkspaceEntry` stores its ignored directories; cached
  workspace restore reapplies raw scanned files before ignored paths, so hidden
  files reappear after clearing ignores.
- Not done in M5.2: ignored-directory UI, `ow r`, session restore, include
  dirs/defines UI, recent files, or broader workspace workflow changes.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `completion_test` passed
  directly with 669 checks and 0 failures; `ctest -R "^completion_test$"`
  passed.

M5.3 implementation status:

- Complete: `GlobalControlService` now displays `ow r` as an `ow` domain child
  command next to `ow 1` and `ow 2`.
- Complete: exact `ow r` queries return the recent-workspaces command instead
  of the numeric count hint.
- Complete: dispatch continues through the existing `MainWindow` recent
  workspaces dialog path; no new session restore, include dirs/defines UI,
  recent files, or broad workspace UX was added.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; Release `completion_test` passed
  directly with 672 checks and 0 failures; `ctest -R "^completion_test$"`
  passed. `gui_smoke_test` was not launched.

### 6. Completion / `;cmd` / `;;cmd`

Goal: make completion commands customizable and clearly separated from Command Layer.

Allowed scope:

- user templates
- custom abbreviations
- slot mode integration
- responsibility boundary between `;cmd`, `;;cmd`, and Command Layer

First milestones:

- M6.1 document current command/template responsibilities
  (complete: `;cmd`, `;;cmd`, Command Layer, Global Control, and removed
  named-action ownership/conflict boundaries are documented)
- M6.2 add user-template storage/query model
  (complete: `UserTemplateService` persists and queries validated user template
  records as `CodeTemplateItem`-compatible data)
- M6.3 add custom abbreviation resolution
  (complete: `CustomAbbreviationService` persists and resolves compact aliases
  for existing `;cmd` and `;;cmd` tokens)
- M6.4 integrate user templates with slot mode
  (complete: service-owned user templates enter the `;;cmd` completion path and
  preserve slot metadata through activation)
- M6.5 make inline commands explicit Tab abbreviations
  (complete: typing `;cmd` / `;;cmd` no longer enters command mode; plain Tab
  expands the nearest registered suffix at arbitrary code positions while
  preserving Slot Mode, Column Mode, popup, and normal Tab priority)
- M6.6 harden inline abbreviation cancellation and lexical context
  (complete: cursor movement/selection cancels active inline sessions without
  editing source; a single state-machine lexical scan handles strings, line
  comments, and block comments)

M6.1 audit status:

- Complete: `;cmd` is documented as inline semantic command completion owned by
  `InlineCommandMode`, `CompletionCommandMode`, `CompletionService`, and
  `CompletionSemanticQuery` over semantic snapshot data.
- Complete: `;;cmd` is documented as inline template expansion owned by
  `CodeTemplateService`, with `EditorCompletionWorkflow` applying insertion and
  starting Slot Mode when template slot metadata exists.
- Complete: inline commands are documented as Tab-only abbreviations rather
  than input-time command mode triggers. The parser accepts arbitrary code
  positions, resolves `;;cmd` before `;cmd`, rejects comments/strings, and uses
  a saved anchor context for semantic scope.
- Complete: the obsolete named-action layer is documented as removed;
  daily editor/app actions are owned by their normal shortcuts, context menus,
  Command Layer registrations, or direct editor/file APIs.
- Complete: Command Layer and Global Control are documented as separate command
  surfaces with separate registries/services/coordinators.
- Complete: conflict boundaries are documented: do not revive `;:cmd`, keep
  `;cmd` semantic, keep `;;cmd` template-only, keep Command Layer application-level, and keep
  Global Control app/workspace/global.
- Verification: documentation inspection plus `git diff --check`.

M6.2 implementation constraints:

- User-template storage/query must extend service-owned template data, not UI
  widgets.
- Built-in template behavior and current Slot Mode metadata must remain
  compatible.
- `;cmd`, Command Layer, and Global Control namespaces must not be changed by the
  user-template storage milestone.

M6.2 implementation status:

- Complete: `UserTemplateService` owns user template validation, persistence,
  reload, exact-token query, add/update, remove, and clear operations.
- Complete: user templates are file-maintained JSON records. The service reads
  a global `user_templates.json` and an optional workspace
  `.zeroslack/user_templates.json`; focused tests can inject both file paths.
- Complete: user template records use compact `;;` command tokens and
  non-empty `body` / `insertText`, reject invalid slot ranges and invalid
  commands, preserve `CodeTemplateSlotList` metadata, and expose load issues
  for invalid JSON or rejected records.
- Complete: query results are `CodeTemplateItem`-compatible, so future template
  popup and Slot Mode work can consume the same data shape.
- Complete: built-in `CodeTemplateService` behavior remains separate and
  unchanged; user templates cannot override built-in template tokens, and the
  reserved `;;h` / `;;pk` holes stay unavailable to user JSON in this phase.
- Not done in M6.2: user-template UI, custom abbreviations, changes to
  `;cmd`, Command Layer, Global Control, or broader Slot Mode activation.
- Verification: Release `completion_test` and `gui_smoke_test` passed through
  `ctest -R "^(completion_test|gui_smoke_test)$" --output-on-failure`.

M6.3 implementation constraints:

- Custom abbreviation storage/query must stay in a service layer, not UI
  widgets.
- Custom abbreviations may resolve only to existing compact `;cmd` semantic
  command tokens or `;;cmd` template command tokens.
- `;:cmd`, Command Layer, and Global Control namespaces must remain unchanged.
- Built-in command/template behavior and current Slot Mode activation must
  remain compatible.

M6.3 implementation status:

- Complete: `CustomAbbreviationService` owns validation, persistence, reload,
  prefix query, intent-scoped query, exact resolution, add/update, remove, and
  clear operations for compact custom abbreviations.
- Complete: custom abbreviations are stored under `QSettings`
  `customAbbreviations/items`, with injectable ini-file storage for focused
  tests.
- Complete: records reject empty or non-compact abbreviations, command-surface
  marker aliases, duplicate aliases, invalid command tokens, and reserved
  `;:` action tokens.
- Complete: resolution returns the target inline command intent and command
  token without scanning workspaces, running Slang, or touching UI rendering.
- Complete: built-in `;cmd` and `;;cmd` behavior remains unchanged; custom
  abbreviations are not Command Layer or Global Control commands.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M6.4 implementation constraints:

- User template integration must consume `UserTemplateService` from
  `CompletionService`, not from UI widgets.
- User template command recognition must stay inside the `;;cmd` template
  namespace and must not change `;cmd`, Command Layer, Global Control, or custom
  abbreviation behavior.
- Built-in `CodeTemplateService` templates must remain first-class and
  compatible.
- User template activation must preserve `CodeTemplateSlotList` and use the
  existing `EditorCompletionWorkflow` / `MyCodeEditor` Slot Mode path.

M6.4 implementation status:

- Complete: `CompletionService` can consume an injected `UserTemplateService`
  for tests and falls back to the singleton service in normal use.
- Complete: compact user `;;token ` commands are recognized as
  `InlineCommandIntent::CodeTemplate` through service-owned user template
  records, while built-in inline descriptors keep priority.
- Complete: template completion results merge built-in `CodeTemplateService`
  items with exact-token `UserTemplateService` items. Workspace user templates
  override global user templates with the same token, but user templates never
  override built-in `;;cmd` records.
- Complete: user template activation carries insert text, primary selection,
  and `CodeTemplateSlotList` metadata through the existing completion
  activation and Slot Mode path.
- Verification: Release `completion_test` and `gui_smoke_test` passed through
  `ctest -R "^(completion_test|gui_smoke_test)$" --output-on-failure`.

Latest user template JSON phase:

- Scope: first-stage file-maintained user templates only. This does not add a
  GUI template editor, import/export, macro recording, variables, new `;cmd`,
  Command Layer, Global Control, Package Tools, or `;:` entry.
- Storage: global templates live in the app config `user_templates.json`;
  workspace templates live at `.zeroslack/user_templates.json` under the active
  workspace. The JSON root may be an array or an object with a `templates`
  array.
- Record fields: `command`, `description`, `body` or `insertText`, and optional
  `slots` entries with relative `name`, `start`, and `length`.
- Merge policy: workspace user templates override global user templates by
  command token. Built-in template tokens cannot be overridden. `;;h` and
  `;;pk` remain reserved holes in this phase and are reported/ignored if user
  JSON tries to define them.
- Runtime: valid user templates enter the existing `;;cmd` template completion
  path. Activation replaces the command input and starts Slot Mode when slot
  metadata exists. Invalid JSON, invalid commands, invalid slots, and conflicts
  are exposed through `UserTemplateService` load reports and do not affect
  built-in templates.
- Coverage checks global load, workspace priority, insertion text, Slot Mode
  metadata, built-in conflicts, invalid JSON, invalid slot/command records,
  reload after file changes, and unchanged `;cmd`, Command Layer, and Global
  Control boundaries.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest user template JSON usage entry:

- Scope: lightweight menu/action access to the existing JSON workflow only.
  This does not add a template GUI editor, import/export, variables, macro
  recording, new `;cmd`, new `;;cmd`, Command Layer, Global Control, or Package
  Tools features.
- Tools / User Templates contains Open Global User Templates, Open Workspace
  User Templates, and Reload User Templates.
- Open Global uses `UserTemplateService`'s global path. Open Workspace requires
  an active workspace and uses `.zeroslack/user_templates.json` under that
  workspace. Missing files are created with only:
  `{ "templates": [] }`
- Reload calls `UserTemplateService::reload()`, shows loaded/ignored counts in
  the status bar, and shows a lightweight issue report when invalid JSON,
  invalid commands, invalid slots, reserved tokens, or conflicts are ignored.
  Issue rows include file, command, field, and reason.
- Coverage checks global skeleton creation/opening, workspace skeleton
  creation/opening, no-workspace failure, reload success status, visible issue
  reporting for invalid records, invalid JSON report text, unchanged built-in
  templates, unchanged `;cmd`, unchanged Command Layer registry, unchanged Global
  Control template list, and the previous user-template insertion/Slot Mode
  regressions.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

### 7. Fold / Fold Shelf

Goal: make Fold Shelf durable and maintainable.

Allowed scope:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

First milestones:

- M7.1 define shelf persistence model
  (complete: owner inventory, versioned item schema, service/model boundary,
  and G7.2 same-file restore requirements are documented)
- M7.2 persist and restore same-file items
  (complete: `FoldShelfPersistenceService` plus model mutation persistence and
  same-file stale restore handling)
- M7.3 cross-file restore
  (complete: `FoldShelfRestoreService` restores selected items into the active
  editor through the existing insertion path and persists consume/remove/stale
  outcomes)
- M7.4 rename/search/clean management actions
  (complete: `FoldBlockShelfModel` owns rename, query/filter, and
  stale/consumed cleanup mutations; the panel provides only management UI)

M7.1 inventory:

- `FoldBlockShelfModel` owns the current in-memory `FoldShelfItem` list and
  item lifecycle: add, consume, remove, and clear.
- `FoldShelfItem` currently carries id, alias, text, source file/module,
  source line range, line count, origin kind, consumed, and stale fields.
- `encodeFoldShelfItem` / `decodeFoldShelfItem` serialize the current item
  shape for drag/drop MIME transport, not durable storage ownership.
- `FoldBlockShelfPanel` renders model items, previews text, accepts editor
  fold-block drops, initiates shelf-item drags, and handles delete/restore
  prompts for moved unconsumed items.
- `EditorFoldingController` owns shelf mode, custom fold extraction,
  move/delete source behavior, shelf-item insertion, and custom fold marker
  editing.
- `MainWindow` owns the dock/model wiring, Global Control `fd s` entry, panel
  visibility, mode highlighting, and restore command dispatch through
  `TabManager` plus the editor insertion API.

M7.1 persistence schema:

- Use a versioned durable root such as `foldShelf/v1/items`, scoped by
  normalized workspace root when available.
- Persist only model/service data: `id`, `alias`, `text`, `sourceFile`,
  `sourceModule`, `sourceStartLine`, `sourceEndLine`, `lineCount`,
  `originKind`, `consumed`, and `stale`.
- Keep `originKind` limited to `moved` and `copied`.
- Store `sourceFile` normalized; UI may derive relative display text but must
  not be the persistence owner.
- Treat MIME JSON as drag/drop transport. Durable load/save may share fields
  but must own validation, migration, and write timing separately.

M7.1 ownership and G7.2 requirements:

- Persistence policy belongs in `FoldBlockShelfModel` or a dedicated Fold
  Shelf persistence service. `FoldBlockShelfPanel` must remain a consumer.
- `MainWindow` may wire model/service lifetime and route restore actions, but
  must not hand-roll serialization.
- G7.2 should persist on model mutations, reload shelf items for the current
  workspace, restore same-file items through the existing editor insertion
  path, and remove/consume items only after successful insertion.
- G7.2 should mark missing/invalid source locations stale and leave cross-file
  relocation, rename, search, and cleanup for later milestones.
- Verification: source inspection of Fold Shelf owner classes plus
  `git diff --check`.

M7.2 implementation constraints:

- Persistence must be service/model-owned; `FoldBlockShelfPanel` must remain a
  consumer.
- Use the G7.1 `foldShelf/v1/items` schema and scope items by normalized
  workspace root.
- Persist add, consume, stale mark, remove, and clear mutations.
- Restore same-file items through the existing `MainWindow` / `TabManager` /
  `MyCodeEditor` insertion path.
- Remove or consume a persisted item only after successful insertion.
- Mark unavailable source file, failed file open, or failed source-location
  insertion as stale.
- Do not add cross-file relocation, rename, search, or clean management.

M7.2 implementation status:

- Complete: `FoldShelfPersistenceService` owns versioned QSettings-backed
  load/save under `foldShelf/v1/workspaces/<scope>/items`, with workspace-root
  hashing and normalized source paths.
- Complete: `FoldBlockShelfModel` can attach a persistence service and
  workspace root, reload persisted items on root changes, and persist add,
  consume, stale mark, remove, and clear mutations.
- Complete: `MainWindow` wires the singleton persistence service into the model
  and updates the model workspace scope on workspace activation/close.
- Complete: restore failure for missing source file, failed open, or failed
  source insertion marks the item stale; successful restore removes the item
  after insertion.
- Complete: `FoldBlockShelfPanel` remains storage-policy free.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M7.3 implementation constraints:

- Cross-file restore must be service/model-owned; `FoldBlockShelfPanel` remains
  a consumer that emits restore requests.
- Restore must target the active editor and use the existing fold-shelf editor
  insertion API instead of UI-owned text editing.
- Persisted items are consumed or removed only after successful insertion.
- Missing active editor or failed insertion marks the item stale and reports a
  clear failure reason.
- Do not add rename, search, or clean management in M7.3.

M7.3 implementation status:

- Complete: `FoldShelfRestoreService` owns active-editor restore reports,
  validates model/item/editor availability, inserts through the existing
  editor fold-shelf insertion path, and records target file/line details.
- Complete: successful restore can consume or remove the item only after
  insertion succeeds; failed target availability or insertion marks the item
  stale with a service-owned failure reason.
- Complete: `FoldBlockShelfPanel` adds a restore request UI action while
  staying storage-policy free.
- Complete: `MainWindow` wires the panel request to the active editor/cursor
  line and reports restore success or failure through existing status/log
  paths.
- Not done in M7.3: rename, search, clean management, broader shelf UX, or
  semantic/workspace analysis changes.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M7.4 implementation constraints:

- Rename, search/filter, and clean management must stay model/service-owned.
- `FoldBlockShelfPanel` may render controls and route user requests, but it
  must not own persistence policy, scan workspaces, or inspect editor text.
- Clean management is limited to explicit removal of stale or consumed shelf
  items.
- Do not add broad Fold Shelf UX beyond rename, search/filter, and clean.

M7.4 implementation status:

- Complete: `FoldBlockShelfModel::renameItem` trims aliases, rejects blank
  aliases, persists successful changes, and emits the existing change signal.
- Complete: `FoldBlockShelfModel::itemsMatching` filters against shelf item
  data only: id, alias, source file, source module, fold text, origin kind,
  and stale/consumed state.
- Complete: `FoldBlockShelfModel::removeConsumedOrStaleItems` removes only
  stale or consumed items, persists the mutation, and reports the removal
  count.
- Complete: `FoldBlockShelfPanel` adds a search field plus Rename and
  Clean Stale/Consumed buttons while remaining a model consumer.
- Not done in M7.4: broader shelf UX, editor semantic behavior, workspace
  scanning, or unrelated Fold Shelf workflow.
- Verification: `git diff --check`; Release `completion_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

### 8. Signal Kernel Graph

Goal: make dense signal graphs usable.

Allowed scope:

- high fanout collapse/grouping
- filtering
- in-graph search

First milestones:

- M8.1 fanout grouping model in service/report layer
  (complete: `SignalKernelGraphReport` now carries high-fanout grouping
  metadata without changing raw nodes/edges)
- M8.2 UI collapse/expand for grouped fanout
  (complete: `SignalKernelGraphPanelCoordinator` renders collapsible fanout
  group summary/header items while preserving raw visible-node actions)
- M8.3 filtering and in-graph search
  (complete: panel-local input/output/cross-module filters and graph search
  consume existing report/node data while preserving fanout collapse behavior)

M8.1 implementation constraints:

- Fanout grouping policy belongs in `SignalKernelGraphService` and report data,
  not in UI widgets.
- Preserve existing `inputs`, `outputs`, and `edges` so non-grouped rendering
  remains unchanged.
- Do not add collapse/expand UI, filtering UI, or graph search in M8.1.

M8.1 implementation status:

- Complete: `SignalKernelGraphFanoutGroup` records group id, role, lane,
  module, display name, grouped node ids, per-group count, total side count,
  cross-module state, and high-fanout state.
- Complete: `SignalKernelGraphReport` exposes the service threshold plus input
  and output fanout group lists.
- Complete: `SignalKernelGraphService` emits grouping metadata only when a
  graph side reaches the service-owned fanout threshold; original nodes, edges,
  and module groups are preserved.
- Complete: focused synthetic graph coverage verifies non-dense graphs stay
  ungrouped and dense output fanout reports group metadata.
- Verification: `git diff --check`; Release `relationship_test` and
  `gui_smoke_test` targets compile/link; `ctest -R "^relationship_test$"`
  passed. `gui_smoke_test` was not launched.

M8.2 implementation constraints:

- Use the G8.1 `SignalKernelGraphReport` fanout group metadata; do not move
  grouping policy into the panel.
- Keep collapse/expand state in `SignalKernelGraphPanelCoordinator`.
- Preserve visible raw node preview, navigation, and rebase handlers.
- Do not add filtering UI or graph search in M8.2.

M8.2 implementation status:

- Complete: high-fanout groups default to collapsed the first time a group key
  appears in the panel.
- Complete: collapsed groups hide their raw nodes, render a summary item, and
  route grouped edges through the summary while deduplicating only collapsed
  group edges.
- Complete: expanded groups render the raw nodes with existing preview,
  navigation, and rebase handlers, plus a clickable group header to collapse
  again.
- Complete: collapse/expand state is panel-owned and is verified through
  offscreen `completion_test` coverage.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M8.3 implementation constraints:

- Use existing `SignalKernelGraphReport` and node fields; do not scan the
  workspace or run Slang from panel code.
- Keep filtering and graph-search state in `SignalKernelGraphPanelCoordinator`.
- Preserve G8.2 fanout group collapse/expand state, grouped edge routing, and
  raw visible-node preview/navigation/rebase handlers.

M8.3 implementation status:

- Complete: the panel exposes search plus input, output, and cross-module
  filters above the graph.
- Complete: filtering rebuilds the visible graph from existing report/node data
  and hides fanout groups when all of their members are filtered out.
- Complete: graph search highlights/focuses visible node matches and collapsed
  fanout-group matches without expanding groups implicitly.
- Complete: offscreen `completion_test` coverage verifies filters, collapsed
  group search, and expanded filtered search behavior.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

### 9. Wave Preview

Goal: show a waveform sketch for selected RTL context only.

Allowed scope:

- selected `always` block waveform sketch
- selected module waveform sketch
- no simulation

Not in scope:

- value simulation
- testbench execution
- waveform database import
- timing-accurate verification

First milestones:

- M9.1 selected-always entry and report refresh
  (complete: Wave Preview refresh is scoped to the selected/current `always`
  block through editor Tree-sitter scope detection and the existing service
  report path)
- M9.2 selected-module entry and scoped report
  (complete: Wave Preview falls back to selected/current module scope when no
  selected/current `always` scope is active)
- M9.3 UI polish for sketch readability
  (complete: canvas legend plus Scope/Legend overview rows make scoped
  sketches easier to interpret without adding simulator behavior)
- M9.4 preview-only controls and wording
  (complete: scope/clock/reset/filter/lane controls plus symbolic/no-testbench
  labels make the boundary explicit without adding simulation)

M9.1 implementation constraints:

- Do not add simulator behavior, waveform database import, testbench execution,
  or timing-accurate verification.
- Use editor/Tree-sitter scope detection for the current/selected `always`
  block and pass a bounded scope into the existing Wave Preview query/report
  path.
- Keep selected-module Wave Preview out of scope until M9.2.

M9.1 implementation status:

- Complete: `TSDocument` exposes a current/selected `always_construct` scope
  target and rejects selections spanning multiple `always` blocks.
- Complete: `MyCodeEditor` exposes an editor-local always scope target, and
  `MainWindow` refreshes the Wave Preview dock from that scope only.
- Complete: `WavePreviewService` continues to own scoped report shaping through
  its existing query fields; UI code remains a report consumer.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M9.2 implementation constraints:

- Preserve M9.1 `always` priority: when the cursor or selection identifies an
  `always` block, the preview remains block-scoped.
- Use editor/Tree-sitter scope detection for the current/selected module and
  pass a bounded module scope into the existing Wave Preview query/report path.
- Do not add simulator behavior, workspace scans, UI-side Slang work, or sketch
  readability polish in M9.2.

M9.2 implementation status:

- Complete: `TSDocument` exposes a current/selected module/interface/program
  scope target and rejects selections spanning multiple RTL containers.
- Complete: `MyCodeEditor` exposes an editor-local module scope target, and
  `MainWindow` falls back to it only when no selected/current `always` scope is
  active.
- Complete: module-scoped reports continue through the existing
  `WavePreviewQuery` / `WavePreviewService` scoped report path.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M9.3 implementation constraints:

- Preserve M9.1 and M9.2 scope selection behavior.
- Do not add simulator behavior, waveform database import, testbench execution,
  or timing-accurate verification.
- Keep readability polish in `WavePreviewPanelCoordinator` as report
  rendering only; do not scan workspace files or run Slang from UI code.

M9.3 implementation status:

- Complete: Wave Preview canvas renders a compact legend for trace sketches and
  assign/blocking/nonblocking code sketches.
- Complete: the tree view adds Scope and Legend overview rows before detailed
  trace, warning, activity, lane, and event rows.
- Complete: focused panel coverage verifies scoped overview rows and reserved
  canvas space for the legend.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M9.4 implementation status:

- Complete: `WavePreviewPanelCoordinator` adds scope, clock, reset, lane filter,
  assign/condition/state/source lane controls, and preview-only/no-testbench
  wording in the summary, legend, and overview rows.
- Complete: coverage verifies selected module/always static lane/event
  generation and includes the larger `cpld_preproc.sv` fixture without
  modifying user RTL files.
- Verification: Debug `completion_test`, `relationship_test`,
  `gui_smoke_test`, `insight_visual_style_test`, and
  `signal_usage_hotspot_panel_test` targets compile/link; focused CTest passed.

### 10. State Transition Graph

Goal: show state transition graph only for selected symbols that structural FSM
discovery identifies as next-state roles.

Allowed scope:

- discover current/next roles from clocked current-state assignments and
  transition logic
- trigger only when the selected symbol is the discovered next-state role
- selected discovered current-state roles must not show the graph
- names are compatibility/display hints, not semantic gates
- service-owned transition extraction/report
- current UI presentation is controls plus transition table plus the shared
  Sugiyama FSM canvas renderer

First milestones:

- M10.1 structural role gating test
  (complete: editor source-symbol action exposes State Transition Graph only
  for selected symbols that are next-state roles in discovered FSM pairs, and
  rejects current-state roles or non-FSM symbols)
- M10.2 transition report service
  (complete: `StateTransitionGraphService` shapes accepted structural
  next-state role requests into selected next-state graph reports)
- M10.3 graph UI rendering and navigation evidence
  (superseded by the new `fsmgraphlayout` Sugiyama renderer; the old
  coordinator-local and `fsmdiagram*` paths remain deleted)
- M10.4 panel controls, inspector, and transition table
  (complete: top signal/current/next controls, visibility toggles, selected
  metadata inspector, and transitions table are rendered from existing report
  data while structural role gating remains unchanged)
- M10.5 path-centric complex layout and alias-state semantics
  (removed/superseded: path-centric layout, alias-heavy nodes, flowBreak, and
  router-led behavior were deleted; current drawing uses Sugiyama layering)

M10.1 implementation constraints:

- Do not add new state-transition extraction, report shaping, or graph
  rendering.
- Keep the trigger policy in a service path, not in direct UI checks.
- Preserve existing RTL Insights FSM graph behavior and use it only after the
  trigger gate accepts the selected symbol.

M10.1 implementation status:

- Complete: `StateTransitionTriggerService` owns structural FSM role gating for
  selected source symbols.
- Complete: editor source-symbol menu/request state enables `Show State
  Transition Graph` only for selected symbols that are discovered next-state
  roles.
- Complete: selected current-state roles, ordinary symbols, and missing module
  context do not dispatch the state-transition action.
- Complete: accepted requests route through `EditorCoordinator` and
  `SemanticPanelRefreshCoordinator` to the existing RTL Insights FSM graph path.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^completion_test$"` and `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M10.2 implementation constraints:

- Preserve M10.1 structural role gating.
- Own accepted-request report shaping in a service path, not UI code.
- Reuse existing semantic/index-backed FSM graph data where practical.
- Do not add new broad graph UI rendering in this milestone.

M10.2 implementation status:

- Complete: `StateTransitionGraphService` validates accepted requests through
  `StateTransitionTriggerService`, builds module FSM graph data through
  `FsmGraphService`, and filters the result to the selected next-state signal.
- Complete: accepted structural next-state role reports expose selected signal,
  module, state count, transition count, and the matching `FsmGraph`.
- Complete: rejected current-state roles, no-FSM modules, and symbols without a
  matching structural next-state graph return service-owned failure reasons.
- Complete: the existing state-transition UI route consumes the new service
  report through RTL Insights without moving extraction or filtering into UI.
- Verification: `git diff --check`; Release `completion_test`,
  `relationship_test`, and `gui_smoke_test` targets compile/link; `ctest -R
  "^relationship_test$"` and `ctest -R "^completion_test$"` passed.
  `gui_smoke_test` was not launched.

M10.3 implementation constraints:

- Preserve G10.1 structural role gating and G10.2 selected next-state report
  filtering.
- UI must consume `StateTransitionGraphReport` data only; current canvas output
  is placeholder-only, not a graph rendering.
- Do not move transition extraction, graph filtering, workspace scanning, or
  Slang execution into UI code.

M10.3 implementation status:

- Complete: State Transition Graph accepted next-state reports now render
  service-backed FSM graph geometry from `fsmgraphlayout` and keep the
  populated transition table.
- Complete: coordinator code maps geometry to scene items only; layout and
  routing remain outside the coordinator.
- Current verification: `relationship_test`, `gui_smoke_test`, and
  `fsm_ui_snapshot_test` pass in the Debug build.

M10.4 implementation status:

- Complete: State Transition Graph and FSM graph surfaces share RTL Insights
  controls/table structure where applicable, with Sugiyama-rendered FSM
  canvases.
- Complete: State Transition Graph populates signal/current/next controls,
  reset/error/unreachable toggles, selected graph metadata, and a transitions
  table from the existing service report.
- Complete: the new regression fixture rejects `cs`/`ns` names when no
  structural state-register update exists, so names remain neither necessary
  nor sufficient.
- Verification: Debug `completion_test`, `relationship_test`,
  `gui_smoke_test`, `insight_visual_style_test`, and
  `signal_usage_hotspot_panel_test` targets compile/link; focused CTest passed.

M10.5 implementation status:

- Removed: path-centric layout, branch lanes, alias-heavy strategy, flowBreak,
  and router-led outside routes remain deleted.
- Current repair: shared layer-middle bus bends were removed; dummy chains are
  straightened before export; reciprocal transitions render as directed curves;
  labels avoid edge segments; long return/forward channels use canonical-linked
  alias nodes; adaptive node widths and compact spacing reduce canvas area.
- Current verification covers pure layout invariants over synthetic and real
  FSM graphs plus GUI/snapshot rendering. The invariants now include collinear
  edge-overlap rejection, label/edge intersection rejection, shared horizontal
  track-overlap rejection, node text line-box bounds, and an absolute crossing
  budget of 2. The review FSM snapshots currently report 0 rendered crossings
  and no fallback aliases. Verification passed for `relationship_test`,
  `fsm_ui_snapshot_test`, `gui_smoke_test`, `insight_visual_style_test`,
  `completion_test`, and `git diff --check`.
- Complete: FSM hover interaction maps edge and label hover to one transition,
  maps canonical/alias state hover to the full canonical family and incident
  transitions, restores visuals on leave, preserves selection, and displays
  wrapped full-condition tooltips from layout output.
- Complete: deterministic FSM fuzz coverage runs 100 fixed seeds through the
  full invariant suite in `relationship_test` without fixture/seed exceptions.
  The final run passes, includes implicit endpoints in 25 seeds, and reports
  its seed range and graph size on failure.

### 11. Module Block Diagram

Goal: show module containment as a block diagram from a selected module.

Allowed scope:

- selected module name as diagram top
- module instance wrapping relationship only
- filter interface declarations, interface instances, and interface-typed
  unresolved children from UI/report consumption
- no signals
- click root module block to jump to module definition
- click child module/instance block to jump to instance declaration and drill
  into the child module when its definition is resolved
- unresolved/blackbox instances remain visible with explicit reasons

First milestones:

- M11.1 service report for module-instance containment from selected module
  (complete: `ModuleBlockDiagramService` builds selected-module containment
  reports from indexed module/instance symbols plus `INSTANTIATES` data,
  excludes signals, and carries module definition links, instance declaration
  links, instance names, and unresolved/blackbox reasons)
- M11.2 render module-only block diagram
  (complete: RTL Insights renders `ModuleBlockDiagramReport` as a module-only
  interactive graph from active or selected module names, including blackbox
  instance nodes)
- M11.3 click navigation to module definitions
  (complete: root modules navigate to definitions, child instances navigate to
  instance declarations, and resolved children drill into their own module
  diagram)
- M11.4 product-shape panel controls and inspector/table
  (complete: toolbar controls, nested block rendering, right inspector, and
  bottom instances table are connected to the service report and selection
  model)
- M11.5 module-only filtering and compact wrapped layout
  (complete: Module Block Diagram rejects interface roots, filters interface
  child instances, and wraps many siblings into multiple columns instead of a
  pure vertical stack)

M11.1 implementation constraints:

- Report shaping must live in a service path, not in UI code.
- Consume existing semantic/hierarchy/relationship data; do not scan workspace
  files or run Slang from UI.
- Include only module containment or wrapping relationships.
- Carry module definition navigation links in report data.
- Defer all visual rendering to M11.2.

M11.1 implementation status:

- Complete: `ModuleBlockDiagramService` owns `ModuleBlockDiagramReport` for a
  selected module query.
- Complete: the report is built from indexed module/instance symbols plus
  `INSTANTIATES` filtering and returns module/instance nodes plus
  instantiation edges.
- Complete: child records are normalized to module definitions when possible,
  while unresolved child module types stay in the report as blackbox nodes
  instead of being dropped.
- Complete: report nodes and edges carry instance names, instance declaration
  links, module definition links, and unresolved reasons.
- Complete: focused relationship coverage verifies root/child containment,
  no signal nodes, edge type filtering, named query resolution, missing-root
  failure reason, carried definition links, instance declaration links, and
  unresolved blackbox nodes.
- Verification: `git diff --check`; Release `relationship_test` target
  compile/link; `ctest -R "^relationship_test$"` passed.

M11.2 implementation constraints:

- UI must consume `ModuleBlockDiagramReport` only.
- Selected module names must become the diagram root.
- Render module containment only; do not render interfaces or signals.
- Do not add workspace scans, UI-side Slang work, or new relationship
  extraction.
- Leave explicit click-navigation evidence to M11.3.

M11.2 implementation status:

- Complete: RTL Insights has a `Module Block Diagram` action for the active
  module context.
- Complete: editor source-symbol actions can dispatch `Show Module Block
  Diagram` for selected names that `ModuleBlockDiagramService` accepts as
  modules.
- Complete: `RtlInsightsPanelCoordinator` renders root and child module nodes
  plus instantiation edges from `ModuleBlockDiagramReport` and does not render
  signals.
- Complete: the module diagram now uses a grid-backed canvas with the selected
  module rendered as a large container and child module/instance nodes arranged
  inside it.
- Complete: child nodes show module type plus instance name; unresolved
  instances render as grey blackbox nodes with their unresolved reason instead
  of producing a blank graph.
- Complete: visible `-` / `Fit` / `+` controls supplement mouse-wheel graph
  zoom.
- Complete: source-symbol routing passes through
  `SemanticPanelRefreshCoordinator`; UI code remains a report consumer.
- Verification: Release `relationship_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "^relationship_test$"` passed.
  `gui_smoke_test` was not launched.

M11.3 implementation constraints:

- Preserve G11.1 report ownership and G11.2 module-only rendering.
- Navigation must use module definition links already carried by
  `ModuleBlockDiagramReport`.
- Do not render signals.
- Do not add workspace scans, UI-side Slang work, or new relationship
  extraction.

M11.3 implementation status:

- Complete: rendered top-module graph nodes carry definition links from
  `ModuleBlockDiagramReport`; child module/instance graph nodes and
  instantiation edges carry instance declaration links plus child definition
  links when resolved.
- Complete: focused panel coverage invokes graph-element navigation for root
  and child module/instance nodes and verifies that the navigation handler
  receives the corresponding module definition or instance declaration file,
  line, and column.
- Complete: double-clicking or test-triggering a resolved child module element
  navigates to its instance declaration and re-renders the Module Block Diagram
  around that child module, so its children remain visible. Unresolved blackbox
  nodes navigate to their instance declaration and do not drill into a missing
  definition.
- Verification: Debug `completion_test`, `relationship_test`,
  `gui_smoke_test`, and `jump_test` targets compile/link; focused CTest runs
  passed. Future Module Block Diagram issues should be captured as compact
  fixtures rather than broad corpus sweeps.

M11.4 implementation status:

- Complete: Module Block Diagram toolbar includes top selection, search, Set
  from selection, Fit, depth, Collapse packages, Show unresolved, layout, and
  more-menu controls.
- Complete: the graph renders module/instance package relationships as nested
  blocks and keeps unresolved children visible with a distinct blackbox state.
- Complete: the right inspector shows selected/type/definition/parent/children/
  unresolved/status/location/workspace/path/depth data and enables only wired
  jump/focus/set-top actions.
- Complete: the instances table lists instance/module/parent/file/status; row
  selection syncs the graph node and double-click uses the existing navigation
  path.
- Verification: Debug `completion_test`, `relationship_test`,
  `gui_smoke_test`, `insight_visual_style_test`, and
  `signal_usage_hotspot_panel_test` targets compile/link; focused CTest passed.

M11.5 implementation status:

- Complete: `ModuleBlockDiagramService` accepts only module definitions as
  roots, filters interface declarations and interface instance targets from
  child reports, and leaves interface data available to other semantic reports.
- Complete: editor source-symbol and relationship-panel activation paths no
  longer present Module Block Diagram as an interface action.
- Complete: RTL Insights lays sibling child modules out in wrapped rows/columns
  under the selected parent instead of a pure vertical stack.
- Verification: `relationship_test` covers interface filtering, interface-root
  rejection, wrapped sibling coordinates, blackbox preservation, and carried
  source links.

### 12. Workspace Project Configuration And Diagnostics Workflow

Goal: make workspace-level SystemVerilog configuration explicit and make
diagnostics usable as a navigation workflow.

Allowed scope:

- per-workspace include dirs
- per-workspace defines
- per-workspace ignored dirs
- per-workspace file extensions
- optional top module / active top
- Problems/diagnostics next/previous navigation
- diagnostic status, owner, current-file summary, and stable jump feedback

Not in scope:

- session restore
- recent files
- broad workspace UX expansion
- new diagnostic engines beyond existing Slang and Semantic index paths
- UI-side Slang execution or workspace scanning

Milestones:

- M12.1 Workspace Configuration Service And Dialog
  (complete: `WorkspaceConfigurationService`, `WorkspaceConfigurationDialog`,
  and `WorkspaceManager::setWorkspaceConfiguration()` persist/apply include
  dirs, defines, ignored dirs, file extensions, and top module)
- M12.2 Diagnostics Navigation And Problems Status
  (complete: `DiagnosticNavigationService`, F8/Shift+F8 actions, owner column,
  current-file summary, analysis state label, and reveal/flash Problems jumps)
- M12.3 Verification And Documentation
  (complete: docs updated; focused Release tests/builds recorded below)

M12.1 implementation status:

- Complete: workspace configuration is stored per workspace through
  `QSettings` with a versioned `workspaceConfiguration/v1` key space.
- Complete: include dirs preserve user order; defines are key/value pairs;
  file extensions default to `.sv`, `.svh`, `.v`, and `.vh`; top module may be
  empty to preserve automatic/default behavior.
- Complete: applying configuration updates `ProjectModel`, refreshes filtered
  workspace file lists, persists ignored dirs through the existing validation
  service, emits existing workspace/file-list signals, and queues analysis
  through the project-change path.
- Complete: workspace analysis keys include include dirs, defines, file
  extensions, and top module so configuration changes do not reuse a stale
  completed workspace-analysis key.

M12.2 implementation status:

- Complete: Problems rows display diagnostic owner, currently `Slang` or
  `Semantic index`, without inventing a new diagnostic owner.
- Complete: the Problems panel displays current-file error/warning/info
  summary and a diagnostics state label (`current`, `stale`, `analyzing`, or
  `background`) using existing analysis signals and band metadata.
- Complete: F8 and Shift+F8 navigate next/previous diagnostics using the
  Problems panel's current scope and severity filters. The navigation service
  sorts by source location after filtering so workspace navigation is
  predictable.
- Complete: Problems double-click navigation now validates missing files and
  invalid locations, routes through `NavigationCommandCoordinator`, and flashes
  the target line after reveal.

M12.3 verification status:

- Passed: Release `completion_test` and `gui_smoke_test` targets compile/link.
- Passed: Release `ctest -R "^completion_test$" --output-on-failure`.
- Fixed after initial M12 acceptance: workspace activation/cached restore now
  applies configuration silently, avoiding extra `filesScanned` /
  `workspaceListChanged` signals and preserving Navigation design hierarchy
  caches for cached workspace switches and closes.
- Passed within Release `gui_smoke_test`: `workspace cached switch emits
  activation only`, `workspace cached switch still avoids rescan`, `navigation
  design cache restores A without rebuild`, and `navigation design cache
  restores B after close`.
- Attempted: Release `ctest -R "^gui_smoke_test$" --output-on-failure`.
  It still fails on existing VENDOR ctrl-click and Wave Preview checks, but the
  required workspace/cache regression checks pass.

### 13. References / Relationships Workflow Closure

Goal: make source-symbol navigation, references, relationship rows, and graph
result jumps easy to find, jump from, and return from.

Allowed scope:

- source-symbol right-click workflow organization
- References result grouping, query context, empty-state reasons, copying, and
  jumps
- Relationships result source jumps and existing graph entry points
- unified navigation path with Back/Forward history isolation per workspace
- status-bar or panel feedback for jump failures

Not in scope:

- package tools
- macro/define semantics, which is owned by Track 15
- Wave Preview expansion
- new graph types
- complex graph algorithms
- expanded State Transition Graph trigger rules

Milestones:

- M13.1 Unified Source-Symbol Context Menu
  (complete: the symbol context menu always shows Go to Definition, Find
  References, Show Relationships, Signal Kernel Graph, State Transition Graph,
  and Module Block Diagram; disabled actions expose reasons; Module Block
  Diagram is limited to existing module definitions; current-state
  names remain rejected)
- M13.2 References Panel Workflow Closure
  (complete: query context, empty reasons, click/activated jump with flash,
  and copy path or file:line)
- M13.3 Relationships Panel Workflow Closure
  (complete: source-code jumps for rows, instance/module-definition jumps where
  existing data supports them, signal driver/consumer/declaration jumps from
  existing relationships, and only existing graph entry points)
- M13.4 Unified Navigation Failure Feedback And History Isolation
  (complete: References, Relationships, and graph jumps share one validated
  navigation path; Back/Forward history is isolated by workspace; failures are
  visible)
- M13.5 Verification And Documentation
  (complete: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release acceptance CTest
  `ctest -R "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` passed)

M13.1 implementation status:

- Complete: source-symbol context menu actions are stable and do not disappear
  when disabled.
- Complete: disabled source-symbol actions carry status/tooltip reasons such
  as no source file, no symbol under cursor, symbol not indexed, next-state
  gating, or module-block gating.
- Complete: Module Block Diagram activation is UI-gated to existing module
  definition records, so ordinary signals and interfaces remain disabled
  without adding new semantic analysis.
- Complete: this milestone is workflow/UI closure only. It does not implement
  package tools, Wave Preview expansion, new graph algorithms, or broader
  State Transition Graph triggers; Track 15 owns macro/define semantics.

M13.2-M13.4 implementation status:

- Complete: References and Relationships panels display query context for the
  active symbol, source file, scope/view/direction/type/depth filters.
- Complete: empty panels render explicit reasons: no symbol under cursor,
  symbol not indexed, no references/relationships found, or workspace analysis
  stale / not ready when the index has no records.
- Complete: result rows support path and `file:line` copy; click and activated
  rows route through the shared navigation path and flash the target line.
- Complete: Relationship rows store existing relationship data for driver,
  consumer, declaration, and instance navigation; instantiation rows prefer
  existing module definition records when available.
- Complete: Relationship result context menus call only existing graphs:
  Signal Kernel Graph, Module Block Diagram, and State Transition Graph.
- Complete: panel and graph jumps are validated for missing files, invalid
  line/column data, stale semantic snapshots or missing symbols, and workspace
  mismatch; workspace mismatch means a target in a different open workspace,
  not an ordinary external result file; failures are surfaced through the
  status bar.
- Complete: `NavigationCommandCoordinator` history entries carry a workspace
  key and Back/Forward prunes entries from other workspaces.
- Complete: acceptance repair for the FSM Graph / State Transition Graph
  panel closure renders service-owned state-register and next-state signal
  nodes, preserves transition edges, adds the existing-data signal-flow edge,
  and lets next-state signal nodes invoke the existing navigation handler.
- Complete: final Release verification passed after the panel rendering and
  workspace-mismatch adjustments: `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link, and `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.

### 14. Package Tools

Goal: give SystemVerilog package scopes a lightweight, package-only insertion
surface for common definitions.

Allowed scope:

- show a Package Tools bar/buttons only when the active editor cursor is inside
  a parseable package scope
- insert first-phase package definition templates for `parameter`,
  `localparam`, `typedef enum`, `typedef struct`,
  `typedef struct packed`, and `function`
- choose insertion positions from Tree-sitter package structure
- default to the line before `endpackage`, with same-kind append anchors when
  a matching package item already exists
- start Slot Mode after insertion and highlight all editable slots
- report clear failure reasons when the current package cannot be determined

Not in scope:

- package/import semantic interpretation
- macro/define handling, which is owned by Track 15
- cross-file package management
- automatic sorting or broad package reordering
- new graph or insight surfaces
- changing existing `;;cmd` template behavior

Milestones:

- M14.1 Package Tools First Phase
  (complete: package-only buttons insert the six requested definition
  templates, stay inside the current package, reject module/interface scope,
  and start Slot Mode)
- M14.1a Verification Baseline Repair
  (complete: Release `relationship_test` rebuild baseline repaired and Package
  Tools phase-1 coverage reinforced without adding Package Tools phase 2)
- M14.2 Future Package Tools Expansion
  (not started: any additional templates, semantic package handling, sorting,
  or cross-file package workflows require a new scoped request)

M14.1 implementation status:

- Complete: `PackageToolService` owns package-only templates and slot metadata
  for parameter, localparam, enum typedef, struct typedef, packed struct
  typedef, and function definitions.
- Complete: `TSDocument::packageToolInsertTarget` derives the current package
  and insertion point from Tree-sitter nodes, rejects module/interface/program
  scope, appends near same-kind direct package items when present, and defaults
  before `endpackage`.
- Complete: `MyCodeEditor::executePackageToolInsert` applies the generated
  template in one edit block and starts Slot Mode with all remapped slots.
- Complete: `MainWindow` renders a lightweight Package Tools bar in the editor
  area only while the active cursor is in a valid package scope.
- Verification: Release `completion_test` and `gui_smoke_test` targets
  compile/link; Release `ctest -R "completion_test|gui_smoke_test"
  --output-on-failure` passed.

M14.1a verification baseline repair status:

- Complete: the Release `relationship_test` rebuild failure was traced to
  generated MinGW build rules that lacked the compiler bin on `PATH`, producing
  blank subcommand failures during object rebuilds. CMake now wraps generated
  compile and link rules with `cmake -E env PATH=...` using the detected
  compiler directory, so `cmake --build` can rebuild `relationship_test` from a
  plain shell without weakening assertions or skipping the target.
- Complete: `completion_test` now checks that all six Package Tools phase-1
  templates have non-empty insert text and slot metadata, and that the packed
  struct template text contains `typedef struct packed`. It also asserts the
  phase-1 tool list remains exactly six entries.
- Complete: `gui_smoke_test` now checks that the six stable
  `packageToolButton_<id>` buttons exist and that the Package Tools button
  count is exactly six; it still only needs one actual click to cover GUI
  insertion and Slot Mode.
- Boundary: no Package Tools phase 2, no package/import semantics, no
  cross-file package management, no package sorting, no graph/insight changes,
  and no `;;cmd` behavior change; Track 15 owns macro/define semantics.
- Verification: Release `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; Release `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$" --output-on-failure`
  passed; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'` passed.

### 15. Macro / Define Semantic Workflow

Goal: make common SystemVerilog macro and preprocessor-branch workflows usable
without turning ZeroSlack into a full preprocessor.

Allowed scope:

- static `define` symbol records and outline rows
- goto definition, hover, and Find References for `` `MACRO`` uses
- clearer undefined macro diagnostics as Semantic index supplements to Slang
- conservative inactive `ifdef` / `ifndef` / `elsif` / `else` / `endif`
  branch decorations using workspace configured defines and static current-file
  `define` / `undef` state

Not in scope:

- Vivado `.xpr` / Tcl parsing
- new define configuration UI
- complete SystemVerilog macro expansion or argument substitution
- formatter-visible text mutation
- changing Package Tools, Command Layer, Slot Mode, diagnostics, references, or
  relationship ownership boundaries

Milestones:

- M15.1 Macro / Define First-Class Semantics
  (complete: outline, goto definition, hover, references, supplemental
  undefined diagnostics, and inactive branch gray decorations are available
  with conservative static behavior)
- M15.1a Acceptance Stabilization
  (complete: GUI smoke reference/relationship dock regression isolates semantic
  services and panel filters; Release acceptance CTest passed twice
  consecutively)

## Huge Workspace Status Audit

Huge Workspace is not an active UX expansion track in this plan. Only audit and
document whether existing low-level strategies still work:

- immutable `ProjectSnapshot` plus all-open-buffer overlay capture
- analysis-band metadata and query ordering
- stale request coalescing, expiration, cancellation, and latest-snapshot restart
- atomic symbol/effective-value/diagnostic publication
- relationship reuse of the published semantic snapshot token
- Activity/status telemetry
- Release performance references for `huge_prj`

Audit milestones:

- HWA.1 list current strategy owners and tests/harnesses
  (complete: current owners and existing verification anchors are documented;
  no Huge Workspace UX features were added)
- HWA.2 run or compile appropriate verification based on environment safety
  (complete: Release build of the HWA-related test/harness targets passed
  without launching GUI executables)
- HWA.3 update docs with confirmed status and gaps
  (complete: current status and remaining audit gaps are documented)

HWA.1 inventory status:

- Current/dirty-open/clean-open/background planning and band metadata are owned
  by `WorkspaceAnalysisPlanService`. Bands remain query/telemetry metadata;
  dirty buffers still replace disk input, and bands do not split semantic
  publication.
- Active/pending request coalescing, stale pending replacement, and request
  telemetry are owned by `WorkspaceAnalysisRequestQueue` and routed through
  `WorkspaceSymbolAnalysisController`.
- `WorkspaceSymbolAnalysisController` captures all open document text and exact
  revisions with the project request. An edit expires the captured worker and
  coalesces a restart from the latest complete `DocumentModel` snapshot.
- `SymbolAnalyzer` materializes one immutable overlay source set, extracts
  symbols, Slang effective-value facts, and diagnostics from it, validates all
  generation/revision gates, and publishes the complete result in one
  GUI-thread transaction. Dirty buffers replace disk input; no dirty-file skip,
  partial checkpoint, staged, or chunked snapshot publication remains.
- Relationship cancellation and stale/cancelled result rejection are owned by
  `RelationshipAnalysisController`, `RelationshipAnalysisWorker`, and
  `RelationshipResultPublisher`. The relationship worker consumes the captured
  semantic snapshot token and its source contents without refreshing open tabs
  through a second Slang symbol analysis.
- Activity visibility is owned by `AnalysisProgressCoordinator` and
  `ActivityLogService`.
- Existing coverage anchors are `completion_test`, `relationship_test`,
  `large_file_perf_test`, and `relationship_perf_test`.
- Release `huge_prj` references remain the current baseline: 428 HDL files,
  about 18.59 MiB, about 5.096s async symbol publication in the focused
  harness, and about 3.891s relationship analysis in the focused harness.

HWA.2 verification status:

- Historical audit path: compile/link relevant Release targets instead
  of launching GUI executables, because this environment has recently shown
  external Windows application-error dialogs during GUI test runs.
- Passed command: Release CMake build target set `completion_test`,
  `relationship_test`, `large_file_perf_test`, and `relationship_perf_test`.
- Artifact check confirmed all four executables exist in the Release build
  directory.
- `ctest` was not run for HWA.2. This historical audit record is not the
  acceptance status of the current corrective milestone, which requires the
  Debug full suite and visible-app validation described above.

HWA.3 confirmed status:

- Owner boundaries are documented for immutable overlay capture, request
  coalescing, symbol/value/diagnostic cancellation and atomic publication,
  snapshot-consistent relationship analysis, Activity telemetry, and
  `SemanticIndexSnapshot` query exposure.
- Existing verification anchors cover the intended strategy surfaces:
  `completion_test`, `relationship_test`, `large_file_perf_test`, and
  `relationship_perf_test`.
- Release compile/link verification for all four HWA-related targets passed in
  HWA.2.
- No Huge Workspace UX or behavior change was implemented during the audit.

HWA.3 open audit gaps:

- HWA.2 did not run the test executables, so runtime behavior is not newly
  confirmed by this audit.
- Full `ctest` was not run during that historical audit because the environment
  had shown external Windows GUI/memory-read dialogs during test execution.
- `relationship_perf_test` was compiled but not executed against
  `test_sv/huge_prj`; the listed Release `huge_prj` timings remain prior
  reference points, not freshly refreshed HWA.3 measurements.
- Activity telemetry was inspected through owner wiring and test anchors, not
  revalidated in a live GUI session.
- Future Huge Workspace work should first close these audit gaps with a safe
  runtime setup before proposing UX or behavior changes.

## Functional Corpus Audit

Retired. `corpus_audit_test` and `test_sv/corpus_audit_report.*` are no longer
maintained because broad corpus sweeps were expensive and produced weak
acceptance signals. Use targeted regression fixtures, GUI smoke, and
feature-specific tests for future coverage.

## Full Feature Audit

Goal: retired. Feature surface ownership now lives in focused regression
targets, GUI smoke, and feature-specific guards rather than a generated
inventory report.

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

## Milestone Definition Of Done

Every implementation milestone must be small, verifiable, and deliverable.

Before a milestone is complete:

- update `readme.md`, `plan.md`, and `goal.md`
- run verification appropriate to the change
- keep UI semantic policy out of UI code
- do not add functionality outside the allowed long-term scope
- commit only when requested, and push only when explicitly authorized

Documentation-only milestones:

- update the relevant docs
- run `git diff --check`
- commit or push only if the user explicitly asks for publication

## Verification Guidance

- Docs only: `git diff --check`.
- Editor-only compile-safe changes: build affected targets such as
  `completion_test`, `ts_doc_test`, or `gui_smoke_test` without launching known
  problematic executables.
- Shared semantic/scheduler/project/snapshot changes: full Ninja and full CTest
  unless executable tests are blocked by the user/environment.
- During CTest, if execution stalls or runs much longer than expected, inspect
  the desktop for a Windows application-error or memory-read dialog before
  treating the run as ordinary long-running test work.
- Visual panels: verify service reports first, then UI smoke if executable runs
  are safe.

Latest acceptance baseline repair:

- Scope: test baseline only; no product feature behavior was changed.
- `completion_test` source-symbol context menu assertions were updated for the
  five-action menu: Find References, Relationships, Signal Kernel Graph, State
  Transition Graph, and Module Block Diagram.
- The test now explicitly verifies ordinary signals keep Module Block Diagram
  disabled, selected module names enable it, and state-transition `ns` /
  `next_state` versus `cs` / `current_state` behavior remains unchanged.
- Release verification passed: `ctest -R "^completion_test$"
  --output-on-failure` and `ctest -R "^relationship_test$"
  --output-on-failure`.

Latest RTL Insight core view modernization:

- Scope: Module Block Diagram, State Transition Graph, and Wave Preview panel
  presentation/behavior only; no new simulator, Vivado integration, QML/WebView,
  or broad FSM engine rewrite.
- Module Block Diagram now exposes a reference-aligned toolbar with top
  selection, Set from selection, Fit, depth, Collapse packages, Show unresolved,
  layout, and more-menu actions. The canvas renders nested module-only blocks,
  and the right inspector plus instances table sync selection and route only
  real jump/focus/re-root actions.
- State Transition Graph keeps structural FSM role gating. Regression coverage
  verifies that `cs`/`ns` names alone do not create a graph, while the UI now
  adds signal/current/next controls, visibility toggles, an inspector, and a
  transitions table.
- Wave Preview remains preview-only/static. The panel now labels output as
  symbolic/no-testbench, adds scope/clock/reset/filter/lane controls, and keeps
  coverage for selected module/always preview generation including
  `test_sv/new/elec_phy_import/phy/cpld_preproc.sv`.
- Debug verification passed: `cmake --build build_verify2 --target
  completion_test relationship_test gui_smoke_test insight_visual_style_test
  signal_usage_hotspot_panel_test`; `ctest --test-dir build_verify2 -R
  "^(completion_test|relationship_test|gui_smoke_test|insight_visual_style_test|signal_usage_hotspot_panel_test)$"
  --output-on-failure`; `git diff --check`.

Latest RTL Insights FSM graph repair:

- Scope: historical FSM candidate filtering repair plus the current Sugiyama
  drawing rewrite.
- `FsmGraphService` now requires parsed case-derived transition evidence before
  publishing an FSM graph candidate, which filters ordinary enum/register
  declarations that merely have a paired next-state-looking signal.
- RTL Insights `FSM Graph` now shows service-backed graph nodes/transition
  edges from `fsmgraphlayout` and a service-backed transition table.
- Regression coverage must verify a non-case enum register is ignored and the
  real `chl_ctrl` `phy_pass_thrg_cfg_cs` / `phy_pass_thrg_cfg_ns` FSM remains
  present with transitions.
- Release verification passed: `completion_test`, `relationship_test`, and
  `gui_smoke_test` targets compile/link; `ctest -R
  "^(completion_test|relationship_test)$" --output-on-failure` passed. The GUI
  smoke executable was not launched to avoid another modal Windows crash dialog
  during this repair loop.

Latest Command Layer refactor:

- The former persistent short-code mode and toggle chord are removed. F24 is a
  hold-to-use application-level command surface, and ordinary backtick input is
  unchanged.
- The registry contains only the ten canonical commands documented in Track 4.
  Query matching is case/space insensitive, ranked deterministically, and
  always requires Enter before dispatch.
- Module/package pickers retain keyboard ownership after opening; releasing F24
  does not cancel them. Column Number Tool is not a Command Layer command and
  remains available through Alt+C.
- The panel continuously shows the query, ranked canonical names, selection,
  descriptions, and failure reasons.

Latest GUI smoke baseline repair:

- Scope: failing Release `gui_smoke_test` baseline only; no new product feature
  was added and no current regression coverage was deleted.
- VENDOR ctrl-click test fixture lookup now resolves from source or build roots so
  Release CTest can find `test_sv/huge_prj/vendor_ip_ctl.sv`.
- Wave Preview scope lookup now keeps safe module/always ranges available when
  Tree-sitter reports localized parse errors inside the scope; the Wave Preview
  test assertions were updated to the current scoped summary wording and canvas
  lane geometry.
- RTL Insights GUI smoke fixture installation now refreshes both the native
  semantic store and snapshot before each panel action, isolating synthetic
  RTL Insights checks from concurrent workspace publication.
- Problems external-diagnostic preservation now uses an unopened external probe
  file and checks for the exact diagnostic row instead of depending on a fragile
  item count.
- Release verification passed: `ctest -R "^completion_test$"
  --output-on-failure` and `ctest -R "^gui_smoke_test$"
  --output-on-failure`; `gui_smoke_test` was repeated successfully.

Latest Package Tools first phase:

- Scope: package-only insertion buttons for the first six requested templates.
- `PackageToolService` owns template text and slot metadata; `TSDocument`
  owns current-package validation and insertion targets; `MainWindow` only
  renders the lightweight button bar.
- Insertions stay inside the current package, default before `endpackage`, and
  append near existing same-kind direct package items when present. Module,
  interface, and program scopes are rejected with clear failure reasons.
- Existing `;;cmd` template behavior is unchanged. Package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Release verification passed: `completion_test` and `gui_smoke_test` targets
  compile/link; `ctest -R "completion_test|gui_smoke_test"
  --output-on-failure` passed.

Latest Macro / Define semantic workflow:

- Scope: conservative static macro semantics, not a full SystemVerilog
  preprocessor.
- `SlangManager` symbol extraction supplements Slang records with static
  `define` macro records, so outline and definition lookup can treat macros as
  first-class symbols while keeping `ifdef` / `ifndef` / `else` / `endif` out
  of outline.
- `` `MACRO`` navigation now resolves current-file definitions first, then
  workspace/include-visible macro definitions from the semantic index.
- Macro hover shows the definition location plus signature/body text, with
  function-like macros showing their parameter list without performing argument
  substitution.
- Find References for macros returns the `define` row and all indexed
  backtick uses through the existing References panel jump/flash workflow.
- Undefined macro diagnostics are supplemented under the Semantic index owner
  without replacing Slang diagnostics.
- Inactive preprocessor branches are grayed conservatively using configured
  workspace defines and static current-file `define` / `undef` state across
  basic `ifdef` / `ifndef` / `elsif` / `else` / `endif` structures.
- Boundaries: no Vivado `.xpr` / Tcl parsing, no define configuration UI, no
  complete macro expansion, no formatter text mutation, and no Package Tools,
  Command Layer, Slot Mode, or relationship ownership change.
- Verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest Macro / Define acceptance stabilization:

- Scope: test isolation and acceptance stability only; no new macro/define
  feature surface.
- The GUI smoke reference/relationship dock regression now runs against an
  isolated local semantic snapshot injected into `ReferenceService`,
  `RelationshipService`, and `HierarchyService`, then restores the services to
  the global semantic index.
- The fixture resets reference and relationship dock filters at entry and exit
  so reference, direct relationship, and hierarchy tree assertions are not
  affected by prior panel state.
- Reference/relationship assertions were not weakened, and Package Tools, Command Layer
  Mode, Slot Mode, diagnostics, references, and relationships behavior outside
  this test fixture was not expanded.
- Release verification passed: `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`; `cmake --build .
  --target completion_test relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure` twice consecutively.

Latest `;m` module instantiation semantic completion:

- Scope: existing `;m` semantic module completion only. `;;m` remains the
  module definition skeleton, and no Package Tools phase 2, include/package/
  import workflow, new command, or Command Layer change was added.
- `CompletionService` now builds a full named instantiation from indexed module
  parameter and port records when available, preserving module definition
  order. Modules without parameters omit `#(...)`; modules without usable port
  records conservatively fall back to the previous simple instantiation text.
- Module instantiation activation starts Slot Mode with slots ordered as
  instance name, parameter values, then port connections. Slot range updates
  now follow text position rather than Tab order so instance-first Slot Mode
  works even when parameter values appear earlier in the generated text.
- Coverage includes parameter+port instantiation text and slot order,
  no-parameter instantiation without `#(...)`, insufficient-semantics fallback,
  unchanged `;;m` skeleton behavior, and GUI activation into Slot Mode.
- Release verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`.

Latest header/include command convergence:

- Scope: explicit header include command entry only. Package/import completion,
  Command Layer, Global Control, and `;;h` template descriptors remain out of scope.
- `;h <query>` now owns include insertion for existing headers. It reuses the
  existing include candidate/filter path and inserts a full SystemVerilog
  `` `include "..."`` statement.
- `;h -n <name>` creates a header and inserts the include. Names without a
  suffix default to `.svh`; `.vh` / `.svh` suffixes are accepted; creation
  prefers the current file directory and refuses to overwrite existing files
  with an explicit already-exists result.
- The old hidden `` `include `` + space trigger was removed, so typing an
  include directive no longer auto-inserts quotes or opens the include popup.
- Coverage now checks existing-header insertion, `.svh` default creation,
  no-overwrite behavior, absent `;;h`, and legacy include-space inactivity.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test`; `cmake --build . --target relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest package import explicit entry:

- Scope: explicit package import insertion only. This is not package
  navigation, Package Tools phase 2, `pkg::symbol` completion, cross-file
  package management, Command Layer, or Global Control work.
- `;pk <query>` searches existing package semantic records through the
  completion service and inserts `import pkg_name::*;` on activation.
- `;p` remains the parameter semantic command, Command Layer `go package` remains the package
  picker / package jump workflow, Package Tools remain package-file editing
  tools, and `;;pk` remains absent.
- Coverage checks package candidate matching, activation text insertion,
  absent `;;pk`, unchanged `;p`, and unchanged `go package` behavior through the
  existing Command Layer smoke coverage.
- Release verification passed: `cmake --build . --target completion_test
  gui_smoke_test relationship_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest package/import semantic repair:

- Scope: unqualified package member visibility for semantic lookup, definition,
  hover, and explicit command candidates. Package members marked `PackageVisible` are not global;
  they are visible only when the current file/scope has an active
  `import pkg::*;`.
- Local/module declarations keep priority over imported package members.
  Same-name members imported from multiple packages are treated as ambiguous
  and are not used for random unqualified jumps.
- `;pk` remains import insertion only, Command Layer `go package` remains package navigation,
  Package Tools remain unchanged, and no ordinary `pkg::symbol` popup exists.
- Debug verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$"
  --output-on-failure`; `git diff --check -- .
  ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

Latest Verification Baseline Repair:

- Scope: baseline repair only after Package Tools phase 1; this is not Package
  Tools phase 2 and does not change product feature behavior beyond making the
  accepted build/test baseline reproducible.
- `relationship_test` was not skipped and no assertions were weakened. The
  rebuild failure was caused by generated MinGW compile/link rules not
  supplying the compiler bin on `PATH`, which allowed blank subcommand failures
  during object rebuilds. The top-level CMake file now injects the detected
  compiler bin into generated compile and link rule launchers.
- Package Tools coverage now verifies all six first-phase templates have
  non-empty text and slot metadata, verifies `typedef struct packed` appears in
  the packed struct template, and verifies the GUI exposes six stable package
  tool buttons with exactly six Package Tools buttons total while preserving
  one clicked insertion/Slot Mode path.
- Existing `;;cmd` template behavior is unchanged. Package/import semantics,
  cross-file package management, package-wide sorting, and new insight/graph
  surfaces remain out of Package Tools scope; Track 15 owns macro/define
  semantics.
- Release verification passed: `cmake --build . --target completion_test
  relationship_test gui_smoke_test`; `ctest -R
  "^(completion_test|relationship_test|gui_smoke_test)$" --output-on-failure`;
  `git diff --check -- . ':!test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv'`.

## Control-Side Acceptance Remediation: Editor Context Hot Path

Status: complete. This is a narrow P1 acceptance repair, not a new roadmap
track or feature goal.

- Added a failing production-path regression to
  `expose_signal_to_top_gui_test` before changing the cache architecture. It
  drives the real `MainWindow` cursor/document connections against 2,049
  workspace files and counts snapshot materialization, workspace-file
  normalization/sorting, hierarchy cache rebuilds, and chip property writes.
- Added authoritative monotonic `ProjectSnapshot::revision` publication in
  `ProjectModel`.
- Added one revision-guarded, deterministic-identity
  `EditorActionWorkspaceContext` cache to `EditorActionContextService`.
  Workspace root, top, and normalized file scope update from
  `projectChanged`; unchanged revisions return before inspecting `allFiles`.
- Replaced hierarchy keys derived from normalized/sorted/joined file lists
  with semantic revision plus cached workspace revision/identity and
  module/file identity.
- Changed `MainWindow` refresh queries to carry only editor/semantic dynamic
  state. `EditorCoordinator` uses the same service instance and lightweight
  query provider for right-click actions.
- Changed the status chip to per-field differential writes for compact/detail
  text, accessibility, semantic/binding properties, tone style sheet, and
  visibility.
- Before: 128 unchanged refreshes caused
  `128 snapshot / 128 normalize / 128 sort / 0 hierarchy rebuild / 896 chip
  writes`. After: `0 / 0 / 0 / 0 / 0`.
- Before: one project change plus 64 unchanged refreshes caused
  `65 snapshot / 66 normalize / 65 sort / 1 hierarchy rebuild / 455 chip
  writes`. After: one new revision plus a duplicate same-revision notification
  and 64 refreshes caused `0 / 1 / 1 / 1 / 0`.
- Release verification: affected tests 4/4, `editor_incremental_test` passed,
  `gui_smoke_test` passed, and full CTest passed 31/31 in 148.87 seconds.
- Protection audit: `current_app_signal_usage_hotspot_full_after.png`,
  `dist/`, `test_sv/huge_prj/.zs`, and `test_sv/new/.zs` remained
  byte-identical and untracked.

## 2026-08-02 Historical Partial Validation Record

- A-G product batches were implemented, but complete acceptance was not
  established.
- Historical Debug serial incremental build: 108/108 steps, 13,808.5 seconds;
  this establishes compile/link success only.
- A historical partial CTest run excluded six targets and passed the remaining
  71/71 in 161.43 seconds. This is not a complete 77-target acceptance result.
- Real-project checks passed for `test_sv/new` and `test_sv/huge_prj` through
  `large_file_perf_test`, `relationship_perf_test`, and
  `expose_signal_to_top_fixture_test`.
- GUI evidence is repeatable: `unified_peek_container_test`,
  `workspace_file_operation_test`, and `expose_signal_to_top_gui_test` verify
  embedded/nonmodal Peek behavior, safe default cancellation, live replan,
  explicit apply, and transaction integration.
- The historical run omitted or capped `gui_smoke_test`,
  `editor_incremental_test`, `editor_structural_input_test`,
  `editor_paste_transaction_test`, `global_control_ow_test`, and
  `editor_multicursor_controller_test`. Their source ranges, CMake conditions,
  and historical evidence are in `goal.md`. The former reproduction cap is
  superseded; that historical repair required registered CTest reruns.

### F Continuation: Reviewed File-Operation Revision

- `WorkspaceFileOperationPlan` now records a deterministic revision token
  containing normalized path identities, source/target snapshots, metadata,
  and a SHA-256 file-content digest.
- Action Registry dry-run returns that token, the embedded Peek flow carries
  the reviewed token into Apply, and the execution route rejects a mismatch
  before preparing open documents or mutating the filesystem.
- Direct service Apply also compares the plan it receives with a fresh plan,
  protecting the interval between route validation and filesystem mutation.
- `workspace_file_operation_test` covers a same-size content replacement with
  its original modification time restored, the dry-run token contract, and a
  rejected Action Registry Apply. A performance guard verifies that Rename and
  Delete capture full content revisions while Copy Full Path and Reveal avoid
  full-file hashing. The final stable run passed 38/38 checks in 0.38 seconds;
  its 9-step serial build passed in 728.1 seconds. Earlier link-only passes took
  337.9 and 376.2 seconds.

### A/D Continuation: Registry-Owned File Shortcuts

- Removed all legacy generated QAction declarations from `mainwindow.ui`.
  This eliminates the non-registry Ctrl+S binding and unused
  Cut/Copy/Paste/Undo/Redo/Font/Print/Esc shadow actions while retaining the
  underlying reusable file and editor functions.
- Added canonical `file.new`, `file.open`, `file.save`, `file.saveAs`, and
  `workspace.open` descriptors. Each owns its F24 command, Action Catalog
  metadata, Shortcut adapter, failure contract, and MainWindow execution
  route; user shortcut overrides now reach the actual application actions.
- MainWindow builds those five adapters from Registry metadata and dispatches
  them through `executeAction` before consuming `FileCommandCoordinator`.
- `action_registry_test` passed in 0.12 seconds after a 19.9-second serial
  build. `expose_signal_to_top_gui_test` independently verifies the real
  MainWindow object tree and New File route; it passed in 0.86 seconds after a
  34-step serial build completed in 527.3 seconds.

### D Continuation: Registry-Executed Context Formatting

- Retained context-menu Replace and formatter actions no longer call editor
  methods before Action Registry execution. When the production MainWindow
  host is present, the context adapter resolves its canonical descriptor and
  dispatches through `executeAction`.
- MainWindow now implements the editor Replace, line comment/uncomment,
  indent/unindent, formatter-profile, format-on-save, selection-format, and
  document-format routes with editor, read-only, and selection guards.
- Context QAction objects expose the canonical `actionId` and
  `executionRoute`. The GUI regression opens a real editor, invokes Comment
  Lines from its real context menu, verifies the application history, moves
  to a second line, and invokes Command Mode `repeat action`.
- `Create Signal Definition` and `Edit Instance Slots` now carry the exact
  right-click cursor through the same Registry handler. A successful
  structural route clears that transient parameter from the recorded
  invocation, so a later repeat resolves the active cursor instead of the old
  menu position.
- The GUI regression invokes Edit Instance Slots on a first instance while the
  text cursor is elsewhere, verifies Slot Mode and history, then repeats on a
  second instance and verifies its distinct actual is selected.
- F12/source-symbol signals and the four retained Insight context actions now
  use the same Registry handler. Their canonical routes execute through
  EditorCoordinator with explicit failure results, then discard the transient
  click cursor for current-symbol repeat semantics.
- The GUI regression drives the real Signal Kernel Graph context entry,
  verifies its Registry route and application history, and checks the visible
  panel title resolves `payload`.
- `refactor.exposeSignalToTop` now enters its canonical high-risk route. The
  route resolves the click cursor, consumes or asks for one hierarchy binding,
  stores the reviewed port/binding as resolved Action parameters, and applies
  only after the normal embedded Peek accepts a current plan.
- High-risk repeat is plan-only. The shared Expose Peek retains editable live
  replan but shows Close and no Apply; the route returns dry-run output without
  calling the transaction apply path.
- The signal-selection-only context menu now publishes
  `refactor.createAssignmentQueue` with its click cursor through a
  MyCodeEditor Registry request signal. MainWindow owns the execution route;
  the editor's direct implementation remains only as a reusable no-host
  fallback.
- The GUI regression selects two semantic signals, triggers the real popup
  action, verifies canonical metadata/history, and checks queue insertion plus
  linked Slot Mode.
- Comment/Uncomment and Indent/Unindent now declare Ctrl+/, Ctrl+Shift+/,
  Ctrl+], and Ctrl+[ in Action Registry. Editor key handling resolves the
  effective binding and publishes the same Registry request as other surfaces;
  only unattached standalone editors use the direct fallback.
- Latest serial verification passed: `action_registry_test` build 4/4 in 6.5
  seconds and test 54/54 in 0.10 seconds; GUI target build 8/8 in 497.0
  seconds and verbose GUI test 80/80 in 1.10 seconds. The only build warnings
  are the test's three pre-existing ignored `QFile::open` results.
- Ctrl+D and Ctrl+Shift+L occurrence selection now enter Action Registry
  execution and history in a hosted editor while preserving direct fallback
  for standalone editor tests. The GUI regression verifies that the first
  Ctrl+D selects the current structural identifier, the second enters
  MultiCursor, and scope selection enters MultiCursor immediately; it passed
  82/82 checks in 1.40 seconds after a 424.6-second relink.
- Assignment and conditional-branch navigation shortcuts plus Join Lines and
  Delete Lines now publish their canonical Actions. MainWindow owns the four
  navigation and two line-operation routes, including explicit no-editor,
  read-only, and structural-failure results. A real MainWindow GUI fixture
  verifies destination selections, document mutation, and history IDs for all
  six routes. The product build passed 7/7 in 552.2 seconds, the final
  test-only relink passed 3/3 in 369.1 seconds, and the GUI test passed 85/85
  checks in 1.24 seconds.
- Standard Undo/Redo/Cut/Copy/Paste/Select All shortcuts and Ctrl+F/G/H no
  longer bypass Registry execution. MainWindow owns their explicit routes;
  MyCodeEditor centralizes clipboard semantics so multi-cursor, column mode,
  whole-line copy/cut, and standalone-editor fallback retain their prior input
  ownership. Interactive Go to Line returns and records the selected line as
  resolved Action parameters. Standard context entries previously hidden by
  the A-batch menu policy remain hidden.
- `action_registry_test` passed in 0.13 seconds after a 4/4, 7.0-second build.
  The GUI dependency rebuild passed 51/51 in 791.9 seconds; its corrected
  test-only relink passed 3/3 in 538.9 seconds, and
  `expose_signal_to_top_gui_test` passed 89/89 checks in 1.35 seconds.
- The remaining editor-owned default shortcuts are Registry-resolved:
  Ctrl+Shift+I executes `format.document`, while definition navigation and RTL
  rename no longer hard-code F12/Ctrl+R. A standalone editor test overrides
  them to Ctrl+F12/Ctrl+Alt+R and proves old chords are inactive and new chords
  emit the semantic requests. The product/GUI build passed 8/8 in 771.9
  seconds, and the GUI test passed 91/91 checks in 1.18 seconds.
- Hosted multi-cursor and column-selection Ctrl+C paths now have explicit GUI
  coverage: each retains its controller-owned distributed/rectangular text and
  enters the common `edit.copy` execution history. The test-only relink passed
  3/3 in 375.4 seconds and the GUI test passed 93/93 checks in 1.47 seconds.
- Reusable clipboard, line-operation, scoped-occurrence, and key-release
  execution moved from the 5246-line runtime shell into
  `editorruntimecommands.cpp`. `editorruntime.cpp` is now 4995 lines;
  `controller_boundary_test` asserts both size and ownership and passed in
  0.10 seconds. The serial GUI rebuild passed 6/6 in 536.7 seconds and its
  93/93 behavior regression passed in 1.28 seconds.
- Navigation Ctrl+1 now belongs to `view.navigation.toggle`; the direct
  `QShortcut` was removed. The Registry test passed in 0.13 seconds, the GUI
  build passed 8/8 in 524.5 seconds, and the real dock/history regression
  passed 94/94 checks in 1.34 seconds.
- Structural selection expansion and next/previous selected-symbol occurrence
  navigation now expose canonical Ctrl+W/E/Q Actions and Command Mode aliases.
  Their algorithms live in `EditorSelection`; `editorruntime.cpp` is 4814
  lines. Registry and boundary tests passed in 0.12 and 0.11 seconds. The
  serial GUI product build passed 67/67 in 515.6 seconds, the corrected
  test-only relink passed 3/3 in 381.4 seconds, and the final GUI run passed
  98/98 checks in 1.31 seconds.
- Alt+Up/Down line movement now executes Registry-owned
  `edit.moveLinesUp/Down` Actions and is implemented by
  `EditorLineOperationController`, including single undo, touched-line
  selection boundaries, cursor preservation, and column-mode exclusion. The
  runtime shell is 4635 lines. Line-operation, Registry, and boundary tests
  passed 41/41, 58/58, and 1/1 in 0.13, 0.13, and 0.12 seconds. The serial GUI
  build completed 67 executed steps in 539.3 seconds and the shortcut/Command
  Mode regression passed 100/100 checks in 1.43 seconds.
- Ctrl+Space Global Control invocation now resolves the effective shortcut of
  the non-menu `view.globalControl` Action and enters MainWindow execution
  history while retaining a standalone coordinator fallback. The Registry
  test passed 59/59 in 0.13 seconds. The serial GUI rebuild completed 36
  executed steps in 503.2 seconds; an override regression proves old-key
  deactivation and new-key activation in a 101/101, 1.40-second GUI run.
- Command Mode hold entry now resolves the single-stroke shortcut of the
  non-menu, non-repeatable `view.commandMode` Action. Physical F24 branching
  is removed; invalid multi-stroke overrides are rejected and the panel/help
  status reflects the active binding. Registry and boundary tests passed
  61/61 and 1/1 in 0.13 and 0.11 seconds. After one diagnostic-free compiler
  exit under parallel build, explicit `--parallel 1` completed 5/5 in 549.9
  seconds; the GUI test passed 102/102 in 1.47 seconds. A separate serial
  `completion_test` build passed 2/2 in 436.3 seconds and ran successfully in
  2.44 seconds.
- Column Number Tool entry now resolves the Registry-owned
  `insert.columnNumbers` shortcut rather than testing physical Alt+C.
  Command Mode, shortcut overrides, and the Action Catalog share
  `editor.columnNumbers.show`; the interactive Action remains non-repeatable.
  Registry and boundary tests passed 1/1 in 0.12 and 0.10 seconds. The
  explicit single-thread GUI rebuild completed 36 executed steps in 675.5
  seconds, and its old/new chord plus panel-identity regression passed within
  102/102 checks in 1.43 seconds.
- Insight Focus Escape Registry migration was isolated but not retained. Three
  offscreen GUI runs consistently showed successful Focus View entry plus an
  enabled QAction with the effective Escape sequence, but no QAction
  activation or `view.insightFocus.leave` history; explicit page focus did not
  change the result, and the subsequent assignment-queue failure was only a
  consequence of remaining in Focus View. The attempted product/test changes
  were removed and the previously verified local Escape path restored.
  That historical evidence did not establish current acceptance. The former
  run cap is superseded, and the target remains part of required regression
  validation.
- Fold Shelf Delete now resolves `fold.shelf.deleteSelected` from Action
  Registry and executes the same `ui.foldShelf.deleteSelected` route from its
  list shortcut or Command Mode. MainWindow owns mutation; the reusable panel
  fallback preserves standalone behavior and moved-block safeguards. Registry
  and boundary tests passed in 0.12/0.10 seconds. A 38-step explicit
  single-thread rebuild completed in 649.5 seconds, and the focused panel GUI
  test passed 18/18 in 0.19 seconds with old Delete deactivated and remapped
  Ctrl+Delete deleting the copied fixture.
- Tab right-click commands now resolve a dedicated 12-item
  `TabContextMenu` surface. The split controller builds labels, Action ids,
  routes, separators, enablement, and dynamic Lock/Unlock state from Registry
  metadata; TabManager supplies clicked-group context and MainWindow executes
  the same routes used by menu and Command Mode. During the focused
  shared-document test, copied view state exposed duplicate `viewId` reuse;
  `SharedDocument::attachView` now regenerates an identity already attached to
  that Document. This prevents view-lock aliasing and keeps split cursor,
  scroll, and session layout identities independent. Registry, boundary,
  split-controller, and final shared-document tests passed in 0.10, 0.08,
  0.15, and 0.69 seconds; the shared test's first 66/69 run was the minimal
  reproduction that led to the source fix, and the second run passed all 70
  checks. Final affected serial builds completed in 27.97, 2.56, 655.35, and
  555.10 seconds. No capped GUI target was executed.
- Design-hierarchy right-click navigation now resolves three Registry Actions
  instead of hand-authored QAction labels and callbacks. Instantiation and
  definition routes preserve workspace/top/instance-path context, while both
  design-tree and file-tree Set Design Top entries execute
  `navigation.design.setTop`; multi-module file choices remain runtime
  parameters under the same descriptor. Navigation also synchronizes when
  attached to an already-open WorkspaceManager. Registry and boundary tests
  passed in 0.10/0.08 seconds. The explicit serial product/test build completed
  39 steps in 744.12 seconds, and `workspace_file_operation_test` passed in
  0.43 seconds with the prior analyze/preview/revision-check cases intact. No
  capped GUI target was executed.
- Bottom-page Pin/Unpin and Close now resolve a dedicated Panel context
  surface. The context model supplies canonical labels/routes and dynamic
  Unpin/disabled-Close state; the selected `panelId` is passed to MainWindow
  through ActionInvocation, so context actions do not depend on the previously
  active page. Command Mode shares both descriptors, and context mutations are
  excluded from Repeat Last Action. Registry and boundary tests passed in
  0.11/0.08 seconds. The initial GUI run's three height failures were caused
  by the new fixture leaving Problems active; restoring the prior Activity tab
  yielded 39/39 in the second minimal run at 0.11 seconds. The final explicit
  serial core build passed 37/37 steps in 203.11 seconds. No capped target was
  executed.

### G Continuation: Registry-Owned RTL Insights Graph Actions

- Added `graph.selection.jump`, `graph.selection.focus`, and
  `graph.moduleBlock.setTop` as non-repeatable GraphPanel and Action Catalog
  descriptors with graph-content requirements and explicit unavailable
  reasons.
- The RTL Insights More menu, Inspector buttons, and module-block Set Top
  button now materialize labels and route metadata from those descriptors and
  enter one `executeAction` path. The scene mapper synchronizes Jump, Focus,
  and Set Top availability for no selection, generic graph selection, resolved
  module selection, and unsupported FSM Set Top state.
- Graph Action execution moved to `rtlinsightspanelactions.cpp`; the existing
  coordinator stays below its 900-line boundary at 834 lines. The five export
  Actions remain independent and retain their existing export host.
- `action_registry_test` passed in 0.09 seconds after a 23.4-second serial
  build; `controller_boundary_test` passed in 0.07 seconds after 2.7 seconds.
  The offscreen `rtl_insight_linkage_test` passed in 0.31 seconds after a
  564.9-second core/test build, covering actual QAction identity, Jump source
  context, Focus success, and FSM Set Top rejection. `relationship_test`
  passed in 15.92 seconds after a 368.6-second build, proving Set Top rebuilds
  `rel_top.u_stage` as a one-node `rel_stage` graph and that the parent fixture
  can be restored. `graph_export_panel_integration_test` passed in 0.37
  seconds; its first build reached a 300.3-second tool limit during compilation
  and the extended serial continuation linked successfully in 354.5 seconds.
  No capped test was executed.

### G Continuation: Registry-Owned Usage Hotspot View Actions

- Added non-repeatable GraphPanel/Action Catalog descriptors for Fit, Zoom In,
  Zoom Out, Center Current, and Reset Layout. The Signal Usage Hotspot Track
  context menu, bound panel controls, and Focus View adapters now request the
  same action ids and execute through `signalusagehotspotpanelactions.cpp`.
- The panel propagates graph-content availability to each QAction/button and
  enables Center Current only for a matching current source line or selected
  usage. Fit/Zoom/Center/Reset return explicit success so failure status no
  longer depends on silent void callbacks.
- Registry and boundary tests passed in 0.09/0.07 seconds after 23.7/2.9-second
  serial builds. The affected panel/core target compiled 46 steps in 559.0
  seconds. `signal_usage_hotspot_panel_test` reached its three-run cap after
  two silent pre-test exits and a GDB-confirmed Windows `0xc0000135` startup
  failure before `main`; its DLL table matches passing GUI targets.
- Independent `graph_export_panel_integration_test` runs passed 33/34, 38/39,
  and 38/39 checks. All export and graph-view routes except a hidden panel
  button test passed. The failure was narrowed to calling `click()` on a
  deliberately hidden control; product Focus View calls `focusZoomIn()`.
  The future assertion now exercises that real adapter, but the target was not
  run beyond its third attempt. Serial rebuilds took 348.6, 345.8, and 433.5
  seconds. Both targets are capped until new evidence exists.

### G Continuation: Registry-Owned Focus View Actions

- Focus View Fit, Zoom Out, and Zoom In now use the existing non-repeatable
  GraphPanel descriptors. The Focus toolbar derives labels, tooltips, action
  ids, routes, and availability from `ActionRegistry`, while the active panel
  registration retains the concrete view callback.
- Registry execution is isolated in `insightfocuscontrolleractions.cpp`; the
  lifecycle shell no longer invokes Fit/Zoom callbacks directly. Clearing or
  restoring Focus state synchronizes Action and button availability.
- `insight_focus_controller_test` passed 1/1 in 0.13 seconds after a 14.2-
  second serial build, covering metadata, one-shot callback dispatch, disabled
  state, and repeat-history exclusion. `controller_boundary_test` passed 1/1
  in 0.10 seconds after a 2.9-second serial build. No capped target was run.

### Historical A-G Build And Partial Test Record (2026-08-03)

- All Debug targets compile and link under explicit `--parallel 1`. The first
  all-target pass found a stale one-argument callback in
  `test_sv/gui_smoke_test.cpp`; the fixture now accepts and checks the product
  Action parameter map. The resumed final segment linked the last 10/10
  targets in 3333 seconds. No capped test was executed during this repair.
- CTest listed 77 targets. A historical partial serial run excluded nine
  targets and passed the remaining 68/68 in 199.58 seconds. This does not prove
  complete acceptance. That partial run exercised A layout, B shared
  documents/splits/persistence,
  C mode/line/slot editing, D Registry/completion/package commands, E
  annotation/Peek/formatter/value behavior, F semantic proposals/search/file
  and RTL edit workflows, and G export/focus/linkage/visible-region/
  incremental-analysis behavior.
- Real-project timings in that run were 7.96 seconds for `test_sv/new`, 93.66
  seconds for `test_sv/huge_prj`, and 4.95 seconds for the fixture test that
  checks both. Both protected `.zs` files retain their baseline length,
  timestamp, and SHA-256 digest.
- `fsm_ui_snapshot_test` regenerated seven nonblank PNGs under
  `artifacts/ui/fsm`; `fsm_selected_transition.png` and the retained Usage
  Hotspot Track/Matrix screenshot were visually inspected. Final
  `git diff --check` passed with line-ending advisories only; no temporary
  log/program entry or residual build/test process was found.
- The historical record proves compile/link for all 77 targets and partial
  execution coverage only. It does not describe the current theme/drawer
  verification. Current status is **控制侧已独立验收通过**;
  the final Shared-Debug unfiltered serial CTest passed 82/82 with zero failures
  in 380.30 seconds wall-clock.

## Commit Policy

- Keep commits coherent and architecture-oriented.
- Do not mix unrelated dirty code into documentation commits.
- Do not push unless explicitly asked by the current task owner.
- Prefer short commit messages that name the product/architecture change.
