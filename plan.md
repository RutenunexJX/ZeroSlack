# ZeroSlack Current Plan

Product version: `v0.25.1`

## Current baseline

Workspace Hub and bounded `suite-context` are delivered, along with the Context Workspace, live insight
providers, tool drawer and shared Wave renderer. This file lists current maintenance work, not historical steps.

The rounded visual refresh, fixed title row, specialized insight surfaces, English-first typography and Catppuccin themes are delivered.

## 顶栏闪退修复（2026-09-11，v0.25.1）

- [x] 定位真实鼠标最大化点击的访问冲突：Qt 对排队的原生消息传入空结果指针，窗口过滤器仍写入该指针。
- [x] 允许鼠标消息使用空结果指针，将最大化／还原延迟至消息处理完成；几何消息仅在存在结果存储时应答。
- [x] 回归覆盖空结果消息与真实主窗口鼠标操作；连续三轮最大化、还原、最小化及 Windows 贴靠测试通过。
- v0.25.1 已构建；相关 8 项回归最终均通过（GUI 冒烟测试重编译后复核通过）。用户已明确授权本次提交、推送与正式包替换，发布时保留 v0.25.0 正式包备份。

## 本批修改（2026-09-11，已实施）

本批已实施；用户于 2026-09-11 授权提交、推送并更新正式包，发布版本为 v0.25.0。

- [x] **非编辑区文字重新设计**：更换字体，以英文阅读和显示效果为优先，同时保证中文可读；重新设计字体颜色、常规与加粗的使用规则，明确标题、操作项、正文和辅助信息的层级。代码编辑区字体不在本项范围内。
- [x] **顶栏文件路径支持操作**：打开文件后，顶栏左上角显示的完整路径应支持复制和跳转；完整路径即使显示时被省略，仍应可获取和复制。通过右键菜单复制绝对路径或在 Explorer 中定位。
- [x] **Windows 分屏兼容性**：检查并修复当前无法使用 Windows 分屏的问题，验证系统贴靠操作，包括拖动窗口至屏幕边缘和 Win+方向键；检查最大化按钮与系统分屏入口的兼容性。
- [x] **Tab 关闭按钮尺寸与对齐**：缩小叉号，使其视觉大小与标签文字相当，并与文字保持协调的水平排列和垂直对齐；保留足够的点击区域。
- [x] **添加 Catppuccin 主题配色**：在现有主题体系中增加 Catppuccin Latte、Frappe、Macchiato 和 Mocha，映射界面、语法和图形语义颜色。

- [x] **恢复专用图功能**：统一工作台通过适配层接入原专用面板，恢复热点图 Track/Matrix、嵌套框图、果核过滤与扇出折叠、专用状态转移布局；保留上下文、导航、Activity 消息与独立窗口机制。工具栏支持横向滚动，首次显示按可用空间缩放，修正果核分组标题遮挡。

实施说明：非编辑区使用 Noto Sans → Segoe UI → Noto Sans SC → Microsoft YaHei UI；普通文字及数字徽标使用常规字重，标题与分组使用半粗体。顶栏路径通过右键菜单复制绝对路径或在 Explorer 中定位。主题提供 Latte、Frappe、Macchiato、Mocha 四种 Catppuccin 变体。

验证：Qt 6.10.2 / MinGW Release；完整视图入口验证了状态图与嵌套框图，专用面板测试覆盖热点双模式、源码跳转、导出、果核过滤／折叠、重复刷新和旧版本上下文。Windows 原生测试已验证窗口样式、最大化按钮命中、最大化／还原与 Win+左方向键贴靠。拖动至边缘及多显示器混合 DPI 尚未进行手工验收。

**发布授权**：用户已确认本批修改，并授权提交推送和更新正式包；保留 v0.24.2 包备份。

## Maintenance

- Select one specific usability issue before starting another implementation slice.
- Keep README, manual, changelog and package metadata aligned with VERSION.
- Preserve semantic ownership, generation checks and read-only CLI boundaries during changes.
- Run the tests relevant to changed behavior; record configuration and date instead of carrying forward old totals.

## Contracts

- [Architecture](ARCHITECTURE.md)
- [Suite protocol](docs/suite-app-protocol.md)
- [Suite workflows](docs/suite-workflows.md)
- [Visual system](docs/suite-ui.md)
- [CLI](docs/cli.md)
- [Wave simulation](docs/wave-simulation.md)
