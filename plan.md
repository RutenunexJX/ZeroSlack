# ZeroSlack Current Plan

Product version: `v0.14.0`

## Current status

- Module Manifest v4 exports unresolved module instances only from the
  selected target's dependency closure, including portable source positions
  and ordered parameter/port associations.
- WaveWorkbench refuses unresolved modules by default. Its shared `Stubs`
  menu persists explicit supported selections and generates passive input-only
  declarations that enter the build fingerprint and run report.
- ZeroSlack validates `explicit-unresolved-module-stubs/v1` and the shared
  selector without changing the wavewidgets v1 C ABI.
- The shared `Run all (N)` action executes stored scenarios sequentially through
  one runner, continues after individual failures, cancels the active and
  pending queue through Stop, and reuses the compiled-model cache.
- Review/Batch exposes per-scenario state, diagnostics, cache provenance,
  duration, and successful Actual waveforms. Batch state is runtime-derived and
  does not change the Stimulus Scenario or simulation session schemas.
- ZeroSlack validates `multi-scenario-batch-run/v1`, the run-all action, and the
  batch result table from the real shared library while retaining the v1 C ABI.

- Stimulus Scenario v5 persists output expectations in watch-only
  `expectedSegments`; the runtime plan continues to contain DUT stimulus only.
- Embedded Simulation Result provides X handling, edge tolerance, a comparison
  summary, mismatch table, synchronized expected/actual highlights, and
  difference navigation.
- ZeroSlack validates `expected-actual-compare/v1` and the comparison controls
  when loading the real shared WaveWorkbench workspace; the C ABI remains v1.
- Stimulus Scenario v5 additionally persists definitions for value-at-time,
  stable-range, and edge-response checks. Results remain derived from the
  current Actual trace and are never stored.
- The shared Review/Checks page creates, edits, removes, runs, and navigates
  lightweight checks. ZeroSlack requires `lightweight-trace-checks/v1`, the
  run action, and the result table from the real shared library.
- WaveWorkbench indexes FST hierarchy and signal metadata through Wellen, then
  decodes transitions only for mapped or user-selected stable signal IDs.
  Cancellation, file identity, generation, batch, and response-size guards
  prevent stale or oversized loads from replacing the current trace.
- The optional `on-demand-fst-trace/v1` capability is accepted only when the
  Wellen reader is deployed beside the real shared library. VCD/CSV behavior
  remains available when the optional helper is absent.

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
- WaveWorkbench passes all `95/95` configured tests against the integration and
  portable-install contracts. ZeroSlack passes `90/90`, including Manifest v4,
  the embedded workspace contract, and the existing visible-Wave latency gate.
- A cross-repository test dynamically loads the real WaveWorkbench shared
  library and validates explicit stubs plus multi-scenario batch controls
  without changing the v1 C ABI or workspace contract.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
