# AppSuite integration

ZeroSlack is one of four independent applications in the AppSuite family. This document is
the single ZeroSlack-side record of that family contract: the transport protocol, the
implemented cross-application workflows, and the shared visual and interaction language.
Application state always stays with its owner.

Current release decision (2026-09-21): the user authorized switching the formal
packages of ZeroSlack 0.29.23, RegMapWorkbench 0.3.2 and Pinloom 0.4.2 to SuiteUi.
Their SDK build options now default to ON, and normal startup selects the SDK
controls. ZeroSlack/RegMap keep the verified 0.1.0 SDK; Pinloom keeps 0.1.1.
Existing profiles remain in place. The classic startup overrides and explicit
OFF builds remain available. WaveWorkbench retains its existing configuration.
Formal packages include the installed SDK notice directory. ZeroSlack CLI shares
the UI-linked core DLL. Native validation gaps below remain open; the default
switch does not change those evidence boundaries. Earlier default-OFF statements
describe the historical implementation stages.
RegMap/Pinloom's existing MSVC CI jobs explicitly build the classic fallback:
the installed release SDK requires MinGW 13.1 and cannot be linked by MSVC.
The SuiteUi release path is validated by the local full regression runs.

Protocol integration baseline: ZeroSlack 0.22.0, Pinloom 0.4.0, WaveWorkbench 0.12.0,
RegMapWorkbench 0.3.0. Each application remains an independent repository.

## Application protocol (`suite-app/v1`)

The public protocol is `suite-app/v1`. Application state stays with its owner.

### Architecture

#### Neutral runtime

The neutral source lives outside every application repository. The installed
`SuiteApp::suiteapp` SDK provides protocol and client integration; `suite-runtime`
provides per-user discovery and routing. CMake exports and JSON schemas define
the public integration boundary.

The runtime owns discovery and routing only. It never owns Pinloom content,
wave projects, register-map projects, or ZeroSlack workspaces.

#### Application roles

Each application publishes one `AppDescriptor` and may expose:

- Resources: stable URI identities resolved by their authoritative owner;
- Actions: typed requests with explicit success or structured failure;
- Surfaces: UI descriptors with native, model, or external fallback modes.

Applications may be both Host and Provider. They communicate with the broker,
not with each other's executable paths or private databases.

#### Transport

- Local-only `QLocalServer` / `QLocalSocket` transport;
- one compact JSON request and one compact JSON response per connection for v1;
- per-user server access, bounded payload, request ID, timeout, and protocol
  validation;
- no TCP listener and no ambient network dependency;
- asynchronous progress and cancellation are reserved as declared v1
  capabilities, not simulated by blocking UI work.

### `suite-app/v1` Contract

Every envelope contains:

```json
{
  "protocol": "suite-app/v1",
  "requestId": "stable-request-id",
  "method": "registry.list",
  "params": {}
}
```

Responses preserve `protocol` and `requestId`, then return either `result` or
the structured error `{ code, message, details }`.

Broker methods:

- `runtime.capabilities`
- `registry.register`
- `registry.unregister`
- `registry.list`
- `resource.resolve`
- `action.invoke`
- `surface.describe`
- `surface.open`
- `health.ping`

Provider descriptors include application ID, display name, semantic version,
process identity, endpoint, protocol range, capabilities, URI schemes,
actions, Surfaces, and launch fallback. Unknown fields are ignored; required
fields are validated strictly.

#### Resources

Resource URI forms are published by the current provider descriptors listed below.

Cross-application state stores only stable URIs and explicit lightweight view
state. Authoritative content is resolved again from the owner.

#### Actions

An action descriptor declares its stable ID, accepted resource schemes,
parameter schema ID, result schema ID, side-effect class, and whether the
provider UI is required. Invocation never relies on human-readable labels.

#### Surfaces

Surface descriptors use one of three modes:

- `model`: provider returns structured data rendered by the Host;
- `native`: a versioned shared component is available and declares Qt,
  compiler, architecture, and ABI compatibility;
- `external`: the owning application opens the resource independently.

Every native Surface must declare an external fallback. Cross-process native
window reparenting is not part of the protocol.

### Provider integration

The neutral implementation lives at `E:\SuiteRuntime\SuiteRuntime` and is
installed for development at `E:\SuiteRuntime\install`. It exports the static
`SuiteApp::suiteapp` SDK and installs `suite-runtime.exe`, `suite-cli.exe`, CMake
package metadata, headers, and protocol schemas.

Runtime discovery is deterministic: an explicit path, then
`SUITEAPP_RUNTIME_EXECUTABLE`, the application directory, the sibling
`../Runtime` directory, and finally `PATH`. Applications remain usable when no
runtime is found; only suite capabilities are unavailable.

The implemented application contracts are:

| Application | Resource | Action | Surface |
| --- | --- | --- | --- |
| ZeroSlack | `zeroslack://source?...` | `zeroslack.source.reveal` | `zeroslack.source.preview` (`model`) |
| Pinloom | `pinloom://entry/...` | `pinloom.entry.open`, `pinloom.source-anchor.create` | `pinloom.entry.preview` (`model`) |
| WaveWorkbench | `wave://project?...` | `wave.project.open` | `wave.waveform` (`native`, external fallback) |
| RegMapWorkbench | `regmap://project?...` | `regmap.project.open` | `regmap.workbench` (`model`) |

Provider calls use a 2-second bound. Runtime probing uses 300 ms and first
startup uses a 3-second bound. The transport accepts a response that arrives
while the peer is completing its write/disconnect sequence, preventing a fast
local provider from being incorrectly removed as `write_failed`. Nested Qt
event loops cannot emit a competing timeout response for an already handled
request.

### Packaging

```text
AppSuite/
|-- Apps/
|   |-- Runtime/
|   |   |-- suite-runtime.exe
|   |   |-- suite-cli.exe
|   |   `-- schemas/
|   |-- ZeroSlack-win64/
|   |-- Pinloom/
|   |-- WaveWorkbench/
|   |-- RegMapWorkbench/
|   `-- Toolchain/
|-- suite-manifest.json
`-- SHA256SUMS.txt
```

Every application executable is placed in an immediate child of `Apps/`, so
the existing `../Runtime` discovery rule resolves to the single
`Apps/Runtime/suite-runtime.exe`. `Toolchain` remains a sibling of
`ZeroSlack-win64` and `WaveWorkbench`, matching the existing Wave toolchain
discovery contract. The suite manifest pins tested component versions and
protocol/ABI ranges. Applications retain private Qt runtime directories; Qt
DLLs and plugins are never flattened into a shared directory. Missing
providers disable only their own capabilities.

The suite assembly script consumes already verified portable component
directories, removes the duplicate WaveWorkbench copy from the ZeroSlack
staging directory, deploys the Runtime's own Qt dependencies, and writes a
manifest plus SHA-256 inventory. Existing standalone packages remain valid;
the family package is an additional distribution form.



The formal package is `E:/PinloomRoot/AppPackage/AppSuite`. Its manifest records component versions;
`SHA256SUMS.txt` records file hashes, not a digital signature. The assembly script
`../scripts/package-app-suite.ps1` consumes prebuilt portable inputs and does not compile applications.
`ReplaceExisting` removes the old destination and moves staging; replacement is not transactional.
ZeroSlack also retains the `pinloom-host/v1` context adapter; the suite protocol does not remove that compatibility path.

## Implemented workflows


### ZeroSlack Workspace Hub

The Hub is a side-panel navigation model scoped to the active workspace and
current source selection. It groups Source, Pinloom, Wave, and RegMap items,
shows availability/stale/missing state, previews model surfaces when available,
and invokes stable native deep links for editing. It never reads another
application's private database.

Updates are generation-tagged. Older provider responses cannot replace a newer
selection. Provider absence degrades only that section. Hub visibility, width,
expanded groups, selection, and preview state are persisted.

### `zeroslack-cli suite-context`

The command is read-only and returns the existing `zeroslack.cli/v1` envelope.
It starts from the semantic cache, discovers explicit workspace references to
Pinloom, Wave, and RegMap resources, and invokes public suite/CLI contracts on
demand. Default output is bounded metadata; `--include` selects providers and
`--max-tokens` enforces a deterministic payload budget. Missing applications,
stale IDs, timeouts, and invalid files are returned as structured diagnostics
without failing unrelated sections.

### Pinloom PDF Viewer Adapter

Pinloom stores PDF identity, page, page-space rectangle, rotation, and crop-box
metadata as the authoritative locator. A `PdfViewerAdapter` owns viewer-specific
window discovery, capture sampling, navigation, and cancellation. SumatraPDF is
the first adapter. Screen pixels and zoom are observations, never persisted
anchor identity. The existing annotated-copy presenter remains the stable
highlight path.

Capture is an explicit success/failure/canceled/timeout state machine. Closing
or changing a viewer cancels pending work without blocking the application.
Legacy locators remain readable and are upgraded only on a successful save.

### WaveWorkbench Scenario lifecycle

Create, duplicate, rename, delete, and reorder Scenario operations use stable
IDs, deterministic names, one-step undo/redo, and safe current-selection
fallback. Deleting the last Scenario is rejected. CLI operations and deep links
share the same validation. External reload preserves a selected Scenario by ID
or reports its removal explicitly.

### RegMapWorkbench external-diff decisions

Disk/CLI changes are presented as stable, dependency-aware items. Users can
accept or reject selected changes or all changes after a validation preview.
Accept applies one atomic WorkspaceStore transaction and is undoable. Reject
keeps the Workbench value and records the decision against the observed disk
digest; a newer disk revision invalidates prior decisions. Parent/child changes
cannot produce dangling Blocks, Registers, Fields, or Enum values. Neither path
silently overwrites the disk.

## Visual and interaction contract


### Shared visual system

Each application implements the same semantic roles locally. No runtime theme
dependency is introduced between otherwise independent products.

#### Foundations

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
- Motion duration, easing, interruption and reduced-motion ownership follow the
  [scenario specification](control-style-consolidation.md#动效场景规格2026-09-20).
  Data movement and navigation never wait for decorative animation. Classic
  instant feedback and optional SDK transitions are separate implementation paths.

#### Product identity

- ZeroSlack uses cyan/blue for source-to-insight relationships.
- Pinloom uses warm amber for captured Anchors and blue for navigation.
- WaveWorkbench uses teal for actual signals and violet for expected/derived
  state.
- RegMapWorkbench uses indigo for address structure and cyan for RTL sync.

Identity accents never replace shared success/warning/error semantics. Text and
critical diagram edges meet readable contrast in light and dark themes.

#### Common shell behavior

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

### Shared visual and interaction language

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

## SuiteUi 第 2 阶段：共享候选与第二应用场景

核对日期：2026-09-20。**本节保留第 2 阶段的静态筛选依据；第 3 阶段已完成，
双应用运行结果与最终提取范围见下方“第 3 阶段”记录。**

ZeroSlack 基线为 `e40938374939f9cd24ff2d6d4612b0524ee1a2fe` / `0.29.21` 加当前未提交的
Qlementine 预览修改；RegMapWorkbench 为 `1847c817a1ba000cbba949ab85ef1428500a22f4` /
`0.3.0`，核查前后工作区干净。第 2 阶段只读 RegMap 源码，没有修改、运行或打包该应用。
26 个相关源码/构建/测试文件的字节哈希和六项密度值核对记录在
`build/evidence/suite-ui-stage2-20260920/source-audit.json`；这是静态证据，不是测试通过记录。

### 候选判定

| 编号 | 候选 | ZeroSlack 实际场景 | RegMap 实际场景 | 第 2 阶段结论 |
| --- | --- | --- | --- | --- |
| C1 | 基础表面、文字、交互色及密度 token | 设置页、普通控件、Insight 状态标签 | 页头动作、批量编辑对话框、项目/同步状态标签 | 进入第 3 阶段；只传通用角色和值，保留两边的主题集合、字体和专业色 |
| C2 | 普通/主操作按钮与复选框状态 | 设置页 Apply/Revert、覆盖开关、Kernel 筛选 | Generate、Sync RTL、Save & Sync；Batch Edit Registers/Fields 的属性开关 | 进入第 3 阶段；必须同时覆盖 QPushButton 和 QToolButton，不改变动作或表格编辑行为 |
| C3 | 状态徽标的呈现 | Live Insight 的 Current、Stale、Updating、Error | fileStateBadge、syncStateBadge 的 Saved、Dirty、Busy、Conflict 等 | 进入第 3 阶段；只验证标签样式、文字、提示、焦点策略与无障碍，不统一业务状态机 |
| C4 | 主题感知通用图标库 | RoundedIcons 的通用与专业图标混合在一个引擎中 | 页头以文字动作呈现；诊断使用 Qt 标准图标，固定地址使用专用图标 | 暂缓；当前没有足以证明共享维护收益的第二应用图标集合 |
| C5 | 完整页头、空状态和加载容器 | Insight 目标候选列表、Pick in editor、保留旧结果 | 项目/页面/Block/筛选空状态，对应不同创建或导航动作 | 放弃本轮整体提取；差异留在应用，能共享的外观已收敛到 C1/C2/C3 |
| C6 | 专业画布、窗口外壳与停靠 | QGraphicsView 专业图、原生浮窗、侧栏竖栈 | QWidget 自绘 BitfieldView/AddressSpaceView、表格和 QSplitter | 不进入首批；按主方案的独立评估项处理，当前不提出统一基类或窗口迁移 |

C4 的复评触发条件：RegMap 明确需要复用已有通用动作/诊断图标，并且样例能验证主题、
禁用态与高 DPI 的共同渲染职责。**截止点为本轮第 3 阶段结论评审**；届时仍未触发，
从本轮提取清单移除，后续只有新需求出现才重新评估。

### C1：可映射的基础角色

两边已有六项完全相同的逻辑尺寸：基础间距 4、紧凑高度 28、普通高度 32、主操作高度 36、
小圆角 6、大圆角 8。来源分别是
[InsightDensityTokens](../src/ui/insightvisualstyle.h:147) 与
[DensityMetrics](E:/RegMapWorkbench/RegMapWorkbench/src/app/workbench_theme.hpp:51)。
这是语义默认值；实际控件高度还需容纳字体和 `minimumSizeHint`，不能统一改成固定高度。
页头边距不合并为单一数值：RegMap 已分别定义水平 8 和垂直 4，ZeroSlack 的字段组织不同。

| 公共角色 | ZeroSlack 来源 | RegMap 来源 | 需要验证的差异 |
| --- | --- | --- | --- |
| app / canvas / panel / raised | appBackground / canvasBackground / panelBackground / surface.raised | application / canvas / panel / raisedSurface | 提供角色映射，保留各应用实际色值 |
| text / muted / disabled / onAccent | textPrimary / textMuted / button.textDisabled / button.textChecked | text / mutedText / mutedText / onAccent | RegMap 合用 muted 与 disabled；不能无条件把两者在公共接口中合并 |
| border / focus / accent | border / focus.ring / accent | border / focus / accent | 对比度和焦点状态逐项验证 |
| control normal / hover / pressed / checked | InsightControlTokens 的明确状态字段 | QSS 中 panel / selection / divider 等组合 | RegMap 当前缺少独立的控件状态结构，需要应用适配层显式填值 |
| info / success / warning / error | statusBar 的各 tone 颜色 | diagnosticInfo / success / warning / error | 业务状态到 tone 的判断保留在应用 |

[InsightTheme](../src/ui/insightvisualstyle.h:222) 同时包含语法色、信号语义色和编辑器语义色；
[WorkbenchTheme::Tokens](E:/RegMapWorkbench/RegMapWorkbench/src/app/workbench_theme.hpp:17)
同时包含地址、访问权限、复位值、RTL 同步和位域色。**两个完整结构都不直接成为公共 API。**

字体保持应用输入：ZeroSlack 的 [UiTypography](../src/ui/uitypography.h:11) 使用角色化逻辑像素；
RegMap 的 [apply](E:/RegMapWorkbench/RegMapWorkbench/src/app/workbench_theme.cpp:488)
选择 Aptos/Segoe UI/Carlito 并使用 10 pt。第 3 阶段不统一字体家族或字号，也不改变代码、
地址、寄存器表格和位域文字。减少动画策略由应用解析后传入；RegMap 的
`QT_REDUCE_MOTION` 与系统偏好逻辑不能被 ZeroSlack 的命令行开关取代。

### C2：第二应用的具体操作链

首个验证切片选择 **RegMap 的批量编辑对话框，加页头三个动作的控件状态**：

- Batch Edit Registers 的 `batchAccessApply` 勾选后启用 `batchAccessEditor`；取消或关闭对话框
  不修改模型；只提交勾选的属性；提交后的既有撤销语义保持。构造入口见
  [main_window.cpp](E:/RegMapWorkbench/RegMapWorkbench/src/app/main_window.cpp:19074)。
- Batch Edit Fields 的 `batchFieldAccessApply`、`batchFieldRangeApply` 等采用相同状态链，
  混合值仍要求用户明确选择，空值含义由原业务处理。入口见
  [main_window.cpp](E:/RegMapWorkbench/RegMapWorkbench/src/app/main_window.cpp:19832)。
- 页头 `generateButton` 是普通操作；`synchronizeButton` 和 `saveSyncButton` 为主操作，
  三者都是 `QToolButton`。检查可用、禁用、焦点、悬停和按下，并沿用真实 QAction/信号连接。
  布局入口见 [main_window.cpp](E:/RegMapWorkbench/RegMapWorkbench/src/app/main_window.cpp:5666)。

与 ZeroSlack 设置页的 Apply/Revert、覆盖开关配对。共享内容限于外观角色和输入状态的
验证契约；业务 action、默认按钮、自动保存、复选框控制哪个字段，仍由原控件和控制器负责。

静态核查确认三个接入前置：

1. RegMap `WorkbenchTheme::apply` 在当前样式不为 Fusion 时会重新安装 Fusion；若直接安装
   Qlementine，下一次换主题会把它替换。第 3 阶段必须先分开“启动时安装后端”和“更新主题”。
2. ZeroSlack [ProductStyle](../src/ui/qlementinebackend.cpp:35) 的主操作映射只处理
   `QStyleOptionButton`；`CC_ToolButton` 只增加焦点描边。复用前必须支持主操作 QToolButton，
   保留其弹出模式和 QAction 语义，不能通过替换控件类型绕过。
3. RegMap [styleSheet](E:/RegMapWorkbench/RegMapWorkbench/src/app/workbench_theme.cpp:186)
   同时含通用选择器和具体对象规则。需要显式列出交给后端的普通控件规则，以及保留的页头、
   表格、反馈条和专业视图规则；不能直接叠加两套全局 QSS 或删除整份样式表。

### C3：状态徽标与业务状态的边界

ZeroSlack 已把 [phaseText/phaseTone](../src/insights/liveinsightscontextview.cpp:42) 与
[statusChipStyleSheet](../src/ui/insightvisualstyle.cpp:1271) 分开。RegMap 的
[updateProjectHeader](E:/RegMapWorkbench/RegMapWorkbench/src/app/main_window.cpp:8746) 和
[updateSyncPresentation](E:/RegMapWorkbench/RegMapWorkbench/src/app/main_window.cpp:13346)
负责判断未保存、冲突、输出失败等状态，再通过 `state` 属性选择外观。

候选仅接收现成的文字、提示、Info/Success/Warning/Error tone 及应用提供的颜色/密度。
不接受 `LiveInsightSnapshot`、`ProjectController`、寄存器/信号对象或业务回调集合。
RegMap 的 `busy` 当前用 Warning，ZeroSlack 的普通 Building 使用 Info；保留各自映射。
验收包含长文本、窄窗口、深浅主题、状态往返、提示和 accessibleName，不能只比较 QSS 字符串。

### 第 3 阶段执行顺序与退出条件

| 步骤 | 具体工作 | 完成依据 |
| --- | --- | --- |
| 3.0 建立第二应用基线 | 独立构建 RegMap，使用临时设置目录与临时项目，记录真实构建退出码、已有 GUI/core/CLI 结果和截图 | 当前源码成功构建后再运行测试；没有从旧 EXE 或文档引用的“通过” |
| 3.1 验证 C1/C2 | 保留 RegMap 原样式基线，增加可选试验入口；拆开主题安装与更新，适配上述批量编辑与页头动作 | 两应用相同的控件状态契约成立；RegMap 主操作 QToolButton 保持功能 |
| 3.2 验证 C3 | 两边真实状态流驱动徽标，保留状态判断及原文案 | 不增加专业类型依赖，状态显示不影响保存、同步和分析流程 |
| 3.3 分项决策 | 记录候选实现、两边适配点、保留例外、验证结果及维护负担 | 每项明确提取/放弃/暂缓；暂缓有触发条件及截止点 |

试验使用当前固定的 Qlementine 版本及已记录补丁。可以在隔离验证目录使用 Qt-only 原型，
不提前发布 `SuiteUi`、创建稳定 SDK API 或让 RegMap 链接 `zeroslack_core`。
候选基础层只需要 Qt Core/Gui/Widgets；RegMap 原有 `RegMap::Core`、QXlsx、YAML 和根构建
中查找的 GuiPrivate 不自动进入候选依赖。它们继续属于相应应用的既有构建。
只有通过 3.3 的候选才进入第 4 阶段；若全部未通过，本轮结束提取。

第 3 阶段验证矩阵：RegMap Light/Dark × 100/125/150/200% × 原样式/试验动画开/试验动画关；
ZeroSlack 对公共试验改动执行六主题、四档缩放及保护区域回归。先覆盖默认数据与长文本，
再检查 1440×900、960×720 等已有响应式场景；滚动容器采用已批准的“显示区域不越界且
控件滚入后完整可达”口径。原生 Windows 与离屏结果分别记录。

直接复用并保留现有断言：RegMap 的 `appliesWorkbookTheme`、
`structuresCompetitionShellResponsively`、`batchEditsAndCopiesCompleteRanges`、
`batchEditsNumericRangesSafely`、`showsUnifiedSyncStateAndGeneratedResults`、
`reportsBlockedUnsavedSyncAndRecovers`，以及 Bitfield/AddressSpace 的键盘、拖拽取消与导航用例；
ZeroSlack 的控件交互、Qlementine 生命周期、Live Insight 状态与高 DPI 用例。
两边相关控件操作要检查信号次数、取消/禁用后不误触发、Tab/Space/Enter、组合框与焦点、
勾选标记和禁用文字可见；若变更了绘制实现，源码字符串断言只能由更强的实际行为断言替代。

第 2 阶段已确认重复职责及具体适配缺口，**当时尚未量化共享后的维护收益**。第 3 阶段逐项记录
保留了多少应用专属例外、升级时需要修改哪些位置及新增验证成本；不以文件行数或两个
结构恰好有同名字段作为提取依据。上游跨版本升级演练仍是独立未完成项。

## SuiteUi 第 3 阶段：双应用验证与提取结论

执行日期：2026-09-20。**第 3 阶段完成；结束时 C1/C2 的有限范围获准进入第 4 阶段，尚未创建或发布
SuiteUi SDK，也未将 Qlementine 改为正式版默认样式。** 后续 SDK 实施见下一节。应用基线沿用上一节。RegMap
生产仓库保持干净；其试验源码在 ZeroSlack 的 `build/evidence/suite-ui-stage3-20260920/`
内生成。ZeroSlack 验证后已清除试验 CMake 注入并重新构建，正式包未替换。

### 实现与职责边界

可复现原型保存在 [scripts/suite_ui_pilot](../scripts/suite_ui_pilot/)。其中
[overlays.json](../scripts/suite_ui_pilot/overlays.json) 记录五个原文件的字节哈希与修改区间，
[prepare.py](../scripts/suite_ui_pilot/prepare.py) 在核对全部输入后生成隔离副本；基线变化时
拒绝执行。没有把 RegMap 的大型主窗口或测试文件整份复制进版本库。

| 部分 | 公共试验职责 | 保留在应用的职责 |
| --- | --- | --- |
| `pilot_tokens.h` | 普通/悬停/按下/禁用的表面、文字、强调色、强调文字和边框，以及焦点色映射；另有徽标 CSS 试验助手 | 完整主题、字体、专业色、状态到颜色的判断、布局与实际密度 |
| `pilot_style.h` | 控件内部焦点描边；主操作 QPushButton/QToolButton 绘制；避免上游外置焦点框与组合框替换 | 主操作角色标记、QAction、默认按钮与键盘语义、对象生命周期 |
| ZeroSlack 适配 | 原 ProductStyle 使用公共绘制策略，普通控件色改为明确输入 | 六个主题、UiTypography、编辑器和专业图隔离、原状态机 |
| RegMap 适配 | 分开安装样式与更新主题；对页头两个主操作 QToolButton 标记角色 | 主题偏好、10 pt 字体、批量编辑提交/取消、保存/同步、表格与位域数据 |

RegMap 只交出普通按钮和页头动作的两组 QSS 绘制规则，其余应用规则继续保留。
表格、BitfieldView、AddressSpaceView 及其子控件使用应用拥有的 Fusion 边界，避免
专业视图的滚动条被全局样式改变。主题更新保留同一个后端；字体在安装后端前保存，
防止 Qlementine 初始化字体污染应用字体。状态徽标增加换行，以容纳原有长文案。
上述修改均留在试验副本，未写入 RegMap 生产仓库。

公共原型只包含 Qt 和 Qlementine 类型，没有 ProjectController、寄存器、信号或
ZeroSlack 专业类型。RegMap 没有链接 `zeroslack_core`。试验构建暂时从 ZeroSlack 的
`thirdparty/qlementine` 取得固定依赖；第 4 阶段须移入中立 SDK，不能把此源码路径作为
正式的跨仓依赖。Qlementine 仍为 `209e549c415f1d828883f9f21a633eb535f9d67d` / 1.5.0.0，
保留既有一个动画清理补丁，本阶段没有新增上游补丁。

### 验证结果与证据边界

环境为 Windows 11、Qt 6.10.2、MinGW GCC 13.1.0。所有构建进程已取得成功退出码后才运行
对应测试。设置与测试工程采用临时目录，离屏 RegMap 使用单独的 Segoe UI/Consolas 字体目录。
这些字体来自本机，仅作本地测试输入，不纳入源码或分发包。

| 验证 | 结果 | 本地证据（相对 `build/evidence/suite-ui-stage3-20260920/`） |
| --- | --- | --- |
| ZeroSlack 公共原型全量 CTest | **124/124 通过**，169.71 秒；包含六主题、四档缩放及保护区域回归 | `zeroslack-ctest.log` |
| RegMap 生产源码基线 + 修正后的测试驱动 | **4/4 CTest 通过**，58.47 秒；GUI **168 项通过** | `baseline-verified-ctest.log`、`regmap-baseline/tests/regmap_gui_tests.log` |
| RegMap 试验适配 + 同一测试驱动 | **4/4 CTest 通过**，58.32 秒；GUI **168 项通过** | `candidate-verified-ctest.log`、`regmap-candidate/regmap/tests/regmap_gui_tests.log` |
| Light/Dark × 100/125/150/200% × classic/动画开/减少动画 | **24/24 组通过**；每组运行真实控件契约和九个已有工作流用例 | `matrix-verified/matrix-results.json` 及逐组日志/截图 |
| ZeroSlack 清除试验注入后的控件与生命周期回归 | **12/12 通过**，19.98 秒 | `zeroslack-restore-ctest.log` |

GUI 的 168 项为 QtTest 报告口径，包含初始化与清理。24 组矩阵中的 classic 使用
试验应用的 Fusion 路径，因此包含徽标呈现/换行修改；它不是完全未改动的生产源码。
独立 `regmap-baseline` 才是生产源码基线。原型新增契约检查实际按钮的禁用与移出取消、
点击信号次数、Space/Tab、复选框与组合框联动、取消后 Undo 深度不变、主题往返后的
表格字体与列数、1440×900/960×720 页头矩形和 minimumSizeHint，以及真实 Dirty→Saved
状态、tooltip 和 accessibleName。勾选、半选、禁用截图在状态过渡结束后采集。

原有测试的异常没有被删除或跳过。启用真实样式及正确字体后，两种样式都暴露了测试驱动
问题：版本字符串硬编码为 0.2.0，QSS 内容最小高度被误当作控件总高度，以及连续插入时
未等待排队创建的重命名编辑框，后续点击也未确保目标行可见。试验测试副本改为读取构建
版本、验证实际几何下限/包含关系、等待编辑框出现并关闭后再改测试模型，滚动到目标行后
确认命中区域与指针状态。寄存器数量、地址位移、固定地址保护、Undo 和模型断言均保留。
未经修正的结果保留在 `baseline-original-harness.log`、`candidate-original-harness.log` 和
`matrix-original-harness.json`，不将其描述为通过。

原生 Windows 使用临时项目的独立 RegMap 试验窗口，已实际执行两行选择、打开 Batch Edit、
Space 勾选、Tab 到组合框、Down 选择、Cancel 返回，并核对 Saved 状态及原寄存器访问值
保持；随后通过 View 菜单切换 Dark。原生桌面确认焦点可见且操作未崩溃；它不等于原生
四档 DPI、DWM 帧节奏或长时间多窗口验证。组合框原有边角样式在 classic 中同样存在，
本轮没有把它归为共享样式回归。

### 逐项提取决策与维护成本

| 候选 | 第 3 阶段决策 | 依据与后续边界 |
| --- | --- | --- |
| C1 基础 token | **提取有限范围** | 共享普通控件状态色与焦点角色的后端映射。完整主题、专业色、字体、布局和各应用密度继续独立；不因六项默认尺寸相同就强制统一高度 |
| C2 按钮/复选框 | **提取** | 两个应用实际使用同一绘制策略，覆盖 QPushButton 与 QToolButton；焦点、主操作色、polish 安全策略和上游兼容修正可在一处维护。输入与 action 继续由 Qt/应用负责 |
| C3 状态徽标 | **放弃单独组件提取** | 两应用操作与显示验证通过，但共同实现仅为小型 CSS 格式化；业务映射、字体、长文本布局和状态文案仍完全属于应用，新增稳定组件 API 的维护成本尚无收益依据 |
| C4 通用图标 | **移出本轮清单** | 到第 3 阶段评审仍没有出现第二应用的明确共同需求，不无限期保留暂缓项 |
| C5 完整页头/空状态 | **维持放弃整体提取** | 创建、导航、目标选择与反馈动作不同，有限共同绘制能力已由 C1/C2 覆盖 |
| C6 窗口/画布/停靠 | **维持独立评估** | 本轮运行不证明可统一窗口或专业渲染器 |

维护收益限于可明确归属的重复职责：公共控件状态映射、焦点描边、两类按钮主操作绘制、
避免危险 polish 的策略可统一修复；无需在两份适配器中同步这些实现。应用仍分别维护
主题转换、QSS 职责分界、专业区域守卫与动画偏好。一次上游升级必须重跑两应用回归及
RegMap 24 组矩阵；公共接口须只暴露 Qt 类型，隐藏试验中直接出现的 Qlementine Theme。
这是进入最小 SDK 的依据，不是已经测得工时、包体或帧率收益。上游跨版本升级演练仍未完成。

### 复现与第 4 阶段入口

从本仓库运行以下命令，生成副本并核对哈希；RegMap 使用上一节固定提交，ZeroSlack 需保留
本轮 Qlementine 预览源码。`--check` 可只核对输入。若源文件变化，先人工复核和重基差异，
不通过更新哈希绕过检查。

```powershell
python scripts/suite_ui_pilot/prepare.py --regmap E:/RegMapWorkbench/RegMapWorkbench --output build/suite-ui-pilot/prototype
```

候选的 CMake 源目录为生成的 `prototype`，参数为 `REGMAP_SOURCE`、`ZEROSLACK_SOURCE`、
`CMAKE_PREFIX_PATH` 和 `SuiteApp_DIR`。本机依赖为 `E:/QT6/6.10.2/mingw_64`、
`E:/SuiteRuntime/install-release/lib/cmake/SuiteApp`；QXlsx/yaml-cpp 使用 RegMap 已有源码缓存，
分别传入 `FETCHCONTENT_SOURCE_DIR_QXLSX` 和 `FETCHCONTENT_SOURCE_DIR_YAML-CPP`，并设
`FETCHCONTENT_FULLY_DISCONNECTED=ON`。独立构建 `regmap_gui_tests`、`regmap_core_tests`、
`regmap_cli_tests`、`regmap_suiteapp_integration_test`、`regmap_contract_test`；
`regmap_native_review` 是五分钟自动退出的临时项目窗口。

纯基线的 CMake 源目录仍为 RegMap，通过 `CMAKE_PROJECT_TOP_LEVEL_INCLUDES` 指向生成的
`baseline-hook.cmake`，只替换测试驱动并隔离配置。ZeroSlack 原型同理使用
`zeroslack-hook.cmake`，开启 `ZEROSLACK_ENABLE_QLEMENTINE=ON`；完成后清空该注入参数并重建。
当前常规 ZeroSlack 构建已完成这一恢复操作。

运行完整 RegMap CTest 时设 `QT_QPA_PLATFORM=offscreen`、`QT_QPA_FONTDIR`、`PILOT_THEME=light`，
以及 `REGMAP_PILOT_STYLE=classic` 或 `qlementine`，Qt/编译器 DLL 目录加入 PATH。
矩阵入口如下；输出包含测试程序与字体哈希，进程退出码和日志位置。

```powershell
python scripts/suite_ui_pilot/run_matrix.py --build build/suite-ui-pilot/candidate --output build/suite-ui-pilot/matrix --font-dir build/evidence/suite-ui-stage3-20260920/fonts --runtime-dir E:/QT6/6.10.2/mingw_64/bin --runtime-dir E:/QT6/Tools/mingw1310_64/bin
```

下一步只为 C1/C2 建立仓外中立的静态 `SuiteUi`：Qt-only 公共接口、私有 Qlementine 后端、
精确版本 CMake 包、资源/许可清单，以及分别构建、固定版本、升级和回退的双应用样例。
先完成这些构建消费条件，再决定接入其他应用；原生窗口专项和正式版默认样式仍分别验收。

## SuiteUi 第 4 阶段：独立 SDK 与双应用接入

执行日期：2026-09-20。已建立独立源码与安装包，并将经过验证的 C1/C2 接入两应用的
默认关闭构建选项。本轮没有提交、推送或替换正式包。源码在
`E:/SuiteUi/SuiteUi`，版本为 **0.1.0**；本地 Git 分支为 `codex/suiteui-bootstrap`，
尚无初始提交或远程地址。当前安装前缀为
`E:/SuiteUi/install/0.1.0-qt6.10.2-mingw13.1-release`。

### 最小 API 与绘制边界

公共头 `SuiteUi/ControlStyle.hpp` 只包含 Qt 类型。`ControlPalette` 接收表面、文字、
强调色、强调文字、边框的普通/悬停/按下/禁用状态及焦点色；`setButtonRole` 只标记
主操作，不改变 default button 或 QAction。`ControlStyle` 接收应用拥有后移交给它的
基础 QStyle，负责 **QPushButton、QToolButton、QCheckBox** 的绘制、尺寸与内部焦点。
其余控件交回基础 QStyle；应用继续拥有主题选择、字体、调色板、布局、密度和专业区域。

相较第 3 阶段原型，SDK 没有把整个 QlementineStyle 安装为全局样式。它把 Qlementine
作为私有绘制器，只转发上述三类控件；避免原型中未获提取批准的组合框、输入框、表格和
滚动条随之变化。后端不执行上游 widget polish，也不改写应用字体或调色板。原生 Qt 控件
继续处理焦点、键鼠、默认按钮和无障碍。验证实际比较非按钮区域的像素与 sizeHint，
并检查主题/动画切换后的应用字体、调色板和 tooltip 调色板。

Qlementine 固定为 `209e549c415f1d828883f9f21a633eb535f9d67d` / 1.5.0.0，仍仅有既存
`stop-all.patch`。未增加上游补丁，也未把 C3 徽标、C4 图标、C5 页头或 C6 窗口/画布
重新纳入 SDK。RegMap 的长状态文案换行和两个应用的专业视图边界仍属于各自应用。

### 构建、版本与分发

SDK 独立 CMake 构建生成静态 `SuiteUi::Controls`。消费者只使用安装前缀与
`find_package(SuiteUi 0.1.0 EXACT CONFIG REQUIRED)`，不读取另一应用的源码。
Qlementine 静态库位于安装包的 `lib/suiteui/private`；不导出上游头文件、窗口组件或
样式插件。Qt Widgets/Svg 是链接依赖；SuiteApp 和业务模型不是依赖。

当前二进制配置为 Windows x64、Qt 6.10.2、GNU 13.1.0、Release；包检查精确 SDK/Qt
版本、编译器和构建配置。0.1 只接受显式 `CMAKE_BUILD_TYPE` 的单配置生成器，每种
工具链/配置使用独立前缀，不把 Release 静态库默默用于 Debug。Qt/编译器 DLL 仍由
应用自行部署。SDK 的图标与字体嵌入静态资源，运行时不读取源码或安装前缀。

`suiteui_install_notices(DESTINATION licenses/SuiteUi)` 随消费者部署完整的 SDK、MIT、
OFL、Apache 许可、字体逐文件来源、上游提交和补丁及 `build-info.json`。ZeroSlack
预览打包脚本从实际安装 SDK 复制该目录；RegMap 通过 CMake Runtime 组件安装。
仅供离屏测试的本机 Segoe UI/Consolas 字体目录没有纳入 SDK 或预览包。

| 消费者 | SDK 入口 | 原样式与回退 | 留在应用的适配 |
| --- | --- | --- | --- |
| ZeroSlack | `ZEROSLACK_ENABLE_SUITEUI=ON`，同时关闭 `ZEROSLACK_ENABLE_QLEMENTINE`；设置 `SuiteUi_DIR` | 默认两选项均 OFF；SDK 预览启动 `--ui-style=classic` 可对照；关闭 SDK 后重新构建可完全移除它 | `suiteuibackend.cpp` 映射六主题和主操作角色，以 RoundedIcons::Style 为后备；既有编辑器/圆环菜单/专业图守卫保留 |
| RegMap | `REGMAP_ENABLE_SUITEUI=ON`；设置同一安装包的 `SuiteUi_DIR` | 默认 OFF；SDK 构建启动时可设 `REGMAP_UI_STYLE=classic`；OFF 独立构建不需要 SDK | `suiteui_adapter.cpp` 映射 Light/Dark、减少动画偏好及主操作 QToolButton；保留其余 QSS、表格/位域/地址图与业务状态 |

RegMap 源码已正式接入这些可选构建路径，区别于第 3 阶段仅修改试验副本。其测试现在
隔离临时设置并应用真实主题；旧版本字符串和排队创建重命名编辑框的测试驱动修正已保留，
没有删除或跳过失败用例。CLI/core 不链接 SuiteUi；ZeroSlack CLI 沿用原有共享 core 边界，
本次未重构其既有 GUI 依赖，也没有使 SuiteApp 协议库依赖 UI SDK。

### 本轮验证

证据目录为 `build/evidence/suite-ui-stage4-20260920/`。只在对应构建成功退出后运行测试。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 从仓外 SDK 源码独立构建、安装 | 成功；SDK CTest **1/1**，QtTest **5 项通过** | `sdk-independent-build.log`、`sdk-independent-ctest.log`、`sdk-neutral-install.log` |
| 安装包独立消费者 | 最终安装包重建、绘制、版本/嵌入资源检查与安装成功；系统 PATH 下离屏运行退出 0 | `consumer-final-configure.log`、`consumer-final-build.log`、`consumer-deployed-smoke.json` |
| 版本与配置拒绝 | 错误版本 0.1.1、Debug、空配置均在配置阶段拒绝 | `package-guard-results.json`、`reject-*.log` |
| ZeroSlack SDK 全量回归 | **124/124**，111.30 秒；包含六主题/四档 DPI 与专业区域 | `zeroslack-sdk-ctest.log` |
| ZeroSlack 链接最终独立 SDK 后的相关回归 | **12/12**，31.46 秒 | `zeroslack-sdk-final-ctest.log` |
| RegMap SDK 全量回归 | **5/5**，59.82 秒；GUI **168 项通过** | `regmap-sdk-ctest.log`、`regmap-sdk/tests/regmap_gui_tests.log` |
| RegMap 主题/缩放/动画矩阵 | **24/24**；真实控件操作及九项既有工作流 | `matrix-final/matrix-results.json` |
| RegMap 关闭 SDK 独立构建 | **5/5**，58.24 秒；最终后端断言复测 **1/1**；不需要 SuiteUi 安装路径 | `regmap-classic-ctest.log`、`regmap-classic-final-controls.log` |
| ZeroSlack 关闭 SDK 和 vendored 后端 | **116/116**，95.10 秒；移除八项仅可选后端构建才注册的用例，未删除任何测试源码 | `zeroslack-classic-build.log`、`zeroslack-classic-ctest.log` |
| 两应用隔离部署 | Windows 系统 PATH 下 `--version` 均退出 0；SDK notices 已安装 | `deployment-smoke.json`、`zeroslack-sdk-package.log`、`regmap-sdk-install.log` |
| 依赖边界 | 两应用 classic 的构建命令与 RegMap CLI 命令均不引用 SDK/后端静态库 | `dependency-boundaries.json` |

矩阵包含 Light/Dark × 100/125/150/200% × classic/动画开/减少动画。额外断言实际启用的
后端名称，防止 SDK 构建意外运行 classic 却被计作 SDK 通过。矩阵保留可执行文件和字体
哈希、退出码、实际窗口/批量编辑截图。最后只增强测试中的后端断言，分别重建 SDK/OFF
契约测试并重跑矩阵；应用实现未因此改变。SDK 安装包的最后修正只收紧 CMake 配置检查，
重新链接后已复测相关控件；没有用旧可执行文件验证新代码。

独立部署验证限于启动与依赖加载，不能作为原生交互验收。以上为第 4 阶段首次提取时的
记录，当时只验证了精确版本拒绝、重新安装消费与取消依赖回退。后续 0.1.0→0.1.1
逐个升级和回退的实测见下节；上游 Qlementine 跨版本兼容性仍未验证。升级应安装新前缀，
旧版本前缀和原样式入口均应保留。

### 复现与后续边界

SDK 构建命令在 `E:/SuiteUi/SuiteUi/README.md`。两应用分别正常配置，仅增加上表中的
构建选项和 `SuiteUi_DIR`；RegMap 本机现有的 SuiteApp、QXlsx、yaml-cpp 依赖与以前相同。
RegMap 矩阵入口新增 `--sdk`：

```powershell
python scripts/suite_ui_pilot/run_matrix.py --sdk --build build/evidence/suite-ui-stage4-20260920/regmap-sdk --output build/suite-ui-sdk/matrix --font-dir build/evidence/suite-ui-stage3-20260920/fonts --runtime-dir E:/QT6/6.10.2/mingw_64/bin --runtime-dir E:/QT6/Tools/mingw1310_64/bin
```

第 3 阶段的 `prepare.py` 仍用于当时固定基线；当前 RegMap 源码已变更，输入哈希校验会
正确拒绝它，不能更新哈希绕过历史基线。第 4 阶段直接使用两个应用的真实源码与安装 SDK。
独立测试包位于证据目录的 `zeroslack-deployed` 和 `regmap-deployed`，不覆盖正式目录。
ZeroSlack 常规构建目录已恢复两种可选后端均 OFF 的 classic；SDK 预览及其配套 DLL
保留在独立部署目录，不能把旧预览 EXE 与新 classic core DLL 混用。

SDK 构建的原生桌面复核与第三应用推广进度见下节。原生专项的未验项继续保留，其他应用
和正式版默认样式不因静态 SDK 提取成功而自动切换。

## SuiteUi 剩余项与第 5 阶段（2026-09-20）

本轮沿用开工时的脏工作区，未提交、推送、改变应用版本或替换正式包。证据与逐文件
差异在 `build/evidence/plan-remainder-20260920/report.md`。以下计数均来自本轮实际构建。

### SDK 逐个升级与回退

SuiteUi 0.1.1 新增向后兼容的 `setTransitionDuration(int)` / `transitionDuration()`：
默认仍为 192 ms，负值明确拒绝，0 ms 立即完成；关闭动画优先于时长配置。新增像素
断言区分 0/600 ms 与减少动画；SDK CTest 1/1、QtTest 6 项通过。冻结的 0.1.0 消费者
仅调整精确版本与安装前缀后编译运行成功，业务代码未修改。

| 步骤 | ZeroSlack | RegMap | 应用修改成本 |
| --- | --- | --- | --- |
| 只升级 ZeroSlack | 0.1.1，124/124 | 保持 0.1.0，5/5 | ZeroSlack CMake 1 行版本与配置前缀；不改另一应用 |
| 再升级 RegMap | 保留前一步结果与独立原生部署 | 0.1.1，5/5 | RegMap CMake 1 行版本与配置前缀；不改业务代码 |
| 分别回退 | 0.1.0，124/124 | 0.1.0，5/5 | 分别恢复各自精确版本和前缀 |

两个前缀并存：`E:/SuiteUi/install/0.1.{0,1}-qt6.10.2-mingw13.1-release`，实际链接行
分别保存在每一步的 `sdk-links.json`。0.1.0 源码快照与安装目录逐文件哈希保持不变。
ZeroSlack 最终常规构建恢复两个后端均 OFF，116/116；旧预览 EXE 和生命周期测试 EXE
已从该 classic 构建目录删除。升级与回退耗时见下方决策材料，不推广为一般成本结论。

### Pinloom 默认关闭的控件适配

接入前静态核对见 `pinloom-audit.md` 及 163 个文件的哈希清单。实际主操作是带
`pinloomControl=primary` 的 QPushButton，另有真实复选框与 QToolButton。保留原 Qt
样式并显式拒绝无法重建的样式；仅分离通用按钮 QSS，应用专属 accent、删除按钮、
PDF/区域选择/预览、列表表格及后续子控件继续使用应用原有绘制边界。

新增 `PINLOOM_ENABLE_SUITEUI=OFF`，ON 精确消费 SuiteUi 0.1.1；适配器留在 Pinloom，
公开头只依赖 Qt/Pinloom 类型。源码无 Qlementine 类型引用。OFF 不传 SuiteUi 安装路径，
实际链接行无 SDK；原样式 QSS 模板及替换参数与开工版本完全一致。

最终 SDK/OFF 各 **14/14**（原 13 项保留，新增 1 项真实控件测试）；Light/Dark ×
100/125/150/200% × classic/动画开/减少动画，共 **24/24**。矩阵使用真实捕获对话框、
相同数据与按可用屏幕推导的滚动宿主尺寸，断言实际后端、Save/Cancel、复选框、标签
创建、主题往返和保护树重挂载。它是离屏控件验收，不是 Pinloom 原生完整窗口验收。

初次矩阵虽通过，截图复核仍发现换主题时残留旧动画颜色、主操作文字色被 QSS 覆盖。
已修复为换主题先清理过渡并显式映射 SDK 主文字色，新增背景及文字像素断言后重新
构建、重跑 14/14 与 24/24。初次和修复后证据分别保留，既有断言没有删除或放宽。

### 原生及规范边界

SDK 0.1.1 原生复核覆盖 ZeroSlack 六主题、键盘左右/四角贴靠、部分浮窗操作；RegMap
覆盖其实际 Light/Dark 两主题与部分贴靠。当前实测系统缩放 150%。工具限制与单屏
硬件使四档系统 DPI、边缘拖动、完整三面板连续拖回、跨屏/断屏未验齐；DWM 采样未
获得有效呈现时间戳，不报告原生帧率。尚未确认产品缺陷，异常按键结果仍需独立复现。
逐项证据和未验原因维护在 [原生缺口清单](unverified-manual-checks.md)。

动效数字统一到 [9 个场景](control-style-consolidation.md#动效场景规格2026-09-20)：
已实现并实测 3、已实现未实测 2、仅规格未实现 4；9 类其余控件有状态/语义尺寸/来源/
可失败断言映射，未覆盖的状态组合明确列出。四项独立评估与默认样式材料见下文；
Kernel 后续四项只在方案中补触发条件和截止点，本轮没有实现这些功能。

## 四项候选的书面评估（2026-09-20）

本节只评估固定版本，不下载、vendoring 或接入代码。窗口/停靠/专业画布不属于 C1/C2，
不能因控件 SDK 已通过回归而获得实施许可。以下许可核对针对所列版本；升级须重新核对。

### QWindowKit 1.5.0：暂缓

[固定版本说明](https://github.com/stdware/qwindowkit/tree/1.5.0)提供窗口 agent、系统按钮
命中与 Windows Snap 集成。它与 ZeroSlack 的 `windowsnapchrome.h`/`workspacechrome.h`
职责直接重叠，不能在同一 HWND 上并行处理非客户区消息；RegMap 当前使用原生标题栏，
没有已确认的同类替换需求。本轮 SDK 的键盘贴靠证据缩小了缺口，但不覆盖拖到屏幕边缘。

采用 [Apache-2.0](https://raw.githubusercontent.com/stdware/qwindowkit/1.5.0/LICENSE)：分发时
保留许可证、版权及适用 NOTICE，标注修改；不授予商标使用权。若未来接入，需固定其
qmsetup 子模块，并逐项核对私有 Qt 接口、Qt 6.10.2/MinGW 13.1 编译兼容性、Snap 菜单、
系统调整大小、圆角/材质、最大最小化、跨 DPI、无障碍及撤回旧消息处理的回退方法。

复评截止为 **2026-10-31**：只有现有窗口代码出现可复现、未能在原责任层解决的原生
缺陷，且可写出同场景对照用例时才启动隔离验证；到期未触发则转为放弃本轮候选。

### Qt Advanced Docking System 4.4.1：放弃本轮接入

[固定版本](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/tree/4.4.1)
提供分组停靠、浮动、自动隐藏与布局保存。ZeroSlack 已有按文档绑定、section 竖栈、指定
插入线、固定/临时视图和共享文档的专门生命周期；ADS 不是这些语义的直接替代。RegMap
当前为固定 splitter 布局，本轮未发现第二个应用需要任意停靠。仅为了样式一致而替换会
扩大对象所有权、布局持久化、关闭/释放与焦点的迁移范围，没有经实测确认的收益。

[许可正文](https://raw.githubusercontent.com/githubuser0xFFFF/Qt-Advanced-Docking-System/4.4.1/LICENSE)
及 [源码声明](https://raw.githubusercontent.com/githubuser0xFFFF/Qt-Advanced-Docking-System/4.4.1/src/FloatingDockContainer.cpp)
为 LGPL-2.1-or-later。分发需保留许可/版权，提供适用的库及修改源码，并满足用户替换或
重新链接库的要求；不能只以“动态链接”宣称义务已履行。若新需求另立提案，前置验证须
覆盖上述全部现有语义、旧布局迁移、至少三面板连续拖回指定位置、DPI 与无障碍，并先
确定合法可复现的构建/重链接材料。本轮不新建此依赖。

### 画布公共工具：暂缓

评估对象限于已有 Qt **6.10.2** 与应用自有代码，不另选通用节点/图形库。可候选的是缩放
控件、可用视口边界和导出对话框；Kernel 信号身份、Wave 时间坐标、RegMap 位域和 PDF
页坐标不具有可互换语义。当前专业渲染器仍各自所有，不能将局部工具提取扩大为统一基类。

[Qt 6.10 许可说明](https://doc.qt.io/qt-6.10/licensing.html)区分商业与开源许可及不同模块；
延续现有 Qt 分发义务，不假设所有模块同一许可。提取自有代码前核对对应文件的版权与
许可，随 SDK 保留原声明；本轮不拷贝代码。前置为两个实际应用提供相同工具契约与独立
用例，证明坐标、焦点/滚轮和导出所有权不泄漏，记录输入延迟与无 SDK 回退。

截止 **2026-11-30**：须出现第二应用至少一个同契约需求并给出两个真实调用点和可失败
断言；到期没有则转为放弃，不以公共工具为名继续挂起专业画布迁移。

### Lucide 0.468.0：放弃本轮接入

[该版本许可](https://raw.githubusercontent.com/lucide-icons/lucide/0.468.0/LICENSE)包含
Lucide 的 ISC 与衍生自 Feather 部分的 MIT 声明；所分发图标需保留适用版权和许可文本，
修改后同样保留。不能以当前主分支许可代替固定版本审计。

通用 SVG 动作图标可减少常用符号的自绘工作，但不能替换用户已确认的 ZeroSlack 品牌
及专业图标语义。第 3 阶段 C4 到截止点仍缺少第二应用的明确共享需求，本轮静态核对
也没有形成需统一的新动作清单，因此维持移出本轮范围的结论。新提案至少需列出两应用
三个相同动作、授权来源、状态着色和缺失图标策略，再验证四档 DPI、基线对齐、深浅色
对比度、资源体积及可访问名称；不能以图标库存在推定有接入收益。

## 正式默认样式决策材料（2026-09-20，一页）

**历史决策材料，已由 2026-09-21 用户“切换”指令作出默认启用决定**。
以下保留切换前的证据、成本和未验项；当前发布范围与回退入口见本文开头。

| 方面 | 可核实证据与限制 |
| --- | --- |
| 可见收益 | 既有同场景 12 帧离屏采样 A=2/C=11 个不同图像，证明 C 有插值；`nativeFramePacingMeasured: false`。本轮 0.1.1 的 0/600 ms 与减少动画优先级有像素断言，仍不证明原生流畅度 |
| 独立升级 | ZeroSlack 单独升 0.1.1 时 124/124，RegMap 留在 0.1.0 时 5/5；RegMap 随后升级 5/5；分别回退 124/124、5/5。每次仅改对应应用 CMake 的 1 行版本与配置前缀，没有被迫改业务代码或同步另一应用 |
| 本机成本 | ZeroSlack 升级/回退构建 223.578/227.657 秒、串行测试 327.44/326.12 秒；RegMap 对应构建 29.782/50.985 秒、测试 68.62/76.95 秒。只适用于此次机器、缓存和配置，不外推长期成本 |
| 维护与风险 | 两份固定 Qlementine 均保留 1 个 stopAll 上游补丁；应用继续维护 QSS/字体/专业视图边界。SDK 升级已验，上游 Qlementine 跨版本迁移成本尚未实测；Pinloom 初次截图复核仍暴露主题过渡和文字色的适配问题，需要单独回归 |
| 原生证据 | ZeroSlack 六主题、键盘左右/四角贴靠及部分浮窗操作；RegMap Light/Dark 与部分贴靠。单屏 150% 已独立测量。四档系统 DPI、完整连续拖回指定插入线、鼠标边缘 Snap、跨屏/断屏与有效 DWM 呈现间隔仍缺完整证据 |
| 切换后的责任 | 默认启动、所有字体/主题、控件与业务输入、可访问性、原生窗口、资源/许可打包及回退均成为默认路径责任。ZeroSlack CLI 经 `libzeroslack_core.dll.a` 间接链接 SDK，不能继续宣称 CLI 与 UI 依赖隔离；须接受此分发成本或先拆分链接边界。其他应用不会因此自动同步升级 |

做默认切换决定前还需：补齐以上原生缺口与可复现呈现测量；验收快速反向/连续操作和
自定义字体；验证正式打包许可与干净环境启动、旧配置升级/回退；明确上游补丁维护人、
版本策略及 CLI 依赖处置。若保留未验项，也必须由用户明确接受其范围，不能写成已通过。
原始数据、失败记录与逐项边界见 `build/evidence/plan-remainder-20260920/report.md`。
