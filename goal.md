# ZeroSlack Current Goal

Product version: `v0.6.0`

## Objective

Complete Wave Simulation slice S11 by embedding the full WaveWorkbench result
workspace in a first-class ZeroSlack editor-area Wave tab while retaining the
standalone application.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.6.0.
- `wavewidgets` provides a versioned runtime factory for the complete simulation
  workspace; ZeroSlack validates its ABI and workspace contract before hosting.
- The embedded and standalone forms consume the same project, stimulus canvas,
  trace canvas, timeline mapping, and scenario/result semantics.
- Wave results use generic tool-tab activation and close behavior without
  contaminating source-editor document queries.
- The portable WaveWorkbench component contains the application, shared widget
  library, CLI tools, contracts, schemas, and examples.
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

Implementation and verification are complete. ZeroSlack passes 90/90 tests and
WaveWorkbench passes 90/90 tests. A real cross-repository runtime check loads
`wavewidgets` dynamically, opens the handshake result project, verifies both
shared canvases, hosts the workspace in a generic tool tab, and captures the
embedded interface. This machine does not have a real Verilator installation,
so the external compile/run path remains verified with deterministic process
fixtures rather than represented as a real RTL compile.
