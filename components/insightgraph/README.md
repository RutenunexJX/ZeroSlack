# Embedded insightgraph

ZeroSlack consumes this first-party component through the in-tree targets
`insightgraph::core` and `insightgraph::render`. It has no install package,
external source checkout, or independent semantic service.

## Production boundary

`core` depends only on QtCore. It owns the existing neutral graph value types,
snapshot store/diff/fingerprint, FSM layout and scene geometry, hierarchy/FSM
projection, Trace branch projection, Heatmap aggregation/filter/sort/CSV and
distribution coordinates, and Feedback presentation of classifications supplied
by the semantic producer. The FSM algorithm, direction, ordering, and logical
unit defaults are preserved. A host may supply the existing text-width callback;
no algorithm queries a screen or initializes a GUI.

`render` depends on QtCore, QtGui and QtSvg. It preserves the existing synchronous
callback rendering, format checks, SVG/PDF/PNG encoders and QSaveFile atomic
replacement. QtGui font/render tests run separately with an offscreen platform.
Core consumers do not link the render target or initialize QGuiApplication.

Neither target includes or links ZeroSlack private headers, semantic/document/UI
DLLs, QtWidgets, Ela or WebView2. Public headers contain no AST/index, QWidget,
QGraphicsItem, editor, controller, or mutable host-business object. The targets
reuse the Qt installation selected by the parent project.

The host remains responsible for RTL extraction and classification, source
validation, source navigation, captured semantic tokens, requests, cancellation,
stale results, workspace/instance identity, pagination caches, selection/history,
saved sessions and UI. The component never reparses RTL or adds a second
authoritative cache. `InsightGraphCore` owns one inherited `GraphStore`, replacing
the previous store at the same lifetime boundary.

## Host consumers

- `src/insights/fsmgraphlayout.cpp` maps the existing FsmGraph through
  `InsightGraphAdapter::fsm` and calls the single component layout implementation.
  `RtlInsightsGraphSceneMapper` retains drawing, theme/font selection and source
  navigation. Layout results retain the original `RtlInsightCodeLink` identity.
- `InsightGraphCore` retains report adapters. FSM/hierarchy projections and the
  store are component implementations consumed by the existing workbench/plugins
  and canvas. The Module entry still opens Design; no module graph UI is restored.
- `SignalTraceHeatModel` retains original evidence and exposes Qt model roles;
  component row indices refer back to this original evidence. Its color palette
  stays in the host; `SignalTraceDistributionDelegate` is a separate host file.
- `SignalTracePanel` uses projected branch paths/status and Feedback labels.
  Original page groups, edges and full source evidence stay available to navigation
  and selection using the unchanged indices/order. Semantic classification does
  not move into presentation.
- `GraphExportSource` retains QPointer/owning-thread capture of QWidget and
  QGraphicsScene. `GraphExportService` forwards only the render callback boundary.

## Compatibility and tests

Old host include paths are thin adapters. `include/zeroslack/semantic/rtlinsightlink.h`
forwards the existing neutral CodeLink definition and retains its helper functions.
Paths and anchors are copied unchanged. Existing stable ID normalization is
preserved solely inside the original ID algorithm; source anchors are not
normalized. Native revision/generation values stay quint64. No wire protocol,
JSON encoding, persistent format, graph mode, shortcut or UI entry is added.

TrackLane/TrackPosition remain in the existing hotspot service and its consumers.
Generic boundary-port aggregation is not implemented and has no invented fields.

With BUILD_TESTING, `insightgraph_core_test` and `insightgraph_consumer_test` link
only `insightgraph::core` and run without an application object. The render test
links only `insightgraph::render`. The root host migration test additionally
compares real semantic output, layout/anchors, heatmap/CSV/distribution, feedback,
session identity and exports against the frozen B binaries. Existing host
lifecycle/navigation and B DPR tests remain required.
