# Source provenance

The component was migrated on 2026-07-26 from the converged standalone
`rtleditcore` working tree whose repository HEAD was
`7db516a5819bbf36898a4d638697d13f6f8ec043`.

At migration time, the current CMake, public headers, implementations, tests,
and documentation in that working tree were untracked. The HEAD therefore
records repository provenance, not a content revision. The production headers
and implementations and the five source-level tests were copied from that
working-tree snapshot; the standalone package consumer was adapted to the
embedded-target boundary described in `README.md`.

No separate license or notice file existed among the standalone source files.
The migrated code is treated as a first-party ZeroSlack component under the
repository's existing terms. No third-party code or license was introduced by
this migration.

Historical build outputs showed deleted registry, lowering, semantic-index,
anchor-resolver, and mock-production artifacts. None of those artifacts is
part of this component.
