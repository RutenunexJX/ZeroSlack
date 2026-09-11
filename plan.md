# ZeroSlack Current Plan

Product version: `v0.25.6`

## Current baseline

Current behavior is documented in the README and user manual. Released changes
and completed implementation history are recorded in CHANGELOG.md and Git.

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
