# ZeroSlack Current Plan

Product version: `v0.25.10`

## Current baseline

Current behavior is documented in the README and user manual. Released changes
and completed implementation history are recorded in CHANGELOG.md and Git.

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
