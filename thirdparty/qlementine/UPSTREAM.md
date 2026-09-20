# Qlementine vendored dependency

- Upstream: https://github.com/oclero/qlementine
- Commit: `209e549c415f1d828883f9f21a633eb535f9d67d`
- Declared version: 1.5.0.0
- Included: root CMakeLists.txt, LICENSE, lib/, cmake/. Examples and Git metadata are excluded.
- License: MIT, copyright 2022 Olivier Cléro; original notices are retained.
- Local source modification: `lib/src/animation/WidgetAnimationManager.cpp`, `stopAll()`.
  Erase from `begin()` until empty instead of incrementing an erased iterator.
  Reproducer and product lifecycle coverage exercise populated animations before disabling.
- `upstream-files.sha256` records the original byte hashes; `patches/stop-all.patch`
  records the sole implementation change. Remaining added files provide provenance and licenses.
  The local `.gitattributes` disables line-ending conversion for this dependency so the
  original hashes remain meaningful across checkouts.

Embedded fonts are unmodified. Inter / Inter Display 4.001 carry
Copyright 2016 The Inter Project Authors (https://github.com/rsms/inter), under
SIL OFL 1.1 (`licenses/Inter-OFL.txt`, retrieved from that project's LICENSE.txt).
Roboto Mono 3.000 carries Copyright 2015 The Roboto Mono Project Authors
(https://github.com/googlefonts/robotomono). These four **embedded files** declare
Apache License 2.0 in their name records; `licenses/RobotoMono-Apache-2.0.txt`
contains the official Apache text. The current upstream font repository's OFL
must not be substituted for the license recorded in these older binaries.
`font-metadata.json` records each binary's names, copyright, license and SHA-256.

ZeroSlack does not adopt these fonts for its UI or editor. They remain compiled
resources of the pinned dependency, so their licenses accompany the preview.

Configure with `-DZEROSLACK_ENABLE_QLEMENTINE=ON`; the default build excludes this
dependency. Updates require a new upstream pin, manifest, patch review, font
audit and rerun of the product interaction / protected-surface comparisons.
