# ZeroSlack Current Plan

Product version: `v0.2.2`

## Current status

- The 0.2.2 implementation covers logical undo cursor restoration, column-mode
  keyboard and rendering fixes, contextual delimiters, explicit double-click
  occurrence highlighting, remembered Alt+C settings, declaration-width
  alignment, and removal of format-on-save.
- Ctrl+S now reuses one immutable cached text snapshot, avoids duplicate
  external-state probes and post-save disk reads, derives raw/logical
  fingerprints from one encoding, skips unchanged workspace-tab visibility
  work, and batches Activity output updates.
- Semantic request coalescing now covers the single-pending-file/different-
  trigger case without allocating the multi-file lookup sets on the common
  one-file save path.
- Focused regressions and the optimized Release full suite pass; all 86
  registered tests are green. No release package or source publication is part
  of this work item.

Superseded plans and completed milestone logs are available in the
[archive index](docs/archive/README.md).
