# AppSuite integration

ZeroSlack is an independent application in the AppSuite family. This document is
the single ZeroSlack-side record of that family contract: the transport protocol, the
implemented cross-application workflows, and the shared visual and interaction language.
Application state always stays with its owner.

ZeroSlack uses Ela for its maintained UI. SuiteApp remains the independent
protocol and automation integration; it does not choose the application UI style.
Obsolete control-preview integration instructions have been retired.

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
wave projects, register-map projects, xIPs libraries, or ZeroSlack workspaces.

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
| xIPs | `xips://show`, `xips://asset/<id>?revision=<n>` | `xips.library.open`, `xips.asset.open` | `xips.library` (`native`, external fallback) |

xIPs 2.1.0 registers through the optional `SuiteApp::suiteapp` SDK. Resource
resolution reads the current library's cached metadata, including the exact
revision and recorded digest; it does not verify or expose snapshot payloads.
The provider's actions only open the library or select an asset. Collect, Use,
and Update remain explicit UI operations. The native component is
`xips-browser.dll`, ABI v1; hosts must match Qt, compiler, architecture, and Ela.
An absent runtime leaves standalone and native library use available.

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
|   |-- xIPs/
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
Its `XipsDirectory` input supplies `xips.exe`, `xips-cli.exe`, `xips-browser.dll`,
the icon, and private Qt/Ela dependencies. The manifest records xIPs as `xips`
with native surface ABI 1. There is no additional xIPs runtime process.
`ReplaceExisting` removes the old destination and moves staging; replacement is not transactional.
ZeroSlack also retains the `pinloom-host/v1` context adapter; the suite protocol does not remove that compatibility path.

## Implemented workflows


### ZeroSlack context panels

Source, analysis and Pinloom resources use independent Context Workspace panels.
The Workspace Hub summary panel and its background refresh/preview pipeline were
removed on 2026-09-23. Restoring an older session discards its Hub records from
docked, floating and document-bound layouts without recreating a missing panel.
Shared associations, source deep links, Pinloom bindings and public Suite contracts
remain in use; applications retain ownership of their private data.

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
- xIPs uses coral for a compact X on stacked asset/revision plates, with a
  graphite tile and a cyan layer connecting it to the family palette.

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
- identity: ZeroSlack, Pinloom, WaveWorkbench, RegMapWorkbench, and xIPs keep distinct
  accent/icon identities while using the same state semantics.

Every implementation must preserve system light/dark adaptation, 100-200%
scaling, accessible names, focus indicators, reduced-motion behavior, and the
current saved panel/window geometry.

## 四项候选的书面评估（2026-09-20）

本节保留独立候选的固定版本评估，不下载、vendoring 或接入代码。
窗口、停靠与专业画布的变更仍须独立授权。以下许可核对针对所列版本；升级须重新核对。

### QWindowKit 1.5.0：暂缓

[固定版本说明](https://github.com/stdware/qwindowkit/tree/1.5.0)提供窗口 agent、系统按钮
命中与 Windows Snap 集成。它与 ZeroSlack 的 `windowsnapchrome.h`/`workspacechrome.h`
职责直接重叠，不能在同一 HWND 上并行处理非客户区消息；RegMap 当前使用原生标题栏，
没有已确认的同类替换需求。已有键盘贴靠证据不覆盖拖到屏幕边缘。

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
