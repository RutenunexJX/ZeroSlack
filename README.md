# ZeroSlack

Current version: `v0.31.1`

Maintenance policy (2026-09-22): Ela is the only maintained UI version.
Future implementation, validation and releases target `ZeroSlack-Ela`; classic and
Qlementine/SuiteUi paths are historical compatibility code and are no longer maintenance targets.

Repository navigation: [source and file categories](ARCHITECTURE.md).

ZeroSlack is a Qt 6 desktop environment for navigating, understanding, and
editing SystemVerilog workspaces. It combines an incremental editor syntax
model with workspace-wide semantic analysis, relationship views, diagnostics,
and preview-first RTL editing workflows.

Context Workspace provides independent source, analysis and Pinloom panels.
The Workspace Hub summary panel has been removed. Explicit Pinloom,
WaveWorkbench and RegMapWorkbench associations remain available to automation:

```powershell
zeroslack-cli suite-context <workspace> --file rtl/top.sv --line 42 `
  --symbol dma_ready --include pinloom,wave,regmap --max-tokens 4000
```

Wave and RegMap associations use `.zeroslack/suite-references.json`; its schema
is `schemas/suite-references-v1.schema.json`.

## Capabilities

This section is an index. Each row states what ZeroSlack does in one line; the detail,
including every shortcut and edge case, lives in the linked chapter or contract document.

| Area | Capability | Detail |
| --- | --- | --- |
| Shell | An empty workspace opens to a compact start page with the Open Project icon and recent project paths; Navigation and Context use full-height columns when editing, with a drawer under the editor and no status bar. | [manual §3](用户手册.md) |
| Shell | Non-editor text follows a proportional typography hierarchy with distinct headings and metadata. | [architecture, visual system](ARCHITECTURE.md) |
| Editing | Editor backgrounds include Resting, Peekaboo and Balancing illustrations, custom images, an off option and opacity control; artwork stays fixed beneath the code while scrolling. | [manual §17](用户手册.md) |
| Panels | Problems and Activity are permanent; Search and Change Preview use the drawer, while Connections opens in a central tab. The optional pages are created on first use and retain their state on reopen. Activity counts unread important messages and clears on open. | [manual §3.3](用户手册.md) |
| Gutter | The number lane sizes itself by line count, diagnostics and Pinloom links share one marker lane, and folding sits beside the code. | [manual §3](用户手册.md) |
| Workspaces | Open, switch, close, rename and revisit multiple workspaces; configure include dirs, defines, ignored dirs, source extensions and the active top module. | [manual §4](用户手册.md) |
| Workspaces | Cached files appear immediately and are reconciled with the directory in the background, so changes made while a workspace was inactive are found without re-analyzing unchanged files. | [manual §4.1](用户手册.md) |
| Sessions | Tabs, layout, navigation filters and scan state restore from local application storage; a workspace `.zs` file is only a read-only legacy import source. | [manual §4.3](用户手册.md) |
| Editing | Incremental highlighting, folding, structural navigation, formatter with ordinary and type-parameter alignment, multi-cursor and column operations, split views, templates, and line/selection operations including triple-click line selection. | [manual §5–§6](用户手册.md), [§13](用户手册.md), [§14](用户手册.md) |
| Editing | The editor context menu is a compact icon-only ring; unavailable actions stay gray and explain why on hover. | [manual §6.1](用户手册.md) |
| Completion | `Ctrl+Space` opens one palette at the caret for scoped symbols (enum values included), templates and application commands, with horizontal category switching and `m <filter>` module lookup. | [manual §7](用户手册.md) |
| Command layer | `F24` is an explicit command search layer: Enter runs a fuzzy result, `F24+D` deletes the selection, and an empty tap repeats the last valid repeatable action. | [manual §8](用户手册.md) |
| Semantics | Slang-backed symbols, diagnostics, definitions, references, relationships, hierarchy, hover and effective compile-time values from the current workspace snapshot. | [manual §10–§11](用户手册.md) |
| Semantics | A clean `Ctrl+S` is a true no-op; changed saves classify their impact and schedule only the needed work off the UI thread, and trivia-only edits never invoke Slang. | [manual §20](用户手册.md) |
| Semantics | Unsaved comment and whitespace edits recover semantic availability after 250 ms of idle time when worker validation proves equivalence; other unsaved edits still require saving. | [manual §20](用户手册.md) |
| Search | `Ctrl+F` / `Ctrl+H` use an inline editor bar; `Ctrl+Shift+F` / `Ctrl+Shift+H` open workspace search and guarded replace. | [manual §12](用户手册.md) |
| Context | The right-side rail opens sections that tile vertically in the sidebar or horizontally in the bottom area, resize at shared dividers, float into native windows, or hide together with `Ctrl+2`. | [manual §5.4](用户手册.md) |
| Context | Temporary source editing uses one in-editor Peek that pins into the sidebar without losing the live editor, undo state, search history or restore identity. | [manual §5.4](用户手册.md) |
| Insights | State-transition and FSM views, nested module block diagrams, signal-kernel graphs, and usage hotspots with Track/Matrix views. | [manual §15](用户手册.md) |
| Insights | Each insight section renders the real view through the same surface the full view uses; source Actions retarget and pin the matching section instead of opening a central tab. | [manual §3, §16](用户手册.md), [architecture](ARCHITECTURE.md) |
| RTL edits | Preview-first rename, connection transform, expose-to-top, scoped replace, instance-pair connection and multi-signal propagation, including module-port synchronization across all instances in one Change Preview transaction. | [manual §17](用户手册.md) |
| Suite | Independent context panels and versioned cross-application associations remain available through public contracts and the read-only CLI. | [AppSuite integration](docs/suite.md), [manual §20](用户手册.md) |
| Settings | Themes including Catppuccin, fonts, shortcuts by Action ID, annotations, analysis, integration and layout; every category except the theme can be scoped globally or per workspace. | [manual §18](用户手册.md) |

Ownership boundary: native documents and external application data stay with their owners.
External application resources use the Suite integration boundary. ZeroSlack does not run simulation,
load waveform-rendering libraries, read another application's database, or include its private headers.
Checks that have never been run on a real desktop are listed in
[unverified manual checks](docs/unverified-manual-checks.md).

## Suite application protocol

ZeroSlack is a `suite-app/v1` provider. It resolves
`zeroslack://source?file=...&line=...&column=...` and
`zeroslack://symbol/<stable-id>?workspace=...`, exposes the
`zeroslack.source.reveal` and `zeroslack.symbol.reveal` actions, and publishes the model Surface
`zeroslack.source.preview`. The adapter uses the neutral `SuiteApp::suiteapp`
SDK; it does not read another application's database or include another
application's private headers.

The optional Runtime is located through `SUITEAPP_RUNTIME_EXECUTABLE`, a local
or sibling `Runtime` directory, or `PATH`. If it is absent, ZeroSlack continues
to run normally and only suite discovery is unavailable. The complete contract
and cross-application verification record are in
[AppSuite integration](docs/suite.md).

## Read-only AI CLI

`zeroslack-cli.exe` reuses the Slang semantic pipeline without opening a GUI.
It emits versioned JSON, JSONL, or Markdown for workspace summaries, source
context, stable symbols, Pinloom code-link metadata, impact graphs, Git changes,
and token-bounded AI bundles. Its semantic cache is stored in the user cache
directory and never in the RTL workspace.

```powershell
zeroslack-cli scan <workspace>
zeroslack-cli summary <workspace>
zeroslack-cli context <workspace> --file rtl/top.sv --line 120
zeroslack-cli bundle <workspace> --query dma --max-tokens 6000 --format markdown
```

The complete command and cache contract is documented in
[integrations](docs/integrations.md).

## Build, run, and test

Requirements are CMake 3.27 or newer, a C++20/C99 toolchain, Qt 6.10.2 with Core,
Gui, Widgets, WidgetsPrivate, Concurrent, Svg, and Test modules, Ninja or another CMake
generator, and initialized `thirdparty/slang`, `thirdparty/tree_sitter`, and
`thirdparty/tree_sitter_systemverilog` sources.

```powershell
git submodule update --init --recursive
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<qt-prefix> -DZEROSLACK_ENABLE_ELA=ON
cmake --build build/local --target demo zeroslack_cli
build/local/ZeroSlack-Ela.exe
build/local/zeroslack-cli.exe --help
$env:ZEROSLACK_TEST_UI_STYLE = 'ela'
ctest --test-dir build/local --output-on-failure -j1 -E 'classic|qlementine'
```

## Panel lifecycle

Settings, Search / Replace, Change Preview and Connections construct their heavy widgets only
on first use. Settings values and workspace context remain available before opening Settings.
Hidden drawer state is retained until its page exists; closing and reopening reuses that page.
Problems and Activity retain their lightweight models and badge updates. Specialized insight
surfaces remain provider-created on demand. Fold Shelf, custom Fold Region, static Wave Preview and Wave Simulation are removed;
syntax folding and the four specialized diagrams remain available.

The retired Module Brief, Signal Journey and Clock/Reset Map report pages are removed.
Shared signal-relationship analysis, clock/reset facts and semantic Diff rendering remain available.

## Versioning and release

`VERSION` is the single manually maintained product version source and must contain exactly
one SemVer value in strict `X.Y.Z` numeric form. Current controlled baseline: `v0.31.1`.
CMake generates `generated/version.h`, which supplies the application title/status version and
the GUI tests. `version_documentation_guard` checks the generated header and the version
markers in this README, the user manual and the package README.

- Every accepted code delivery increments the product version before it is pushed or packaged;
  use at least a `PATCH` increment even for a defect-only delivery.
- `PATCH`: defect fixes and quality improvements to existing behavior.
- `MINOR`: complete new user features or user workflow extensions.
- `MAJOR`: stable compatibility commitments. Before `1.0.0`, ZeroSlack stays in the `0.x` line.
- Dependency versions are tracked separately; the user-visible product version carries no
  dependency labels.

Release steps: update `VERSION`; reconfigure CMake so `generated/version.h` is regenerated;
build and run the Ela release verification targets; create a signed-off release tag named
`ela-vX.Y.Z`. Stage with `scripts/package-ela.ps1 -Formal`, then publish the verified
`ZeroSlack-Ela-win64` directory under `E:\PinloomRoot\AppPackage\AppSuite\Apps`.
The executable remains `ZeroSlack-Ela.exe`; the package directory never includes the version,
so existing shortcuts remain valid. Releases no longer create ZIP archives. The product
version is recorded in `VERSION`, the application display, guarded documents and the tag.
The former classic release channel is no longer maintained.
The current Ela release is `0.31.1` (`ela-v0.31.1`). Workspace Hub and its dedicated
background refresh pipeline are removed; legacy Hub layout records are discarded during
session restoration. Independent panels and shared cross-application contracts remain.
Editor backgrounds offer Resting,
Peekaboo, Balancing and custom images, with global opacity settings and a None option.
The image stays fixed behind source text in normal, folded, split and floating views.
Context floating windows use
ElaWidget for window controls, hit testing and resizing, with window-scoped Ela Acrylic
that follows the application theme. Unsupported systems and disabled transparency use
an opaque fallback; content, pinning and workspace ownership retain their existing behavior.
Module block diagrams show the current top and all descendants, with dashed unreachable
instances, depth-based colors, hover feedback and synchronized double-click/source navigation.
The full hierarchy remains visible while inactive branches fade. Fit lives in the panel
header; search, fold controls, zoom buttons and the more menu are removed. Context panels
tile vertically in the sidebar and horizontally in the bottom area, with adjustable dividers
and persistent sizes. Ela's hosted-tab extension handles
tab drag/drop, split targets and floating editor windows. Closing a floating container
returns its tabs; closing a tab retains unsaved-document confirmation. Context floating
windows use ElaWidget and hosted drag handles. The application retains split layout,
document lifetime and workspace ownership. The workspace picker appears in the
sidebar, or in the title bar when collapsed, with full paths, unsaved markers and
workspace-specific close decisions. Switching retains live buffers, the active file,
cursor and scroll positions. External/untitled files carry a TEMP label and remain
available across switches. Their analysis, instance binding, session membership and
crash recovery are isolated from the active project. Source files can also be opened
via application arguments. Project/Settings icons remain in the expanded sidebar.
Formal Ela packaging requires a clean source tree and matching generated application version.
Existing classic user settings remain separate. See
[Ela migration and validation](docs/ela-migration.md).

## Current goal and open work

Maintain a focused SystemVerilog editor with Tree-sitter structural editing and Slang semantic
authority. Provider-based Context Workspace, live insights
and the read-only AI CLI are implemented. Native documents and external application
data remain with their owners. Context content is placed by the user: sections stack in the
sidebar, detachable views open in native floating windows with workspace geometry memory, and
temporary source editors keep their in-editor overlay. Insight sections render the same view
their full view uses and stay pinned to the target the user chose.

Native context floating windows request Windows 11 Desktop Acrylic. Their background opacity
setting changes the tint only, leaving text and icons opaque. Unsupported systems, disabled
transparency, high contrast and battery saver use the theme's solid background.
Floating panels, lists, text previews and graph canvas backgrounds share that surface; docking
restores ordinary backgrounds while retaining graph content and selection.

Future usability work addresses one concrete workflow at a time. No whole-window redesign is
scheduled. Released changes and their acceptance results live in the Git history alone.

### Context-panel simplification — implemented 2026-09-22

状态：17 项纳入 0.31.0，Ela 构建和 26 项针对性回归通过。验收边界与既有 smoke 失败见 [模块框图与停靠改造](docs/module-diagram-modernization.md)。

1. 删除上下文图表面板中的 `Follow Editor` 控件及跟随模式切换功能。
2. 删除同一工具行的 `Pin`／`Pinned` 控件及其独立开关状态。
3. 模块框图画布占满工具栏之外的可用视图，随窗口放大、最大化及面板尺寸变化自适应。
   图形保持比例与紧凑节点布局，合理适配视口，消除固定内容尺寸和冗余边距造成的大面积空白。
4. 删除模块框图工具栏中的放大（`+`）与缩小（`−`）按钮，保留鼠标滚轮缩放。
5. 删除模块框图的搜索功能，包括搜索按钮、实例／模块搜索框及关联搜索逻辑。
6. 删除模块框图更多（`…`）下拉菜单及其中全部功能：`Jump`、`Focus`、`Set Top`、
   `Open in Temporary Editor`、`Export Graph…`、`Depth` 调整和 `Show unresolved` 开关。
   一并移除菜单入口及模块框图专用关联逻辑。
7. 重新设计模块框图面板顶栏，将 `Fit`（适配视图）按钮移至顶栏。
   结合上述功能删减整理标题、当前模块信息与必要操作，减少多层工具行及冗余空白。
8. 模块框图默认显示当前顶层及其全部子模块，不限制层级；不包含工作区中未被该顶层实例化的模块。
9. 不可达模块保留在图中，使用虚线边框标识。
10. 重新调整模块配色，以层级为着色依据，同一层级的模块使用同一种颜色。
11. 鼠标悬浮模块时，通过高亮或轻微放大提供反馈。
12. 双击模块时，框图定位到该模块，代码编辑区同步跳转到对应模块源码。
    该图内导航与源码联动独立于第 6 项删除的下拉菜单入口。
13. 跳转后仍显示顶层模块，但将其虚化，保留层级上下文并突出当前目标模块。
14. 鼠标侧键支持模块浏览历史的回退与前进，并保持框图和代码编辑区的跳转联动。
15. 移除模块节点上的向下折叠三角和向右跳转三角图标。
16. 更换模块悬浮时的手形光标，暂定使用标准箭头，配合模块高亮提供交互反馈。
17. 悬浮窗支持拖入侧栏或底栏停靠；同一区域中的多个面板可并行平铺、同时可见，
    并通过拖动分隔条调整各面板的尺寸与空间比例。

面板保留显式目标并接收语义更新；完整视图沿用该目标。旧 Follow/Pin 状态不再读取，旧停靠布局默认恢复到侧栏。

### Pending UI refinement — requirements synchronized 2026-09-11

弱边框部分已在 v0.25.8 实施（见该版本的提交）；其余内容仍为候选。当前不新增动效。

**弱边框与表面层级**

- 用轻微背景明度差区分区域：编辑区最亮，侧栏和底栏略暗；沿用现有主题、布局及各专用图表逻辑。
- 固定面板保持连续、平坦，减少框中框和密集分隔线，仅在必要的调整尺寸边界保留弱分隔线。
- 浮窗使用轻微描边和柔和短阴影；不得恢复圆环菜单透明窗口的 Windows 矩形阴影。
- 选中行使用低饱和强调色，普通行保持统一背景。
- 已查看侧对话参考图，仅采纳表面层级和弱边框方向。Project/Settings 继续遵循当前图标入口规则，图表内容仅为示意。参考图本地路径：`C:/Users/14971/.codex/generated_images/01a08fac-ef9b-7631-8600-2494f37b4d37/exec-cb77852e-9d66-4423-a56d-cc2388ebbf86.png`。

**动效按场景管理**

时长、曲线、中断方式、减少动画规则和验证状态统一维护在
[动效场景规格](docs/control-style-consolidation.md#动效场景规格2026-09-20)。
原先的建议时长不再作为另一套规格。当前仅维护 Ela 正式包。
Ela 0.29.39 的左右栏、底栏和右栏区块使用共享合成过渡。图标不缩放、不弹跳；
代码输入、导航、光标、滚动、图表拖动和连续缩放不等待装饰动画。
ZeroSlack 的 `--no-ui-animations` 只控制 SuiteUi/Qlementine 控件，不等同于全应用减少动画，
也不与 RegMap 的 `QT_REDUCE_MOTION`/系统偏好合并。

### Remaining validation

`ZEROSLACK_ENABLE_ELA=ON` builds the separate Ela application. Version 0.29.25 migrates
eight basic control types at 60 creation sites. The 0.29.26 development build adds
tree rendering and tab bars at 19 creation sites, including the initial Designer editor tab group.
Document close confirmation, split/move operations and tree item models retain their existing
controllers. The 0.29.27 development build adds 117 specialized toolbar/form control
creation sites and Ela radio buttons. Diagram rendering, transactions and undo remain
unchanged. Version 0.29.28 adds Ela inputs and action buttons to common dialogs,
the editor find/replace bar, Peek and floating-panel controls. Qt still owns modal
results, cancellation and native window frames. Version 0.29.29 adds Ela menus and
list/table rendering, including command results and settings shortcut tables. Qt retains
action ownership, item models, editing delegates and keyboard behavior. Native file
pickers and custom radial menus remain unchanged.
Version 0.29.31 adopts ElaAppBar for the main title layout and window buttons. The
host retains native snap/resize handling and unsaved-document close decisions;
the title path menu and sidebar toggle remain available. ElaWindow is not used.
Version 0.29.32 moves the left Project/Settings/sidebar controls, right context rail
and bottom panel buttons to ElaToolButton. QAction state, repeat-click collapse,
context menus and Problems/Activity badges retain their existing behavior.
Version 0.29.33 adopts ElaScrollArea for settings, stacked panels and detail/image
viewports, and ElaPlainTextEdit for read-only logs, diffs, recovery and Peek content.
Host fonts, text palettes, selection/copy, document data and scroll positioning remain
under Qt and the existing controllers; the code editor keeps its specialized implementation.
Version 0.29.34 adopts ElaText for common headings, descriptions, state labels and
form captions. Host typography, semantic/disabled colors, selection, links and
mnemonic buddies retain their Qt behavior. Workspace Configuration now also uses
ElaScrollArea with viewport-aware sizing of its nested lists and tables.
Version 0.29.35 moves the complete left sidebar into ElaNavigationBar. Its native
display-mode animation controls opening and closing; a documented content-host
extension lays out the existing Project/Settings header and Files/Design views.
The Qt dock remains the workspace layout slot, with one size synchronization at
transition completion. Dragged width, interrupted transitions and Ctrl+1 are retained.
Version 0.29.36 reduces sidebar resize overhead: Ela applies its animated width once
per value, and the editor reuses diagnostics and semantic highlights when the visible
document range is unchanged. Scrolling, edits and newly exposed lines still refresh.
Visible text also uses a bounded Qt layout cache to reduce repeated glyph shaping.
Version 0.29.37 separates sidebar presentation from live document layout. Ela owns
the display modes and transition contract; Windows DirectComposition animates two
unscaled temporary surfaces at the desktop composition cadence. The live editor
resizes only at transition boundaries. Input settles the transition before delivery,
and snapshots are released afterwards. Device creation is warmed on a worker;
unsupported or unready graphics use the same layout-isolated raster fallback.
Version 0.29.38 fixes the sidebar handoff: the native visual now overlays the host
directly, allowing Qt to paint the final layout and focus before it is detached.
Snapshots include the complete Ela sidebar background and border, including when
opening from a collapsed bar, to avoid switching appearance at the endpoint.
Version 0.29.39 shares this compositor with the right Context sidebar, bottom drawer
and collapsible Context sections. Snapshots translate and clip without scaling text
or resizing the editor every frame. The bottom button bar stays fixed. Reversals
continue from the current position; different panels settle the previous transition
before starting. Classic keeps its existing behavior, and restored layouts apply immediately.

Version 0.29.30 adds interruptible Ela wheel scrolling to settings and Workspace Hub,
tree expansion transitions to Files/Design/Workspace Hub, and horizontal tab scrolling
without changing the selected document or its close/split controllers. Pixel-based
touchpad scrolling is applied directly; scroll ranges remain immediate.
Ela is mutually exclusive with SuiteUi and Qlementine.
The editor, window management and specialized diagram models/canvases remain in place.

An isolated Qlementine product preview is available behind the default-off
`ZEROSLACK_ENABLE_QLEMENTINE` CMake option. It uses an independent application
profile and `ZeroSlack-Qlementine-Preview.exe`; the formal package remains separate.
Build, packaging, native validation results and remaining limitations are recorded
in [control style validation](docs/control-style-consolidation.md).

The separate default-off `ZEROSLACK_ENABLE_SUITEUI` option consumes an installed
`SuiteUi 0.1.0` package with exact version matching. It is mutually exclusive with
the vendored preview option. Only push buttons, tool buttons and checkboxes use
the shared renderer; application fonts, palettes, other controls and professional
views retain their existing ownership. See [SDK integration](docs/suite.md#suiteui-第-4-阶段独立-sdk-与双应用接入)
for build switches, deployment, rollback and validation limits.
Set `SuiteUi_DIR` to the installed SDK's `lib/cmake/SuiteUi` directory when enabling it.
`scripts/package-release.ps1` stages the classic formal package and refuses any
configuration that enables an optional backend.

0.29.23 shipped SuiteUi as the formal default; 0.29.24 reverts that default to
classic because the SDK control path was judged unacceptably sluggish in real
desktop use. The cause is not yet diagnosed, so the SDK integration is kept as a
default-off option rather than removed.

Every check that has never been run on a real desktop is tracked in
[unverified manual checks](docs/unverified-manual-checks.md): native frame measurements,
multi-monitor and cross-screen dragging, consecutive restarts, the sidebar stack gestures,
document-bound tab interaction, the temporary editor comparison, the two
semantic-availability checks, and native appearance of the consolidated button/checkbox states.
Windows edge dragging and mixed-DPI multi-monitor behavior remain
the oldest open items; keyboard snapping and title-bar mouse operations already have regression
coverage.

### Maintenance rules

- Select one specific usability issue before starting another implementation slice.
- Keep this README, the manual and package metadata aligned with `VERSION`.
- Classify first-party sources under `src/` and keep CMake and source-policy checks aligned.
- Remove superseded local build/package outputs; keep the active Release build, current release
  evidence, toolchain inputs and referenced test fixtures.
- Preserve semantic ownership, generation checks and read-only CLI boundaries.
- Run tests relevant to changed behavior; record the configuration and date.

## Documentation

ZeroSlack is licensed under [Apache License 2.0](LICENSE). Third-party components
and their licenses are recorded in [third-party notices](THIRD-PARTY-NOTICES.md).

The repository keeps eight documents. This README is the entry point; the other seven are:

| Document | Holds |
| --- | --- |
| [用户手册](用户手册.md) | Every user-facing interaction, shortcut and edge case (Chinese) |
| [Architecture](ARCHITECTURE.md) | Ownership, threading, extension constraints, repository layout, visual system |
| [AppSuite integration](docs/suite.md) | `suite-app/v1` protocol, visual contract, dual-application validation and the minimal SuiteUi extraction decision |
| [Integrations](docs/integrations.md) | Read-only CLI and external application boundaries |
| [Unverified manual checks](docs/unverified-manual-checks.md) | Checks never run on a real desktop |
| [Kernel view continuity](docs/kernel-view-continuity-implementation.md) | Baseline, implementation scope and validation of the 0.29.19 view-state work |
| [Control style consolidation](docs/control-style-consolidation.md) | Button/checkbox ownership, A/C comparison, optional Qlementine preview, native checks and protected-view validation |

Historical acceptance logs and superseded status reports are intentionally
kept out of this current-facts document. Released changes are recorded in the Git history:
`git log` for the change itself and `git show <tag-or-commit>` for the acceptance results
recorded with each release.
