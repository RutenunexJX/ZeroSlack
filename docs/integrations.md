# ZeroSlack integrations

ZeroSlack owns the workspace/file CLI and explicit external resource associations. The cross-application family contract is in [AppSuite integration](suite.md).

## Workspace and file CLI

### Purpose

`zeroslack-cli.exe` provides read-only, machine-oriented workspace queries and
an explicit-file formatting command. Its primary consumer is an AI agent that must inspect a large RTL
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

- Workspace queries never edit RTL, workspace configuration, `.zs`, or Pinloom links.
  Only `format <file> --write` edits its explicitly named file.
- Semantic cache files live under the operating-system user cache directory,
  or an explicit `--cache-dir`; no cache is written into the workspace.
- The existing Slang/SemanticIndex pipeline remains the semantic authority.
- Pinloom links are read through `PinloomCodeLinkStore`; Pinloom databases are
  not accessed directly and linked content is not fetched eagerly.
- Every response uses the versioned `zeroslack.cli/v1` envelope. Workspace query
  responses carry a workspace revision. Symbol results expose both a semantic ID intended to
  survive line movement and an exact snapshot ID for auditability.
- `suite-context` also returns an independent `suiteRevision` over its explicit
  association files and referenced Wave/RegMap projects, so suite-only changes
  do not masquerade as an unchanged semantic workspace.

### Commands

```text
zeroslack-cli format <file> [--check | --write]
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

### Explicit-file formatting

`format <file>` defaults to `--check`; `--write` and `--check` are mutually
exclusive. `--format json|jsonl|markdown` still selects output serialization.
The command opens only the explicit file and uses the editor's
`FormatterService::formatDocument` with its default options, without starting
the GUI, scanning a workspace, or querying Suite/Questa.

The `data` object contains `file`, `changed`, `written`, `outcome`, `diagnostic`,
and `formattedLines`. Outcomes are `Applied`, `Unchanged`,
`ConservativeFallback`, and `Rejected`. A conservative result preserves the
formatter's diagnostic and is not reported as complete formatting. Exit codes
are 0 for unchanged checks or successful writes, 1 for a check needing changes,
and 2 for arguments, I/O, lossless conversion, or safety rejection. `ok` means
the request was handled successfully; it remains true for exit 1.

The existing UTF-8 and BOM-selected UTF-16/UTF-32 reader and encoder must round
trip the original bytes before formatting. Uniform LF, CRLF, and CR line endings
and the BOM are retained; mixed endings and text changed by the reader's
normalization are rejected. Immutable tokens, comments, strings and existing
preprocessor protections remain enforced. A changed write uses `QSaveFile`
without direct-write fallback and rechecks the original bytes, physical path and
modification time immediately before replacement. Cooperative formatter writes
share a per-file lock. An unrelated process replacing the file after that final
check is outside this local compare-before-replace boundary. Unchanged files
are not rewritten. Query cache behavior is unaffected.

### Cache and identity

The workspace revision hashes normalized project configuration, the ordered
relative source paths, and SHA-256 for every RTL source. Cache writes use an
atomic save file. Cache version 5 stores `zsym-v2` semantic IDs for symbols and
relationship endpoints. These IDs include declaration kind, case-sensitive
name, source file, and the complete declaring-owner chain. Every source path
in that chain is workspace-relative when inside the workspace; external paths
retain their absolute identity. Source/owner positions are excluded for named
declarations, so line movement and relocating an unchanged workspace preserve
IDs. Legacy owned records without a declaration key conservatively use a
snapshot identity. Anonymous process-like records retain their structural or
position-sensitive identity. The `zexact-v1` ID still hashes the full existing
`SymbolStableKey`, including its current source position and absolute paths.

Caches from versions 3/4 are rejected; normal queries rebuild them, while
`--no-refresh` fails without using stale IDs. Old `zsym-v1` IDs and their symbol
URIs are not aliases to v2: they may be ambiguous and must be reacquired by
querying the declaration. ID queries never fall back to name matching; name
queries may still return several candidates. The `zeroslack.cli/v1` response
envelope and the `zeroslack://symbol/<id>?workspace=...` URI format are unchanged.
The SuiteApp resolver uses the same shared identity generator; its runtime
opening behavior requires the optional SDK and is not covered by CLI tests.

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
