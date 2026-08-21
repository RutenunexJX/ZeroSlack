# ZeroSlack Current Plan

Product version: `v0.17.3`

## Current status

- ASCII numeric peeks expose every radix row, including hexadecimal output.
- Column-mode keyword completion uses the same structural keyword source as
  ordinary editing, and active rectangular selections use a light-pink fill.
- Formatter declaration alignment covers typedef-typed variables and enum
  values containing concatenations. Format Document is the sole user-facing
  formatter action; generated RTL transactions retain an internal snippet
  formatter without exposing a second editor workflow.
- Rename resolves symbols at punctuation boundaries, and declaration
  organization ignores preprocessing barriers that occur only after the final
  declaration being moved.

- Workspace activation restores the cached file list immediately and always
  follows it with an asynchronous directory reconciliation. A changed file set
  is published once; an unchanged set does not retrigger semantic analysis.

- The editor context menu can replace ordinary multiline or rectangular column
  selections with equal-width whitespace in one undoable transaction.
- A Tree-sitter-authored organization plan moves complete top-level net and
  variable declarations to the current module's safe declaration section. It
  preserves dependency preambles, declaration order, attached comments, and
  procedural locals, treats module-level macro definitions as preamble items,
  and rejects unsafe syntax or preprocessor boundaries.

- The Windows package includes a pinned `WaveWorkbench/toolchain` containing
  Verilator 5.050, MinGW 13.1, GNU Make, runtime DLLs, licenses, and a manifest.
  Package, staging, object, and default release destination names contain no
  spaces. Explicit Settings Center paths still override portable discovery.
- ZeroSlack passes the resolved compiler, Verilator root, make program, and
  portable `PATH` to the runner. The native launcher adds context-time support
  for WaveWorkbench's `VerilatedContext` harness and preserves quoted source
  arguments. Missing tools continue to be diagnosed independently.

- Module Manifest v5 adds portable declaration/driver links, source columns,
  and stable semantic identities for ports and internal observations while
  preserving WaveWorkbench v1-v4 readers.
- The embedded result exposes Declaration and Drivers navigation for mapped
  Actual signals. The editor Action can reveal a semantic signal in an open
  result from the same workspace; ambiguous, unmapped, and cross-workspace
  requests fail explicitly.
- ZeroSlack validates `result-source-navigation/v1` and the required Qt
  meta-object methods/signal without changing the `wavewidgets` v1 C ABI or
  workspace contract.

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
- WaveWorkbench passes all `96/96` configured tests against the integration and
  portable-install contracts. ZeroSlack passes `90/90`, including Manifest v5,
  the embedded workspace contract, and the existing visible-Wave latency gate.
- A cross-repository test dynamically loads the real WaveWorkbench shared
  library and validates explicit stubs, multi-scenario batch controls, and the
  result/source navigation contract without changing the v1 C ABI or workspace
  contract.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
