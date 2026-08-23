# ZeroSlack Context Workspace Plan

Status: host migration and Pinloom provider complete; insight providers ready

## Objective

Replace the editor-specific temporary overlay with a reusable context
workspace that can host temporary editing, Pinloom resources, waveform views,
FSM graphs, module diagrams, and future contextual tools without coupling
their content to docking or window-management policy.

The migration must preserve the existing temporary-editor document identity,
shared-document editing, search, navigation history, save behavior, and source
navigation while removing the current hand-written geometry and global event
ownership machinery.

## Product model

The context workspace has two deliberately separate presentation modes:

1. **Peek** is a single transient preview anchored to the editor's right edge.
   Opening another transient resource replaces it. Peek does not support free
   dragging, arbitrary edge docking, or manual resize grips. It closes through
   the explicit rail action, `Esc`, or its close button. Ordinary focus changes
   do not destroy an in-progress contextual edit.
2. **Pinned** is a persistent right-side dock managed by the application layout.
   It uses a native splitter, supports multiple resource tabs, can float through
   normal Qt dock behavior, and participates in workspace-session restoration.

A narrow context rail is the stable entry point. Click is the primary action;
hover opening is optional policy and must never be required to use the feature.
Every resource can be promoted from Peek to Pinned without losing view state.

## Architecture

### Core data

`ContextResource` is the only cross-provider resource description. It contains:

- stable resource and provider identifiers;
- a provider-owned URI;
- display title and optional icon key;
- workspace identity and source location when applicable;
- serializable provider state.

Transient QWidget pointers, absolute geometry, and domain-specific semantic
objects are not part of the resource contract.

### Provider boundary

`IContextContentProvider` owns domain behavior and view construction. A provider
must declare whether it can open a resource, create and destroy its view, save
and restore provider-owned state, and expose compact/full-view capabilities.

Registered providers:

- `TemporaryEditorContextProvider` reuses `TabManager`, shared documents, and
  the existing temporary-editor navigation/search behavior.
- `PinloomContextProvider` resolves stable Pinloom identities through the local
  `pinloom-host/v1` contract without copying Pinloom's library into ZeroSlack.
- Later providers: Wave, FSM, and Module Diagram.

Providers do not dock, float, resize, persist application layout, or install
application-wide event filters.

### Hosts and controller

- `ContextRail`: explicit resource/provider entry points and active-state UI.
- `ContextPeekHost`: one transient resource view and minimal common chrome.
- `ContextDockHost`: persistent tabbed content inside a right `QDockWidget`.
- `ContextWorkspaceController`: resource routing, Peek-to-Pin promotion,
  history, active resource, provider registration, and session serialization.
- `PanelLayoutController`: remains the authority for application panel layout;
  it gains a generic side-panel registration path instead of context-specific
  content knowledge.

`MainWindow` wires dependencies only. It must not implement resource routing or
presentation geometry.

## Width and presentation policy

Providers declare minimum, preferred, and supported presentation classes:

- compact: 320-420 px, suitable for snippets and metadata;
- normal: 480-720 px, suitable for documents and temporary editing;
- full tool area: required for waveforms and large graphs.

Content that cannot remain usable at sidebar width exposes `Open Full View`
instead of being compressed into an unreadable panel.

## Persistence

Qt main-window state remains responsible for dock geometry. The `.zs` workspace
session stores only portable context state:

- pinned resource URIs and tab order;
- active pinned resource;
- provider-owned serializable state;
- preferred Peek and dock widths;
- rail visibility and optional hover policy.

Transient Peek content is not restored by default. Paths inside provider state
must use the existing workspace-relative identity rules.

## Migration stages

### Stage 1: Foundation

- Add `ContextResource`, provider capabilities, and provider interface.
- Add empty `ContextRail`, `ContextPeekHost`, `ContextDockHost`, and controller.
- Register a right-side context dock through the panel layout layer.
- Add pure contract tests and GUI lifecycle smoke tests.
- Keep the existing `TemporaryEditorDrawer` as the active implementation.

Exit criterion: the new host can open, replace, pin, unpin, and close a mock
resource without touching temporary-editor behavior.

Completed on 2026-08-23. The resource/provider contract, rail, Peek host,
persistent dock host, and controller were introduced with contract and
lifecycle coverage.

### Stage 2: Temporary editor provider

- Move document/view construction and resource navigation behind
  `TemporaryEditorContextProvider`.
- Preserve shared `QTextDocument`, save, search, source navigation, and history.
- Route `ui.temporaryEditor.open` and navigation open handlers through the
  context workspace.
- Keep a compatibility adapter only while old and new tests are compared.

Exit criterion: all current temporary-editor scenarios work in Peek and Pinned
modes, including unsaved shared-document edits.

Completed on 2026-08-23. The provider uses `TemporaryEditorSession` and
`TabManager` auxiliary views, retaining one authoritative `QTextDocument`,
save behavior, file identity changes, Back/Forward history, per-history folding,
and the cached file/symbol search catalog. Product routes no longer target the
legacy drawer.

### Stage 3: Interaction convergence

- Make the rail the stable open/close entry point.
- Add one reusable preview tab and persistent pinned tabs.
- Add Back, Forward, Pin, Close, and Open Full View common actions.
- Remove arbitrary title dragging, four-edge stowing, size grips, dock-preview
  overlays, and application-wide popup discovery from the old drawer.

Exit criterion: no context interaction depends on global event filtering or
manual edge geometry.

Completed on 2026-08-23. Rail activation, Peek replacement, exact Pin/Unpin
view transfer, provider-gated common actions, native dock tabs, Focus Mode, and
Light/Dark styling now share one interaction model. The old popup discovery,
edge handles, preview overlays, title dragging, and manual resize path were
removed.

### Stage 4: Workspace restoration

- Extend workspace-session state with versioned portable context resources.
- Restore pinned tabs after providers are registered.
- Ignore unavailable providers without blocking workspace activation.
- Add relocation and stale-resource tests.

Exit criterion: moving a workspace preserves valid pinned resources without
persisting absolute paths.

Completed on 2026-08-23. Session schema v3 stores pinned resources, order,
active tab, widths, and visibility. The temporary-editor provider persists only
a workspace-relative file identity; relocation, missing-provider, stale-file,
and workspace-isolation tests pass. Transient Peek state is intentionally not
restored.

### Stage 5: Pinloom integration

- Add provider URIs for documents, anchors, and content excerpts.
- Open code-linked resources in Peek; Pin makes them persistent.
- Keep source code free of embedded Pinloom content copies.
- Define explicit unavailable-library and missing-anchor states.

Completed on 2026-08-23. Pinloom exposes a bounded user-local
`pinloom-host/v1` bridge for capabilities, unified search, stable identity
resolution, and authoritative opening. ZeroSlack adds a Pinloom rail provider,
search and preview view, stable `pinloom://` URI round-tripping, Peek/Pin
promotion, session restoration, automatic resident launch, and explicit
offline or missing-entry states. Session data contains identity, URI, and view
query only; Pinloom content is always resolved from Pinloom.

Source-link authoring is also complete. A non-empty editor selection can attach
an existing Pinloom entry or create a Pinloom-owned source anchor through
`createSourceAnchor`. ZeroSlack stores only the portable source range and stable
URI in `.zeroslack/pinloom-links.json`; relative file identity and surrounding
text relocate the link after workspace moves or nearby edits.

### Stage 6: Insight providers and cleanup

- Add Wave, FSM, and Module Diagram providers with full-view promotion.
- Delete `TemporaryEditorDrawer`, its geometry settings, edge handle, preview
  overlay, and obsolete event-filter tests after parity is demonstrated.
- Update the user manual and architecture documentation.

Host cleanup and documentation were completed on 2026-08-23. Wave, FSM, and
Module Diagram remain independent provider additions: their existing full-size
views are unchanged until each has a deliberate compact-sidebar presentation.
They can be added without changing Context Workspace ownership or persistence.

## Verification strategy

ZeroSlack passes all `92/92` configured tests, including the dedicated Pinloom
provider/URI/lifecycle suite and the existing Context Workspace regressions.
Pinloom passes all `6/6` configured tests, including its host-bridge suite; a
real hidden Pinloom process also passed capabilities, search, and stable-
identity resolution checks over the local bridge. Source-anchor creation is
covered by the same bridge suite.

- Unit tests for resource identity, provider routing, replacement, promotion,
  ordering, serialization, and unavailable providers.
- GUI tests for Peek lifecycle, pinning, tab switching, splitter resizing,
  focus/popup stability, and dark/light theme changes.
- Shared-document tests proving main and context editors observe the same text,
  undo stack, file identity, and external-file conflict state.
- Workspace tests for restore, relocation, missing resources, and schema
  upgrades.
- Existing temporary-editor, panel-layout, GUI smoke, navigation, and session
  suites remain required throughout migration.

## Non-goals and constraints

- Do not introduce a second document model or semantic fact source.
- Do not duplicate Wave/FSM/Pinloom data inside the workspace-session file.
- Do not retain old and new presentation systems after migration parity.
- Do not add provider-specific branches to `MainWindow` or panel geometry code.
- Do not use timers or hover delays to hide focus and ownership defects.
- Do not include existing user RTL changes or generated artifacts in this work.

## Protected working-tree items

The following pre-existing items are outside this initiative and must remain
untouched:

- `test_sv/new/elec_phy_import/ctrl/chl_ctrl.sv`
- `current_app_signal_usage_hotspot_full_after.png`
- `dist/`
- `test_sv/huge_prj/.zs`
- `test_sv/new/.zs`
