# AppSuite integration

ZeroSlack is one of four independent applications in the AppSuite family. This document is
the single ZeroSlack-side record of that family contract: the transport protocol, the
implemented cross-application workflows, and the shared visual and interaction language.
Application state always stays with its owner.

Current source baseline: ZeroSlack 0.22.0, Pinloom 0.4.0, WaveWorkbench 0.12.0,
RegMapWorkbench 0.3.0. Each application remains an independent repository.

## Application protocol (`suite-app/v1`)

The public protocol is `suite-app/v1`. Application state stays with its owner.

### Architecture

#### Neutral runtime

The neutral source lives outside every application repository. The installed
`SuiteApp::suiteapp` SDK provides protocol and client integration; `suite-runtime`
provides per-user discovery and routing. CMake exports and JSON schemas define
the public integration boundary.

The runtime owns discovery and routing only. It never owns Pinloom content,
wave projects, register-map projects, or ZeroSlack workspaces.

#### Application roles

Each application publishes one `AppDescriptor` and may expose:

- Resources: stable URI identities resolved by their authoritative owner;
- Actions: typed requests with explicit success or structured failure;
- Surfaces: UI descriptors with native, model, or external fallback modes.

Applications may be both Host and Provider. They communicate with the broker,
not with each other's executable paths or private databases.

#### Transport

- Local-only `QLocalServer` / `QLocalSocket` transport;
- one compact JSON request and one compact JSON response per connection for v1;
- per-user server access, bounded payload, request ID, timeout, and protocol
  validation;
- no TCP listener and no ambient network dependency;
- asynchronous progress and cancellation are reserved as declared v1
  capabilities, not simulated by blocking UI work.

### `suite-app/v1` Contract

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

#### Resources

Resource URI forms are published by the current provider descriptors listed below.

Cross-application state stores only stable URIs and explicit lightweight view
state. Authoritative content is resolved again from the owner.

#### Actions

An action descriptor declares its stable ID, accepted resource schemes,
parameter schema ID, result schema ID, side-effect class, and whether the
provider UI is required. Invocation never relies on human-readable labels.

#### Surfaces

Surface descriptors use one of three modes:

- `model`: provider returns structured data rendered by the Host;
- `native`: a versioned shared component is available and declares Qt,
  compiler, architecture, and ABI compatibility;
- `external`: the owning application opens the resource independently.

Every native Surface must declare an external fallback. Cross-process native
window reparenting is not part of the protocol.

### Provider integration

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

### Packaging

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



The formal package for this branch is
`E:/PinloomRoot/AppPackage/AppSuite-ElaWidgetTools`. The main branch retains
`E:/PinloomRoot/AppPackage/AppSuite`; the two destinations are independent.
The manifest records component versions;
`SHA256SUMS.txt` records file hashes, not a digital signature. The assembly script
`../scripts/package-app-suite.ps1` consumes prebuilt portable inputs and does not compile applications.
`ReplaceExisting` removes the old destination and moves staging; replacement is not transactional.
ZeroSlack also retains the `pinloom-host/v1` context adapter; the suite protocol does not remove that compatibility path.

## Implemented workflows


### ZeroSlack Workspace Hub

The Hub is a side-panel navigation model scoped to the active workspace and
current source selection. It groups Source, Pinloom, Wave, and RegMap items,
shows availability/stale/missing state, previews model surfaces when available,
and invokes stable native deep links for editing. It never reads another
application's private database.

Updates are generation-tagged. Older provider responses cannot replace a newer
selection. Provider absence degrades only that section. Hub visibility, width,
expanded groups, selection, and preview state are persisted.

### `zeroslack-cli suite-context`

The command is read-only and returns the existing `zeroslack.cli/v1` envelope.
It starts from the semantic cache, discovers explicit workspace references to
Pinloom, Wave, and RegMap resources, and invokes public suite/CLI contracts on
demand. Default output is bounded metadata; `--include` selects providers and
`--max-tokens` enforces a deterministic payload budget. Missing applications,
stale IDs, timeouts, and invalid files are returned as structured diagnostics
without failing unrelated sections.

### Pinloom PDF Viewer Adapter

Pinloom stores PDF identity, page, page-space rectangle, rotation, and crop-box
metadata as the authoritative locator. A `PdfViewerAdapter` owns viewer-specific
window discovery, capture sampling, navigation, and cancellation. SumatraPDF is
the first adapter. Screen pixels and zoom are observations, never persisted
anchor identity. The existing annotated-copy presenter remains the stable
highlight path.

Capture is an explicit success/failure/canceled/timeout state machine. Closing
or changing a viewer cancels pending work without blocking the application.
Legacy locators remain readable and are upgraded only on a successful save.

### WaveWorkbench Scenario lifecycle

Create, duplicate, rename, delete, and reorder Scenario operations use stable
IDs, deterministic names, one-step undo/redo, and safe current-selection
fallback. Deleting the last Scenario is rejected. CLI operations and deep links
share the same validation. External reload preserves a selected Scenario by ID
or reports its removal explicitly.

### RegMapWorkbench external-diff decisions

Disk/CLI changes are presented as stable, dependency-aware items. Users can
accept or reject selected changes or all changes after a validation preview.
Accept applies one atomic WorkspaceStore transaction and is undoable. Reject
keeps the Workbench value and records the decision against the observed disk
digest; a newer disk revision invalidates prior decisions. Parent/child changes
cannot produce dangling Blocks, Registers, Fields, or Enum values. Neither path
silently overwrites the disk.

## Visual and interaction contract


### Shared visual system

Each application implements the same semantic roles locally. No runtime theme
dependency is introduced between otherwise independent products.

#### Foundations

- Spacing scale: 4, 8, 12, 16, 24, and 32 px.
- Control heights: 28 px compact, 32 px default, 36 px primary.
- Corner radii: 6 px controls, 8 px cards, 10 px floating surfaces.
- Typography: platform UI font for navigation and prose; monospace only for
  code, addresses, signals, values, paths, and command grammar.
- Type hierarchy: 12 px metadata, 13 px body/control, 14 px strong labels,
  18-20 px page titles. Font weight, not color alone, identifies hierarchy.
- Surfaces: app background, canvas, panel, raised panel, selected surface,
  overlay, border, and strong border.
- Text: primary, secondary, muted, disabled, inverse, link.
- Status: information, success, warning, error, stale, and updating.
- Interaction: hover, pressed, keyboard focus, selected, drag target, and
  disabled have distinct tokens in both themes.
- Animation is limited to 120-180 ms state transitions and progress indicators;
  data movement and navigation never wait for decorative animation.

#### Product identity

- ZeroSlack uses cyan/blue for source-to-insight relationships.
- Pinloom uses warm amber for captured Anchors and blue for navigation.
- WaveWorkbench uses teal for actual signals and violet for expected/derived
  state.
- RegMapWorkbench uses indigo for address structure and cyan for RTL sync.

Identity accents never replace shared success/warning/error semantics. Text and
critical diagram edges meet readable contrast in light and dark themes.

#### Common shell behavior

- The top level exposes one concise primary toolbar, not duplicated menu and
  toolbar commands without hierarchy.
- Navigation, work canvas, and details/diagnostics form a stable three-region
  model. Regions are resizable with visible handles and sensible minimums.
- Page headers show title, current target, freshness/dirty state, and at most
  three primary actions. Secondary actions use an overflow menu.
- Empty states state what is missing and expose one corrective action.
- Loading preserves prior useful content when safe and shows scope plus status.
- Errors appear near the failed task with a diagnostic detail route; routine
  failure does not create modal loops.
- Status bars report durable state only. Instructions and selection detail live
  next to the affected surface.
- Keyboard focus is always visible. Tab order follows the visual hierarchy;
  existing high-frequency shortcuts remain available and are documented in
  tooltips.

### Shared visual and interaction language

The applications keep their own information architecture. They share semantic
tokens and component behavior instead of copying one window layout:

- roles: canvas, panel, raised surface, border, text, muted text, accent,
  selection, focus, success, warning, and error;
- density: 4 px base spacing, 28/32 px compact controls, 36 px primary controls,
  6/8 px radii, and consistent panel-header padding;
- states: visible hover, pressed, checked, disabled, keyboard focus, loading,
  stale, empty, warning, and error states;
- typography: one application title level, one panel-title level, body,
  secondary metadata, and monospaced technical data;
- navigation: Tab/Shift+Tab traversal, Enter activation, Escape dismissal,
  Ctrl+Z/Ctrl+Y history, stable selection after refresh, and no arrow-key
  interception while editing text;
- feedback: non-modal status/notice surfaces for recoverable work; modal dialogs
  only for destructive or choice-requiring actions;
- identity: ZeroSlack, Pinloom, WaveWorkbench, and RegMapWorkbench keep distinct
  accent/icon identities while using the same state semantics.

Every implementation must preserve system light/dark adaptation, 100-200%
scaling, accessible names, focus indicators, reduced-motion behavior, and the
current saved panel/window geometry.
