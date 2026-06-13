# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals. This file defines how to keep moving.

## Next Architecture Blocks

Start each turn from a current-state audit. Phase 3 work should add RTL understanding features through `snapshot -> Query Service/service report -> UI render`.
Prefer multiple medium-sized, clearly themed architecture blocks only when their files and lifetimes are independent enough to verify together:

1. Finish service/report foundations for Clock/Reset Domain Map, FSM State Transition Graph, Semantic Diff, and remaining relationship browsing improvements.
2. Add focused UI rendering only after the service report shape is stable.
3. Keep moving panel and editor read policy into Query Services. Coordinators should map UI state into service options, then render service reports.
4. Touch scheduler, analyzer, document, or editor ownership only for a clearly proven Phase 3 blocker.
5. When touching editor or completion workflows, move the whole workflow behind the appropriate model/service/coordinator boundary instead of adding one-off relays.
6. Remove test-only private access only when the replacement API is clearly a production boundary, not a broad test seam.

Prioritize production-code architecture progress. Do not use pure assertion expansion as the main increment. Add tests only as focused regression protection directly tied to a production change.

## Batch Selection

Good batch candidates:

- different coordinator boundaries
- Query Service read paths
- panel query builders and report shaping
- service fixtures for independent RTL understanding reports
- focused fixture coverage tied to production changes
- unrelated UI routing cleanup
- test cleanup that replaces private access with stable production-facing APIs

Poor batch candidates:

- broad simultaneous changes across `TabManager`, `DocumentModel`, and `MyCodeEditor`
- async scheduler lifecycle changes
- `SemanticIndex` snapshot publication or analyzer write-back changes
- relationship analysis watcher/request/finish ownership changes that share the same controller state
- multiple new semantic extractors that change the same relationship publication path
- migrations where signal lifetime or API shape is still unstable
- mixed model/editor/save migrations that obscure document ownership
- broad test rewrites that expose internals just to satisfy assertions

When blocks touch the same core file or API boundary, reduce batch size and verify sooner.

Avoid:

- broad scattered rewrites
- unrelated cleanup
- qmake files
- `.claude`
- old Tree-sitter symbol parser
- regex relationship analysis
- long-lived perflog

## Validation And Commit Rhythm

For code/test changes:

- after each block, run light sanity only: affected target build or compile, focused CTest, `git diff --check`, and relevant `rg` or static boundary scans
- before the commit, run full Ninja, full `ctest --output-on-failure`, changed/new source/doc ASCII and trailing-whitespace scans, and the forbidden-file guard
- if a block touches risky lifecycle, snapshot publication, or unsettled API shape, run full verification before continuing the batch
- after unified verification passes, create one large local commit for the completed turn
- every final handoff reports the remaining Phase 3 percentage

For docs-only cleanup:

- `git diff --check`
- changed doc ASCII and trailing-whitespace scans
- forbidden-file guard

## Commit Policy

- Keep work organized so each turn remains reviewable as a coherent architecture increment.
- After full verification passes, create one large local commit for the completed turn.
- Do not push unless explicitly asked.
- Keep commit messages concise and architecture-oriented.
- Docs-only cleanup gets one local docs commit after docs-only hygiene passes.

## Documentation Policy

Update docs by replacement, not accumulation. Keep `readme.md` as current handoff, `plan.md` as execution policy, and `goal.md` as stable product/architecture goal.
