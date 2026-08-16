# ZeroSlack

Current version: `v0.2.2`

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
  views, templates, and explicit command completion.
- Query Slang-backed symbols, diagnostics, definitions, references,
  relationships, hierarchy, hover information, and effective compile-time
  values from the current workspace snapshot.
- Inspect RTL through Problems, Design, RTL Insights, state-transition and FSM
  views, module block diagrams, signal journeys, signal-kernel graphs, usage
  hotspots, and symbolic wave previews.
- Preview and apply guarded RTL changes through the existing rename,
  connection, expose-to-top, scoped replace, instance-pair connection, and
  multi-signal propagation workflows.
- Save and restore workspace-local tabs, layout, navigation filters, and scan
  state. Current sessions use local application storage; a workspace `.zs`
  file is accepted only as a legacy read-only import source.

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
