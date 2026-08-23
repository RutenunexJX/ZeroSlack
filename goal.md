# ZeroSlack Current Goal

Product version: `v0.20.0`

## Active initiative

Complete the provider-based Context Workspace described in
[`CONTEXT_WORKSPACE_PLAN.md`](CONTEXT_WORKSPACE_PLAN.md). The host foundation,
temporary-editor migration, portable session restoration, theme integration,
legacy-overlay cleanup, and Pinloom integration are complete. Wave, FSM, and
diagram content can enter through providers rather than introducing new window
management code.

Verification status: ZeroSlack passes all `92/92` configured tests. Pinloom
passes all `6/6` configured tests, and the real cross-process host bridge has
been verified for capabilities, search, stable-identity resolution, and source-
anchor creation.

## Objective

Replace the temporary editor's bespoke overlay with one reusable Context
Workspace while retaining the portable Wave Simulation baseline, editor
semantics, and single SystemVerilog syntax/semantic fact sources.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.20.0.
- Pinloom search, resolve, open, and source-anchor creation use
  `pinloom-host/v1`; ZeroSlack stores portable code-range-to-URI links without
  copying authoritative Pinloom content.
- Context resources open through a registered provider into one transient Peek
  or the native tabbed Pinned Dock; Pin and Unpin move the exact live view.
- Temporary editing shares `TabManager` document identity, text, undo state,
  search catalog, history, folding, save, and workspace file operations.
- Pinned resource order, active tab, widths, visibility, and provider state are
  portable across workspace relocation; missing providers fail independently.
- No legacy drawer geometry, edge handle, preview overlay, compatibility
  controller, application-wide popup observer, or obsolete test target remains.
- ASCII numeric peeks display every radix row, including hexadecimal output.
- Column-mode Tab completion expands canonical SystemVerilog keywords across
  the selected rows in one undoable edit, and its selection is visibly pink.
- Format Document aligns typedef-typed declarations and enum concatenations;
  no user-facing Format Selection route, action, command, or editor API remains.
- Structured is the only formatter policy; no Indent-Only profile, formatter
  profile setting, or editor context-menu Format section remains.
- Rename accepts a caret on either insertion boundary of a semantic symbol,
  including punctuation-adjacent boundaries.
- A successful rename refreshes semantic facts from the exact edited document
  revisions, so a second `Ctrl+R` operation works without saving first.
- The default `Ctrl+Space` Symbols view searches all insertable symbols visible
  in the current scope, including enum values, without exposing another
  module's internal signals; selectors remain optional narrowing filters.
- Signal organization remains available when a preprocessing directive occurs
  only after the final late declaration, while unsafe crossed barriers still
  fail closed.
- Cached workspace activation remains immediate while a background directory
  reconciliation discovers files changed outside ZeroSlack.
- Ordinary multiline and rectangular column selections can be replaced with
  equal-width whitespace while retaining their line structure and one-step undo.
- Signal organization is derived from the current Tree-sitter module, moves only
  complete top-level net and variable declarations, preserves dependencies and
  attached comments and module-level macro definitions, excludes procedural
  locals, and fails closed at unsafe syntax or preprocessing boundaries.
- Users can select Verilator and C++ compiler executables in global Settings,
  while empty fields retain deterministic automatic discovery.
- Portable adjacent tools are preferred over ambient `PATH`, resolved paths are
  passed explicitly to the runner, and failures identify each missing tool.
- The formal Windows package contains Verilator 5.050, MinGW 13.1, GNU Make,
  required runtime DLLs, and licenses in one stable ZIP with a SHA-256 manifest.
- The first simulation prepares the bundle outside the UI thread in a
  content-addressed local cache; later runs reuse it, and legacy expanded
  toolchain layouts retain their existing priority and behavior.
- A clean target computer can compile and run a real RTL fixture without a
  system compiler installation or global environment changes.
- `wavewidgets` provides a versioned runtime factory for the complete simulation
  workspace; ZeroSlack validates its ABI and workspace contract before hosting.
- The embedded and standalone forms consume the same project, stimulus canvas,
  trace canvas, timeline mapping, and scenario/result semantics.
- Wave results use generic tool-tab activation and close behavior without
  contaminating source-editor document queries.
- The portable WaveWorkbench component contains the application, shared widget
  library, CLI tools, contracts, schemas, and examples.
- VCD scope components remain exact, the Actual hierarchy supports search and
  scope/leaf visibility control, and initial visibility follows trace mapping.
- Verilator tracing covers deep hierarchy, structs, and underscore-prefixed
  signals without weakening build-cache invalidation.
- Multiple semantic clock candidates remain independent, each clock can be
  edited from the result toolbar, and asynchronous events retain exact ticks.
- Clock and event edits reuse the compiled model rather than changing its cache
  identity.
- Module Manifest v3 carries portable Slang selectors and structured leaf facts;
  Stimulus Scenario v3 restores those bindings without reparsing source text.
- The runner wrapper reconstructs supported struct, array, and interface roots,
  and rejects unsupported shapes without unsafe flattening.
- Stimulus Scenario v4 stores expected ranges only for watch lanes, and the
  runtime plan never drives those values into the DUT.
- The embedded workspace compares only outputs with expectations, marks both
  waveforms, lists differences, and navigates expected and actual locations.
- ZeroSlack requires `expected-actual-compare/v1` from the real shared workspace
  without changing the v1 C ABI.
- Stimulus Scenario v5 stores lightweight check definitions, while results are
  derived only from the current trace and invalidated with scenario or trace changes.
- The embedded workspace exposes `lightweight-trace-checks/v1`, a run action,
  result table, failure localization, and signal/tick navigation.
- FST metadata loading does not decode transitions; mapped or selected signals
  are loaded in bounded batches and merged only when file identity and
  generation still match.
- The real shared workspace advertises `on-demand-fst-trace/v1` only when the
  adjacent Wellen reader exists, and its portable package contains the helper
  plus attribution and operating limits.
- Module Manifest v4 records unresolved instances and ordered associations only
  inside the selected target dependency closure while retaining v1-v3 readers.
- Unresolved modules fail before toolchain execution unless the user explicitly
  selects a supported passive input-only stub; unsupported constructs remain
  disabled with a reason.
- Stub selection persists in simulation session v2 and contributes to generated
  build inputs, cache identity, and run evidence.
- The shared workspace advertises `explicit-unresolved-module-stubs/v1` and
  exposes the `Stubs` selector without changing its v1 C ABI.
- Every stored scenario can run sequentially through the existing runner;
  individual failures continue, cancellation stops the active item and marks
  the remaining queue cancelled, and all items share the compiled-model cache.
- The shared Batch review exposes per-scenario state, diagnostics, cache
  provenance, duration, and successful waveform selection without persisting
  derived batch state or changing the scenario/session schemas.
- ZeroSlack requires `multi-scenario-batch-run/v1`, the `Run all` action, and
  the batch result table from the real shared workspace without changing the
  v1 C ABI.
- Module Manifest v5 carries portable declaration/driver links and stable
  semantic identities without replacing Slang as the relationship fact source.
- The shared result can navigate a mapped Actual signal to its declaration or
  drivers, and ZeroSlack can reveal a source signal in an open result belonging
  to the same workspace. Ambiguous or unmapped matches are rejected.
- ZeroSlack requires `result-source-navigation/v1` and validates the bidirectional
  Qt meta-object contract without changing the v1 C ABI.
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

S13.1 is complete. The Windows package can keep its pinned native Verilator
5.050 and MinGW 13.1 toolchain as one stable ZIP beside the application. The
first simulation verifies size and SHA-256, rejects unsafe archive paths, and
extracts into a content-addressed local cache on a worker thread. Ready caches
are reused without hashing or extraction. ZeroSlack then propagates `PATH`,
`VERILATOR_ROOT`, and `MAKE` exactly as for the legacy expanded layout.
