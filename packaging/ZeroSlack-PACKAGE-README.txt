ZeroSlack v0.29.8

Build profile: Shared Release
Qt: 6.10.2
Compiler: MinGW 13.1.0
slang: 10.0.14
Suite WaveWorkbench: v0.12.0
Bundled Wave toolchain: Verilator 5.050, MinGW 13.1.0, GNU Make
Release root: E:\PinloomRoot\AppPackage\AppSuite

Run ZeroSlack.exe. Keep the Runtime, WaveWorkbench, and Toolchain directories
beside the ZeroSlack-win64 directory under AppSuite\Apps. Runtime provides the
shared SuiteApp broker; WaveWorkbench provides waveform views and simulation;
Toolchain contains wave-toolchain-v1.zip and wave-toolchain-bundle.json. The
first simulation verifies and extracts this bundle into the local
application-data cache; later runs reuse it. The target computer does not need
a separate Verilator or C++ compiler installation or system PATH changes.

The package directory and executable names remain fixed so existing Windows
shortcuts continue to work across upgrades. Package, toolchain, build-cache,
staging, and Verilator object directory names supplied by this release contain
no spaces. User source paths remain supported through quoted process arguments.

External Verilator and compiler executables may still be selected under
Settings > Simulation. Empty settings prefer an existing expanded portable
toolchain, then the verified bundle cache, environment variables, and PATH.
Legacy standalone ZeroSlack packages remain supported.

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
Source insight commands and the Wave command retarget and pin the matching
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

This package uses Qt 6.10.2 under the GNU Lesser General Public License v3
(LGPLv3). Qt is not modified and is linked dynamically: its DLLs sit beside
ZeroSlack.exe and may be replaced with your own compatible Qt 6 build, which is
the right LGPLv3 reserves for you. The LGPLv3 text ships with the Qt runtime
files in this package; Qt sources are available from https://download.qt.io.

The bundled Wave simulation toolchain contains Verilator 5.050 (LGPLv3 or
Artistic License 2.0), the MinGW-w64 GCC 13.1.0 runtime (GPLv3 with the GCC
Runtime Library Exception) and GNU Make (GPLv3). They run as separate programs
and are not linked into ZeroSlack. Each keeps its own license files inside the
toolchain directory.
