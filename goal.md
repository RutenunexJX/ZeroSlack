# ZeroSlack Current Goal

Product version: `v0.5.0`

## Objective

Complete Wave Simulation slice S10 by making the existing external simulation
pipeline directly usable from normal ZeroSlack editing and Design workflows.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.5.0.
- Current module, selected `always`, selected signal, and exact Design instance
  entries preserve their semantic context through preparation and execution.
- Unsaved buffers are compiled as one coherent workspace snapshot.
- Module Manifest v2 remains portable and WaveWorkbench retains v1 import
  compatibility.
- Build/run failures carry source file, line, and column back to ZeroSlack.
- Both repositories pass their complete configured suites without relaxed
  assertions.

## Current status

Implementation and verification are complete. ZeroSlack passes 89/89 tests and
WaveWorkbench passes 88/88 tests. Formal entries, selected-`always` observation
scope, explicit source observations, exact instance targeting, scenario/cache
reuse, and structured source diagnostics are covered. This machine does not
have a real Verilator installation, so the external process path is verified
with the deterministic runner fixture rather than represented as a real RTL
compile. Shared-widget embedding remains S11 and is not part of this goal.
