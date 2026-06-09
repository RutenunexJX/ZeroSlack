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

The latest development step moved one relationship-tree report policy boundary
from `MainWindow` into `HierarchyService` and migrated one legacy completion
definition read behind `DefinitionService`. It also moved several relationship
completion helpers onto name-based `RelationshipService` queries and routes
all/typed/file-scoped completion symbol reads through `SearchService`.

- `HierarchyReport` now carries root-level direction groups with direction,
  nodes, and count.
- `MainWindow` relationship tree rendering now consumes those report groups
  instead of deriving root direction counts from string-keyed UI items.
- `CompletionManager::findSemanticDefinitions` now uses `DefinitionService`
  instead of reading definitions directly from `SemanticIndex`.
- `CompletionManager` relationship completion and scoring helpers now pass
  symbol names to `RelationshipService` instead of pre-resolving IDs inside
  `CompletionManager`.
- `CompletionManager::getSemanticSymbolsByType` now uses `SearchService`
  instead of reading typed symbols directly from `SemanticIndex`.
- `CompletionManager::getSemanticSymbolsForFile` now uses `SearchService`
  instead of reading file-scoped symbols directly from `SemanticIndex`.
- `CompletionManager::getAllSemanticSymbols` now uses `SearchService`
  instead of reading all symbols directly from `SemanticIndex`.
- `MyCodeEditor` no longer includes or casts to `MainWindow`, `TabManager`,
  `WorkspaceManager`, `ModeManager`, `SymbolAnalyzer`, or `NavigationManager`.
- `MyCodeEditor` relationship-analysis debounce emits
  `relationshipAnalysisRequested`; `MainWindow` connects that signal to the
  scheduler-backed request path.
- `MyCodeEditor` alternate-mode state and file commands now use
  `MainWindow`-supplied state and editor signals instead of direct manager
  reads or `TabManager` calls.
- `MyCodeEditor` include fallback resolution and file opening now use callbacks
  installed by `MainWindow`; current-file-relative and workspace fallback
  resolution are owned by `WorkspaceManager::resolveIncludePath`.
- `MainWindow` header no longer includes `mycodeeditor.h` or grants
  `friend class MyCodeEditor`; editor-specific use is kept in `mainwindow.cpp`.
- `MainWindow` editor setup is centralized in `configureEditor()`, and include
  fallback is delegated to `WorkspaceManager::resolveIncludePath`.
- `AnalysisScheduler` owns document-close semantic cleanup: it cancels pending
  open-file analysis, drops cached relationship content, reanalyzes remaining
  open documents, and invalidates closed-file relationships.
- `SymbolAnalyzer` analyzes open document content lists and no longer depends
  on `TabManager` for the open-documents analysis path.
- `relationship_test` keeps the minimal snapshot-backed regression coverage
  for the new hierarchy report groups.
- Existing uncommitted report fixture coverage also includes snapshot-backed
  `RelationshipReport` direction/type grouping and `ReferenceReport` file/type
  grouping.

## Latest Validation

Validation passed after the latest code/test step:

- `cmake --build ... --target gui_smoke_test`
- `ctest -R "gui_smoke_test" --output-on-failure`
- `cmake --build ... --target relationship_test`
- `ctest -R "relationship_test" --output-on-failure`
- full default target rebuild
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

- Extract one remaining clean MainWindow progress or refresh policy boundary into AnalysisScheduler.
- Move another UI/editor read path or analysis policy check behind SemanticIndex,
  a Query Service, or AnalysisScheduler.
- Thin another `MainWindow` refresh path without changing UI behavior.

Prioritize production-code architecture progress. Do not use pure
`relationship_test.cpp` assertion expansion as the main increment. Add tests
only as the smallest regression protection directly tied to a production code
change.

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
- Current uncommitted diff should contain `HierarchyService` root direction
  report grouping, `MainWindow` consumption of that report shape,
  `CompletionManager` definition reads through `DefinitionService`,
  `CompletionManager` relationship helper queries through name-based
  `RelationshipService`, focused report regressions, and compact handoff docs,
  unless it has been committed. Local warning noise is expected.
  It should also include `CompletionManager` all/typed/file-scoped symbol reads
  through `SearchService` unless that work has been committed.

Rules:
- Use Qt 6 + CMake + Ninja only.
- Do not restore demo.pro, *.pro, *.pri, qmake, .claude, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived perflog.
- Keep source/test/UI/CMake and handoff docs English / ASCII.
- Prefer git diff --name-only over git status noise.
- Do one small, verifiable continuation at a time.
- Prioritize production-code architecture progress; do not use pure
  relationship_test.cpp assertion expansion as the main increment.
- Add tests only as the smallest regression protection directly tied to a
  production code change.
- Stop for handoff when context is getting large, after a coherent validated increment, or before the next step becomes broad/risky.

Latest completed continuation:
- HierarchyReport carries root-level direction groups and MainWindow consumes them for relationship tree root direction rows.
- CompletionManager definition lookup now goes through DefinitionService instead of SemanticIndex directly.
- CompletionManager relationship completion and scoring helpers now use RelationshipService name-based queries instead of pre-resolving symbol IDs.
- CompletionManager typed symbol reads now go through SearchService instead of SemanticIndex directly.
- CompletionManager file-scoped symbol reads now go through SearchService instead of SemanticIndex directly.
- CompletionManager all-symbol reads now go through SearchService instead of SemanticIndex directly.
- MyCodeEditor no longer includes or casts to MainWindow or manager classes;
  relationship analysis, alternate-mode file commands, include fallback
  resolution, and file opening are routed through signals or injected callbacks.
- MainWindow header no longer includes mycodeeditor.h or grants MyCodeEditor
  friendship.
- MainWindow editor setup is centralized in configureEditor(), while workspace
  and current-file include fallback are owned by WorkspaceManager::resolveIncludePath.
- AnalysisScheduler owns document-close semantic cleanup and remaining
  open-document reanalysis; SymbolAnalyzer now takes open document content
  lists instead of reading TabManager directly.
- relationship_test checks snapshot-backed RelationshipReport direction/type grouping for outgoing and incoming instantiation rows.
- relationship_test checks snapshot-backed ReferenceReport file/type counts and file group metadata for an instantiation reference row.
- relationship_test checks snapshot-backed HierarchyReport root direction groups and node links.
- Handoff docs were updated and compacted.
- Validation passed: gui_smoke_test target build, ctest -R gui_smoke_test, relationship_test target build, ctest -R relationship_test, full default target rebuild, full CTest 6/6, git diff --check, non-ASCII scan, and forbidden-file guard.

Continue toward goal.md with one small, verifiable step.
```
