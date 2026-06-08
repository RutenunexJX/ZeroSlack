# ZeroSlack Handoff

ZeroSlack is currently a lightweight SystemVerilog workspace browser/editor, not a full IDE. The active architecture direction is:

```text
Tree-sitter live syntax and editor scope
Slang semantic facts
ProjectModel / DocumentModel / AnalysisScheduler
SemanticIndex / SemanticIndexSnapshot
Query Services
Thin UI consumers
```

## Current State

- Workspace: `E:\ZeroSlack\ZeroSlack`
- Branch: `tree_sitter_and_slang`
- Version: `0.0.20/slang25` in `version.h`
- Latest commit: use `git log -1 --oneline`
- Build path: Qt 6 + CMake + Ninja only
- Expected real diff after the handoff commit/push: empty, except local warning noise.

Common git warnings on this machine are not content errors:

- `unable to access C:\Users\14971/.config/git/ignore: Permission denied`
- `LF will be replaced by CRLF`

Prefer `git diff --name-only`, `git diff --stat`, and `git diff --check` over `git status` noise.

## Hard Rules

- Use Qt 6 + CMake + Ninja only.
- Do not restore `demo.pro`, any `*.pro`, any `*.pri`, or qmake.
- Do not restore `.claude/` or Claude local config.
- Do not restore SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived scattered perflog probes.
- Keep source, tests, UI strings, CMake, and comments English / ASCII.
- Keep these handoff docs concise and English / ASCII unless the user asks otherwise.
- Do not commit or push unless the user explicitly asks.
- Never discard user changes or use destructive git commands unless explicitly requested.

## Latest Completed Work

The latest development step moved struct-variable type lookup, struct-member
completion reads, and command-mode struct symbol reads behind `SemanticIndex`,
making the affected services snapshot-aware.

- `SemanticIndex::getStructTypeForVariable()` now resolves packed and unpacked
  struct variables, preferring the current module and falling back to global
  matches.
- `SemanticIndex::getStructMembers()` now provides sorted struct-member symbols
  from either the live database or a snapshot.
- `SemanticIndex::getModuleContextSymbolsByType()` now provides module-context
  command symbols through the facade, including module-range filtering,
  include/import expansion, and relationship fallback.
- `CompletionService::getStructTypeForVariable()` now reads through
  `SemanticIndex` instead of calling `CompletionManager` directly.
- `CompletionService` now serves struct-member completions and completion
  symbols from `SemanticIndex`, preserving the existing fuzzy prefix behavior.
- `CompletionService::findCommandCompletionSymbols()` now serves packed/unpacked
  struct type and variable symbol results from `SemanticIndex`.
- `DefinitionService` now resolves `var.member` struct context through its
  injected `SemanticIndex`, so snapshot-backed definition services do not depend
  on the global completion singleton for semantic lookup.
- `completion_test` adds snapshot-only struct-variable type assertions for
  module preference, fallback, unpacked struct variables, member completions,
  fuzzy member filtering, returned member symbols, and command-mode struct
  symbol results.
- `jump_test` adds a snapshot-only same-name struct-member assertion proving
  `DefinitionService` resolves `snap_pixel.red` to `snap_pixel_t.red`.

This moves another semantic read behind the facade and strengthens
snapshot-backed service coverage without changing UI behavior.

## Latest Validation

Validation passed after the latest code/test step:

- `cmake --build ... --target completion_test`
- `ctest -R "completion_test" --output-on-failure`
- rebuilt all CTest executables after shared core changes
- full `ctest --output-on-failure`: 6/6 passed
- `git diff --check`
- changed/new source/test/UI/CMake/handoff non-ASCII scan: empty
- forbidden-file guard: no `demo.pro`, no `*.pro`, no `*.pri`, `.claude` absent

## Current Architecture Summary

- `ProjectModel` publishes `ProjectSnapshot` with workspace root, SV files, include dirs, defines, and optional project config.
- `DocumentModel` tracks open documents, dirty/saved state, text versions, cursor state, and live module scope.
- `AnalysisScheduler` owns analysis trigger timing, debounce/cancel policy, relationship background work, diagnostics refresh requests, and relationship data refresh requests.
- `AnalysisProgressCoordinator` owns workspace analysis progress dialog policy and cancel state.
- `SemanticIndex` is the semantic facade. It still wraps live `sym_list` in places, but services and UI should read through the facade.
- `SemanticIndexSnapshot` stores read-only symbols, relationships, diagnostics, cached file content, and minimal scope names. Background analysis can publish base/enriched snapshots.
- `SemanticIndex` also owns struct-variable type lookup and struct-member symbol
  reads for services that need `var.member` context, member completion, or
  command-mode struct symbol results.
- Query services exist for definition, completion, relationship, hierarchy, reference, diagnostics, and search.
- Problems / References / Relationships panels consume service reports for sorting, filtering, counts, grouping, and result shape.
- `MainWindow` is being thinned. It should coordinate UI, not own analysis policy or semantic query policy.

## Next Best Steps

Pick one small, verifiable increment:

- Add another focused real Problems / References / Relationships fixture assertion.
- Extract one remaining clean MainWindow progress or refresh policy boundary into AnalysisScheduler.
- Move another UI/editor read path behind SemanticIndex or a Query Service.
- Strengthen snapshot-backed service coverage without broad rewrites.

Avoid broad CompletionManager or MainWindow rewrites unless there is a narrow testable slice.

## Handoff Cadence

Do one small, verified continuation at a time. Stop and update this handoff when:

- context is getting large,
- a coherent increment is validated,
- the next step is broad or risky,
- validation is blocked,
- or the user asks to stop.

Do not keep appending long session history. Replace summaries with the current state.

## Useful Commands

```powershell
$env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\CMake_64\bin;E:\QT6\Tools\Ninja;$env:PATH"

& "E:\QT6\Tools\CMake_64\bin\cmake.exe" --build "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --target relationship_test -- -j4

& "E:\QT6\Tools\CMake_64\bin\ctest.exe" --test-dir "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" -R "relationship_test" --output-on-failure

& "E:\QT6\Tools\CMake_64\bin\ctest.exe" --test-dir "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --output-on-failure
```

## New Session Opener

```text
Please use the zeroslack-handoff-dev skill to take over ZeroSlack and continue iterative development.

First read readme.md, plan.md, goal.md, and version.h, then inspect:
- git log -1 --oneline
- git branch --show-current
- git diff --name-only
- git status -sb

Known current state:
- Workspace: E:\ZeroSlack\ZeroSlack
- Branch: tree_sitter_and_slang
- Version: 0.0.20/slang25
- Latest commit: use git log -1 --oneline.
- Current uncommitted diff should be empty, except local warning noise.

Rules:
- Use Qt 6 + CMake + Ninja only.
- Do not restore demo.pro, *.pro, *.pri, qmake, .claude, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived perflog.
- Keep source/test/UI/CMake and handoff docs English / ASCII.
- Prefer git diff --name-only over git status noise.
- Do one small, verifiable continuation at a time.
- Stop for handoff when context is getting large, after a coherent validated increment, or before the next step becomes broad/risky.

Latest completed continuation:
- relationship_test now checks RelationshipReport incoming CLOCKS/RESETS report shape against the real rel_top fixture.
- Handoff docs were compacted to short English / ASCII summaries to reduce future context load.
- Validation passed: relationship_test target build, focused CTest, full CTest 6/6, git diff --check, non-ASCII scan, forbidden-file guard.

Continue toward goal.md with one small, verifiable step.
```
