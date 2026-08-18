# ZeroSlack Current Plan

Product version: `v0.6.0`

## Current status

- Wave Simulation is a formal Action Registry workflow and opens its complete
  WaveWorkbench result workspace as a first-class editor-area tool tab.
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
- `wavewidgets` exposes a versioned C ABI factory while the standalone
  WaveWorkbench application consumes the same shared canvases and project
  contract. Stimulus and result canvases share one pure timeline mapping.
- Generic tool tabs participate in split activation and close lifecycle without
  being treated as source editors. The standalone application is optional for
  normal embedded operation and remains available as an explicit fallback.
- The complete configured Debug suite passes all 90 tests. WaveWorkbench passes
  all 90 tests against the same integration and portable-install contracts.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
