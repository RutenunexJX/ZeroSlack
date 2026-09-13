# ZeroSlack Current Plan

Product version: `v0.28.0`

## Current baseline

Current behavior is documented in the README and user manual. Released changes
and completed implementation history are recorded in CHANGELOG.md and Git.

## Context 整侧栏开关 — 2026-09-13 (v0.28.0)

- 新增注册表管理的 `View > Context Sidebar` / `Ctrl+2`，统一显示或隐藏整个 Context 侧栏，保留 section、视图、顺序与宽度。
- 侧栏为空时禁用显示入口，命令返回明确原因；通过 rail 打开内容后可用。

## 浮层/侧栏 第 4 期 · 可折叠竖栈与拖动 — 2026-09-13 (v0.27.0)

- 四期功能实现已完成：放置维度、原生浮层、多实例/文档绑定、单 dock 内的竖栈与跨 surface 拖动。
- 第 1 步检查点：完整构建，指定回归 10/10，加窄布局共 11/11，四个 DPI 变体各 163/163，全量 106/107。
- 第 2 步已完成：客户区拖回手柄、插入位置指示与不可分离资源限制落地；最终四缩放各 175/175，指定回归加窄布局 11/11，实际 v5 导入 177/177，全量 106/107 与基线一致。
- v6 保留资源列表，独立保存 section 折叠和高度；v5 迁移仅展开原活动项。旧读端忽略 v6 Context 状态。
- 原生桌面观感、拖放手感、多显示器与连续重启实测仍未测；见 [验收记录](docs/sidebar-stack-review.md)。

## 浮层/侧栏 第 3 期 · 多实例与文档绑定 — 2026-09-13 (v0.26.0)

- 第 1 步检查点已达标：多实例、统一收起和 rail 右键菜单，指定回归 10/10，全量 106/107 与基线一致。
- 第 3 期两步实现及自动化验收已完成：文档绑定、关闭通知和每文档布局落地，四个 DPI 变体各 151/151，指定回归 10/10，全量 106/107 与基线一致。布局按相对路径归档，最多保留 32 个最近使用文档。
- v5 保存实例与收起状态，恢复上限 16；保留 v4 标量几何与读取兼容，旧构建无法恢复 v5 Context 状态。
- 原生边框增加显示后自纠正；真实跨屏与连续重启手工验证仍未测。
- 此阶段未包含竖栈与拖动，现已由第 4 期接续；证据见 [floating multi review](docs/floating-multi-review.md)。

## 浮层/侧栏 第 1 期 · 地基 — 2026-09-12 (v0.25.11)

- 将 Context 放置拆分为 surface、persistence、binding 三个维度；本期只支持原有三种组合，不含新功能。
- 集中能力映射与 rail 切换规则，保持视图复用、宽度/尺寸记忆以及 v3 工作区状态双向兼容。
- 未纳入版本管理的两个旧 `.zs` 样本保持缺失；相关测试不修改、不跳过，106/107 的唯一已知失败被接受。
- 后续三期依次为：真浮层单实例 → 多实例与绑定 → 侧栏竖栈。本期不提前实现。
- 验收证据见 [context placement review](docs/context-placement-review.md)。

## 浮层/侧栏 第 2 期 · 真浮层单实例 — 2026-09-12 (v0.25.12)

- 已完成可分离 Context 的原生 Tool 窗口、失焦透明度和工作区几何记忆。
- 修正超出 200% DPI 可用区域的测试夹具；四个 DPI 变体各 104 项通过，指定回归 10/10，全量 106/107 与基线一致。
- 原生跨屏交互及 show 前后 frameMargins 时序未测；0.25.12 正式包已于 2026-09-13 更新。
- temporaryEditor 显式不可分离，继续使用原 overlay；单实例互斥和跨容器移动保持同一 view。
- Context 状态升至 v4，完整读取 v3，旧构建不支持恢复 v4；屏幕缺失或标题栏不可达时回落主屏。
- 第 3 期仍为多实例/绑定/每文档布局及批量操作，第 4 期仍为侧栏竖栈与拖动手势，本期不实现。
- 验收和原生桌面验证限制见 [floating context review](docs/floating-context-review.md)。

## 语义可用性 第 1 阶段 — 2026-09-12 (v0.25.10)

- 编辑停止 250 ms 后，只对当前工程内、有已发布基准的 Dirty 文件提交单文件 EditIdle 请求。
- 工作线程以分类器和 token 位置映射兼容性为门禁；仅注释与空白改动恢复 Current，继续保留未保存状态。
- 非 trivia、错误树和不兼容映射明确丢弃；不调用 Slang、不升级分析范围、不写语义缓存，不改变 action 的 SemanticCurrent 要求。
- 保存、关闭、工程变更、禁用策略及退出取消定时器；后续编辑沿用版本失效机制。
- 验收与量化证据见 [edit-idle review](docs/edit-idle-review.md)。更大语义影响面的空闲分析留待后续阶段。

## Compact layout — 2026-09-12 (v0.25.9)

- Editor tabs stop shrinking and scroll; the rule survives MainWindow style refresh and split creation.
- Context and specialized insight toolbars wrap at control boundaries. Header actions occupy a separate row; no dock minimum/default width is increased.
- Temporary editor Context titles follow the source editor tab; internal identities remain in resource data and tooltips.
- Added independent compact-layout invariants and retained the seven existing regression tests. Evidence and quantitative results: [compact-layout review](docs/compact-layout-review.md).
- Removed only the verified superseded local release copies 0.25.2, 0.25.4, 0.25.5, 0.25.6 and 0.25.7. Current release copies, build products and historical evidence remain.

## Pending UI refinement — requirements synchronized 2026-09-11

弱边框部分纳入 v0.25.8，见下文；其余内容仍为候选。当前不新增动效。

### 弱边框与表面层级

- 用轻微背景明度差区分区域：编辑区最亮，侧栏和底栏略暗；沿用现有主题、布局及各专用图表逻辑。
- 固定面板保持连续、平坦，减少框中框和密集分隔线，仅在必要的调整尺寸边界保留弱分隔线。
- 浮窗使用轻微描边和柔和短阴影；不得恢复圆环菜单透明窗口的 Windows 矩形阴影。
- 选中行使用低饱和强调色，普通行保持统一背景。
- 已查看侧对话参考图，仅采纳表面层级和弱边框方向。Project/Settings 继续遵循当前图标入口规则，图表内容仅为示意。参考图本地路径：`C:/Users/14971/.codex/generated_images/01a08fac-ef9b-7631-8600-2494f37b4d37/exec-cb77852e-9d66-4423-a56d-cc2388ebbf86.png`。

### 克制的微动效

以下时长为建议初值，尚非逐项确认的硬性参数。

- 第一轮优先：按钮背景悬停/按下 80–120ms；Files/Design/Pinloom 条目底纹 80–100ms；信息浮窗原位淡入约 100ms、关闭约 60ms；圆环菜单及子项条淡入/底纹过渡 60–100ms。图标不缩放、不弹跳。
- 后续候选：侧栏宽度过渡 120–160ms，先验证编辑器重排及图表性能；底栏沿用现有约 140ms；分段选中底纹 100–140ms；Activity 数字更新时背景轻微提亮一次 120–180ms；复制成功图标短暂改为勾号约 800ms。
- 动画应可中断，菜单立即可操作；代码输入、光标、滚动、图表拖动和连续缩放保持即时响应。
- 建议统一“减少动画”设置，优先复用 Qt Widgets、InsightVisualStyle 和 PanelLayoutController 现有主题、密度及动画基础。

## Weak borders — 2026-09-11 (v0.25.8)

- Fixed panel boundaries, tab/toolbar separators and table headers use subtle theme-relative border colors; panel title labels use a bottom separator instead of a surrounding box.
- Information popups and the radial menu use neutral, low-contrast outlines. The radial popup retains its disabled Windows rectangular shadow.
- Focus indicators, selected markers, graph internals, layout and existing animation behavior remain unchanged; no new animation is introduced.
- Light/dark screenshot review and four relevant regression checks passed.

## Popup shadow correction — 2026-09-11 (v0.25.7)

- Disabled the native Windows shadow on the transparent radial popup to remove rectangular lines along its right and bottom edges.
- Added a native window-class regression check: it reproduced the shadow before the fix and passed after it. Menu execution and dismissal tests passed.

## Implemented context menu — 2026-09-11 (v0.25.6)

- Replace the editor context menu with a compact icon-only ring and a separate rounded rectangular action bar. Keep existing semantic commands and specialized graph panels.
- Give each action a distinct glyph; show executable actions in the theme accent color and unavailable actions in gray. Show names and unavailable reasons on hover.
- Keep undo, redo, cut, copy, paste, select-all, go-to-definition and go-to-line outside this menu; retain their existing shortcuts.
- Seven relevant regression checks passed. Native Windows popup tests and light/dark rendering checks at 100%, 150% and 200% scale passed.

## Implemented UI changes — 2026-09-11 (v0.25.5)

以下六项纳入 v0.25.5。构建及七项相关回归检查通过；Windows 原生测试验证了图标悬停像素变化和最大化／最小化操作。

1. **Project 图标**：仅显示图标，移除按钮上的名称；保留悬停提示和无障碍名称。
2. **图标悬停背景**：Project 及顶栏最大化、最小化按钮在鼠标悬停时显示清晰可见的背景反馈，颜色跟随当前主题。
3. **功能图标再次点击收起**：对应功能窗口已打开时，再次点击其图标应收起该窗口；再次打开时恢复。设置仍沿用中央标签页的打开方式，不放入侧栏。
4. **列表行底纹**：Files、Design、Pinloom 取消奇偶行交替底色；统一使用轻微染色的基础底色、灰色悬停底色和主题强调色选中底色，选中条目悬停时进一步加深。
5. **完整移除 Focus Mode**：删除功能实现、菜单及命令入口、快捷键和相关状态保存／恢复逻辑，同步清理文档与相关测试；普通侧栏收起功能保留。
6. **Commands 过滤**：`Ctrl+Space` 面板的 `Commands` 不显示 F24 专用指令；F24 Command Mode 自身的指令保留。

## Repository maintenance

- Classify first-party sources under `src/`; keep CMake and source-policy checks aligned.
- Remove superseded local build/package outputs and completed historical documents.
- Keep the active Release build, current release evidence, toolchain inputs and referenced test fixtures.
- See [repository layout](docs/repository-layout.md) for ownership and placement rules.

## Remaining validation

- Manually verify Windows edge dragging and mixed-DPI multi-monitor behavior;
  keyboard snapping and title-bar mouse operations already have regression coverage.

## Maintenance rules

- Select one specific usability issue before starting another implementation slice.
- Keep README, manual, changelog and package metadata aligned with VERSION.
- Preserve semantic ownership, generation checks and read-only CLI boundaries.
- Run tests relevant to changed behavior; record the configuration and date.

## Contracts

- [Architecture](ARCHITECTURE.md)
- [Suite protocol](docs/suite-app-protocol.md)
- [Suite workflows](docs/suite-workflows.md)
- [Visual system](docs/suite-ui.md)
- [CLI](docs/cli.md)
- [Wave simulation](docs/wave-simulation.md)
