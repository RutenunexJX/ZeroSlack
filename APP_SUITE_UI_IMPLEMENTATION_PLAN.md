# AppSuite UI Implementation Plan

Status: active implementation plan

Date: 2026-08-25

Repositories remain independent:

- ZeroSlack: `E:/ZeroSlack/ZeroSlack`
- Pinloom: `E:/Pinloom/Pinloom`
- WaveWorkbench: `E:/WaveWorkbench/WaveWorkbench`
- RegMapWorkbench: `E:/RegMapWorkbench/RegMapWorkbench`

The formal package remains outside the source workspaces and is not rebuilt as
part of this goal.

## Scope freeze

This plan turns the recorded “competition UI” direction into a finite delivery.
Completion requires a coherent design system, responsive application shells,
redesigned primary workflows, deterministic interaction tests, and reproducible
visual evidence in all four applications. It does not require replacing Qt,
rewriting stable domain services, inventing new product features, or packaging.

The implementation must also complete the previously recorded functional work:

- Pinloom command grammar and native PDF/Word/Visio/Excel Anchor capture;
- ZeroSlack Context Workspace width/height resizing and persistence;
- ZeroSlack live Module/FSM/Hotspot/Wave insight providers;
- WaveWorkbench-owned reusable waveform rendering for preview and results.

## Shared visual system

Each application implements the same semantic roles locally. No runtime theme
dependency is introduced between otherwise independent products.

### Foundations

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

### Product identity

- ZeroSlack uses cyan/blue for source-to-insight relationships.
- Pinloom uses warm amber for captured Anchors and blue for navigation.
- WaveWorkbench uses teal for actual signals and violet for expected/derived
  state.
- RegMapWorkbench uses indigo for address structure and cyan for RTL sync.

Identity accents never replace shared success/warning/error semantics. Text and
critical diagram edges meet readable contrast in light and dark themes.

### Common shell behavior

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

## Per-application delivery

### ZeroSlack

- Keep the editor as the dominant canvas. Reduce competing chrome and use the
  Context Rail for secondary tools.
- Make Context Peek directly width/height resizable and persist both dimensions;
  keep the native pinned dock resizable and persist its width.
- Add compact Context providers for Module Diagram, State Diagram, Signal
  Hotspot, and Wave Preview. Full interaction opens in an editor-area tool page
  or resizable dock.
- Introduce revision-aware Live Insight sessions, stable visual identities,
  latest-generation acceptance, stale-last-valid behavior, and state-preserving
  refresh.
- Consolidate Module/FSM/Hotspot rendering around shared graph tokens,
  interaction, layout boundaries, and scene-diff behavior.
- Use the WaveWorkbench waveform component for symbolic preview after contract
  parity; retain explicit Symbolic Preview provenance.
- Refine rail, dock headers, insight page headers, status chips, empty states,
  and responsive 960/1440 px layouts without changing editor semantics.

### Pinloom

- Replace whitespace legacy commands with canonical semicolon grammar.
- Preserve the foreground target before `Shift+Space` takes focus.
- Add a visible compact quick-action row for Rectangle Anchor and Text Anchor.
- Route every successful native locator capture through one confirmation dialog
  for name, aliases, tags, and pinned state.
- Add deterministic PDF rectangle/text capture, verified jump completion, and
  document-scoped persistent highlights.
- Add Word bookmark, Visio shape UniqueID, and Excel range/name capture with
  explicit authorization for document mutations.
- Redesign the command surface as query, quick actions, ranked result cards,
  contextual actions, and a compact status footer. Remove decorative bitmap
  dependence from information-bearing areas.
- Apply the shared surface, typography, field, table, badge, empty-state, and
  focus system to Anchor, Clip, Root, Settings, and confirmation windows.

### WaveWorkbench

- Split waveform data, timeline mapping, layout, rendering, and interaction
  from the current canvas into a reusable view layer.
- Add a versioned `wave-preview/v1` payload and a discoverable standalone
  waveform-view factory while preserving the complete workspace v1 ABI.
- Make symbolic preview and simulation result share lane, grid, value, cursor,
  zoom, selection, and theme rendering while retaining mode/provenance labels.
- Replace hard-coded light colors with semantic light/dark tokens.
- Recompose the main window into primary run controls, stimulus/expected canvas,
  actual canvas, and a collapsible review/details region with clear hierarchy.
- Preserve all existing editing, compare, batch, check, trace, and source
  navigation semantics through adapters and regression tests.

### RegMapWorkbench

- Introduce semantic light/dark tokens matching the suite roles while retaining
  RegMap identity.
- Recompose the main workbench into a compact project/address navigation rail,
  register table canvas, bitfield/detail inspector, and diagnostics/sync drawer.
- Add a page header with project identity, dirty/sync state, Generate and Sync
  primary actions, and overflow for secondary commands.
- Improve register/address tables with sticky readable headers, aligned address
  and reset columns, field type/status badges, selection hierarchy, and compact
  empty/error states.
- Improve the bitfield view with proportional bit spans, reserved-field
  differentiation, access/reset encoding, keyboard selection, and linked table
  focus.
- Preserve YAML/RTL/manifest generation, three-way merge, diagnostics, and CLI
  behavior; UI changes consume existing core facts rather than reparsing files.

## Delivery order

1. Commit this scope and the per-repository implementation documents.
2. Implement Pinloom command/capture architecture and visual shell.
3. Implement WaveWorkbench reusable waveform view and theme/shell.
4. Implement ZeroSlack Context resizing, Live Insight sessions/providers,
   graph redesign, and WaveWorkbench integration.
5. Implement RegMapWorkbench shell, theme, table, bitfield, and diagnostics UI.
6. Run per-repository full configured tests and cross-repository ABI/host tests.
7. Capture reproducible light/dark evidence and audit every acceptance item.
8. Commit and push each repository independently. Do not package.

## Verification matrix

- Functional: existing suites plus focused tests for every new state transition.
- Concurrency: rapid input, cancellation, stale completion, close/reopen, and
  application shutdown cannot publish obsolete state or access deleted widgets.
- Persistence: relocation-safe workspace/user settings, dimension bounds, and
  schema compatibility are covered by round-trip tests.
- Responsive: 960x720 and 1440x900 content tests; no clipped primary action.
- Scaling: 100, 125, 150, and 200 percent logical DPI evidence where supported.
- Themes: light and dark screenshots for the primary workflow of each app.
- Accessibility: visible focus, keyboard-only primary flow, non-color status
  labels, accessible names, and contrast checks for text/status tokens.
- Performance: expensive parse, layout, native capture, and trace preparation
  do not run on the UI thread; representative large fixtures retain current
  regression budgets unless an explicitly recorded new budget is stricter.
- Safety: no test writes to user documents or protected examples; native Office
  mutation tests use dedicated disposable fixtures and explicit authorization.

## Completion criteria

- Every per-application checklist is implemented and linked to automated or
  reproducible evidence.
- Pinloom native capture round trips and failure paths are deterministic.
- ZeroSlack visible insights converge on the latest valid editor revision and
  preserve compatible view state.
- Symbolic and simulated waveforms render through WaveWorkbench's shared layer.
- All four primary workflows present the shared visual hierarchy in light and
  dark themes without clipped controls at the defined responsive sizes.
- All configured and cross-repository tests pass without relaxed assertions.
- Each repository is clean except for pre-existing protected user changes, and
  each implementation commit is pushed to its own `origin/main`.
- No application package or AppSuite package is generated.
