# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals. This file defines how to keep moving.

## Next Architecture Blocks

Prefer each turn to advance three or more medium-sized, clearly themed, mostly independent architecture blocks:

1. Finish reducing direct `SymbolAnalyzer` reachability from UI/navigation code by routing analysis events through `AnalysisScheduler`, `SemanticRuntimeCoordinator`, `AnalysisCoordinator`, models, or services.
2. Continue the `DocumentModel` / `MyCodeEditor` boundary migration in split blocks. Keep file identity, text/version, dirty/saved state, cursor/live module, and Tree-sitter document ownership as separate commit-sized themes.
3. When touching editor or completion workflows, move the whole workflow behind the appropriate model/service/coordinator boundary instead of adding one-off relays.

Prioritize production-code architecture progress. Do not use pure assertion expansion as the main increment. Add tests only as focused regression protection directly tied to a production change.

## Batch Selection

Good batch candidates:

- different coordinator boundaries
- Query Service read paths
- focused fixture coverage tied to production changes
- unrelated UI routing cleanup

Poor batch candidates:

- broad simultaneous changes across `TabManager`, `DocumentModel`, and `MyCodeEditor`
- async scheduler lifecycle changes
- `SemanticIndex` snapshot publication changes
- migrations where signal lifetime or API shape is still unstable

When blocks touch the same core file or API boundary, reduce batch size and verify sooner.

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

- after each block, run light sanity only: affected target build or compile, focused CTest, `git diff --check`, and relevant `rg` or static boundary scans
- after two or three blocks, run full Ninja, full `ctest --output-on-failure`, changed/new source/doc ASCII and trailing-whitespace scans, and the forbidden-file guard
- if a block touches risky lifecycle, snapshot publication, or unsettled API shape, run full verification before continuing the batch

For docs-only cleanup:

- `git diff --check`
- changed doc ASCII and trailing-whitespace scans
- forbidden-file guard

## Commit Policy

- Keep work organized so each block can be committed separately.
- After batched full verification passes, create one local commit per coherent block, not one giant commit.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Docs-only cleanup gets one local docs commit after docs-only hygiene passes.

## Documentation Policy

Update docs by replacement, not accumulation. Keep `readme.md` as current handoff, `plan.md` as execution policy, and `goal.md` as stable product/architecture goal.
