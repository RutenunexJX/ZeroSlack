# ZeroSlack Development Plan

Use `readme.md` for handoff state and `goal.md` for stable product/architecture goals. This file defines how to keep moving.

## Next Architecture Blocks

Choose one medium-sized block:

1. Move a related group of UI/editor/completion semantic reads or analysis policy checks behind `SemanticIndex`, Query Services, models, or `AnalysisScheduler`.
2. Extract another complete `MainWindow` coordination responsibility into a focused coordinator or existing scheduler/model boundary.
3. Thin one editor/completion workflow end-to-end without changing visible behavior.

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

- build affected targets and run focused CTest after the coherent block is complete
- run full Ninja and full `ctest --output-on-failure` when shared behavior, service APIs, or UI wiring are touched
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

Update docs by replacement, not accumulation. Keep `readme.md` as current handoff, `plan.md` as execution policy, and `goal.md` as stable product/architecture goal.
