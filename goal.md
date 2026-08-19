# ZeroSlack Current Goal

Product version: `v0.8.0`

## Objective

Complete S12.2 by supporting independent semantic clock domains and exact
asynchronous stimulus events in the embedded WaveWorkbench result workspace,
while retaining the S11 shared-widget architecture and S12.1 hierarchy browser.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.8.0.
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
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

Implementation and verification are complete. WaveWorkbench and ZeroSlack each
pass 90/90 tests. A real cross-repository runtime check dynamically loads the
WaveWorkbench shared library, validates hierarchy and multi-clock/async-event
capabilities, opens the handshake result workspace, and completes the tool-tab
close lifecycle. The native result-window screenshot shows the clock-domain and
Timing controls above the shared Stimulus/Actual views. This machine does not
have a real Verilator installation, so the external compile/run path remains
verified with deterministic process fixtures rather than represented as a real
RTL compile.
