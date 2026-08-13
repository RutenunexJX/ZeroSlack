# ZeroSlack Current Goal

Product version: `v0.2.0`

## Objective

Establish a verifiable 0.2.0 product/document baseline and reduce
`MainWindow` ownership by moving workspace session/lifecycle orchestration and
the four high-risk RTL action launch paths into dedicated coordinators without
changing user-visible session or edit semantics.

## Completion criteria

- CMake, generated version metadata, application presentation, current
  documentation, and tests agree on 0.2.0.
- The prior README, plan, and goal contents are preserved byte-for-byte as
  [dated archive records](docs/archive/README.md).
- The coordinator owns session state, debounce timer, lifecycle connections,
  capture/save/restore/clean sequencing, and workspace isolation.
- `RtlActionCoordinator` owns validation, document capture, instance selection,
  launch, preview, and panel coordination for RTL rename, connection transform,
  instance-pair connection, and multi-signal propagation while reusing the
  existing planning and transaction workflows.
- `MainWindow` contains only coordinator construction, narrow UI-state bridges,
  route delegation, and necessary top-level user command entry points for these
  responsibilities.
- Focused tests and all non-deferred complete-suite checks pass without
  relaxed thresholds or removed tests; deferred performance gates remain
  enabled and explicitly documented.

## Current status

Version/document governance and both coordinator extractions are implemented.
The main program and complete repository target set build successfully. The
version/coordinator/GUI focused matrix passes, and independent control-side
acceptance completed on 2026-08-13. The load-sensitive Wave Preview latency
threshold is deferred from this milestone by product decision; no threshold
was relaxed or removed.
