# Embedded rtleditcore

This directory is ZeroSlack's authoritative first-party implementation of the
high-reliability structured Action transaction core. Production builds consume
it only through the in-tree `rtledit::core` target. No external source tree,
installed package, `CMAKE_PREFIX_PATH`, or machine-specific path participates
in configuration or compilation.

## Production boundary

The public surface consists only of:

- core positions, ranges, document versions, and semantic identity;
- `WorkspaceDocumentManager`, text edits, and `PatchEngine`;
- edit plans, source diffs, risk/preview policy, and per-edit provenance;
- the typed `signal.exposeToTop` request, resolved facts, planner, diagnostics,
  and Add/Reuse/Conflict decisions.

The core does not parse SystemVerilog. ZeroSlack supplies saved-project
semantics from Slang and live-buffer structural anchors from Tree-sitter. The
core validates those typed facts, produces deterministic edits, previews the
result, and applies the complete multi-file transaction.

Apply performs a full preflight of semantic generation, document versions,
expected text, ranges, read-only state, and provenance before changing any
document. A failure after a partial document update restores that document
first, then restores earlier documents in reverse order, and reports any
restore failure or residual change.

There is no production Action registry, generic lowering layer, independent
semantic-index provider, heuristic text-anchor resolver, or mock document
manager.

## Tests and public-header boundary

With `BUILD_TESTING=ON`, the component registers six CTest tests covering text
edits, the patch transaction and rollback paths, edit plan/diff/provenance,
the typed expose planner, the minimal API smoke path, and an embedded consumer
smoke test.

The mock document manager is confined to `tests/support` and is linked only
through `rtledit::test_support`. It is not reachable from the production
include directory or `rtledit::core`.

The former standalone install-package consumer smoke is intentionally replaced
by an equivalent in-tree boundary test. It checks the exact public header set,
rejects every legacy or mock header, and runs a consumer executable that gains
headers and symbols solely by linking `rtledit::core`. This internal component
does not install or generate a CMake package configuration.
