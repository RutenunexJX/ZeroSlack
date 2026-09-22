ZeroSlack v0.29.39

Build profile: Shared Release
Qt: 6.10.2
Compiler: MinGW 13.1.0
Default UI: classic controls (SuiteUi is a default-off optional backend, not in this package)
slang: 10.0.14
Suite WaveWorkbench: v0.12.0
Release root: E:\PinloomRoot\AppPackage\AppSuite

Run ZeroSlack.exe. Runtime provides the optional shared SuiteApp broker.
Existing ZeroSlack settings and workspace profiles are retained.
0.29.23 shipped SuiteUi as the default; 0.29.24 reverted it to classic after real
desktop use showed unacceptable control sluggishness. The cause is not yet diagnosed.
WaveWorkbench remains a separate sibling application for simulation and waveform viewing.
ZeroSlack no longer runs simulation, loads wavewidgets.dll, or unpacks a Wave toolchain.
The fixed ZeroSlack-win64 directory and executable names preserve existing shortcuts.

Settings, Search / Replace, Change Preview and Connections create their pages on first use.
Reopening retains their state. Fold Shelf, custom Fold Region, static Wave Preview and Wave Simulation are removed.
The four specialized diagrams and syntax folding remain available.
The old Module Brief, Signal Journey and Clock/Reset Map report pages are removed.
Shared signal relationships, clock/reset analysis and semantic Diff remain available.

Activity replaces the status bar. Scan, semantic and operation messages are
retained in Activity. Important unread messages increment its numeric badge;
viewing Activity marks displayed messages as read. Ordinary progress stays passive.

Navigation and Context remain full-height columns; the bottom drawer occupies
only the area beneath the editor. Drag the sidebar divider to widen charts.
Reopening the sidebar retains its width. Settings opens in a central tab.
Ctrl+2 or View > Context Sidebar toggles the entire Context sidebar without
closing its sections. Showing an empty sidebar opens the first rail provider's
default view and restores a hidden rail. Each insight section renders that
insight's real view instead of a text summary; drag the section boundary or the
sidebar divider to size it, and Follow Editor freezes it on the current result.
Source insight commands retarget and pin the matching
section rather than opening a central tab; the full view stays on the header.
Supported charts can open in the main area. Explicit Peek previews remain available.
Detachable Context resources can float in multiple native windows. Right-click a
Context rail icon to create or focus views, collect them into the sidebar, or
bind them to the current document. The separate rail toggle hides/restores all
floating views without closing them. Document layouts retain up to 32 recent
paths; source editors keep their single overlay. Context sections can be shown
together, collapsed, resized and reordered inside one sidebar. Drag an eligible
section outside to float it; use a floating window's client-area handle to drag
it back to an indicated position. Source editors do not support native drag-out.
State v6 reads v5 with only the old active section expanded; older builds ignore
v6 Context state on downgrade.

Native floating windows use Desktop Acrylic on supported Windows 11 systems.
Appearance > Floating background opacity adjusts only the background tint;
panels, lists, text previews and graph canvas backgrounds share the material.
Text, icons, graph nodes and edges stay opaque; docking restores ordinary backgrounds.
Unsupported systems, disabled transparency, high contrast and battery saver
use the theme's solid background.

The left sidebar groups the file tree with Project and Settings icons at the top.
Click Project for commands. Collapse the entire sidebar and restore it from the title bar.
Click Settings again to close its active central tab; Context icons collapse or
expand their selected sections while preserving view state. Files, Design and Pinloom use uniform rows
with distinct hover and selection backgrounds. Focus Mode has been removed.
Ctrl+Space hides F24-exclusive commands; F24 Command Mode retains those commands.
The themed title bar stays visible in its own row without covering the editor.
Rounded controls and graph nodes share a scalable outline icon system.
Fixed-panel separators and popup outlines use subtle theme-relative borders.
Panel titles use bottom separators; this visual update adds no animation.
The editor context menu uses an icon-only ring and rounded rectangular action
bars. Hover for names and unavailable reasons; gray actions cannot execute.
Escape, the center close button, or an outside click dismisses the menu.
Only Problems and Activity remain permanent in the drawer. Ctrl+F/H reuse the
inline find/replace bar; Ctrl+Shift+F/H open workspace search/replace.

Non-editor typography prefers installed Noto Sans with system font fallbacks,
and uses proportional text with distinct title, body, metadata
and badge sizes. Settings groups and navigation explanations have clearer spacing.
Editor font preferences are preserved. Catppuccin Latte, Frappe, Macchiato and
Mocha are available in Settings > Appearance. Palette attribution and its MIT license
are in resources/catppuccin/LICENSE.txt.

Right-click the title file path to copy its absolute path or reveal it in Explorer.
Windows maximize and keyboard snap retain native window capabilities.

The unified workbench hosts dedicated graph panels, including Hotspot Track/Matrix,
nested module blocks, Kernel filtering/fanout and State Transition controls.

Licensing
---------

ZeroSlack is licensed under Apache License 2.0. A full list of third-party
components and their licenses is in THIRD-PARTY-NOTICES.md.
ZeroSlack's license is in LICENSE; the Qt, embedded font, and other dependency
license texts are in the licenses directory beside ZeroSlack.exe.

This package uses Qt 6.10.2 under the GNU Lesser General Public License v3
(LGPLv3). Qt is not modified and is linked dynamically: its DLLs sit beside
ZeroSlack.exe and may be replaced with your own compatible Qt 6 build, which is
the right LGPLv3 reserves for you. The LGPLv3 text ships with the Qt runtime
files in this package; Qt sources are available from https://download.qt.io.

Sibling applications and their toolchains retain their own license notices. They are not
linked into ZeroSlack or required for its editing and analysis features.
