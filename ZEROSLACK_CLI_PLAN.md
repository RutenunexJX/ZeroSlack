# ZeroSlack CLI implementation plan

## Purpose

`zeroslack-cli.exe` provides a read-only, machine-oriented view of a ZeroSlack
workspace. Its primary consumer is an AI agent that must inspect a large RTL
project and its Pinloom code links without opening the GUI or loading complete
source files into one prompt.

## Boundaries

- The CLI never edits RTL, workspace configuration, `.zs`, or Pinloom links.
- Semantic cache files live under the operating-system user cache directory,
  or an explicit `--cache-dir`; no cache is written into the workspace.
- The existing Slang/SemanticIndex pipeline remains the semantic authority.
- Pinloom links are read through `PinloomCodeLinkStore`; Pinloom databases are
  not accessed directly and linked content is not fetched eagerly.
- Every response uses the versioned `zeroslack.cli/v1` envelope and carries a
  workspace revision. Symbol results expose both a semantic ID intended to
  survive line movement and an exact snapshot ID for auditability.

## Commands

```text
zeroslack-cli scan <workspace>
zeroslack-cli status <workspace> [--require-current]
zeroslack-cli summary <workspace>
zeroslack-cli context <workspace> --file <path> --line <n>
zeroslack-cli symbol <workspace> <name-or-id>
zeroslack-cli anchors <workspace> [--file <path>]
zeroslack-cli impact <workspace> --symbol <name-or-id> [--depth <n>]
zeroslack-cli changed <workspace> --base <git-ref>
zeroslack-cli bundle <workspace> --query <text> --max-tokens <n>
```

Global output formats are `json` (default), `jsonl`, and `markdown`. Queries
refresh a stale cache unless `--no-refresh` is supplied. `scan` always rebuilds
the cache, while `status` only inspects it.

## Cache and identity

The workspace revision hashes normalized project configuration, the ordered
relative source paths, and SHA-256 for every RTL source. Cache writes use an
atomic save file. Stable semantic IDs hash declaration kind, owner scope,
owner name, and symbol name; file identity is used only when no semantic owner
exists. Anonymous process-like records also include declaration identity. The
exact ID hashes the full existing `SymbolStableKey`, including its current
source position.

## Verification

Tests cover cache placement and invalidation, versioned output, stable IDs
across line movement, source context, anchor metadata without eager content,
relationship impact, token-bounded bundles, stale status, and read-only
workspace hashes. The release package must include `zeroslack-cli.exe` beside
`ZeroSlack.exe`.
