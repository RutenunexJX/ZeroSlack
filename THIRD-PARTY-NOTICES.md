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
| [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) | `454cac2d57a47d3cc28577dc817793aec1881ca7`, with Qt 6.10 compatibility changes | MIT, © 2024 Liniyous | `thirdparty/ElaWidgetTools/LICENSE` |
| [Font Awesome Free Solid](https://github.com/FortAwesome/Font-Awesome/tree/6.7.2) | 6.7.2, replacing Ela's generated icon font | SIL OFL 1.1, © Fonticons, Inc. | `thirdparty/ElaWidgetTools/Font/FontAwesome-LICENSE.txt` |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) | vendored | MIT, © 2018 Max Brunsfeld | `thirdparty/tree_sitter/LICENSE` |
| [tree-sitter-systemverilog](https://github.com/gmlarumbe/tree-sitter-systemverilog) | vendored, see note | MIT, © 2024-2025 Gonzalo M. Larumbe | `thirdparty/tree_sitter_systemverilog/LICENSE` |
| ICU Unicode tables used by tree-sitter | ICU 58+ | Unicode/ICU license | `thirdparty/tree_sitter/lib/src/unicode/LICENSE` |
| Qt 6 (Core, Gui, Widgets, Concurrent, Svg, Test) | 6.10.2 | LGPLv3, see note below | `E:/QT6/Licenses/` in the build environment |

**Grammar provenance.** `thirdparty/tree_sitter_systemverilog` was vendored as
plain files, without a license text, an upstream URL or a version. The origin was
established by comparing the vendored grammar against the candidate upstream:
both declare the grammar name `systemverilog`, and of 814 vendored rule names 813
are also present upstream — a 99.6% match, with the only difference being one rule
renamed (`text_macro_identifier` against the upstream `_text_macro_identifier`).
The vendored copy therefore predates the current upstream by a small number of
revisions. Its license text is now kept beside the sources.

**Qt note.** ZeroSlack links Qt 6 dynamically. The build environment used for
releases carries Qt 6.10.2 installed under the **open-source (LGPLv3)** terms:
`licenseInfo.txt` in the Qt installation records `License type [Opensource]`, the
installation ships the LGPL, GPL3-exception and FDL texts, and it contains none of
the `licheck` binaries a commercial or evaluation installation would carry.

Distributing a binary built that way carries LGPLv3 obligations. ZeroSlack already
satisfies the structural one: Qt is linked dynamically and shipped as replaceable
DLLs beside the executable, so a recipient can substitute their own Qt build. The
package must also pass on the LGPL text and state that Qt is used; see
`packaging/ZeroSlack-PACKAGE-README.txt`. A build made against a commercial Qt
license would follow that agreement instead, and this note would no longer apply.

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
