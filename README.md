# ZeroSlack

Current version: `v0.21.0`

ZeroSlack is a Qt 6 desktop environment for navigating, understanding, and
editing SystemVerilog workspaces. It combines an incremental editor syntax
model with workspace-wide semantic analysis, relationship views, diagnostics,
and preview-first RTL editing workflows.

## Current user capabilities

- Open, switch, close, rename, and revisit multiple workspaces; configure
  include directories, defines, ignored directories, source extensions, and
  the active top module.
- Edit SystemVerilog with incremental highlighting, folding, structural
  navigation, formatter support, multi-cursor and column operations, split
  views, templates, and an explicit `Ctrl+Space` palette for scoped symbols,
  templates, and application commands. The palette opens at the active caret
  and supports horizontal category switching without leaving its filter.
  Triple-clicking selects a complete logical line, including its line break
  when one exists.
- Open contextual content through the right-side Context Rail. Temporary source
  editing appears in one transient Peek and can be pinned into the native
  tabbed Context Dock without losing the live editor, undo state, search
  history, or workspace-relative restore identity.
- Query Slang-backed symbols, diagnostics, definitions, references,
  relationships, hierarchy, hover information, and effective compile-time
  values from the current workspace snapshot.
- Inspect RTL through Problems, Design, RTL Insights, state-transition and FSM
  views, module block diagrams, signal journeys, signal-kernel graphs, usage
  hotspots, and symbolic wave previews.
- Run the formal Wave Simulation workflow from the current module, an enclosing
  `always` block, a selected source signal, or an exact Design instance. The
  run captures unsaved workspace buffers, restores saved scenarios, reuses the
  compiled model when only stimulus changes, and opens the complete shared
  WaveWorkbench workspace in an editor-area Wave tab. Structured build/run
  diagnostics still map back to source; the standalone application remains an
  explicit recovery path when the shared widget cannot be loaded. The Actual
  result area includes a searchable instance/signal hierarchy with scope and
  leaf checkboxes, so internal signals can be added without predeclaring every
  observation. Multiple semantic clock candidates become independent clock
  domains; the result toolbar edits each domain and switches between its clock
  grid and exact 1-tick asynchronous input events. Packed structs, fixed
  unpacked arrays, and explicit-modport interface inputs are edited as grouped
  Slang semantic leaves; the runner wrapper reconstructs the original ports,
  and structured scenarios survive safe root-port renames. Output watch lanes
  can carry independent expected ranges; Compare checks only those outputs
  against the current trace, highlights mismatches in both waveforms, and
  navigates each difference without driving expected values into the DUT.
  Review also supports persisted value-at-time, stable-range, and edge-response
  check definitions. Their results are derived only from the current Actual
  trace, become stale with scenario or trace changes, and navigate to the
  involved signal and tick without becoming a second waveform fact source.
  Standard FST results use the adjacent Wellen reader: hierarchy metadata is
  available immediately, while transitions are decoded only for mapped or
  checked signals. The shared workspace advertises this optional capability
  only when its helper deployment is complete; VCD/CSV remain independent.
  Module Manifest v5 also records unresolved module instances in the selected
  dependency closure. Simulation refuses them by default; the shared `Stubs`
  menu can explicitly enable supported input-only passive stubs without
  representing dependency behavior. `Run all (N)` executes every stored
  scenario sequentially, continues after individual failures, shares the
  compiled-model cache, and presents per-scenario status, diagnostics, cache
  provenance, duration, and selectable Actual waveforms in the Batch review
  page. Stop cancels the current item and the remaining queue.
  Mapped Actual signals now retain portable declaration and Slang driver links:
  the result toolbar returns to either source location, while an editor signal
  can reveal its corresponding row in an already open result. Stable semantic
  identities are preferred over source-location fallback; ambiguous, unmapped,
  cross-workspace, and non-portable paths are rejected rather than guessed.
  The Windows release keeps its self-contained Verilator 5.050, MinGW 13.1,
  and GNU Make toolchain in one stable sibling ZIP plus a SHA-256 manifest.
  The first simulation verifies and extracts it asynchronously into the local
  application-data cache; later releases and runs reuse that cache instead of
  synchronizing or unpacking nearly 19000 small files. Existing expanded
  `WaveWorkbench/toolchain` layouts remain supported. Explicit paths can still
  be selected under `Settings > Simulation`; empty fields prefer expanded or
  cached bundled tools, then environment variables and `PATH`. Child build and
  simulator processes receive the complete portable runtime environment, and
  a failed probe reports each unavailable tool separately.
- Preview and apply guarded RTL changes through the existing rename,
  connection, expose-to-top, scoped replace, instance-pair connection, and
  multi-signal propagation workflows. Module-port changes can be synchronized
  across all source instances through one High+Diff transaction.
- Save and restore workspace-local tabs, layout, navigation filters, and scan
  state. Current sessions use local application storage; a workspace `.zs`
  file is accepted only as a legacy read-only import source.
- Open contextual content through the right-side Context Rail. A transient
  Peek overlays the editor without changing its split model; Pin moves the
  exact live view into a native tabbed Context Dock. Temporary editing shares
  the authoritative document, undo state, search catalog, and navigation
  history with ordinary editor views. Pinned resources, order, active tab, and
  portable provider state restore with the workspace, including after its root
  directory is moved.
- Restore cached workspace files immediately, then reconcile them with the
  directory in the background so files added, removed, or renamed while the
  workspace was inactive are discovered without forcing unchanged semantic
  analysis.
- Treat a clean `Ctrl+S` as a true no-op. Changed saves classify their semantic
  impact and schedule only the required file/dependency work outside the UI
  thread; trivia-only edits do not invoke Slang.
- Use F24 as an explicit command search layer: Enter executes a fuzzy result,
  `F24+D` deletes the current selection, and an empty F24 tap repeats the last
  currently valid repeatable action.
- Search every insertable symbol visible in the current scope from the default
  `Ctrl+Space` Symbols view, including enum values, then optionally narrow the
  results by semantic kind. Explicit `m <filter>` performs workspace-module
  lookup, while member and expected-enum contexts receive prioritized results.
- Rename supported semantic symbols from the compact `Ctrl+R` popup; Enter
  applies the validated transaction across affected document buffers and
  refreshes their in-memory semantic state, so another rename does not require
  an intervening save. Navigate
  lexical and Tree-sitter structural fields with the keyboard, and align
  indexed fields without changing tokens.
- Toggle the case of a selected text range from the editor context menu. The
  context menu stays focused on selection-aware or semantic operations, while
  Replace, comment, uncomment, indent, and unindent remain shortcut Actions.
- Replace an ordinary multiline or rectangular column selection with an equal
  amount of whitespace while preserving line structure and one-step undo.
  Organize complete top-level signal declarations in the current module through
  a Tree-sitter-backed context action that preserves dependency declarations,
  module-level macro definitions, comments, declaration order, and procedural
  locals, and refuses unsafe moves.
- Hold `Alt+Up` or `Alt+Down` to move logical lines continuously while the
  caret follows the moved text; once column selection is active,
  `Shift+Left` / `Shift+Right` extends or contracts its column span.

## Suite application protocol

ZeroSlack is a `suite-app/v1` provider. It resolves
`zeroslack://source?file=...&line=...&column=...`, exposes the
`zeroslack.source.reveal` action, and publishes the model Surface
`zeroslack.source.preview`. The adapter uses the neutral `SuiteApp::suiteapp`
SDK; it does not read another application's database or include another
application's private headers.

The optional Runtime is located through `SUITEAPP_RUNTIME_EXECUTABLE`, a local
or sibling `Runtime` directory, or `PATH`. If it is absent, ZeroSlack continues
to run normally and only suite discovery is unavailable. The complete contract
and cross-application verification record are in
[Suite App Protocol Plan](SUITE_APP_PROTOCOL_PLAN.md).

## Build, run, and test

Requirements are CMake 3.27 or newer, a C++20/C99 toolchain, Qt 6 with Core,
Gui, Widgets, Concurrent, Svg, and Test modules, Ninja or another CMake
generator, and initialized `thirdparty/slang`, `thirdparty/tree_sitter`, and
`thirdparty/tree_sitter_systemverilog` sources.

```powershell
git submodule update --init --recursive
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<qt-prefix>
cmake --build build/local --target demo
build/local/demo.exe
ctest --test-dir build/local --output-on-failure -j1
```

`VERSION` is the single manually maintained product version. CMake generates
`generated/version.h`, which supplies the application title/status version and
the GUI tests. `version_documentation_guard` checks the generated header and
the current-document version markers.

## Documentation

- [Architecture](ARCHITECTURE.md)
- [Changelog](CHANGELOG.md)
- [Current plan](plan.md)
- [Current goal](goal.md)
- [Version policy](VERSIONING.md)
- [Historical records](docs/archive/README.md)

Historical acceptance logs and superseded status reports are intentionally
kept out of this current-facts document.
