# Third-party components

Original SimDock application code is Apache-2.0; see LICENSE and NOTICE.
Third-party components retain their own licenses.

Independent source snapshots taken on 2026-09-23 from the installed ZeroSlack vendor tree. SimDock builds its own copies and does not link to ZeroSlack.

- ElaWidgetTools, pinned to upstream `454cac2d57a47d3cc28577dc817793aec1881ca7`. Copyright 2024 Liniyous. MIT. Shared patches 01–27 match ZeroSlack `75180fad5e5f5142684cf092649deffe5720994d`; patches 28–30 fix list style lifetime, overlay scrollbar lifetime and combo popup padding. SimDock's existing English menus are recorded in `patches/simdock/01-english-context-menus.patch`. See thirdparty/elawidgettools/LICENSE and UPSTREAM-REVISION.md.
- FontAwesome icon font supplied with ElaWidgetTools. Its original font, icon and code license notices are preserved in thirdparty/elawidgettools/Font/FontAwesome-LICENSE.txt and redistributed as licenses/FontAwesome-OFL.txt.
- Tree-sitter runtime. Copyright 2018 Max Brunsfeld. MIT. See thirdparty/tree_sitter/LICENSE.
- Tree-sitter SystemVerilog grammar. Copyright 2024-2025 Gonzalo M. Larumbe. MIT. See thirdparty/tree_sitter_systemverilog/LICENSE.

Questa is a separately installed dependency and is not redistributed. Qt is
installed separately for development; portable packages include Qt libraries.

The dependency fact extraction in src/analyzer.cpp is adapted from ZeroSlack's src/semantic/semanticdependencygraph.cpp (2026-09-23 snapshot). The adapter uses SimDock's own UTF-8 Tree-sitter parser and workspace index; it does not link ZeroSlack.

ZeroSlack code and compatibility changes retain Apache-2.0 attribution
(Copyright 2026 RutenunexJX). See LICENSE, NOTICE and
`thirdparty/elawidgettools/ZeroSlack-Apache-2.0.txt`. Copying or adapting source
still requires attribution even when no ZeroSlack application DLL is linked.

Tree-sitter's Unicode/ICU-derived UTF helpers have additional notices in
`thirdparty/tree_sitter/lib/src/unicode/LICENSE`; the package includes this as
`licenses/Tree-sitter-Unicode.txt`. Tree-sitter's MIT text does not replace it.

Graphical stimulus semantic analysis links the same Slang compiler used by ZeroSlack (10.0.14, feb532a05 in the validated build), using ZeroSlack's `slangparseoptions.h` and in-memory compilation pattern. It does not link the ZeroSlack application. Slang is copyright (c) 2015–2026 Michael Popoloski, MIT; fmt is MIT; Slang's vendored Boost.Unordered uses the Boost Software License 1.0. Their notices are staged under the build's `semantic-licenses` directory and included by the package script.

AppSuite support optionally links the separately installed SuiteApp SDK. The portable AppSuite package includes Qt runtime libraries and the LGPLv3 text in licenses/Qt-LGPLv3.txt. Questa is never bundled.

MinGW libgcc/libstdc++ retain their GPL texts and the applicable GCC Runtime
Library Exception. MinGW-w64 and winpthreads retain their notices. The package
copies these from the selected compiler's license directory.

When wavewidgets and WaveWorkbenchEla are bundled, the matching Tickx package's
LICENSE, NOTICE, THIRD-PARTY-NOTICES.md and complete licenses directory accompany
them under `licenses/Tickx`. The package script verifies that the two DLLs match
the supplied notice-bearing Tickx package before copying its notices.

The optional SuiteApp SDK/runtime is not licensed by this repository. Before
public binary distribution, provide corresponding sources and any required
relinking/installation information for the actual Qt build; license texts alone
do not establish complete LGPL compliance. See
https://www.qt.io/development/open-source-lgpl-obligations,
[asset provenance](docs/ASSET-PROVENANCE.md) and the remaining
[public-release checks](docs/PUBLIC-RELEASE-REVIEW.md).
