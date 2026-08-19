# ZeroSlack Current Goal

Product version: `v0.10.0`

## Objective

Complete S12.4 by making expected output waveforms a persisted, non-driving
part of the embedded WaveWorkbench scenario and comparing them with the current
simulation trace, while retaining the S11 shared-widget architecture and prior
S12 behavior.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.10.0.
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
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

S12.4 implementation and dual-repository verification are complete. Watch-lane
expectations round-trip through Stimulus Scenario v4, remain outside the DUT
runtime plan, and are compared in the shared Simulation Result workspace with
visible mismatch and navigation evidence. This machine does not have a real
Verilator installation, so the external compile/run path is verified with
deterministic process fixtures rather than represented as a real RTL compile.
ZeroSlack passes `90/90` configured Debug tests and WaveWorkbench passes `92/92`
offscreen tests.
