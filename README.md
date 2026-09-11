# ZeroSlack

Current version: `v0.25.1`

ZeroSlack is a Qt 6 desktop environment for navigating, understanding, and
editing SystemVerilog workspaces. It combines an incremental editor syntax
model with workspace-wide semantic analysis, relationship views, diagnostics,
and preview-first RTL editing workflows.

The Workspace Hub groups the active source context, Pinloom bindings, and
explicit WaveWorkbench and RegMapWorkbench resources in the existing Context
Workspace sidebar. The same versioned associations are available to automation:

```powershell
zeroslack-cli suite-context <workspace> --file rtl/top.sv --line 42 `
  --symbol dma_ready --include pinloom,wave,regmap --max-tokens 4000
```

Wave and RegMap associations use `.zeroslack/suite-references.json`; its schema
is `schemas/suite-references-v1.schema.json`.

## Current user capabilities

- The title bar stays visible in its own row. Rounded controls, consistent scalable outline icons,
  and rounded graph nodes share the existing Light/Dark palettes. Non-editor text follows the
  [UI typography hierarchy](docs/ui-typography.md), with proportional body text and distinct headings and metadata.

- Navigation and Context occupy independent full-height side columns. The bottom drawer stays beneath
  the editor. Context rail buttons open a resizable sidebar; reopening it retains its width. Chart views
  can open in the main area using their full-view action. Explicit Peek previews remain available.
- The left rail contains Project and Settings only. Settings and connection tools open in central tabs.
  The menu bar is hidden; existing commands remain in the Project context menu and keep their shortcuts.
- The themed window title bar appears near the top edge and hides after three seconds away without moving
  editor content. Window controls, dragging and edge resizing remain available.
- Only Problems and Activity are permanent drawer buttons. Search, Change Preview and Shelf appear on demand.
  Ctrl+F/H reuse an inline editor find/replace bar; Ctrl+Shift+F/H open workspace search/replace.
- The gutter sizes its number lane by document line count, shares a diagnostic/Pinloom marker lane, and
  places folding beside the code. A link corner mark remains visible when a diagnostic takes priority.

- Activity retains workspace scan, semantic analysis and operation messages. Its drawer button shows the
  number of unread important messages; opening Activity acknowledges them, and Clear removes the log and
  count. Ordinary progress does not increment the badge or open the drawer. The window has no status bar;
  panel controls remain available through the View menu and drawer buttons.

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
  across all source instances through one Change Preview transaction.
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
`zeroslack://source?file=...&line=...&column=...` and
`zeroslack://symbol/<stable-id>?workspace=...`, exposes the
`zeroslack.source.reveal` and `zeroslack.symbol.reveal` actions, and publishes the model Surface
`zeroslack.source.preview`. The adapter uses the neutral `SuiteApp::suiteapp`
SDK; it does not read another application's database or include another
application's private headers.

The optional Runtime is located through `SUITEAPP_RUNTIME_EXECUTABLE`, a local
or sibling `Runtime` directory, or `PATH`. If it is absent, ZeroSlack continues
to run normally and only suite discovery is unavailable. The complete contract
and cross-application verification record are in
[Suite application protocol](docs/suite-app-protocol.md).

## Read-only AI CLI

`zeroslack-cli.exe` reuses the Slang semantic pipeline without opening a GUI.
It emits versioned JSON, JSONL, or Markdown for workspace summaries, source
context, stable symbols, Pinloom code-link metadata, impact graphs, Git changes,
and token-bounded AI bundles. Its semantic cache is stored in the user cache
directory and never in the RTL workspace.

```powershell
zeroslack-cli scan <workspace>
zeroslack-cli summary <workspace>
zeroslack-cli context <workspace> --file rtl/top.sv --line 120
zeroslack-cli bundle <workspace> --query dma --max-tokens 6000 --format markdown
```

The complete command and cache contract is documented in
[ZeroSlack CLI](docs/cli.md).

## Build, run, and test

Requirements are CMake 3.27 or newer, a C++20/C99 toolchain, Qt 6 with Core,
Gui, Widgets, Concurrent, Svg, and Test modules, Ninja or another CMake
generator, and initialized `thirdparty/slang`, `thirdparty/tree_sitter`, and
`thirdparty/tree_sitter_systemverilog` sources.

```powershell
git submodule update --init --recursive
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<qt-prefix>
cmake --build build/local --target demo zeroslack_cli
build/local/demo.exe
build/local/zeroslack-cli.exe --help
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
- [Suite workflows](docs/suite-workflows.md)
- [Wave simulation](docs/wave-simulation.md)

Historical acceptance logs and superseded status reports are intentionally
kept out of this current-facts document.
