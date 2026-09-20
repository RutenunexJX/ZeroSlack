# Third-party notices

ZeroSlack itself is licensed under Apache License 2.0; see [LICENSE](LICENSE).
This file records the third-party components it builds against, embeds, or ships
beside, and where each component's own license text lives. Every statement below
was checked against the license file in this repository or in the component's own
source tree, not against memory.

The Windows portable package includes this notice, ZeroSlack's `LICENSE`, and
copies of the dependency license texts in `licenses/` beside `ZeroSlack.exe`.
The paths in the tables below identify the corresponding source-tree copies.

## Built into the application

Optional Qlementine previews additionally contain Qlementine 1.5.0.0 at
`209e549c415f1d828883f9f21a633eb535f9d67d` (MIT, © 2022 Olivier Cléro),
with one local animation-map cleanup correction. See
`thirdparty/qlementine/UPSTREAM.md` and its recorded patch. The default build
does not include it. Preview packages include `licenses/Qlementine-MIT.txt`,
`licenses/Qlementine-UPSTREAM.md`, `licenses/Inter-OFL.txt` and
`licenses/RobotoMono-Apache-2.0.txt`.

The embedded, unmodified Inter / Inter Display 4.001 fonts are Copyright 2016
The Inter Project Authors (https://github.com/rsms/inter), SIL OFL 1.1.
Embedded Roboto Mono 3.000 is Copyright 2015 The Roboto Mono Project Authors
(https://github.com/googlefonts/robotomono); these binaries declare Apache 2.0.
Their per-file attribution and hash records are in
`thirdparty/qlementine/font-metadata.json`, also included with preview licenses.

Alternatively, `ZEROSLACK_ENABLE_SUITEUI=ON` links the separately installed
SuiteUi 0.1.0 static control SDK (Apache-2.0) and its private Qlementine backend.
The two preview build options are mutually exclusive. In this configuration,
packaging copies the installed SDK's complete notice directory to
`licenses/SuiteUi/`, including `SuiteUi-Apache-2.0.txt`, `Qlementine-MIT.txt`, both
font licenses, `font-metadata.json`, `UPSTREAM.md`, `stop-all.patch`, `NOTICE.txt`
and `build-info.json`. The pinned backend and embedded fonts are the same as
above. No SDK DLL or external font directory is required at runtime.

| Component | Version | License | License text |
| --- | --- | --- | --- |
| [slang](https://github.com/MikePopoloski/slang) | 10.0.14 | MIT | `thirdparty/slang/LICENSE` (submodule) |
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

## Sibling applications

WaveWorkbench, Pinloom and RegMapWorkbench are separate products with their own
repositories and licenses. ZeroSlack talks to them across versioned IPC
boundaries and contains none of their source. See
[AppSuite integration](docs/suite.md). WaveWorkbench and its toolchain retain their
own license files when distributed in AppSuite; ZeroSlack no longer bundles or executes a simulator.

## Test fixtures

The real-workspace RTL fixtures this suite can exercise are not part of this
repository and are excluded by `.gitignore`. They belong to their respective
owners. See `test_sv/fixture_names.h` for how the tests refer to them without
naming them.
