# ZeroSlack Functional Corpus Audit

- Generated: 2026-07-01T18:18:18Z UTC
- Roots: test_sv/new, test_sv/huge_prj
- Files: 454
- Modules: 424
- Always/process records: 3942
- Signal/port candidates: 49345
- Semantic records: 117069
- Relationships: 82636
- Diagnostics: 704
- Elapsed: 765682 ms

## Feature Summary

| Feature | Pass | Fail | Skipped | Timeout | Empty-but-valid |
| --- | ---: | ---: | ---: | ---: | ---: |
| `state_transition_graph` | 2 | 0 | 423 | 0 | 0 |
| `signal_kernel_graph` | 32 | 0 | 1960 | 0 | 0 |
| `module_block_diagram` | 208 | 0 | 0 | 0 | 216 |
| `wave_preview` | 1047 | 0 | 2957 | 0 | 4 |
| `semantic_baseline` | 874 | 0 | 0 | 0 | 5 |

## Key Failures

- No hard failures recorded by the audit harness.

## Notes

- Real corpus files were opened read-only; reports are written under `test_sv`.
- `empty-but-valid` means the service returned a coherent empty/root-only result, not a full feature pass.
- `skipped` means the corpus item did not contain the required structural trigger shape or was explicitly skipped by a deterministic audit coverage cap.

## Known Issues And Residual Risk

- State Transition Graph is structure-discovered and deterministically bounded by module count. Clocked current<=next pairs drive next-state positive cases and current-state negative cases; skipped modules either have no structural FSM pair under the current extractor or are beyond the audit sample cap.
- Signal Kernel Graph uses deterministic bounded coverage: relationship endpoint signals are prioritized, up to 4 signals are graphed per module, and up to 32 graph builds are attempted per run. Skipped cases carry explicit no-candidate, per-module sample cap, or global sample cap reasons and are not feature-level known issues.
- Wave Preview uses source-discovered always/process records with a deterministic process preview cap; module fallback remains explicit.
- Empty outline source files are classified as preprocessor/comment-only, guarded, skipped, or real outline failures instead of being hidden.
