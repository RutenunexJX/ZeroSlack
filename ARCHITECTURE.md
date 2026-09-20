# ZeroSlack Architecture

This document describes the implementation represented by the current source,
CMake targets, and regression tests.

## Runtime structure

`demo` is a thin executable linked to `zeroslack_core`. The core contains the
Qt Widgets shell, editor and workspace models, coordinators, semantic services,
and feature workflows. `components/rtleditcore` is a separate non-UI editing
core used for structured workspace edit plans and transactions.

`MainWindow` is the top-level widget host and composition root. It creates the
tab/workspace models, analysis runtime, navigation and panel coordinators,
settings, notifications, and command surfaces. Workspace-session lifecycle and
high-risk RTL action launch policy are owned by dedicated coordinators.
`MainWindow` supplies narrow UI bridges and delegates the registered routes;
feature services and workflows remain the implementation authorities.

## Parser and UI responsibilities

- Tree-sitter is the editor-local structural layer. Each `TSDocument` owns a
  parser, a UTF-16 piece-table buffer, and an incrementally edited syntax tree.
  It supplies highlighting ranges, folding and scope boundaries, structural
  navigation, and conservative insertion targets. A `TSDocument` stays on its
  editor's UI thread and is not shared across threads.
- Slang is the semantic authority. `SymbolAnalyzer`, `SlangManager`, and the
  workspace/incremental analysis controllers build symbols, diagnostics,
  relationships, preprocessor facts, types, and effective values from an
  immutable project/document snapshot. UI code does not independently infer
  these facts.
- Qt owns the object lifecycle, widgets, signals/slots, settings, timers, and
  background dispatch. Coordinators translate model/service results into UI
  updates; widgets do not scan workspaces or execute semantic compilations.

Tree-sitter can answer incomplete-buffer structural questions immediately;
Slang answers authoritative cross-file and elaboration-sensitive questions
after a valid semantic publication. Features must preserve this boundary.

## Main services and data flow

```text
WorkspaceManager + ProjectModel       TabManager + DocumentModel
                 \                    /
                  immutable snapshots
                          |
                  AnalysisScheduler
                          |
       WorkspaceSymbolAnalysisController / workers
                          |
          Slang semantic facts and diagnostics
                          |
                 SemanticIndex snapshot
                    /             \
       query/feature services   relationship analysis
                    \             /
          coordinators, panels, editor presentation
```

- `WorkspaceManager` owns open workspace entries, active selection, scanning,
  cached scan state, recent workspaces, ignore rules, and configuration applied
  through `ProjectModel`.
- `TabManager` and `DocumentModel` own open views, file identity, buffer state,
  dirty/saved revisions, cursor state, split groups, and external-file conflict
  handling.
- `AnalysisScheduler` owns debounce, request routing, cancellation, and
  lifecycle. `WorkspaceSymbolAnalysisController` and related workers consume
  captured snapshots. `SemanticIndex` is the UI-facing semantic fact source.
- Relationship, navigation, diagnostics, hover, graph, preview, and RTL edit
  services query published snapshots and return explicit reports or edit
  plans. Existing high-risk workflows remain the sole planning/application
  path for their actions.
- `WorkspaceSessionStateService` serializes per-workspace session state to
  local application storage and imports legacy `.zs` state without modifying
  it. `WorkspaceSessionCoordinator` owns capture/save/restore/clear sequencing,
  workspace activation and close connections, clean suppression, the single
  debounce timer, and stale-activation guards. It depends explicitly on
  `WorkspaceManager`, `TabManager`, optional `PanelLayoutController` state
  notifications, and narrow UI capture/restore/status callbacks assembled by
  `MainWindow`.
- `ContextWorkspaceController` owns the right-side Context Rail, one transient
  Peek, and a native tabbed Context Dock. Domain content enters through
  `IContextContentProvider`; providers create views and serialize only their
  portable resource state, while the controller owns presentation, exact
  Peek/Pin transfer, lifecycle, and workspace isolation. The temporary-editor
  provider reuses `TabManager` auxiliary views and the authoritative shared
  document. The Pinloom provider consumes a versioned local IPC contract and
  stores only stable Pinloom identity, URI, and provider view state; Pinloom
  remains authoritative for search, content, anchor resolution, and opening.
  Provider rail activation is resolved by the provider contract rather than
  provider-specific `MainWindow` branches. `MainWindow` registers providers
  but does not calculate context geometry or inspect provider state.
- `RtlActionCoordinator` owns validation, parameter recovery, document capture,
  instance-selection UI, panel activation, and launch/preview orchestration for
  RTL rename, instance-connection transform, instance-pair connection, and
  multi-signal propagation. Its explicit hosts are `TabManager`,
  `WorkspaceManager`, `SemanticDockCoordinator`, `PanelLayoutController`, and
  a parent `QWidget`; semantic-context resolution and panel presentation use
  narrow callbacks. It owns its signal connections and delegates planning,
  transaction, preview, and undo semantics to the existing
  `RtlHighRiskEditPanelCoordinator`, `InstancePairConnectionWorkflow`,
  `MultiSignalPropagationWorkflow`, and shared
  `WorkspaceEditDocumentManager`. It does not depend on `MainWindow` or create
  a second semantic/edit fact source.

## Threading and incremental-analysis boundaries

Editor-local Tree-sitter updates are synchronous on the GUI thread. Workspace
scanning and semantic/relationship analysis may run asynchronously. Requests
carry workspace identity, generation, document revision, and cancellation
state; publication is accepted only when those values still match the active
snapshot. An edit invalidates stale work and queues analysis from the latest
complete open-buffer overlay rather than publishing partial semantic truth.

GUI objects and panels are updated on the GUI thread. Workers receive copied
snapshots and must not retain editor widgets, `MainWindow`, or mutable project
models. Shutdown cancels and joins analysis while the semantic runtime remains
alive.

## Extension constraints

- Keep semantic truth in Slang-backed index/services and structural truth in
  `TSDocument`; do not add regex or panel-local alternatives.
- Add UI behavior through a coordinator or a narrow service/report contract.
  `MainWindow` should remain composition and top-level hosting code.
- Preserve snapshot identity, revision, cancellation, and GUI-thread
  publication checks when extending analysis.
- Route high-risk RTL launch behavior through `RtlActionCoordinator` and reuse
  the existing RTL workflows. A new panel must not duplicate planning or
  edit-application semantics.
- Add new sidebar content as an `IContextContentProvider`; do not create another
  floating-window, docking, persistence, or global-event ownership system.
- Keep cross-application integrations behind versioned bounded IPC contracts.
  Do not read or duplicate another application's private database in UI code.
- Persist portable engineering configuration separately from local UI/session
  state. Do not write local tabs or layout into project configuration.
- Add behavior-focused tests at the owning boundary and keep full-text
  document snapshots out of policy guards.


## Context and Live Insights contract

`ContextResource` carries stable provider/resource identity, a provider URI, workspace/source identity,
title and serializable provider state. It never serializes widget pointers or domain semantic objects.
Providers own content construction, destruction, capabilities and portable view state; hosts own geometry.
Temporary Editor, Pinloom, Live Insights and Workspace Hub use the registered provider boundary.

Peek has left, bottom and corner resize handles. Width is bounded to 280–920 px and the available editor
width; height is bounded from 220 px to available editor height. Double-click restores provider preferences.
Pin/Unpin transfers the live view. Native docks retain Qt geometry restoration; context state is persisted
through the workspace session service, which imports legacy `.zs` without using it as a new authoritative store.

Live Insight requests carry document/workspace generation and cancellation. Latest valid results win;
pending or failed analysis may retain explicitly stale last-valid content. Stable node identities preserve
compatible layout, selection and source navigation. Kernel, Module, Hotspot and State retain their
specialized surfaces. Fold Shelf, custom Fold Region, static Wave Preview and Wave Simulation are no longer compiled or registered.
Editor folding is derived only from Tree-sitter syntax nodes; comments do not define custom fold ranges.

Workspace Hub groups Source, Pinloom, Wave and RegMap resources. Provider failure affects only its section;
old replies cannot cross workspace/selection generations. Hub selection, groups and view geometry persist.
See [AppSuite integration](docs/suite.md) and [integrations](docs/integrations.md).


## Floating window material

`ContextFloatingWindow` retains its native `Qt::Tool` frame and extends the DWM frame into an
alpha backing store. Windows 11 build 22621+ can provide `DWMSBT_TRANSIENTWINDOW` (Desktop
Acrylic); failed/disabled composition uses an opaque themed paint path. Background tint is
painted separately from content, and the existing floating opacity preference now controls that
tint rather than whole-window alpha. No polling timer or focus-dependent whole-window fade is
used. Theme/system changes refresh the backdrop without replacing the HWND or hosted view.
Transparent surface styling is scoped to the floating host, including dock/toolbars, lists,
tables, text previews and scroll content. Local panel/title/search/graph styles include the same
ancestor rule so they cannot mask the host with a solid fill. `InsightGraphView` skips only its
canvas background while hosted there; scene items, heat colors, grid and export retain their
rendering. Reparenting restores ordinary surfaces without saving or overwriting view styles.
`context_floating_background_test` verifies composition over a two-color backdrop, late-created
content, theme changes and restoration on docking. `context_floating_preview` provides populated
Hotspot Track/Matrix and Kernel panels plus document controls for native desktop verification;
offscreen tests do not prove DWM appearance.

## Deferred panel construction

`DeferredPanel` keeps a stable layout host and creates its content on first show or explicit
workflow access. Search caches its context/service bindings; Change Preview delays its widget;
Connections constructs its two tabs and workflow objects together. Settings loads effective values
through `SettingsCenterService` before constructing its central settings page on first access.
Factories are cancelled with their owner. Reopening reuses the page and its signal connections.
`PanelLayoutController` retains unmaterialized view state and applies it after content creation.
Problems and Activity keep their startup models so diagnostics and unread badges remain live.

## Specialized insight surfaces

`LiveInsightToolPage` retains the shared context, navigation, status and detach lifecycle.
Production pages request specialized surfaces from `RtlInsightWorkbench`. `InsightViewSurface`
adapts the existing typed panels: SignalKernelGraphPanelCoordinator for Kernel and
RtlInsightsPanelCoordinator for Block, Hotspot and State Transition. Panel-owned report
models, nested geometry, FSM layout, Track/Matrix modes, filters, inspector and export
remain authoritative. Embedded docks are content containers, not registered bottom docks.

Module Brief, Signal Journey and Clock/Reset Map report pages and their controls are removed.
The ModuleBriefService and ClockResetDomainService report aggregators are removed as well.
SignalJourneyService remains the data source for SignalKernelGraphService; clock/reset relation
extraction and semantic query services remain shared infrastructure. The tree surface and its
navigation still serve semantic Diff and empty/ready states. Refresh dispatches to the selected
specialized view, preserves hotspot access paths and Track/Matrix mode, and retains immutable
semantic Diff comparisons instead of falling back to a retired report.

A sidebar section renders that same surface rather than a summary of it, so the section and the
full view are one implementation at two sizes. `InsightViewSurface` ignores both size-hint
directions: a hidden stack page keeps the wrapped toolbar height it was last measured at, and an
honored vertical hint would let a page nobody is looking at dictate the workbench minimum height.
Section height and sidebar width stay under the user's drag handles. `ContextViewCapabilities`
carries an opt-in `preferredSectionHeight`; it defaults to 0, which keeps the existing even split,
and only a stored height above zero overrides a provider's suggestion. A view publishes its own
freshness through `contextStatusText` / `contextStatusTooltip` properties, which the host reads
onto the section header; the host never reaches into the view's own chrome.

The source insight Actions retarget and pin the section for their kind
instead of opening a central tool tab, carrying the chosen symbol's member access path. Choosing a
target in the editor is an editor mode, not a dialog: candidates are enumerated from the visible
region only, the first-level decision queries `SemanticIndex` at most once per publication and
caches by snapshot revision, and only the chosen candidate runs the real service. A target that
cannot be rendered reports the service's own reason and leaves the mode running.

`InsightGraphCore` / `InsightCanvas` remain available for generic graph consumers and their
own tests. They are not the mandatory renderer or a lossy intermediate model for specialized
views. Do not replace a native surface until its interaction and representation parity has
been tested through the actual tool-page route. Same-document older revision updates are
rejected; unchanged contexts do not rebuild a specialized view. Theme refreshes stay inside
the panel and preserve its view state.

Long specialized toolbars wrap at control boundaries through `CompactFlowLayout` instead of
imposing their width on the workbench or shrinking button labels. A newly selected surface fits
after its visible geometry settles. Expanded Kernel fanout headers reserve space above their nodes
and remain inside module wrappers.

## Repository layout

Source layout established from `743c1417` (0.25.2). File moves preserve the existing
build targets and unique quoted header names; they do not create new runtime layers.

| Directory | Responsibility |
| --- | --- |
| `src/app/` | Application entry point, main window and export declarations |
| `src/analysis/` | Analysis scheduling, workers and diagnostic publication |
| `src/semantic/` | Slang collection, semantic index, symbol and relationship services |
| `src/editor/` | Editor interaction, rendering, folding, formatting and popovers |
| `src/completion/` | Completion, snippets and templates |
| `src/commands/` | Command registry, search, rename and RTL editing workflows |
| `src/documents/` | Documents, tabs, file synchronization and recovery |
| `src/workspace/` | Workspace lifecycle, projects, transactions and Workspace Hub |
| `src/navigation/` | Source navigation, definition previews and navigation management |
| `src/insights/` | Specialized graph services, panels, workbench and export |
| `src/ui/` | Shared shell, context surfaces, icons, typography and notifications |
| `src/settings/` | Settings and appearance/configuration UI |
| `src/integrations/{pinloom,suite}/` | External application adapters |
| `src/cli/` | Read-only CLI implementation and entry point |
| `components/` | Independently scoped reusable libraries |
| `test_sv/` | Regression tests and referenced HDL fixtures |
| `cmake/` | Build helpers, source-module include paths and policy guards |
| `resources/`, `images/`, `config/` | Fonts, licenses, platform assets and editor resources |
| `schemas/` | Current wire-format schemas; superseded manifest versions stay in Git history |
| `scripts/`, `packaging/` | Reproducible application and suite packaging tools |
| `docs/` | Current technical and maintenance documentation |
| `build/`, `artifacts/`, `.toolchain-build/` | Ignored local outputs and reproducible caches |

Root files are limited to CMake/resource manifests, version metadata and the main
README, architecture, manual and maintenance entry points. Released change history is
kept in the Git history rather than in a root document. Qt resource manifests stay at the
root to preserve their existing resource URLs.

Add new application sources to the appropriate `src/` module and explicitly list
them in `CMakeLists.txt`. A new module must also be listed in
`cmake/source_layout.cmake`. Avoid duplicate header basenames while modules share
quoted include names. Source-policy guards recursively inspect `src/`.

Keep fixtures used by CTest even when their filenames look experimental. Do not
store logs, screenshots, generated executables or package staging copies beside
source files. Retain the active build, the latest release evidence and toolchain
inputs; remove superseded builds and staging copies after release verification.
Historical plans and concept boards are recoverable from Git rather than copied
into additional archive directories.
## Visual system

### Button and checkbox ownership

The standard backend is SuiteUi as of 0.29.23; classic remains a startup fallback.
The ownership details below also describe that fallback. Optional builds with
`ZEROSLACK_ENABLE_QLEMENTINE=ON` also provide a startup-only Qlementine backend and
an isolated preview executable. `ApplicationThemeManager` selects the backend;
the public interface exposes no Qlementine types. The preview keeps the existing
Qt controls and typography, moves standard control drawing to the adapter and
retains explicit shell QSS. Editor, radial-menu and specialized graph-view trees
use the classic style on every descendant, including children created later or
reparented between containers. See [preview validation](docs/control-style-consolidation.md)
for ownership exceptions, profile isolation, the upstream patch and native limits.

`ZEROSLACK_ENABLE_SUITEUI=ON` is the default, mutually exclusive backend that consumes
the independently installed `SuiteUi 0.1.0` static package. Its Qt-only public API
contains control-state colors, a primary-button role and `ControlStyle`.
`suiteuibackend.cpp` maps application roles and colors; Qlementine types remain
inside the SDK. The SDK delegates all controls except QPushButton, QToolButton
and QCheckBox to the application's `RoundedIcons::Style` fallback. It never owns
application theme selection, fonts, data models, shell layouts or professional
renderers. The CLI retains its existing core boundary and indirectly carries the
SDK through `zeroslack_core`; it is not UI-dependency-free. The SuiteApp protocol
SDK remains separate. See [SuiteUi integration](docs/suite.md#suiteui-第-4-阶段独立-sdk-与双应用接入).

`InsightControlStyle` defines the normal, hover, pressed, checked, focus and disabled
states for application push buttons, tool buttons and checkboxes. Application QSS owns
their surfaces and spacing. `RoundedIcons::Style` paints only the checkbox indicator,
including ticks and indeterminate marks; item-view check indicators keep their existing
delegate/style behavior. Neither layer replaces QWidget input or accessibility behavior.

`applyToolbarButton`, `applyPrimaryButton` and `applySegmentedCheckBox` assign roles and
font/style-derived minimum sizes. They do not install local QSS or theme subscriptions.
Settings Apply uses the primary role. Explicit shell controls (title bar and rails) retain
their scoped rules. A constant border width prevents focus transitions from moving content.
State changes are immediate; this consolidation does not introduce animation.

See [control style validation](docs/control-style-consolidation.md) for the application and
floating-host coverage, the six-theme/four-DPI matrix, and the remaining native checks.

### Non-editor typography

The UI uses a proportional sans-serif face (Noto Sans, Segoe UI, Noto Sans SC, Microsoft YaHei UI, then the platform fallback). Code and diffs retain their editor fonts. Symbol popovers use the editor font for names and code types, and proportional UI fonts for labels and actions. Sizes below are logical pixels and follow display scaling.

| Role | Size | Weight | Use |
| --- | --- | --- | --- |
| Page title | 18 | 600 | Settings category title |
| Panel title | 14 | 600 | Dock titles and context headings |
| Body | 13 | 400 | File names, controls, values, menu items, Activity |
| Section | 12 | 600 | Table headers and form/navigation group labels |
| Metadata | 12 | 400 | Descriptions, scope summaries, diagnostic counts |
| Badge | 11 | 400 | Compact unread counters |

Selected tabs use weight 600; unselected tabs and ordinary values remain 400. Supporting text uses the existing secondary text color rather than the disabled color. Light/Dark use softer primary text and distinct secondary text; Catppuccin provides Latte, Frappe, Macchiato and Mocha palettes. Do not add arbitrary letter spacing to file names or Chinese text.

Use `UiTypography::apply` for explicit widget roles and the shared InsightVisualStyle helpers for themed labels. Graph font helpers preserve their supplied graph font and are separate from these UI roles. Reapplying a role is idempotent: metadata must not get progressively smaller on theme changes. Avoid global font QSS rules that override editor-owned popup and code fonts.

Spacing follows a 4 px rhythm: 8 px between related controls, 12–16 px within forms, and 16–20 px between major groups. Tree rows reserve 24 px plus vertical padding; tabs use 8 px vertical and 14 px horizontal padding. Activity paragraphs have 4 px bottom spacing. Navigation explanations occupy a full row above their actions to remain readable in a narrow sidebar.

Validate Light/Dark transitions, proportional glyph metrics, editor font isolation, narrow sidebars, and 125%/150%/200% display scaling. Offscreen Windows previews load installed system UI fonts because the offscreen Qt platform does not provide the native font database; these font files are not bundled or redistributed.

The left Navigation dock keeps its `QMainWindow` docking role so the bottom drawer remains aligned with the editor. Its body and header live in a fixed-width, clipped viewport owned by `NavigationPaneCoordinator`. User-triggered collapse and expansion animate the dock's width and slide that content horizontally; session restore and the welcome-page transition apply visibility immediately. Keep the viewport's minimum width at zero and preserve the last manually resized expanded width, or the dock layout will jump at the end of the transition.


The fixed title row exposes a right-click menu on the file path: Copy full path and
Reveal in Explorer. Display width does not constrain the copied absolute path.
Tab close glyphs are centered within the original hit area and capped at the text scale.
Windows chrome retains native overlapped-window capabilities and exposes HTMAXBUTTON
for the system snap menu while drawing the themed title row.
### Color themes

Settings → Appearance → Color theme includes Catppuccin Latte (light), Frappe,
Macchiato and Mocha (dark), in addition to Light and Dark. The selection uses the
existing settings persistence and live theme-change mechanism.

Colors are from the [official Catppuccin palette 1.8.0](https://github.com/catppuccin/palette)
(MIT). Base/mantle/surface colors define the background hierarchy; text/subtext define
readable primary and secondary text. Blue identifies active controls and focus; red,
yellow and green identify error, warning and success. Semantic and syntax colors use
mauve, peach, green, blue and teal. Graph fills mix the corresponding semantic accent
into the base color. Checked controls use base-colored text over a blue background.

Palette attribution and the full MIT license text are kept with the other third-party
license files, in `resources/catppuccin/LICENSE.txt`.
