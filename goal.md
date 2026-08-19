# ZeroSlack Current Goal

Product version: `v0.12.0`

## Objective

Complete S12.6 by adding Wellen-backed FST metadata indexing and on-demand
transition loading, while retaining VCD/CSV behavior, the S11 shared-widget
architecture, and prior S12 behavior.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.12.0.
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
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

S12.6 implementation and feature-specific dual-repository verification are complete. Wellen
indexes FST metadata without transition decoding; mapped and checked signals
load on demand with cancellation and stale-generation protection. The shared
library capability is conditional on the adjacent helper deployment, and the
portable installation includes the complete closure. This machine does not
have a real Verilator installation, so the external compile/run path remains
verified with deterministic process fixtures rather than represented as a real
RTL compile. WaveWorkbench passes `93/93` offscreen tests. The latest complete
ZeroSlack Debug run passes `89/90`; only the existing visible-Wave latency gate
in `editor_incremental_test` remains open (repeated p95 `6.36-8.74 ms` against
`6 ms`), while all correctness, incremental, and zero-full-copy assertions pass.
