# ZeroSlack Long-Term Goal Model

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
