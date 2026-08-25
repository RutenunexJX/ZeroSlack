# Context Workspace Resize Implementation

Status: active

## Required behavior

- Transient Peek exposes visible left and bottom resize handles so users can
  adjust width and height independently or together from the corner.
- Peek remains anchored to the editor region's right and bottom edges. Resizing
  never moves it outside the editor region or below the usable minimum content
  size.
- Width is bounded to 280-920 px and available editor width. Height is bounded
  to 220 px through the available editor height. Double-clicking a handle
  restores the provider-preferred size.
- Width and height are persisted in `ContextWorkspaceState` and restored with
  version-compatible defaults. Out-of-range legacy or external values are
  clamped.
- Pinned Context continues to use native dock/splitter resizing. Its width is
  captured after actual dock resize and restored without overriding Qt main
  window state during layout restoration.
- Resize cursors, focus, keyboard Escape, Peek replacement, Pin/Unpin, Full View,
  editor resize, and workspace switch remain deterministic.

## Implementation boundaries

- `ContextPeekHost` owns pointer hit testing and transient resize geometry.
- `ContextWorkspaceController` owns persistence notifications and provider
  preferred-size resets.
- `ContextWorkspaceState` advances schema version while reading version 1.
- Providers declare preferences but never manipulate host geometry.
- No application-wide mouse filter or legacy free-floating drawer is restored.

## Verification

- Unit tests cover clamp/default/round-trip behavior for both dimensions.
- GUI tests drag left, bottom, and corner handles; verify anchoring and bounds;
  verify double-click reset; then close, restore, pin, and unpin.
- 960x720 and 1440x900 tests verify that resize handles remain reachable and
  editor content retains a usable area.
- Existing Context Workspace, panel layout, workspace session, and GUI smoke
  tests remain required.
