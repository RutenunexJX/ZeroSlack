# Live Insight Visualization Plan

Status: active; packaging is excluded.

## Purpose

Redesign Module Diagram, State Diagram, Signal Hotspot, and Wave Preview as a
coherent live-insight system suitable for sustained RTL editing and formal
product demonstrations. The work must improve information hierarchy and
visual quality without creating a second SystemVerilog syntax or semantic fact
source.

## Product layout

The four insights become Context Workspace providers with three presentation
levels:

- The Context Rail/sidebar provides entries, current scope, freshness state,
  Follow Editor and Pin controls, and a compact thumbnail or summary.
- A full interactive canvas opens as an editor-area tool page and can be split
  beside source code or pinned in a resizable dock.
- A shared details drawer exposes the selected node, edge, port, transition,
  hotspot evidence, signal value, and source-navigation actions.

A complete graph or waveform is not compressed into the narrow sidebar. The
sidebar is the navigation and awareness surface; the editor-area tool page is
the primary analysis surface. Each provider retains independent visibility,
pinning, and Follow Editor state.

## Live update pipeline

All four providers use one revision-aware pipeline:

```text
document edit
  -> syntax generation
  -> latest valid semantic snapshot for the same document revision
  -> insight report
  -> stable-ID visual model
  -> layout diff
  -> scene diff
```

- An edit marks the visible insight as updating immediately. Rebuilds are
  debounced in the 200-400 ms range and run outside the UI thread.
- Results are accepted only when workspace, document identity, revision,
  provider context, and request generation still match. Older work is
  cancelled where possible and otherwise discarded on completion.
- A transient syntax error retains the last valid visualization and marks it
  stale instead of clearing the canvas. The stale state links to the blocking
  diagnostic.
- Hidden providers only become dirty. They rebuild when revealed or pinned,
  avoiding four unconditional layouts after every keystroke.
- A compatible update preserves viewport, zoom, selection, collapsed groups,
  filters, and Follow Editor state. Stable semantic IDs drive model and scene
  diffs so unchanged items do not flicker or move unnecessarily.
- Source-to-visual and visual-to-source synchronization uses the same semantic
  locations and identities as the editor. Name-only matching is not a fact
  source.
- Wave Preview remains symbolic and inexpensive. Full compile and simulation
  are explicit user actions and never run on each source edit.

## Shared visualization architecture

ZeroSlack introduces a common path instead of maintaining three unrelated
manual scene implementations:

```text
InsightReport
  -> InsightGraphModel
  -> LayoutStrategy
  -> SceneDiff
  -> InsightCanvas
```

The common canvas owns zoom, pan, fit, search, minimap, selection, source
navigation, accessibility, theme tokens, export, and presentation mode.
View-specific strategies remain responsible for domain layout:

- Module Diagram uses EDA-style module cards, left/right port placement,
  orthogonal nets, bus-width badges, hierarchy collapse, and crossing-aware
  routing.
- State Diagram distinguishes initial, terminal, dead, unreachable, and
  currently selected states; transition conditions can be grouped and expanded
  without duplicating edges.
- Signal Hotspot presents Track and Matrix as switchable views rather than two
  permanently competing canvases. A consistent heat scale, ranked evidence,
  and the details drawer explain every score.
- Every renderer uses shared typography, spacing, state colors, selection,
  diagnostics, and source-location affordances. Hard-coded light-theme colors
  are not permitted.

Qt Graphics View may remain as the rendering substrate where it is adequate;
the redesign concerns the model, layout, scene diff, interaction, and visual
system rather than replacing Qt solely for appearance.

## Wave Preview ownership

WaveWorkbench becomes the owner of waveform rendering and interaction for both
symbolic preview and simulated results. ZeroSlack remains the owner of
SystemVerilog semantic analysis and symbolic-value derivation.

- WaveWorkbench provides a standalone, embeddable, theme-aware waveform view
  through a versioned capability and factory while preserving the existing
  complete simulation-workspace ABI.
- ZeroSlack publishes a neutral `wave-preview/v1` payload containing timebase,
  lanes, symbolic segments, unknown/provenance state, stable signal identities,
  and portable source links. WaveWorkbench does not parse SystemVerilog to
  reconstruct these facts.
- The view accepts generation-tagged full replacement first; bounded incremental
  updates may be added only after replacement semantics and cancellation are
  verified.
- Symbolic Preview and Simulated Result use the same waveform visual language
  but retain explicit labels and provenance so inferred values cannot be
  mistaken for simulator output.
- ZeroSlack's local `WavePreviewCanvas` is removed only after the shared view
  reaches interaction, theme, performance, accessibility, and contract parity.

## Delivery sequence

1. Add provider sessions, freshness state, revision/generation guards, stable
   visual models, and Context Workspace entries while adapting the current
   renderers temporarily.
2. Introduce the common graph canvas and migrate Module Diagram, State Diagram,
   and Signal Hotspot one at a time, preserving source synchronization and
   view state.
3. Add the reusable WaveWorkbench waveform-view contract, theme system, and
   shared renderer; integrate `wave-preview/v1` in ZeroSlack.
4. Remove superseded ZeroSlack canvases only after parity tests, then complete
   responsive layout, presentation mode, export, and contest evidence.

Each repository is audited, tested, committed, and pushed independently. The
formal AppSuite package is updated only on an explicit packaging request.

## Acceptance criteria

- Module Diagram, State Diagram, Signal Hotspot, and Wave Preview are available
  from one sidebar group and can open as full, resizable editor-area views.
- Visible insights converge on the newest valid document revision after edits;
  stale results never replace a newer result, and transient syntax errors retain
  the last valid canvas with an explicit stale marker.
- Undo/redo, rapid typing, file switch, workspace switch, hidden/reopen, scope
  change, and Follow Editor/Pin transitions have deterministic automated tests.
- Compatible refreshes preserve viewport and selection and avoid full-scene
  flicker. Expensive analysis, layout, and waveform preparation do not block
  the UI thread.
- All graph types share theme tokens, navigation, minimap, fit/search, source
  synchronization, and SVG/PDF export behavior.
- Symbolic Preview and Simulated Result render through WaveWorkbench's shared
  waveform layer and are visibly distinguished by mode and provenance.
- Cross-repository tests load the real shared library, validate capabilities
  and malformed/stale payload rejection, and verify source navigation in both
  directions without weakening the existing simulation workspace contract.
- Dark/light theme, 100/125/150/200 percent scaling, 960 px compact layout,
  1440 px normal layout, and representative large RTL fixtures receive
  automated/offscreen checks plus reproducible visual evidence.
