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
compatible layout, selection and source navigation. Module, FSM and Hotspot use shared graph presentation;
Wave preview delegates rendering to WaveWorkbench while ZeroSlack retains symbolic facts.

Workspace Hub groups Source, Pinloom, Wave and RegMap resources. Provider failure affects only its section;
old replies cannot cross workspace/selection generations. Hub selection, groups and view geometry persist.
See [suite workflows](docs/suite-workflows.md) and [wave simulation](docs/wave-simulation.md).
