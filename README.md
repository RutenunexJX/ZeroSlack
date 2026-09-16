# ZeroSlack

Current version: `v0.29.8`

Repository navigation: [source and file categories](ARCHITECTURE.md).

ZeroSlack is a Qt 6 desktop environment for navigating, understanding, and
editing SystemVerilog workspaces. It combines an incremental editor syntax
model with workspace-wide semantic analysis, relationship views, diagnostics,
and preview-first RTL editing workflows.

The Workspace Hub groups the active source context, Pinloom bindings, and
explicit WaveWorkbench and RegMapWorkbench resources in the existing Context
Workspace sidebar. The same versioned associations are available to automation:

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
| Shell | Independent full-height Navigation and Context columns, a drawer under the editor, a persistent title-bar row, and no status bar; the menu bar is hidden and its commands live in the Project context menu. | [manual §3](用户手册.md) |
| Shell | Non-editor text follows a proportional typography hierarchy with distinct headings and metadata. | [architecture, visual system](ARCHITECTURE.md) |
| Drawer | Problems and Activity are permanent; Search, Change Preview, Connections and Fold Shelf appear on demand. Activity counts unread important messages and clears on open. | [manual §3.3](用户手册.md) |
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
| Context | The right-side rail opens a resizable sidebar of collapsible sections that can be resized, reordered, floated into native windows, bound to a document, or hidden as a whole with `Ctrl+2`. | [manual §5.4](用户手册.md) |
| Context | Temporary source editing uses one in-editor Peek that pins into the sidebar without losing the live editor, undo state, search history or restore identity. | [manual §5.4](用户手册.md) |
| Insights | Problems, Design, RTL Insights, state-transition and FSM views, module block diagrams, signal journeys, signal-kernel graphs, usage hotspots and symbolic wave previews. | [manual §16](用户手册.md) |
| Insights | Each insight section renders the real view through the same surface the full view uses; source Actions and the Wave command retarget and pin the matching section instead of opening a central tab. | [manual §3, §16](用户手册.md), [architecture](ARCHITECTURE.md) |
| RTL edits | Preview-first rename, connection transform, expose-to-top, scoped replace, instance-pair connection and multi-signal propagation, including module-port synchronization across all instances in one Change Preview transaction. | [manual §17](用户手册.md) |
| Wave | Simulation runs from the current module, an enclosing `always` block, a selected signal or an exact Design instance. ZeroSlack owns the module manifest, stimulus preparation, unsaved-buffer capture, the portable toolchain and result-to-source navigation; WaveWorkbench owns scenario editing, execution, trace reading and waveform rendering. | [integrations, Wave simulation](docs/integrations.md), [manual §16.9](用户手册.md) |
| Wave | The Windows release carries Verilator 5.050, MinGW 13.1 and GNU Make as a verified sibling bundle that is extracted into the local cache on first use; explicit paths stay configurable under `Settings > Simulation`. | [integrations, Wave simulation](docs/integrations.md) |
| Suite | Workspace Hub groups source, Pinloom, Wave and RegMap resources, and the same versioned associations are available to automation. | [AppSuite integration](docs/suite.md), [manual §21](用户手册.md) |
| Settings | Themes including Catppuccin, fonts, shortcuts by Action ID, annotations, analysis, simulation, integration and layout; every category except the theme can be scoped globally or per workspace. | [manual §18](用户手册.md) |

Ownership boundary: native documents and external application data stay with their owners.
ZeroSlack loads the shared WaveWorkbench workspace through a versioned ABI and does not keep a
second waveform renderer, read another application's database, or include its private headers.
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

Requirements are CMake 3.27 or newer, a C++20/C99 toolchain, Qt 6 with Core,
Gui, Widgets, Concurrent, Svg, and Test modules, Ninja or another CMake
generator, and initialized `thirdparty/slang`, `thirdparty/tree_sitter`, and
`thirdparty/tree_sitter_systemverilog` sources.

```powershell
git submodule update --init --recursive
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=<qt-prefix>
cmake --build build/local --target demo zeroslack_cli
build/local/demo.exe
build/local/zeroslack-cli.exe --help
ctest --test-dir build/local --output-on-failure -j1
```

## Versioning and release

`VERSION` is the single manually maintained product version source and must contain exactly
one SemVer value in strict `X.Y.Z` numeric form. Current controlled baseline: `v0.29.8`.
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
build and run the release verification targets; create a signed-off release tag named `vX.Y.Z`;
publish Windows artifacts under `E:\PinloomRoot\AppPackage\AppSuite`, a path that intentionally
contains no spaces. The Windows package directory and archive basename stay fixed as
`ZeroSlack-win64` — never put the product version in either package filename, so existing
shortcuts remain valid. The product version is recorded only in `VERSION`, the application
display, the guarded document markers, and the release tag.

## Current goal and open work

Maintain a focused SystemVerilog editor with Tree-sitter structural editing and Slang semantic
authority. Workspace Hub, provider-based Context Workspace, live insights, Wave simulation
integration and the read-only AI CLI are implemented. Native documents and external application
data remain with their owners. Context content is placed by the user: sections stack in the
sidebar, detachable views open in native floating windows with workspace geometry memory, and
temporary source editors keep their in-editor overlay. Insight sections render the same view
their full view uses and stay pinned to the target the user chose.

Future usability work addresses one concrete workflow at a time. No whole-window redesign is
scheduled. Released changes and their acceptance results live in the Git history alone.

### Pending UI refinement — requirements synchronized 2026-09-11

弱边框部分已在 v0.25.8 实施（见该版本的提交）；其余内容仍为候选。当前不新增动效。

**弱边框与表面层级**

- 用轻微背景明度差区分区域：编辑区最亮，侧栏和底栏略暗；沿用现有主题、布局及各专用图表逻辑。
- 固定面板保持连续、平坦，减少框中框和密集分隔线，仅在必要的调整尺寸边界保留弱分隔线。
- 浮窗使用轻微描边和柔和短阴影；不得恢复圆环菜单透明窗口的 Windows 矩形阴影。
- 选中行使用低饱和强调色，普通行保持统一背景。
- 已查看侧对话参考图，仅采纳表面层级和弱边框方向。Project/Settings 继续遵循当前图标入口规则，图表内容仅为示意。参考图本地路径：`C:/Users/14971/.codex/generated_images/01a08fac-ef9b-7631-8600-2494f37b4d37/exec-cb77852e-9d66-4423-a56d-cc2388ebbf86.png`。

**克制的微动效**（时长为建议初值，尚非逐项确认的硬性参数）

- 第一轮优先：按钮背景悬停/按下 80–120ms；Files/Design/Pinloom 条目底纹 80–100ms；信息浮窗原位淡入约 100ms、关闭约 60ms；圆环菜单及子项条淡入/底纹过渡 60–100ms。图标不缩放、不弹跳。
- 后续候选：侧栏宽度过渡 120–160ms，先验证编辑器重排及图表性能；底栏沿用现有约 140ms；分段选中底纹 100–140ms；Activity 数字更新时背景轻微提亮一次 120–180ms；复制成功图标短暂改为勾号约 800ms。
- 动画应可中断，菜单立即可操作；代码输入、光标、滚动、图表拖动和连续缩放保持即时响应。
- 建议统一“减少动画”设置，优先复用 Qt Widgets、InsightVisualStyle 和 PanelLayoutController 现有主题、密度及动画基础。

### Remaining validation

Every check that has never been run on a real desktop is tracked in
[unverified manual checks](docs/unverified-manual-checks.md): native frame measurements,
multi-monitor and cross-screen dragging, consecutive restarts, the sidebar stack gestures,
document-bound tab interaction, the temporary editor comparison, and the two
semantic-availability checks. Windows edge dragging and mixed-DPI multi-monitor behavior remain
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

The repository keeps six documents. This README is the entry point; the other five are:

| Document | Holds |
| --- | --- |
| [用户手册](用户手册.md) | Every user-facing interaction, shortcut and edge case (Chinese) |
| [Architecture](ARCHITECTURE.md) | Ownership, threading, extension constraints, repository layout, visual system |
| [AppSuite integration](docs/suite.md) | `suite-app/v1` protocol, cross-application workflows, shared visual contract |
| [Integrations](docs/integrations.md) | Read-only CLI contract and the Wave simulation boundary |
| [Unverified manual checks](docs/unverified-manual-checks.md) | Checks never run on a real desktop |

Historical acceptance logs and superseded status reports are intentionally
kept out of this current-facts document. Released changes are recorded in the Git history:
`git log` for the change itself and `git show <tag-or-commit>` for the acceptance results
recorded with each release.
