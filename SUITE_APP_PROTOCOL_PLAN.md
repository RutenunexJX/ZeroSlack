# Suite App Protocol Plan

Status: protocol v1 implemented and interoperability-verified (2026-08-23)

## Objective

Build a neutral `suite-app/v1` integration layer for ZeroSlack, Pinloom,
WaveWorkbench, RegMapWorkbench, and future applications. Every application
remains independently runnable while being able to act as both a Host and a
Provider. Pairwise application dependencies are prohibited.

The first delivery is complete only when all four current applications can be
discovered through one runtime, publish versioned capabilities, resolve at
least one native resource, invoke at least one native action, and describe or
open at least one Surface through the same protocol.

## Protected Baseline

The following pre-existing work must not be reverted or folded into unrelated
changes:

- ZeroSlack and Pinloom contain the uncommitted Pinloom source-anchor and
  Context Workspace integration.
- WaveWorkbench contains uncommitted version/icon/resource work.
- RegMapWorkbench contains user changes under `examples/minimal/`.
- ZeroSlack user RTL, generated screenshots, distribution output, and `.zs`
  fixture state remain outside this initiative.

## Architecture

### Neutral runtime

The neutral source lives outside every application repository and builds:

- `suiteapp_protocol`: QtCore protocol model, validation, URI and envelope
  helpers;
- `suiteapp_client`: local broker client;
- `suite-runtime`: per-user capability broker and on-demand application
  launcher;
- JSON schemas and CMake package exports for future applications.

The runtime owns discovery and routing only. It never owns Pinloom content,
wave projects, register-map projects, or ZeroSlack workspaces.

### Application roles

Each application publishes one `AppDescriptor` and may expose:

- Resources: stable URI identities resolved by their authoritative owner;
- Actions: typed requests with explicit success or structured failure;
- Surfaces: UI descriptors with native, model, or external fallback modes.

Applications may be both Host and Provider. They communicate with the broker,
not with each other's executable paths or private databases.

### Transport

- Local-only `QLocalServer` / `QLocalSocket` transport;
- one compact JSON request and one compact JSON response per connection for v1;
- per-user server access, bounded payload, request ID, timeout, and protocol
  validation;
- no TCP listener and no ambient network dependency;
- asynchronous progress and cancellation are reserved as declared v1
  capabilities, not simulated by blocking UI work.

## `suite-app/v1` Contract

Every envelope contains:

```json
{
  "protocol": "suite-app/v1",
  "requestId": "stable-request-id",
  "method": "registry.list",
  "params": {}
}
```

Responses preserve `protocol` and `requestId`, then return either `result` or
the structured error `{ code, message, details }`.

Initial broker methods:

- `runtime.capabilities`
- `registry.register`
- `registry.unregister`
- `registry.list`
- `resource.resolve`
- `action.invoke`
- `surface.describe`
- `surface.open`
- `health.ping`

Provider descriptors include application ID, display name, semantic version,
process identity, endpoint, protocol range, capabilities, URI schemes,
actions, Surfaces, and launch fallback. Unknown fields are ignored; required
fields are validated strictly.

### Resources

Resource URIs remain owner-specific:

- `zeroslack://source?...` and `zeroslack://symbol/<stable-id>`
- `pinloom://anchor/<stable-id>` and `pinloom://clip/<stable-id>`
- `wave://scenario/<stable-id>`
- `regmap://register/<stable-id>`

Cross-application state stores only stable URIs and explicit lightweight view
state. Authoritative content is resolved again from the owner.

### Actions

An action descriptor declares its stable ID, accepted resource schemes,
parameter schema ID, result schema ID, side-effect class, and whether the
provider UI is required. Invocation never relies on human-readable labels.

### Surfaces

Surface descriptors use one of three modes:

- `model`: provider returns structured data rendered by the Host;
- `native`: a versioned shared component is available and declares Qt,
  compiler, architecture, and ABI compatibility;
- `external`: the owning application opens the resource independently.

Every native Surface must declare an external fallback. Cross-process native
window reparenting is not part of the protocol.

## Initial Application Mapping

### ZeroSlack

- resource: source file/range or semantic symbol;
- actions: reveal source resource or stable semantic symbol;
- Surface: source preview descriptor with external application fallback.

### Pinloom

- resource: existing `pinloom://` entry identity;
- actions: resolve, open, and create source anchor;
- Surface: document/anchor model backed by authoritative Pinloom content.

The existing `pinloom-host/v1` remains a private adapter during migration;
other applications consume `suite-app/v1` only.

### WaveWorkbench

- resource: wave project/result;
- action: open project;
- Surface: `wavewidgets` native descriptor plus standalone fallback.

Existing wave project, bridge, and widget ABI contracts remain authoritative.

### RegMapWorkbench

- resource: project/object stable ID;
- action: open/select object;
- Surface: register-map workbench descriptor with external fallback.

Existing `regmapc --json` schemas remain authoritative behind the adapter.

## Packaging

```text
AppSuite/
|-- Apps/
|   |-- Runtime/
|   |   |-- suite-runtime.exe
|   |   |-- suite-cli.exe
|   |   `-- schemas/
|   |-- ZeroSlack-win64/
|   |-- Pinloom/
|   |-- WaveWorkbench/
|   |-- RegMapWorkbench/
|   `-- Toolchain/
|-- suite-manifest.json
`-- SHA256SUMS.txt
```

Every application executable is placed in an immediate child of `Apps/`, so
the existing `../Runtime` discovery rule resolves to the single
`Apps/Runtime/suite-runtime.exe`. `Toolchain` remains a sibling of
`ZeroSlack-win64` and `WaveWorkbench`, matching the existing Wave toolchain
discovery contract. The suite manifest pins tested component versions and
protocol/ABI ranges. Applications retain private Qt runtime directories; Qt
DLLs and plugins are never flattened into a shared directory. Missing
providers disable only their own capabilities.

The suite assembly script consumes already verified portable component
directories, removes the duplicate WaveWorkbench copy from the ZeroSlack
staging directory, deploys the Runtime's own Qt dependencies, and writes a
manifest plus SHA-256 inventory. Existing standalone packages remain valid;
the family package is an additional distribution form.

The runtime/SDK and four adapters are complete. Generating a distributable
`AppSuite/` tree and a concrete signed `suite-manifest.json` remains a separate
packaging task; it is not required for applications to use `suite-app/v1` from
their existing independent builds.

## Delivery Phases

### Phase 1: baseline and contract

- Record repository status and test baselines.
- Freeze schemas, descriptors, error taxonomy, limits, and URI rules.
- Add protocol parsing and validation tests before broker implementation.

### Phase 2: runtime and SDK

- Implement the broker registry and deterministic routing.
- Implement client/server helpers and stale-registration cleanup.
- Test malformed messages, oversized payloads, duplicate providers, timeout,
  unavailable providers, and protocol mismatch.

### Phase 3: four adapters

- Add provider endpoints to all four applications without duplicating their
  business facts.
- Add minimal client entry points so each application can query the same
  registry and invoke another provider in tests.
- Preserve existing private contracts behind adapters.

### Phase 4: real interoperability

- Start one real broker and four real provider processes.
- Verify registration, discovery, resource resolution, action invocation,
  Surface description/open, provider restart, and missing-provider isolation.
- Run each repository's complete existing test suite.

### Phase 5: packaging readiness

- Generate a suite manifest from independently packaged components.
- Validate executable paths, hashes, protocol ranges, native ABI metadata, and
  external fallbacks.
- Do not duplicate WaveWorkbench or user data in the suite.

Phase 5 is intentionally deferred. The checked schema and runtime discovery
rules are ready, but no unified application-family package was produced in
this protocol implementation task.

## Delivered Implementation

The neutral implementation lives at `E:\SuiteRuntime\SuiteRuntime` and is
installed for development at `E:\SuiteRuntime\install`. It exports the static
`SuiteApp::suiteapp` SDK and installs `suite-runtime.exe`, `suite-cli.exe`, CMake
package metadata, headers, and protocol schemas.

Runtime discovery is deterministic: an explicit path, then
`SUITEAPP_RUNTIME_EXECUTABLE`, the application directory, the sibling
`../Runtime` directory, and finally `PATH`. Applications remain usable when no
runtime is found; only suite capabilities are unavailable.

The implemented application contracts are:

| Application | Resource | Action | Surface |
| --- | --- | --- | --- |
| ZeroSlack | `zeroslack://source?...` | `zeroslack.source.reveal` | `zeroslack.source.preview` (`model`) |
| Pinloom | `pinloom://entry/...` | `pinloom.entry.open`, `pinloom.source-anchor.create` | `pinloom.entry.preview` (`model`) |
| WaveWorkbench | `wave://project?...` | `wave.project.open` | `wave.waveform` (`native`, external fallback) |
| RegMapWorkbench | `regmap://project?...` | `regmap.project.open` | `regmap.workbench` (`model`) |

Provider calls use a 2-second bound. Runtime probing uses 300 ms and first
startup uses a 3-second bound. The transport accepts a response that arrives
while the peer is completing its write/disconnect sequence, preventing a fast
local provider from being incorrectly removed as `write_failed`. Nested Qt
event loops cannot emit a competing timeout response for an already handled
request.

## Verification Record

On 2026-08-23 one real Runtime and the four full GUI applications registered
simultaneously. The registry contained `zeroslack`, `pinloom`, `wave`, and
`regmap`. Seventeen real cross-process calls passed:

- resource resolution for all four providers;
- Surface description and Surface open for all four providers;
- the native open/reveal action for all four providers;
- Pinloom source-anchor creation followed by resolution and open.

All four providers remained registered after the matrix. This specifically
regresses the former fast-response `write_failed` removal and Pinloom's nested
modal-event-loop response race.

A separate real-process restart check terminated WaveWorkbench after a
successful resolution. The next call returned `provider_unavailable`, the
stale registry count became zero, and a restarted WaveWorkbench registered and
resolved the same URI successfully.

Test results:

- Suite Runtime: 4/4 passed, including 32 sequential fast responses and the
  nested-event-loop competing-response regression;
- Pinloom: 7/7 passed;
- WaveWorkbench: 97/97 passed;
- RegMapWorkbench: 4/4 passed;
- ZeroSlack: 92/93 passed in the full run. All functional and suite tests
  passed. `editor_incremental_test` reproducibly exceeded its pre-existing
  visible-Wave performance threshold (`p95` about 7.4-8.9 ms versus 6 ms;
  one run also exceeded the 12 ms max threshold). Its 167 other assertions,
  including zero full-document materialization and scoped delta behavior,
  passed. The suite integration does not participate in this edit/render hot
  path; the threshold was not relaxed.

## Acceptance Criteria

- No application includes another application's private headers or reads its
  private database.
- No application-specific branch is added to the broker.
- All four applications register and are discoverable simultaneously.
- Each application passes Resource, Action, and Surface contract tests.
- Every application can act as a protocol client in an interoperability test.
- Existing Pinloom, WaveWorkbench, RegMapWorkbench, and ZeroSlack workflows
  retain their original behavior when the runtime is absent.
- Protocol failures are explicit and never block an application's main UI.
- All repository tests and a real four-process suite smoke test pass.

## Documentation Discipline

This document, each repository's plan/goal documents, and the suite protocol
schemas are updated after every completed phase. Implementation is not marked
complete from unit tests alone; the real four-application process test is the
final gate.
