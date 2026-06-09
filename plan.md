# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals. This file defines how to keep moving.

## Direction

Keep moving ZeroSlack toward:

- `ProjectModel`
- `DocumentModel`
- `AnalysisScheduler`
- `SemanticIndex`
- `SemanticIndexSnapshot`
- Query Services
- snapshot-backed UI/service reads
- thinner `MainWindow`

Tree-sitter remains the live syntax/editing layer. Slang remains the semantic fact source.

## Current Foundation

Implemented and in active use:

- CTest targets: `ts_doc_test`, `completion_test`, `jump_test`, `relationship_test`, `gui_smoke_test`, `large_file_perf_test`
- ProjectModel and DocumentModel minimal boundaries
- AnalysisScheduler, AnalysisProgressCoordinator, AnalysisCoordinator, AnalysisCommandCoordinator
- FileCommandCoordinator, NavigationCommandCoordinator, ModeCommandCoordinator
- SemanticRuntimeCoordinator and SemanticPanelRefreshCoordinator
- SemanticIndex facade and SemanticIndexSnapshot storage
- Query services for definition, completion, relationship, hierarchy, reference, diagnostics, and search
- Problems, References, Relationships, and Navigation pane coordinators
- EditorCoordinator and TabManager support APIs for editor workflow routing
- Real multi-file relationship fixture coverage for core relationship/navigation behavior

Still transitional:

- Some live `sym_list` consumption remains behind facade/service boundaries.
- `MainWindow` still has high-level UI composition and callback wiring to thin.
- Completion/editor paths still have stateful legacy pieces.
- More snapshot-backed service coverage is needed.

## Latest Completed Block

The latest block moved scope completion and current-module lookup behind `CompletionService`.

- `CompletionService` owns snapshot-backed scope completion and current-module lookup.
- Legacy `CompletionManager` APIs delegate those paths to `CompletionService`.
- `SemanticIndex::findCompletions()` no longer depends on `CompletionManager`.
- `completion_test` covers the new snapshot-backed service and facade paths.

It was validated with focused completion build and CTest, full build, full CTest, `git diff --check`, ASCII scan, trailing-whitespace scan, and forbidden-file guard.

## Next Architecture Blocks

Choose one medium-sized block:

1. Move a related group of UI/editor/completion semantic reads or analysis policy checks behind `SemanticIndex`, Query Services, models, or `AnalysisScheduler`.
2. Extract another complete `MainWindow` coordination responsibility into a focused coordinator or existing scheduler/model boundary.
3. Thin one editor/completion workflow end-to-end without changing visible behavior.
4. Move a related completion/editor legacy state path behind existing service or model APIs.

Prioritize production-code architecture progress. Do not use pure assertion expansion as the main increment. Add tests only as focused regression protection directly tied to a production change.

Avoid:

- broad scattered rewrites
- unrelated cleanup
- qmake files
- `.claude`
- old Tree-sitter symbol parser
- regex relationship analysis
- long-lived perflog

## Validation Policy

For code/test changes:

- batch related production-code changes first
- build affected targets when compile risk is meaningful or the block is done
- run focused CTest after the coherent block is complete
- run full `ctest --output-on-failure` when shared behavior or service reports are touched
- run `git diff --check`
- run changed/new source/doc ASCII and trailing-whitespace scans
- run the forbidden-file guard

For docs-only cleanup:

- `git diff --check`
- changed doc ASCII and trailing-whitespace scans
- forbidden-file guard

## Commit Policy

- After a medium-sized coherent architecture block passes the agreed build/test/hygiene gates, create a local commit automatically.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Docs-only cleanup does not require an automatic commit unless the user asks or it is bundled with a completed architecture block.

## Documentation Policy

At every handoff or block completion, update docs by replacement, not accumulation.

- `readme.md`: current handoff only. Keep current state, hard rules, latest verified block, validation, architecture snapshot, next steps, and startup checklist.
- `plan.md`: execution policy only. Keep current foundation, next block menu, validation, commit, documentation, and handoff policies.
- `goal.md`: stable destination only. Keep product goals, target architecture, principles, remaining gaps, and definition of done.

Automatic stale-content cleanup rules:

- Replace the previous latest block when a new latest block exists.
- Delete detailed older block chains after they are represented by the architecture snapshot.
- Delete validation output that no longer describes the current diff or latest commit.
- Delete stale next steps once they are completed, contradicted, or too broad to guide the next session.
- Keep commit hashes only when they identify the latest local handoff state.
- Keep docs compact enough for a new session to read before coding.

## Handoff Policy

At handoff, record only:

- current diff/commit shape
- latest completed work
- latest validation
- next best step
- rule or workflow changes
