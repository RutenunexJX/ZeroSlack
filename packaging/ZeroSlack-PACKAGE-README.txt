ZeroSlack v0.31.13 Windows package

Ela is the maintained UI backend. Launch ZeroSlack.exe. The fixed package directory is
ZeroSlack-win64. Consult build-info.json for the product version, release
channel, exact commit and branch. Formal packages have dirty=false and use
the release tag vX.Y.Z.

Release 0.31.13 restores the visible ZeroSlack name and version at the top left,
beside the workspace icon strip. Opening or switching files preserves this app
identity, with room for workspace icons and window controls in narrow windows.

Release 0.31.12 refines workspace icons, Files/Design navigation, sidebar toggling
and the icon-only More toolbox with hover names and persistent pinning. Splitter
allocation and navigation teardown during theme changes are fixed. Windows saves
recover from transient read leases with bounded atomic retries and renewed external
change checks; persistent locks and conflicts preserve unsaved local content.

Release 0.31.11 also fixes focused native-list teardown discovered by the xIPs
integration checks. Ela patch 28 keeps the view style alive until Qt finishes
clearing focus and destroying the viewport. The shared browser ABI remains p27.

Release 0.31.10 uses interruptible ElaDrawerArea transitions for the right sidebar,
bottom drawer and context-panel folding. Qt QSplitter now owns parallel-panel
resizing. Combo boxes restore Ela popup and indicator animations without blocking
selection or dismissal; ordinary views use smooth wheel scrolling and tree expansion
accepts new input immediately. Editor background caching reduces large-window
sidebar repaint cost. Menus use Ela's reveal while accepting input immediately.
Includes Ela patches 26-27 with unchanged MIT/OFL attribution. Embedded xIPs
requires the matching Ela patch-27 native browser ABI.

Release 0.31.9 adopts Ela sidebar display modes and overlay slides, and immediate
tab floating/merging through Ela's gesture routing. Context windows use ElaDockWidget
and Qt dock previews/animations instead of the 0.31.7 landing snapshot. Content state,
workspace ownership and parallel-panel sizes are preserved. Settings and bottom-panel
page switches use ElaCentralStackedWidget. The original circuit-Z icon is retained
with its two accidental transparent holes repaired. Includes Ela patch 25.

Release 0.31.8 uses Ela lists for completion candidates, ElaDialog for common input
and configuration windows, ElaContentDialog for confirmations, and ElaToolTip for
ordinary hover hints. Completion selection maps proxy/source indexes correctly.
Keyboard acceptance, cancellation, unsaved/conflict decisions and interactive symbol
cards are preserved. Tooltips wrap long paths without taking focus; dialog masks
are dismissed immediately on close. Includes Ela compatibility patch 24.

Release 0.31.7 previews floating-window dock targets without changing the editor layout.
Releasing the window transfers the existing content with a 200 ms snapshot transition;
text and diagrams move without per-frame scaling. Bottom context panels occupy their
own full-width row above Problems/Activity. Cancellation, interruption and fallback
preserve document, cursor, scrolling and undo state.

Release 0.31.6 fixes checkbox indicators after theme changes and refreshes Problems
when its drawer reopens or diagnostics arrive during the reveal animation.

Release 0.31.5 simplifies context-window title bars to the title, optional Fit and Close.
Dragging the title moves the window or docks it in the sidebar/bottom area; double-click
maximizes or restores it. Edge resizing remains available. Canceling a title drag restores
the previous dock visibility without moving or reconstructing the content.

Release 0.31.4 makes context windows frameless through Ela while preserving title-bar
controls, drag, DPI-aware edge resizing and workspace ownership. Module diagrams use
a borderless canvas, with the collapse arrow, breadcrumb row and full-view icon removed.
Fit, mouse-side-button history and source navigation remain available.

Release 0.31.3 replaces the former classic package with the maintained Ela application.
The executable, window title and shortcuts use the ZeroSlack name. Only the current
local package is retained; no backups or ZIP archives are produced. The existing
ZeroSlack/ZeroSlack-Ela configuration directory remains the storage identity so
settings, recent workspaces and sessions continue without migration.

Release 0.31.2 merges context-window titles and actions into a single Ela title bar.
The outer Context dock container no longer floats. The shared temporary editor now
uses the same Ela window, retaining pinning, navigation and document ownership.
Legacy floating dock layouts migrate to individual windows. Source-editor wallpaper
retains an opaque theme base instead of inheriting the generic transparent panel rule.

Release 0.31.1 removes the Workspace Hub summary panel, right-rail entry and
dedicated background refresh pipeline. Old Hub layout and view-state records are
discarded during session restoration. Independent source, analysis and Pinloom
panels, shared associations and the read-only suite-context CLI remain available.

Release 0.31.0 shows the complete module hierarchy, uses dashed unreachable instances
and matching colors at each depth, and synchronizes double-click navigation with source.
Fit is in the panel header; the old follow/pinned row, search, folding, zoom buttons
and more menu are removed. Context panels tile with adjustable dividers in both docks.
Editor backgrounds offer Resting, Peekaboo, Balancing, custom images and None under
Settings > Appearance. Opacity is global and saved with Apply Global. Backgrounds stay
fixed behind source text, including folded, split and floating editors. No ZIP is produced.

Release 0.30.3 uses ElaWidget for context floating windows and Ela's window-scoped
Acrylic material. Window controls, resizing and theme changes use the Ela path;
unsupported systems or disabled transparency fall back to an opaque background.
Pinning, content ownership and workspace behavior are preserved. Editor tab windows
and double-click symbol cards are outside this change. The current package includes the
Ela MIT license, Font Awesome license and all bundled local compatibility patches.

Version 0.30.2 adds Ela breadcrumbs and a compact toolbar to module block diagrams.
Nested module boxes size to their text and children, wrap with the viewport width,
and shrink when folded. Folding and navigation use instance paths. Source jumps
are separate from entering modules; the module instance table and inspector are removed.

Version 0.30.1 uses Ela's local hosted-tab extension for tab drag/drop, split targets
and floating editor windows. Closing a floating container returns its tabs to the
main window; closing a tab still checks locks and unsaved changes. Context floating
windows introduced ElaAppBar and hosted drag handles; 0.30.3 uses the ElaWidget shell.
Shared documents, undo, workspace
ownership and TEMP isolation remain application-managed.

The workspace picker shows full paths, unsaved markers and
per-workspace close decisions. Switching retains live buffers, the active file,
cursor and scroll positions. External/untitled files are marked TEMP, remain
available across switches and keep analysis, recovery and session ownership
separate from the active project. Saving writes back to the original file.
Source files can also be opened through application arguments. Project/Settings
icons remain only in the expanded sidebar; the collapsed title bar has the picker.
It includes all migration stages below, including the shared panel
compositor. Historical development-version labels describe when each feature was added.

Settings continue using the INI profile under Qt's AppConfigLocation for
ZeroSlack/ZeroSlack-Ela. The historical storage name is retained for compatibility;
renaming the executable does not reset or replace this profile. Workspace-local
files remain with their workspace.

The classic, Qlementine and SuiteUi paths remain historical compatibility code;
they are no longer maintained or included in current release validation.

Ela controls: welcome page, settings, workspace configuration, navigation filters, Problems,
Activity and context panel controls. Existing window management,
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
