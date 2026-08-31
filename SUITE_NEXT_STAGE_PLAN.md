# AppSuite Next-Stage Implementation Contract

## Scope

This stage completes five product-level items without merging repositories or
replacing each application's established workspace layout:

1. ZeroSlack Workspace Hub for source, Pinloom, WaveWorkbench, and
   RegMapWorkbench context.
2. A read-only `zeroslack-cli suite-context` aggregation command.
3. A viewer-independent PDF capture adapter boundary in Pinloom.
4. One shared visual and interaction language, implemented independently in
   each application.
5. Complete WaveWorkbench Scenario lifecycle and RegMapWorkbench per-change
   external-diff decisions.

## Repository ownership

- ZeroSlack: Workspace Hub, suite-context, suite design contract, ZeroSlack UI.
- Pinloom: PDF Viewer Adapter and Pinloom UI.
- WaveWorkbench: Scenario lifecycle and WaveWorkbench UI.
- RegMapWorkbench: external-diff accept/reject and RegMapWorkbench UI.
- SuiteRuntime remains a neutral protocol broker and is not a UI dependency.

Each repository is built, tested, committed, and pushed independently. Formal
packages are produced only after all source commits pass the main-task audit.

## Shared visual and interaction language

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

## ZeroSlack Workspace Hub

The Hub is a side-panel navigation model scoped to the active workspace and
current source selection. It groups Source, Pinloom, Wave, and RegMap items,
shows availability/stale/missing state, previews model surfaces when available,
and invokes stable native deep links for editing. It never reads another
application's private database.

Updates are generation-tagged. Older provider responses cannot replace a newer
selection. Provider absence degrades only that section. Hub visibility, width,
expanded groups, selection, and preview state are persisted.

## `zeroslack-cli suite-context`

The command is read-only and returns the existing `zeroslack.cli/v1` envelope.
It starts from the semantic cache, discovers explicit workspace references to
Pinloom, Wave, and RegMap resources, and invokes public suite/CLI contracts on
demand. Default output is bounded metadata; `--include` selects providers and
`--max-tokens` enforces a deterministic payload budget. Missing applications,
stale IDs, timeouts, and invalid files are returned as structured diagnostics
without failing unrelated sections.

## Pinloom PDF Viewer Adapter

Pinloom stores PDF identity, page, page-space rectangle, rotation, and crop-box
metadata as the authoritative locator. A `PdfViewerAdapter` owns viewer-specific
window discovery, capture sampling, navigation, and cancellation. SumatraPDF is
the first adapter. Screen pixels and zoom are observations, never persisted
anchor identity. The existing annotated-copy presenter remains the stable
highlight path.

Capture is an explicit success/failure/canceled/timeout state machine. Closing
or changing a viewer cancels pending work without blocking the application.
Legacy locators remain readable and are upgraded only on a successful save.

## WaveWorkbench Scenario lifecycle

Create, duplicate, rename, delete, and reorder Scenario operations use stable
IDs, deterministic names, one-step undo/redo, and safe current-selection
fallback. Deleting the last Scenario is rejected. CLI operations and deep links
share the same validation. External reload preserves a selected Scenario by ID
or reports its removal explicitly.

## RegMapWorkbench external-diff decisions

Disk/CLI changes are presented as stable, dependency-aware items. Users can
accept or reject selected changes or all changes after a validation preview.
Accept applies one atomic WorkspaceStore transaction and is undoable. Reject
keeps the Workbench value and records the decision against the observed disk
digest; a newer disk revision invalidates prior decisions. Parent/child changes
cannot produce dangling Blocks, Registers, Fields, or Enum values. Neither path
silently overwrites the disk.

## Verification gates

- focused model, protocol, UI, accessibility, migration, and failure tests;
- Release build and complete repository CTest;
- deep-link and suite-provider matrix;
- large-project performance regression checks;
- `git diff --check`, protected-file audit, and explicit task-file staging;
- formal package SHA-256, English WaveWorkbench docs, CLI smoke tests, Runtime
  health, four provider versions, and Runtime final-provider auto-exit.
