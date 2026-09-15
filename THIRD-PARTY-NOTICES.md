# Third-party notices

ZeroSlack itself is licensed under Apache License 2.0; see [LICENSE](LICENSE).
This file records the third-party components it builds against, embeds, or ships
beside, and where each component's own license text lives. Every statement below
was checked against the license file in this repository or in the component's own
source tree, not against memory.

## Built into the application

| Component | Version | License | License text |
| --- | --- | --- | --- |
| [slang](https://github.com/MikePopoloski/slang) | 10.0.14 | MIT | `thirdparty/slang/LICENSE` (submodule) |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) | vendored | MIT, © 2018 Max Brunsfeld | `thirdparty/tree_sitter/LICENSE` |
| tree-sitter-systemverilog grammar | vendored | **unresolved — see below** | none in this repository |
| ICU Unicode tables used by tree-sitter | ICU 58+ | Unicode/ICU license | `thirdparty/tree_sitter/lib/src/unicode/LICENSE` |
| Qt 6 (Core, Gui, Widgets, Concurrent, Svg, Test) | 6.10.2 | see note below | Qt installation |

**Unresolved: the SystemVerilog grammar.** `thirdparty/tree_sitter_systemverilog`
was vendored as plain files with no license text, no upstream URL and no version
recorded, so this repository cannot state its license. The most likely upstream is
[gmlarumbe/tree-sitter-systemverilog](https://github.com/gmlarumbe/tree-sitter-systemverilog)
(MIT, © 2024-2025 Gonzalo M. Larumbe), but that has not been confirmed against the
vendored files. Confirm the origin and add its license text before distributing
this repository publicly.

**Qt note.** ZeroSlack links Qt 6 dynamically. Binary distributions built against
an open-source (LGPLv3) Qt carry that license's obligations — keeping the linkage
dynamic so Qt can be replaced, passing on the license text, and stating that Qt is
used. Builds against a commercial Qt license follow that agreement instead. Which
one applies depends on the Qt installation used for the build, not on this
repository.

## Embedded resources

| Component | License | License text |
| --- | --- | --- |
| 0xProto | SIL Open Font License | `resources/fonts/0xproto/LICENSE.txt` |
| Geist Mono | SIL Open Font License | `resources/fonts/geist-mono/LICENSE.txt` |
| Intel One Mono | SIL Open Font License | `resources/fonts/intel-one-mono/LICENSE.txt` |
| Iosevka | SIL Open Font License | `resources/fonts/iosevka/LICENSE.txt` |
| Maple Mono | SIL Open Font License | `resources/fonts/maple-mono/LICENSE.txt` |
| Monaspace Neon | SIL Open Font License | `resources/fonts/monaspace-neon/LICENSE.txt` |
| Catppuccin palette | MIT | `resources/catppuccin/LICENSE.txt` |

## Shipped beside the application, not linked into it

The Windows package carries a portable Wave simulation toolchain as separate
executables. They are invoked as child processes and are not linked into
ZeroSlack.

| Component | Version | License |
| --- | --- | --- |
| Verilator | 5.050 | LGPLv3 or Artistic License 2.0 |
| MinGW-w64 GCC runtime | 13.1.0 | GPLv3 with the GCC Runtime Library Exception |
| GNU Make | bundled | GPLv3 |

Each component keeps its own license files inside the toolchain bundle.
`scripts/build-portable-verilator.ps1` and `scripts/package-wave-toolchain.ps1`
assemble that bundle from upstream releases; they do not modify the components
beyond the patch in `packaging/verilator-v5.050-windows-portable.patch`.

## Sibling applications

WaveWorkbench, Pinloom and RegMapWorkbench are separate products with their own
repositories and licenses. ZeroSlack talks to them across versioned IPC and ABI
boundaries and contains none of their source. See
[AppSuite integration](docs/suite.md).

## Test fixtures

The real-workspace RTL fixtures this suite can exercise are not part of this
repository and are excluded by `.gitignore`. They belong to their respective
owners. See `test_sv/fixture_names.h` for how the tests refer to them without
naming them.
