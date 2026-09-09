# AppSuite visual and interaction contract

The four applications implement semantic roles locally; no shared UI runtime is required.
These are maintenance constraints, not a mandate to redesign every application shell.

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
