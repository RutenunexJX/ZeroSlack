# Suite application protocol

The public protocol is `suite-app/v1`. Application state stays with its owner.

## Architecture

### Neutral runtime

The neutral source lives outside every application repository. The installed
`SuiteApp::suiteapp` SDK provides protocol and client integration; `suite-runtime`
provides per-user discovery and routing. CMake exports and JSON schemas define
the public integration boundary.

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

Broker methods:

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

Resource URI forms are published by the current provider descriptors listed below.

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

## Provider integration

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



The formal package is `E:/PinloomRoot/AppPackage/AppSuite`. Its manifest records component versions;
`SHA256SUMS.txt` records file hashes, not a digital signature. The assembly script
`../scripts/package-app-suite.ps1` consumes prebuilt portable inputs and does not compile applications.
`ReplaceExisting` removes the old destination and moves staging; replacement is not transactional.
ZeroSlack also retains the `pinloom-host/v1` context adapter; the suite protocol does not remove that compatibility path.
