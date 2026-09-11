# Repository layout

Source layout established from `743c1417` (0.25.2). File moves preserve the existing
build targets and unique quoted header names; they do not create new runtime layers.

| Directory | Responsibility |
| --- | --- |
| `src/app/` | Application entry point, main window and export declarations |
| `src/analysis/` | Analysis scheduling, workers and diagnostic publication |
| `src/semantic/` | Slang collection, semantic index, symbol and relationship services |
| `src/editor/` | Editor interaction, rendering, folding, formatting and popovers |
| `src/completion/` | Completion, snippets and templates |
| `src/commands/` | Command registry, search, rename and RTL editing workflows |
| `src/documents/` | Documents, tabs, file synchronization and recovery |
| `src/workspace/` | Workspace lifecycle, projects, transactions and Workspace Hub |
| `src/navigation/` | Source navigation, definition previews and navigation management |
| `src/insights/` | Specialized graph services, panels, workbench and export |
| `src/ui/` | Shared shell, context surfaces, icons, typography and notifications |
| `src/settings/` | Settings and appearance/configuration UI |
| `src/integrations/{pinloom,wave,suite}/` | External application adapters |
| `src/cli/` | Read-only CLI implementation and entry point |
| `components/` | Independently scoped reusable libraries |
| `test_sv/` | Regression tests and referenced HDL fixtures |
| `cmake/` | Build helpers, source-module include paths and policy guards |
| `resources/`, `images/`, `config/` | Fonts, licenses, platform assets and editor resources |
| `schemas/` | Current and compatibility-tested wire-format schemas |
| `scripts/`, `packaging/` | Reproducible packaging tools and portable-toolchain patches |
| `docs/` | Current technical and maintenance documentation |
| `build/`, `artifacts/`, `.toolchain-build/` | Ignored local outputs and reproducible caches |

Root files are limited to CMake/resource manifests, version metadata and the main
README, architecture, manual, changelog and maintenance entry points. Qt resource
manifests stay at the root to preserve their existing resource URLs.

Add new application sources to the appropriate `src/` module and explicitly list
them in `CMakeLists.txt`. A new module must also be listed in
`cmake/source_layout.cmake`. Avoid duplicate header basenames while modules share
quoted include names. Source-policy guards recursively inspect `src/`.

Keep fixtures used by CTest even when their filenames look experimental. Do not
store logs, screenshots, generated executables or package staging copies beside
source files. Retain the active build, the latest release evidence and toolchain
inputs; remove superseded builds and staging copies after release verification.
Historical plans and concept boards are recoverable from Git rather than copied
into additional archive directories.

## Cleanup baseline: 2026-09-11

- Root tracked files: 600 before classification, 14 afterwards. All 583 relocated
  C++ source/header files retain their original contents; the main-window UI is
  colocated with its implementation.
- Removed the unused `mytextedit.ui` prototype, the completed September 9 document
  cleanup audit, and the four-file September 10 visual concept directory. Current
  interaction and styling rules remain in the manual and technical documentation.
- Removed 195 obsolete local output paths (about 31.3 GiB): old Debug/shared/static
  build trees, superseded release staging copies, throwaway probes, logs and old
  toolchain smoke outputs. Test fixtures, licensed resources and compatibility
  schemas remain available.
- Retained `build/Desktop_Qt_6_10_2_MinGW_64_bit-Release`, `build/release-0.25.2`
  and current portable-toolchain inputs. Latest release logs and screenshots are
  grouped under `build/evidence/0.25.2`.
- Validation: Qt 6.10.2 / MinGW Release GUI and CLI rebuilt; eight selected CTest
  cases passed, including GUI, specialized insights, popovers, controller
  boundaries and build/source-policy guards.

The local deletion manifest and source-move map are retained in
`build/cleanup-20260911`. Git history at `743c1417` preserves the removed tracked
documents and prototype. This cleanup does not replace the installed AppSuite package.
