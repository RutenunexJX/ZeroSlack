# ZeroSlack Current Plan

Product version: `v0.9.0`

## Current status

- Module Manifest v3 exports Slang-authored selectors for packed struct,
  fixed unpacked array, and explicit-modport interface inputs. Type and symbol
  identities are portable workspace-relative digests and do not expose an
  absolute workspace path.
- WaveWorkbench imports each structured root as a grouped set of editable
  leaves, Stimulus Scenario v3 persists those bindings, and the generated
  runner wrapper reconstructs the original SystemVerilog port shape.
- Unsafe or unsupported structured shapes fail explicitly instead of falling
  back to total-width flattening. The current interface subset requires an
  explicit modport and no interface constructor ports.

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
- Actual results expose the VCD instance hierarchy in a searchable checkbox
  tree. The initial visible set follows the scenario trace mapping; internal
  signals remain discoverable and can be added or removed without changing the
  stimulus model.
- WaveWorkbench preserves exact VCD scope components, traces deep hierarchy,
  structs, and underscore-prefixed signals, and advertises the compatible
  `internal-signal-hierarchy/v1` capability without changing the v1 C ABI.
- Multiple semantic clock candidates are imported as independent domains.
  The shared result toolbar exposes per-clock period, phase, duty-cycle, and
  edge editing together with exact 1-tick asynchronous stimulus placement.
- Stimulus-only clock or event changes reuse the compiled model. Runtime tests
  cover unequal clock periods/phases and a non-grid input transition without
  changing the build-cache fingerprint.
- ZeroSlack validates the optional `multi-clock-async-events/v1` capability and
  the corresponding shared-widget controls before accepting the real workspace.
- The complete configured Debug suite passes all 90 tests. WaveWorkbench passes
  all 91 tests against the same integration and portable-install contracts.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
