# Ela 原生交互适配

状态：实现、构建与后台验证完成（2026-09-24）；纳入 0.31.9 发布，桌面观感验收待执行。适用范围：仅 Ela 版本。

## 目标与边界

恢复上游已有的交互控制流程，应用侧保留业务模型、文档所有权、工作区隔离和布局持久化。上游代码的必要扩展必须记录在 `thirdparty/elawidgettools/UPSTREAM-REVISION.md`，保留 MIT 许可与作者归属。

参考上游版本：`454cac2d57a47d3cc28577dc817793aec1881ca7`。

## 实施清单

- [x] 侧栏：移除外部快照过渡回调，复用 Ela 的显示模式与覆盖滑出控制；保留 Files/Design 内容、宽度调整和状态恢复。
- [x] 导航：现有 Ela 树的选中、展开、主题切换与键盘交互通过回归，保留业务模型。
- [x] Tab：接回 ElaTabBar 的拖动手势与 ElaTabWidgetPrivate 的即时浮动、进入合并、离开再浮动流程。
- [x] 文档适配：保留未保存内容、共享文档、锁定状态、临时文件与工作区隔离；取消恢复原索引和标签信息，关闭和销毁遵循既有所有权。
- [x] 页面切换：设置分类、Problems/Activity 使用 ElaCentralStackedWidget Popup；支持反向、重入、隐藏、缩放、删除和输入终止过渡。
- [x] 停靠：使用 ElaDockWidget / Qt AnimatedDocks 的原生浮动与停靠预览；保留侧栏、底栏并排与尺寸调整，移除 Ela 路径重复的落位快照过渡。
- [x] 后台验证：Release 应用构建、Qt 回归、主窗口集成流程和离屏基准完成。
- [x] 文档：上游补丁 25、控制关系和测试边界已记录。

## 最终控制关系

| 交互 | 控制方 | 应用保留的职责 |
| --- | --- | --- |
| 左侧栏宽度 | ElaNavigationBar，255 ms / OutCubic | Files/Design 内容、宽度范围、QMainWindow 布局恢复 |
| 窄窗侧栏覆盖滑出 | 从 ElaWindow 提取并共享到 ElaNavigationBar 的 225 ms / OutCubic 位置动画 | 窗口宽度小于 850 时的入口、父容器、外部点击与 Escape |
| Tab 拖出、进入、离开 | ElaTabBar 手势和 ElaTabWidgetPrivate 路由，托管文档适配器搬运真实页面 | 文档关闭决策、工作区可见性、分屏、页面所有权 |
| 设置、底栏页面切换 | ElaCentralStackedWidget | 类别选择、诊断与 Activity 数据 |
| Context 浮动及停靠 | ElaDockWidget / Qt QMainWindow AnimatedDocks | 资源生命周期、停靠后的并排排序与尺寸、滚动位置、布局保存 |

Context 在 Qt 落位动画结束后把同一个 QWidget 交回现有 ContextDockHost；并非将整个
业务布局替换为 Ela 示例的窗口模型。浮窗保留单标题栏、可用时的 Fit、关闭和 Ela Acrylic。
原来松手后播放的 ContextDockTransition 快照在 Ela 构建中不再创建。

右侧栏整区开合、底栏抽屉和 section 折叠继续使用现有 PanelCompositor。Ela 没有与这些
业务布局直接等价的控制器；本轮不把它们描述为 Ela 原生，也不重写专业编辑器或图画布。
托管 Tab 和导航内容扩展仍属于本地适配，详细差异见
[`UPSTREAM-REVISION.md`](../thirdparty/elawidgettools/UPSTREAM-REVISION.md)。

## 验证结果

- Release `ZeroSlack.exe`、`libzeroslack_core.dll`、`ElaWidgetTools.dll` 构建成功。
- 最终 17 组 CTest 全通过，耗时 28.88 秒：100%／200% 控件、导航、侧栏、动效、底栏布局、
  主窗口和浮窗；以及 200% 下的托管 Tab、设置面板、Context 工作区。
- 托管 Tab 16 个 QtTest 项通过；覆盖真实页面即时浮动/合并/再次浮动、五向分屏、取消、
  源销毁、文档关闭、控制器销毁、工作区隐藏、未保存确认和临时文件。
- Context 工作区 238 项检查通过，设置面板 32 项检查通过。停靠回归保留文档、光标、撤销、
  滚动位置、并排顺序、尺寸和视图身份。
- 隐藏 HWND 下分别验证无边框几何/边缘命中与窗口级 Acrylic，100%／200% 均通过；
  测试未显示窗口、改变前台焦点或操作桌面鼠标。
- 补丁 25 从前一仓库版本重放 17 个供应商源文件，SHA-256 全部一致。MIT／字体 OFL 许可未更改。

本地记录：`build/ela-migration/native-interaction-regression-final.txt`、
`ela_document_tabs_test-native.txt`、`ui_panel_motion_test-native.txt`、
`hidden-native-{1,2}.txt`、`ela-native-provenance.json`。

## 离屏性能与未验范围

基准使用实际 MainWindow、5000 行 SystemVerilog、模块框图与三个窗口尺寸，分别执行
12 次左栏/右栏/底栏/section 切换。左栏从实际 NavigationPaneCoordinator 入口驱动；
记录布局事件间隔及 CPU 事件耗时，不将这些数字换算为显示器实际帧率。

最终结果记录：`build/ela-migration/ela-native-panel-benchmark-final.json`。

| 左侧栏窗口尺寸（逻辑像素，DPR 1） | 事件间隔中位数 | P95 | 最大间隔 |
| --- | --- | --- | --- |
| 1000 × 700 | 16.0 ms | 17.2 ms | 48.6 ms |
| 2560 × 1392 | 16.0 ms | 17.2 ms | 48.0 ms |
| 3840 × 2160 | 21.3 ms | 30.8 ms | 52.0 ms |

4K 下布局和编辑器重绘成本仍然可见，不据此声明稳定 60 FPS 或无结束闪动。
该记录覆盖开合两个方向，取代直接调用隐藏 ElaNavigationBar 所得的中间测量。

桌面鼠标工具在本轮不可用；没有验收真实 OLE Tab 拖放、窗口管理器合成、跨屏混合 DPI、
最大化流畅度和结束闪动。这些项目继续保留在
[未验证的手工检查项](unverified-manual-checks.md#4-侧栏底栏平铺与拖出拖回手势)，
原生鼠标验证前仍需通知用户。后台回归通过不代表桌面观感已通过。

## 验收

1. 拖动 Tab 时显示实际页面，进入目标时即时合并，离开时可以再次浮动。
2. Escape、窗口关闭、工作区切换和源/目标销毁不会误关文档或遗失视图。
3. 侧栏开合、快速反向、窗口缩放及焦点恢复正常；覆盖模式点击外部可以收起。
4. 上下/左右分屏和 Context 并排停靠仍可调整尺寸并恢复布局。
5. 全屏高 DPI 下检查卡顿及结束闪动，原生实现是否满足性能要求以实测为准；本轮尚未完成桌面观感验收。

XIPS 集成属于其他任务，不纳入本次发布。交互实施阶段完成后，按用户的发布请求，
本记录与原图标缺口修复一起纳入 `v0.31.9`，更新现有正式包。
