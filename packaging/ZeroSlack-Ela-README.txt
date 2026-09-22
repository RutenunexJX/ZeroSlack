ZeroSlack Ela Windows package

This is the independent Ela branch application. Launch ZeroSlack-Ela.exe.
It does not replace the classic formal package. The fixed package directory is
ZeroSlack-Ela-win64. Consult build-info.json for the product version, release
channel, exact commit and branch. Formal packages have dirty=false and use
the release tag ela-vX.Y.Z.

Release 0.29.40 hides the welcome page's duplicate Project/Settings rail while the
left sidebar is visible and restores it when the sidebar closes.
It includes all migration stages below, including the shared panel
compositor. Historical development-version labels describe when each feature was added.

Settings use an isolated INI profile under Qt's AppConfigLocation for
ZeroSlack/ZeroSlack-Ela. Recent workspaces, editor settings and workspace sessions
do not share the classic application's user profile. Workspace-local files are
still shared if you explicitly open and change the same workspace.

Use ZeroSlack-Ela.exe --ui-style=classic for the original controls in this same
isolated profile. Changing the backend requires restarting the application.

Ela controls: welcome page, settings, workspace configuration, navigation filters, Problems,
Activity, workspace hub and context panel controls. Existing window management,
editor, specialized diagrams and their data models remain in use.
The 0.29.26 development build also uses Ela tree rendering and tab bars.
Tree item models, unsaved-document confirmation and split/drag controllers are retained.
The 0.29.27 development build adds specialized toolbar/form controls and radio buttons.
Diagram renderers, transactions and undo are retained. Version 0.29.28 adds Ela inputs and
action buttons to common dialogs, editor search, Peek and floating-panel controls.
Qt retains modal results, cancellation and window frames. Version 0.29.29 adds Ela menus
and list/table rendering while preserving action ownership, item models, editing delegates
and keyboard behavior. Native file pickers and custom radial menus remain unchanged.
See build-info.json for the release channel.
Version 0.29.30 adds 160ms interruptible wheel scrolling to settings and Workspace Hub,
tree expansion transitions to Files/Design/Workspace Hub, and horizontal tab scrolling.
Precision touchpad scrolling is immediate. Document selection, close confirmation and
split ownership remain with the existing controllers.
Version 0.29.31 adopts ElaAppBar title layout and window buttons, keeping native
snap/resize handling, the title path menu, sidebar expansion and document close policy.
Canceling unsaved-document confirmation keeps the window open. ElaWindow is not used.
Version 0.29.32 adds ElaToolButton to the left navigation controls, right context
rail and bottom panel bar. Existing icons, action state, repeat-click collapse,
context menus and status badges are retained. Project remains icon-only.
Version 0.29.34 adds ElaText headings, descriptions, state labels and form captions,
preserving host fonts, semantic/disabled colors, selection, links and mnemonic buddies.
Workspace Configuration also uses ElaScrollArea with nested view height adaptation.
Version 0.29.35 hosts the whole left sidebar in ElaNavigationBar, including its
Project/Settings header and Files/Design content. Ela owns display modes, width
animation and inner layout; the existing Qt dock stores its workspace position.
Compatibility patch 16 documents the custom-content and interruptible-motion APIs.
Version 0.29.36 reduces repeated layout/presentation work during sidebar motion.
Compatibility patch 17 batches the animated width into one constraint update;
unchanged visible editor lines retain their diagnostic and semantic selections.
Visible text uses a bounded layout cache; scrolling out releases cached shaping data.
Version 0.29.37 uses Windows DirectComposition for sidebar transition presentation.
Ela retains display modes and timing; the live editor resizes only at boundaries.
Temporary images are never scaled and are released after the transition. Input
settles the layout first. Device creation runs in the background; the raster fallback
also avoids per-frame document layout. No additional UI runtime is bundled.
Version 0.29.38 paints the live layout and restored focus before detaching the
transition visual, which now attaches directly above the host's raster surface.
The complete Ela sidebar frame is captured so its background and border match
the live endpoint, including transitions started with a collapsed sidebar.
Version 0.29.39 reuses the compositor for the right Context sidebar, bottom drawer
and Context section folding. Content moves and clips without text scaling or
per-frame editor layout. The bottom button bar stays fixed; reverse clicks continue
from the current position. Different panels share one presenter, and restored
layouts apply immediately. Specialized graph and editor components are retained.
Version 0.29.33 adopts ElaScrollArea and read-only ElaPlainTextEdit for settings,
stacked/detail panels, Activity, Pinloom previews, diffs and recovery previews.
Existing fonts, palettes, selection/copy and document/scroll state are preserved.
The code editor remains specialized. Scrollbars appear only when needed.

The application links Qt and ElaWidgetTools dynamically. Keep the distributed
DLLs and platform plugins with the executable. Do not substitute another Qt
version: this Ela source uses private Qt 6.10.2 headers.

Licenses and exact Ela provenance/patches are under licenses/. See also LICENSE
and THIRD-PARTY-NOTICES.md. ElaWidgetTools is MIT; its unmodified Font Awesome
Free Solid 6.7.2 font is SIL OFL 1.1. ZeroSlack remains Apache-2.0.
