# ZeroSlack Current Plan

Product version: `v0.5.0`

## Current status

- Wave Simulation is a formal Action Registry workflow rather than a hidden
  experimental menu entry.
- The current module, an enclosing `always` block, an editor-selected signal,
  and an exact Design hierarchy instance can start a run without manually
  opening WaveWorkbench or handling interchange files.
- An `always` selection narrows only the initial visible observation set; the
  complete module and its unsaved workspace source snapshot remain the compile
  target.
- Module Manifest v2 carries portable observation scope and semantic signal
  metadata. WaveWorkbench continues to accept v1 manifests and imports v2
  internal observations as watch lanes.
- Build and run diagnostics use structured source locations and expose a
  direct source-navigation action in ZeroSlack.
- The MinGW shared-core export generator preserves valid symbols emitted after
  `dlltool` ordinal wraparound, with an explicit ABI policy guard.
- The complete configured Debug suite passes all 89 tests. WaveWorkbench passes
  all 88 tests against the same integration contract.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
