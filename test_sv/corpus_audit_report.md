# ZeroSlack Functional Corpus Audit

- Generated: 2026-07-01T10:53:48Z UTC
- Roots: test_sv/new, test_sv/huge_prj
- Files: 454
- Modules: 424
- Always/process records: 3942
- Signal/port candidates: 49345
- Semantic records: 117069
- Relationships: 82636
- Diagnostics: 704
- Elapsed: 1174515 ms

## Feature Summary

| Feature | Pass | Fail | Skipped | Timeout | Empty-but-valid |
| --- | ---: | ---: | ---: | ---: | ---: |
| `state_transition_graph` | 90 | 0 | 400 | 0 | 0 |
| `signal_kernel_graph` | 388 | 0 | 1592 | 0 | 12 |
| `module_block_diagram` | 208 | 0 | 0 | 0 | 216 |
| `wave_preview` | 3963 | 0 | 15 | 0 | 30 |
| `semantic_baseline` | 874 | 0 | 0 | 0 | 5 |

## Key Failures

- No hard failures recorded by the audit harness.

## Notes

- Real corpus files were opened read-only; reports are written under `test_sv`.
- `empty-but-valid` means the service returned a coherent empty/root-only result, not a full feature pass.
- `skipped` means the corpus item did not contain the required structural trigger shape, such as no clocked FSM pair or no always block.

## Known Issues And Residual Risk

- State Transition Graph is structure-discovered: clocked current<=next pairs drive next-state positive cases and current-state negative cases. Skipped modules have no structural FSM pair under the current extractor.
- Signal Kernel Graph uses a bounded deep-call budget: 4 signal graph attempts per module and 400 total attempts. Skipped candidates are counted explicitly.
- Wave Preview uses source-discovered always/process records when workspace symbol extraction does not expose process nodes; module fallback remains explicit.
- Empty outline source files are classified as preprocessor/comment-only, guarded, skipped, or real outline failures instead of being hidden.
