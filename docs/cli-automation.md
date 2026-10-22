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
semantic source lists to 512 files. `sources` preserves compilation order;
alternatively `projectId` uses the saved project's Build inputs and include paths.

## Templates and TB authoring

| Command | Input and result |
| --- | --- |
| `templates.examples` | Returns the bundled, editable TB/DUT/clock/reset sample records. Does not install them. |
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
| `dut.inspect` | Same insertion context, `templateId` for a module-instance template; optional `module` and expression-string `parameters`. Returns real Slang modules, defaults and elaborated ports. |
| `document.candidates` | `instanceId`, `slot`, `sources`/`projectId`. Returns values and semantic evidence, including clock/reset roles. |
| `document.input` | `expectedRevision`, `instanceId`, `slot`, `value`, semantic source context; optional `dryRun`. Enforces the slot contract and updates linked occurrences. |
| `document.format` | `expectedRevision`, optional `dryRun`. Preserves instance IDs, slots and nesting using the editor's formatter and range mapper. |

For `@dut`, `document.insert` additionally accepts `module`, `instance`,
`parameters`, `bindings` (maps of expression strings), `reuse` (explicit signal
names), and `replaceId`. It uses the same declaration and instantiation planner
as the editor. No signal role is inferred from a spelling convention.

Typical sequence: inspect an empty file; preview then insert the `@tb` record;
change its name input; inspect the TB's `instances` Code region; inspect and insert
the DUT there; insert clock and reset templates in the `behaviors` Code region;
query/edit their inputs; format and inspect again. Insert at an unoccupied range
inside the selected parent region (for example its end), not inside an existing
child template. Every mutation must use the latest returned document revision.

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

Simulation Build inputs are an explicit selection. The GUI, `simulation.validate`,
and actual runs share Slang preprocessing and package dependency ordering.
`simulation.validate` reports `compilationOrder`, `includeOnly`, `dependencies`,
and `diagnostics`. Each source is a separate compilation unit; workspace macros
and include paths apply to every unit. Headers are include-only, and module
instantiation does not introduce a compilation-order edge. Missing or ambiguous
providers and package cycles block a run with diagnostics. A source edit or rescan
refreshes the GUI plan; running always validates the inputs again.

`simulation.put` also accepts `designTopModule` and `designTopFile` to associate
an explicit, source-qualified Design root with the existing project ID. These
fields do not replace `dutName`/`dutFile` or `tbName`/`tbFile`. Creating/updating
a project does not activate it; `simulation.open` selects the same unique active
project used by Design and Build inputs. Unresolvable saved roots stay visible
with diagnostics. `simulation.project` reports the GUI's pending/diagnostic/order
state when connected to a GUI session.

| Command | Input and result |
| --- | --- |
| `simulation.projects` | Lists saved projects and storage diagnostics. Querying does not migrate legacy storage. |
| `simulation.project` | `projectId`; returns disk project/revision plus actual selected GUI workbench state when available. |
| `simulation.put` | `expectedRevision`, `project` patch, optional `projectId` and `dryRun`. Omit ID and use revision `missing` to create. |
| `simulation.open` | `projectId`, explicit GUI session; adopts that workspace/project and locates the workbench. Dirty documents prevent a workspace change. |
| `simulation.validate` | `projectId`; runs existing Build input validation. |
| `support.install` | `kind: "print"` or `"behaviors"`; materializes the existing support sources, returns paths. Add them explicitly to Build inputs. |
| `simulation.run` | `projectId`, `expectedRevision`, explicit `simulator` executable, optional `timeoutMs` (100–3600000; default 120000). Requires a persistent session. |
| `simulation.status` | `runId`; returns progress/state, bounded log, completion and process cleanup state. |
| `simulation.events` | `runId`, optional `offset` and `limit` (1–1000); structured print records and next offset. |
| `simulation.cancel` | `runId`; cancels preparation or the owned Questa process tree. Poll until done and `processAlive: false`. |
| `simulation.clear` | `runId`; releases the retained completed result after process cleanup. |

Project fields: `name`, `sources`, `includeDirectories`, `designTopFile`, `designTopModule`, `dutFile`, `dutName`,
`tbFile`, `tbName`, `durationNs`. Project writes use storage revisions and adopt
the matching open workbench only after its current edit/busy state is checked.
Retired graphical stimulus fields are rejected.

Start `zeroslack-cli serve --profile <directory>` to run without a GUI. Its first
JSON line identifies the session; it remains running for API calls. An API request
`{"command":"session.shutdown"}` to that headless session ends it and cleans up
owned work. GUI sessions cannot be closed through this command.

Automation runs use the same preparation and Questa session services in console
mode, with a distinct run owned by the selected automation session. They do not
pretend that the workbench's interactive Run button was activated. One retained
run is allowed per session. At most 10000 events and 1 MiB of log characters are
retained; truncation counters are returned. Poll/collect results before clearing
or shutting down. Source saves and project changes during a run do not alter its
captured inputs. Only owned transient simulation files are automatically removed.
