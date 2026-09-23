# ZeroSlack integrations

ZeroSlack owns the read-only CLI and explicit external resource associations. The cross-application family contract is in [AppSuite integration](suite.md).

## Read-only CLI

### Purpose

`zeroslack-cli.exe` provides a read-only, machine-oriented view of a ZeroSlack
workspace. Its primary consumer is an AI agent that must inspect a large RTL
project and its Pinloom code links without opening the GUI or loading complete
source files into one prompt.

`suite-context` extends that read-only view across the application family. It
combines source-index counts, Pinloom code links, and explicit WaveWorkbench
and RegMapWorkbench resources while keeping failures isolated per provider.

```text
zeroslack-cli suite-context <workspace> [--file <path>] [--line <n>]
  [--symbol <name-or-id>] [--include pinloom,wave,regmap]
  [--max-tokens <n>]
```

Wave and RegMap associations are declared in
`.zeroslack/suite-references.json` with schema
`zeroslack.suite-references/v1`. Referenced files must resolve inside the
workspace. The command does not scan private application storage and does not
start Suite Runtime or a GUI. An already-running public Suite provider may
enrich the bounded local metadata through `resource.resolve`.

### Boundaries

- The CLI never edits RTL, workspace configuration, `.zs`, or Pinloom links.
- Semantic cache files live under the operating-system user cache directory,
  or an explicit `--cache-dir`; no cache is written into the workspace.
- The existing Slang/SemanticIndex pipeline remains the semantic authority.
- Pinloom links are read through `PinloomCodeLinkStore`; Pinloom databases are
  not accessed directly and linked content is not fetched eagerly.
- Every response uses the versioned `zeroslack.cli/v1` envelope and carries a
  workspace revision. Symbol results expose both a semantic ID intended to
  survive line movement and an exact snapshot ID for auditability.
- `suite-context` also returns an independent `suiteRevision` over its explicit
  association files and referenced Wave/RegMap projects, so suite-only changes
  do not masquerade as an unchanged semantic workspace.

### Commands

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

### Cache and identity

The workspace revision hashes normalized project configuration, the ordered
relative source paths, and SHA-256 for every RTL source. Cache writes use an
atomic save file. Stable semantic IDs hash declaration kind, owner scope,
owner name, and symbol name; file identity is used only when no semantic owner
exists. Anonymous process-like records also include declaration identity. The
exact ID hashes the full existing `SymbolStableKey`, including its current
source position.

### Verification

Tests cover cache placement and invalidation, versioned output, stable IDs
across line movement, source context, anchor metadata without eager content,
relationship impact, token-bounded bundles, stale status, and read-only
workspace hashes. The release package must include `zeroslack-cli.exe` beside
`ZeroSlack.exe`.

## WaveWorkbench boundary

ZeroSlack no longer provides Wave Preview, Wave Simulation, an embedded Wave result page,
simulation toolchain discovery or bundle extraction. Simulation and trace viewing belong to
the independent WaveWorkbench application.

`suite-context` retains explicit Wave project associations through the public Suite
contract. It does not create simulation manifests or execute a simulator.
The AppSuite packager still accepts WaveWorkbench and its own Toolchain as sibling components;
those files are not ZeroSlack runtime dependencies and must retain their own licenses.
