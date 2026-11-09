# CLI automation

The full ZeroSlack build adds `zeroslack-cli capabilities`, `sessions`, `serve`, and
`api <request.json|-> [--profile directory] [--session sessionId]`. Existing
`scan`, `symbol`, `summary`, `anchors`, `impact`, `context`, `bundle`, `changed`, `status`, `suite-context` and
`format` commands retain their existing protocol. Use `--help` for their options.
The minimal `ZEROSLACK_CLI_ONLY` build retains the original semantic CLI only.

`api` accepts one UTF-8 JSON object. Its response is
`{"schema":"zeroslack.automation/v1","ok":true,"data":{...}}`, or an error with
`error.code` and `error.message`. Exit codes are 0 for success, 3 for a conflict
or a required session, and 2 for other failures. Diagnostics go to stderr.
The original formatter retains its own 0/1/2 convention.

Paths resolve against an explicit, existing `workspace`. Document paths must
remain inside it. Document ranges use **UTF-16 code units**, not UTF-8 bytes or
Unicode code points. Requests are limited to 1 MiB; editable documents to 8 MiB;
template semantic source lists to 512 files. For template operations, `sources`
preserves the supplied order and legacy `projectId` supplies saved inputs.
Simulation derives its inputs from an explicit TB file and declared top.

## Templates and inputs

| Command | Input and result |
| --- | --- |
| `templates.examples` | Returns the bundled, editable clock/reset sample records. Does not install them. |
| `templates.list` | `workspace`, optional `file`, `scope` (`workspace` or `global`). Returns effective records, origin, input contracts, and the selected storage's `revision`. |
| `templates.get` | Same context plus stable `id`. Returns the effective record. |
| `templates.validate` | `record`; validates using the normal template service. |
| `templates.put` | `record`, `expectedRevision`, optional `dryRun`. Creates or replaces that record's stable ID. |
| `templates.delete` | `id`, `expectedRevision`, optional `dryRun`. Removes a user record from the selected storage. |

Use returned IDs rather than deriving IDs from labels. Built-in records have
`builtin: true`. User templates support snippets, semantic task/function calls,
and the existing simulation kinds. A record's `simulation.file` restricts it to
one file; `simulation.sourceFile` identifies a task/function declaration source.
Relative paths in these two fields are resolved against `workspace`. File scope
overrides workspace scope, then global scope, then built-ins, as in the editor.

For a new workspace, query `templates.examples`, then submit each desired
record with `templates.put`. Read the current storage revision before each put;
the first absent storage reports `missing`. Existing definitions are not
automatically replaced merely by querying examples.

| Command | Required operation fields beyond `workspace` and `file` |
| --- | --- |
| `document.inspect` | Returns text, `revision`, `diskRevision`, instance IDs, input ranges/contracts and ownership tree. A new offline file may be absent. |
| `document.insert` | `expectedRevision`, `templateId`, `start`; optional `length`, `parentId`, `parentSlot`, `sources`/`projectId`, `dryRun`. |
| `document.candidates` | `instanceId`, `slot`, `sources`/`projectId`. Returns values and semantic evidence, including clock/reset roles. |
| `document.input` | `expectedRevision`, `instanceId`, `slot`, `value`, semantic source context; optional `dryRun`. Enforces the slot contract and updates linked occurrences. |
| `document.format` | `expectedRevision`, optional `dryRun`. Preserves instance IDs, slots and nesting using the editor's formatter and range mapper. |

Inspect an existing or handwritten module; preview then insert an
`@definitions` or `@process` container at a valid module position.
Inside a process Code input, insert `@repeat` or another compatible template.
Read instance IDs and input ranges from `document.inspect`; provide
`parentId` and `parentSlot` for nested insertion, then query/edit inputs,
format and inspect again. Insert at an unoccupied range in the parent Code
input, not inside an existing child template. Every mutation must use the latest
returned document revision. No signal role is inferred from a spelling convention.

Offline changes write the document and hash-bound template metadata to the same
profile. Reopen with that profile to retain editable ownership. `dryRun` returns
the proposed text/metadata without writing. Offline insert/input/format also accept
`output` and `expectedOutputRevision` (`missing` only for a new output). Output
metadata is bound to the new path. A stale source, target or storage is rejected.

## Explicit GUI sessions

`sessions` returns active local sessions with IDs, process IDs, kind and profile.
There is no implicit choice of window. `api ... --session <id>` uses that session's
profile and document buffer. `--profile` selects offline storage only; it does not
change a running session's profile. `session.describe` reports open documents and
active template editing. `document.open` opens and locates an existing file in the
selected GUI session, then returns its actual adopted contents.

The `document.*` automation contract addresses text/code editor buffers and their
template metadata. Markdown, `.zblock` and PDF pages use separate GUI document
sessions and are not editable through this contract; unsupported document kinds
return `unsupported_document_kind` before opening or changing a page.
`capabilities.documentContract` lists this boundary. GUI pages share stable
region/group/page identities and workspace layout restoration; this does not add
CLI commands for dragging pages or editing Web/PDF content.

Online insert/input/format change the shared editor document and become ordinary
undo steps. They report `adopted: true`, `written: false` and the actual `dirty`
state. `document.undo` and `document.redo` require the latest `expectedRevision`.
`document.save` additionally requires `expectedDiskRevision`; it uses the normal
save service and protects its saved disk baseline. Active input editing, stale
versions and external conflicts are rejected without overwriting a buffer.
Offline document operations refuse a workspace open in a GUI and identify the
session to select. Explicit headless sessions enforce the same rule.
The legacy `format` command also rejects an open GUI document (including checks
and unchanged writes); use `api` with that explicit session for its live contents.

`written`, `adopted` and `viewLocated` are separate results. Reading or saving a
file does not imply a view was focused. Online output-to-another-path is not
supported; use the normal GUI Save As flow when a live document needs a new path.

## Simulation

Simulation contract version 2 uses an explicit `workspace`, `tb` and `top`.
The top is a module/program declaration, not a filename-derived guess. GUI and
headless validation and runs share the TB dependency planner. It selects the
actual elaborated closure, orders packages before users, and treats headers as
include-only. Each source remains a separate compilation unit with the workspace
macro/include settings; module instantiation does not impose a compile-order
edge. Missing or ambiguous providers and unsupported external libraries return
diagnostics. Use Simulation settings for an explicit definition-to-file mapping.

`simulation.validate` returns `compilationOrder`, `includeOnly`,
`participatingFiles`, package `dependencies`, `elaborationDependencies`,
and `diagnostics`. GUI validation includes open modified buffers and does not
save them. Before running, the GUI saves only participating modified documents
through normal conflict checks and captures the inputs again. A failed save,
read-only input, external conflict or stale plan prevents launch. Headless
sessions compile disk inputs.

| Command | Input and result |
| --- | --- |
| `simulation.projects` | Read-only saved legacy projects and storage diagnostics; does not migrate their storage. |
| `simulation.project` | `projectId`; returns the saved project/revision with `readOnlyCompatibility: true`. |
| `simulation.open` | `tb` and `top`, explicit GUI session; selects the target and reveals compact controls. |
| `simulation.validate` | `tb` and `top`; plans without starting Questa. |
| `support.install` | `kind: "print"` or `"behaviors"`; materializes existing support sources. Referenced support is discovered by the planner. |
| `simulation.run` | `tb`, `top`, explicit `simulator` executable; persistent GUI or headless session required. |
| `simulation.status` | `runId`; state, outcome, bounded log, plan and process state. |
| `simulation.events` | `runId`, optional `offset` and `limit` (1–1000); structured print records and next offset. |
| `simulation.cancel` | `runId`; cancels preparation or the running session. |
| `simulation.clear` | `runId`; releases a completed or cancelled retained result. |

Example validation request:

```json
{"command":"simulation.validate","workspace":"D:/work/design","tb":"tb/top_tb.sv","top":"top_tb"}
```

For legacy compatibility, `projectId` can replace `tb` and `top`; a legacy
run also requires its latest `expectedRevision`. Only its saved TB and include
settings are mapped. Legacy source selections do not limit the dependency
closure, and selecting a simulation target does not change the active Design
project. Existing project/TB/stimulus files are preserved. `simulation.put`
returns `retired_operation`; explicit simulation `sources`, `durationNs`
or `timeoutMs` returns `retired_parameter`. These parameters are not ignored.

Runs continue under TB control. `finished` means an explicit successful
`$finish`; `exhausted` means no scheduled events remain. `fatal`,
`failed`, `paused` (`$stop` or breakpoint), `cancelled`,
`preparation_timeout` and `process_exit` remain distinct outcomes.
Preparation has a bounded wait; normal Running has no total runtime limit.
A client's response/poll timeout does not stop simulation.

Start `zeroslack-cli serve --profile <directory>` for a persistent headless
session. Its first JSON line identifies the session. Send
`{"command":"session.shutdown"}` to end that headless session and clean its
owned work; GUI sessions cannot be shut down through this command.

GUI automation shares the workspace simulation controller and cannot start a
second run while the GUI is busy. Completed GUI sessions can retain Questa for
waveform inspection; headless runs close it when execution completes.
Hiding or closing a presentation page does not stop the workspace run.
One result is retained per automation session; collect it and clear it before
another automation run. At most 10000 events and 1 MiB of log characters are
retained, with truncation counters. Only owned transient artifacts are removed.
