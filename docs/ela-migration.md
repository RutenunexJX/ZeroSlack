# ZeroSlack Ela 实际迁移

维护范围（2026-09-22）：仅维护 Ela 版本。后续功能、修复、验证和正式发布均以
基于 Ela 的 `ZeroSlack` 为目标；classic、Qlementine 和 SuiteUi 保留为历史兼容代码，不再并行维护。
默认 CMake 配置启用 Ela，正式包使用 `ZeroSlack-win64` 目录和 `ZeroSlack.exe`，不再生成 ZIP。
下文旧版本中的双后端验证与 ZIP 记录仅描述历史发布。

## 0.31.9 Ela 原生交互适配与图标修复（2026-09-24）

侧栏恢复 ElaNavigationBar 的显示模式动画，并共享 ElaWindow 的窄窗覆盖滑出流程；
原外部快照回调已移除。保留 Files/Design 内容、宽度调整、布局恢复与仅工程/设置的入口。
Tab 接回 ElaTabBar 和 ElaTabWidgetPrivate 的手势路由，拖出即显示实际文档，进入标签栏即时
合并、离开再次浮起。文档关闭决策、工作区隔离、临时文件和分屏模型仍由应用管理。

Context 浮窗采用 ElaDockWidget，Qt 负责拖动、停靠预览与落位动画；应用在落位完成后把
同一个视图交回现有并排面板，保留顺序、尺寸、滚动位置和布局持久化。浮窗保持单标题栏、
可用时的 Fit 与关闭按钮，以及 Ela Acrylic。Ela 构建不再使用 ContextDockTransition 落位快照。
设置分类和 Problems/Activity 的页面切换使用 ElaCentralStackedWidget Popup，支持快速切换、
删除页面、隐藏、缩放和输入时终止过渡。右侧栏整区开合、底栏抽屉和面板折叠仍使用原有
合成器：Ela 没有与这些专用布局直接等价的业务控制器。

供应商变更记录为补丁 25，MIT／字体 OFL 许可保留；完整边界和验证记录见
[原生交互实施记录](ela-native-interaction-plan.md)。

应用图标沿用原电路式 Z、方形节点与圆角底板，只修补两处透明缺口；Windows ICO
包含 16／24／32／48／64／128／256 像素资源。资源编译显式依赖 ICO，避免仅修改图标时
可执行文件仍嵌入旧资源。正式包版本为 0.31.9，不生成 ZIP 或旧版备份。

## 0.31.8 通用弹出界面迁移（2026-09-23）

代码补全候选列表采用 ElaListView 和 ElaScrollBar，行高随字体调整，保留候选分类、
语义颜色和元数据。QCompleter、补全模型和编辑器命令流程继续负责筛选与插入。
修正源模型与 QCompleter 代理模型混用索引的问题，首项选中、方向键及 Enter／Tab 激活
使用对应模型的索引，Esc 继续取消。

通用输入、工作区配置、最近工作区、恢复预览、实例配对和 Pinloom 图片预览采用
精简 ElaDialog 外壳；警告、信息和未保存确认采用 ElaContentDialog。
未保存确认仍默认取消，外部冲突时禁用保存，保存失败继续中止关闭。
对话框关闭即移除遮罩，支持无父窗口、重复打开、父窗口缩放及详情展开。
这些兼容修正记录为 Ela 补丁 24；五个供应商文件重放后与工作树逐字节一致，
记录为 `build/ela-migration/ela-surface-provenance.json`，MIT／字体 OFL 许可保留。

普通控件、列表条目、标签及编辑器标记提示使用 ElaToolTip。提示不抢焦点，长路径可换行，
按屏幕可用范围定位；输入、滚轮、离开条目、隐藏／销毁所属窗口和主题切换会清除提示。
交互式符号信息卡、专业图画布及原生文件选择器继续保留专用实现。

Release 构建完成，18 项针对性 CTest 全部通过（43.27 秒），覆盖 100%／125%／150%／200%
的补全与提示、对话框和布局，以及主窗口、菜单列表和补全模型。包含 GUI smoke 的
1010 项检查、0 失败；记录为 `build/ela-migration/ela-surface-regression-final.log`。
高 DPI 审计分别检查 Ela 标题栏和正文边界，仍检查两者不重叠、控件最小尺寸和滚动可达性。
实际控件离屏截图位于 `build/ela-migration/ela-surface-images/{1,2}/`，配置界面位于
`ela-surface-layout/`。本轮未占用桌面鼠标，未验收原生窗口合成表现。
正式发布使用 `v0.31.8` 标签，直接替换 `ZeroSlack-win64`，不生成 ZIP 或旧版备份。
发布前重建 GUI／CLI；10 项发布检查通过，覆盖 100%／200% 的补全提示、对话框、
主窗口和浮窗，以及 CLI 与版本文档一致性。记录为
`build/ela-migration/release-0.31.8-build.log` 和 `release-0.31.8-tests.log`。

## 0.31.6 诊断刷新与回归修复（2026-09-23）

复选框的选中与部分选中标记使用当前主题的前景色，供应商变更记录为补丁 23。
Problems 面板监听实际视图的重新显示，并允许展开动画期间更新诊断，避免抽屉重挂载或
动画遮挡使内容保留旧结果。

回归夹具同步当前 Ela 控件、TEMP／工作区归属、上下文浮窗和语义发布流程；文件监听测试
等待原生监听就绪后执行实际原子保存，仍检查单次文件通知且不触发工程重扫。
原 11 项失败全部修复，Ela 全量回归 196/196 通过，GUI smoke 为 1010 项检查、0 失败；
文件监听单项重复 12 轮通过。记录位于 `build/failure-review/closure-ela-full.log` 和
`build/failure-review/watcher-probe-2.log`。

0.31.6 Release 重建与 8 项发布检查通过，覆盖 100%／200% 主窗口和浮窗、上下文恢复、
临时编辑器、CLI 及版本一致性。记录为 `build/failure-review/release-0.31.6-build.log`
和 `build/failure-review/release-0.31.6-tests.log`。

仓库 README 保持不变。发布版本以 `VERSION` 为源，版本守卫校验生成头文件、用户手册和
包内说明；README 仍保留固定包目录名称的检查，不再要求随每次发布更新版本标记。

## 0.31.3 统一正式包名称（2026-09-23）

Ela 版本成为唯一正式包，替换原 `ZeroSlack-win64`，移除独立的 `ZeroSlack-Ela-win64`。
窗口标题、可执行文件和快捷方式统一为 ZeroSlack；继续使用既有 INI 存储标识和配置路径，
保留设置、最近工程与会话。`--version` 显示正式产品名称及版本。

`package-release.ps1` 使用 Ela 构建、完整许可证和补丁清单、版本元数据与 SHA-256 清单；
旧 `package-ela.ps1` 转发到同一个流程。发布标签改为 `vX.Y.Z`，旧标签保留为历史记录。
本地只保留当前包，不生成备份或 ZIP；套件清单和桌面入口同步改为正式路径。

Release GUI／CLI 重建通过；7 项发布检查通过，覆盖 100%／200% 主窗口与浮窗、
上下文布局恢复、临时编辑器和版本文档一致性。GUI／CLI 版本探测均为 0.31.3，
配置文件哈希保持不变。记录为 `build/ela-migration/release-0.31.3-build.log`、
`release-0.31.3-tests.log`；验证未占用桌面鼠标。

## 0.31.2 上下文单标题栏（2026-09-23）

侧栏和底栏的 Context 停靠容器隐藏外层标题条，取消整块容器的拖动和浮动入口。
面板仍通过自己的标题条拖出；独立浮窗使用 ElaWidget，将资源名、停靠手柄、Pin、
可用时的 Fit／完整视图与窗口按钮合并在一条 ElaAppBar 中。长标题按可用宽度省略，
悬停显示全文，导航和未保存标记变化同步更新标题。直接使用现有 Ela 公开接口，未增加供应商补丁。

临时编辑器允许分离，浮窗与停靠区间搬运同一个共享文档视图，保留导航、撤销、
搜索和 Esc 关闭。旧 QMainWindow 会话中的整块 Context 浮动状态先恢复为停靠容器，
再将内容迁为单独的 Ela 浮窗并保留尺寸及持久化属性；不可分离或超过 16 窗恢复上限的
面板保留在停靠区。窗口几何继续按现有屏幕可用区域规则校正。

源码编辑器的背景图片需要不透明的主题底色，已为它覆盖浮窗通用透明样式，避免浅色正文
背景被错误混合成黑色；普通面板继续共享 Acrylic 材质。

Ela Release 构建及 14 项相关 CTest 通过，覆盖 100%～200% 上下文布局恢复、
100%／200% 的单标题栏与背景、主窗口、临时编辑器共享文档及搜索。
新增断言检查 280～920 逻辑像素宽度下按钮不重叠、正文直接接在标题栏后、
标题动态变化、Esc、旧 Qt 浮动容器迁移及新会话再次恢复。
记录为 `build/ela-migration/context-single-title-build-final.log` 和
`context-single-title-tests-final.log`；实际临时编辑器离屏截图位于
`context-single-title-review/100/`、`context-single-title-review/200/`。
本轮未占用桌面鼠标，未重新验收 Windows 原生拖动手感和毛玻璃合成画面。
本版使用 `ela-v0.31.2` 标签，沿用 `ZeroSlack-Ela-win64` 正式目录，不生成 ZIP。
发布前重建 GUI／CLI，7 项发布检查通过，覆盖 100%／200% Ela 主窗口和浮窗、
旧布局迁移、临时编辑器共享文档及版本文档一致性。记录为
`build/ela-migration/release-0.31.2-build.log` 和 `release-0.31.2-tests.log`。

## 0.31.1 移除 Workspace Hub（2026-09-23）

删除 Workspace Hub 的右侧入口、汇总面板、专用模型／会话／跨应用打开适配器和后台刷新。
仅用于 Hub 的状态控件与回归目标一并移除，动效基准改用模块框图面板。
旧会话在读取和恢复时清理 Hub 的停靠、浮窗、文档绑定布局及专用设置，其他面板继续恢复。
保留 Pinloom 独立面板和绑定、源码／分析图入口、Suite 公共接口及 `suite-context` 只读查询。
下文 Workspace Hub 的迁移记录保留为历史说明。

Ela Release 构建及 11 项相关 CTest 通过，覆盖 100%／200% 主窗口入口与动效、
新旧停靠／浮窗／文档绑定布局恢复、Pinloom 面板与关联存储、分析图 provider、CLI 和版本文档。
原生桌面鼠标未被占用。证据：`build/ela-migration/remove-workspace-hub-build.log`
及 `remove-workspace-hub-tests.log`。
本版使用 `ela-v0.31.1` 标签，直接替换 `ZeroSlack-Ela-win64` 正式目录，不生成 ZIP。
发布前重建 GUI／CLI，4 项发布检查通过：100%／200% Ela 主窗口、上下文布局恢复及版本文档。
记录为 `build/ela-migration/release-0.31.1-build.log` 和 `release-0.31.1-tests.log`。

## 0.31.0 模块框图、并行停靠与编辑器背景（2026-09-23）

模块框图与停靠的 17 项改造已完成：当前顶层及全部后代、不限制层级、不可达实例虚线、
同层级配色、双击定义跳转与侧键历史、保留且虚化父级。Fit 移至标题条，移除 Follow Editor、
Pin/Pinned 工具行以及模块搜索、折叠、缩放按钮和更多菜单。侧栏纵向、底栏横向平铺内容，
分隔条调尺寸并保存 v7 布局；浮窗往返继续复用同一内容实例。26 项针对性 CTest 通过。
详细范围、原生未验项及扩展 smoke 的 20 项既有失败对照见
[模块框图与停靠改造](module-diagram-modernization.md)。

源码编辑视图接入内置 Resting 背景，另提供 Peekaboo（A「偷偷探头」）和 Balancing
（C「差一点就倒」）可选项。`Settings > Appearance` 可关闭、更换为本地图片并调整浓度，
默认 55%，通过 Apply Global 保存，不写入工作区。素材见
[背景资源](../resources/backgrounds/README.md)。

图片完整等比、靠右下固定，文字和选区绘制在其上方；折叠投影、分屏和浮动源码视图共用此绘制路径。
缩放结果缓存复用，滚动后重绘固定背景；浅色和深色主题补齐图片外侧底色，避免矩形边界。
背景单独变化时跳过字体与行高重设，保持共享文档的撤销记录和未保存内容。

Ela Release 构建及 7 项相关 CTest 通过，覆盖 100%／200% 背景渲染、设置读写及控件、
折叠与滚动、共享视图隔离、撤销记录、图片缺失回退和编辑性能。原生桌面鼠标未被占用。
本地证据：`build/ela-migration/editor-background-tests.log`、`editor-background-build-final.log`，
实际控件的离屏截图位于同目录 `editor-background-review/`。

本版使用 `ela-v0.31.0` 标签及 `ZeroSlack-Ela-win64` 正式目录，不生成 ZIP。
版本文档检查改为校验 Ela 包说明，不再要求更新已停止维护的经典包说明。
发布前重建 GUI、CLI 和相关测试，8 项发布检查全部通过：四档 DPI 的 Ela 主窗口与浮窗、
文档标签、100%／200% 编辑器背景及版本文档一致性。记录为
`build/ela-migration/release-0.31.0-build.log` 和 `release-0.31.0-tests.log`。

分支：`codex/zeroslack-ela-migration`，基线 `3164377`。

用户在 2026-09-21 明确要求开始移植。本轮从实际应用接入 Ela，不再以此前
对照实验中的主观流畅度观察作为实施前置条件；这不改变历史报告的测量结论。

## 0.30.3 ElaWidget 上下文浮窗与 Acrylic

Ela 构建的 `ContextFloatingWindow` 直接继承上游 `ElaWidget`，由其管理标题栏、窗口按钮、
原生命中测试和边缘缩放，移除应用层单独创建与转发 `ElaAppBar` 的代码。先构造独立窗口，
再附加主窗口作为 owner，避免 Ela 在构造时给主窗口添加标题栏或修改主窗口边距。
窗口保持非模态 `Qt::Tool`，关闭 ElaWidget 默认的全局置顶，关闭操作仍交给上下文资源控制器。

模块框图等可分离的上下文内容共用该外壳。专用图表、同一内容实例在浮窗／侧栏间移动、
文档绑定、位置恢复与背景浓度保持原逻辑。Acrylic 改由 Ela 的原生材质路径应用，新增按窗口的
`ElaApplication::applyWindowDisplayMode` 兼容接口，不修改 Ela 的全局显示模式；应用层不再
在 Ela 构建中设置 DWM 材质属性。应用继续绘制背景色覆盖层，文字和图标保持完全不透明。
关闭透明效果、高对比度、节电、系统不支持或原生调用失败时回退实色；关闭材质同时清理整窗
玻璃边距。编辑器标签浮窗与双击符号信息卡不属于本次替换范围。
经典构建仍使用 QWidget；Ela 的编译定义与链接依赖公开传播，确保调用方与核心 DLL 的类布局一致。
新增第 20 份 Ela 兼容补丁和来源说明，保留原 MIT／字体 OFL 许可证及此前 19 份补丁。

Release 构建及 11 项相关 CTest 通过，覆盖窗口按钮、主窗口不受影响、关闭与内容回收、
侧栏往返、位置恢复、模块框图，以及 100%／200% 的浮窗布局和背景透出。
日志为 `build/ela-migration/ela-floating-widget-build.log` 和 `ela-floating-widget-tests.log`。
另以 `ZEROSLACK_ENABLE_ELA=OFF` 构建经典版本，3 项上下文／浮窗测试全部通过，记录为
`ela-floating-classic-build.log` 和 `ela-floating-classic-tests.log`。
验证为 Qt 离屏后台测试，未占用桌面鼠标；Windows 原生拖动手感和 DWM 合成效果尚未重新验收。

Acrylic 接入后重新构建，11 项 Ela／上下文 CTest 通过，见 `ela-acrylic-final-tests.log`。
经典构建的 3 项上下文／浮窗测试也通过，见 `ela-acrylic-classic-tests.log`。
第 20 份补丁从上一提交重放后，4 个供应商源文件逐字节一致，见 `ela-acrylic-provenance.json`。
另以隐藏 HWND 运行 `context_floating_window_test acrylicIsWindowScoped`，验证系统材质属性
确为 Acrylic（3）、关闭后为 None（1），主题切换后深浅色正确，且其他窗口和 Ela 全局模式
不变，见 `ela-acrylic-native-settle.txt`。主题属性在 Windows 事件处理完成后验收。
该原生接口测试没有显示窗口或操作鼠标，不能代替对实际模糊画面的视觉验收。

本版使用 `ela-v0.30.3` 发布标签和独立的 `ZeroSlack-Ela-win64` 正式包，
包含 Ela MIT／字体 OFL 许可证、来源说明及全部 20 份兼容补丁。
发布前重建 GUI、CLI 与主窗口／标签测试，额外 4 项检查通过：100%／200% 主窗口与浮窗、
文档标签以及版本文档一致性，见 `release-0.30.3-build.log` 和 `release-0.30.3-tests.log`。

## 0.30.2 模块框图紧凑布局

模块框图顶部接入 `ElaBreadcrumbBar` 与 `ElaToolBar`，使用现有 `ElaToolButton` 显示圆角图标。
窄面板中工具栏换至面包屑下方，搜索框按需展开；深度及 unresolved 显示收纳到更多菜单。
经典主题使用同一套交互状态，保留 QWidget 回退。

专用嵌套模块画布继续使用 QGraphicsView。节点按字体度量、实例名称和子节点边界计算尺寸，
同级节点随画布宽度换行，取消根模块 540×240 的下限和小图自动放大。折叠状态与导航路径按
完整实例路径记录，支持同一模块的多个实例；搜索展开命中节点的祖先。源码 Jump 与进入模块
分别执行，返回按钮和面包屑可恢复祖先视图。底部实例列表与右侧详情已从模块模式移除，
FSM／状态转移图仍保留其专用表格与详情。供应商源码及许可证没有新增变更。

Release 构建与 5 项相关 CTest 通过（含 relationship 的 886 项断言）。新增交互与销毁用例在
100%／200% 离屏验证通过，覆盖内容尺寸、窄窗换行、独立实例折叠、搜索展开、Ela 面包屑点击、
返回及源码跳转边界。日志为 `build/ela-migration/module-block-adaptive-tests.log` 和
`module-adaptive-{100,200}.txt`；实际控件截图位于 `module-adaptive-{100,200}/`。
本轮验证未占用桌面鼠标。发布标签为 `ela-v0.30.2`，继续使用独立的
`ZeroSlack-Ela-win64` 正式目录及 ZIP，包内保留依赖许可证与 19 份 Ela 补丁。
发布前重新构建 GUI／CLI，6 项发布检查通过，覆盖 Ela 主窗口、标题栏、模块框图、标签托管、
工作区切换与版本文档一致性，记录于 `build/ela-migration/release-0.30.2-build.log` 和
`release-0.30.2-tests.log`。

## 0.30.1 文档分屏与浮窗托管

编辑器分组使用 `ElaTabWidget` 的本地托管扩展。Ela 负责标签拖拽阈值、拖拽数据、
五向落点预览、跨组转移、拖出成窗及浮窗标题栏；同组排序和横向滚动继续由 ElaTabBar 处理。
上游没有 IDE 分屏布局控制器，因此保留 `EditorSplitController` 的 QSplitter 布局和分组登记，
由 Ela 的落点回调创建目标分组。Ela 路径退出旧的应用层标签拖拽处理，避免两个控制器竞争。

关闭编辑器浮窗将标签返回主窗口，关闭标签仍经过 TabManager 的锁定检查和未保存确认。
移动复用同一个编辑器及共享文档，保留撤销、光标、工作区归属与 TEMP 标记。
切换工作区隐藏不属于当前工作区的浮窗，TEMP 浮窗保持可见；窗口级 QAction 快捷键共享到浮窗。
拖拽数据限制在同一文档控制器内，悬停不移动页面，Esc 取消不生成新窗口；拖拽结束后才回收空源容器。
收拢最外层分隔容器时显式重设剩余编辑器的父对象，防止旧分隔容器释放时连带删除文档视图。

上下文浮窗使用 ElaAppBar 管理窗口操作与原生命中测试，区块拖出、重排及拖回使用本地扩展
`ElaDragHandle`。ContextWorkspaceController 保留资源实例、固定／预览、文档绑定和工作区生命周期。
现有 DWM 毛玻璃背景与专用图表内容保持原路径。

这些接口属于兼容扩展，并非未经修改的 Ela 上游能力，记录在第 19 份补丁及
`thirdparty/elawidgettools/UPSTREAM-REVISION.md`。MIT 和 Font Awesome 的许可证分发规则不变。
验证采用 Qt 离屏后台测试；未占用桌面鼠标，也未以此宣称 Windows 原生拖拽手感或 DWM 合成效果已验收。

Release 构建与 17 项相关 CTest 通过，覆盖经典／Ela 窗口、侧栏、上下文浮窗、共享文档、
未保存确认、工作区会话和切换。新增 `ela_document_tabs_test` 在 100%／200% 下通过，覆盖五向落点、
跨控制器拒绝、浮窗返回、TEMP 可见性、锁定与取消关闭、Esc，以及拖拽结束前源容器存活。
第 19 份补丁在上一提交的供应商文件上重放后，6 个文件逐字节一致。
构建、CTest 和新增测试日志位于 `build/ela-migration/ela-hosted-build-final.log`、
`ela-hosted-tests-final.log`、`ela-document-tabs.txt` 和 `ela-document-tabs-200.txt`。
发布标签为 `ela-v0.30.1`，沿用独立的 `ZeroSlack-Ela-win64` 目录与 ZIP，分发 19 份 Ela 补丁和依赖许可证。
更新版本后重新构建 GUI／CLI，5 项发布检查通过，涵盖标签托管、工作区切换、Ela 主窗口／标题栏和
版本文档一致性，记录于 `build/ela-migration/release-0.30.1-build.log`、`release-0.30.1-tests.log`。

## 0.30.0 多工作区与临时文件

侧栏新增工作区名称入口，收起后移至标题栏。菜单区分已打开与最近工作区，显示完整路径、
活动勾选和未保存标记，支持逐个关闭。切换复用内存中的编辑器，保留未保存内容、活动文件、
光标和滚动位置；关闭只询问所属文档，取消后保留工作区，确认后包含锁定标签一起关闭。

外部及未命名文件标记为 TEMP，排列在工程标签之后；切换及关闭工作区时保留。
TEMP 不进入工程会话或实例绑定，恢复记录使用独立本机分区，保存仍写回原文件。
外部文件采用单文件语义分析，不继承工程宏或包含路径，不触发工程重编译；切换或关闭工程
清空索引后恢复 TEMP 分析。新增启动参数支持直接打开源文件。嵌套工作区按最深根目录归属。

14 项发布 CTest 通过，包括共享文档、未保存决策、恢复、文件身份、窗口与侧栏、工作区会话、
新切换交互、分析调度及版本一致性。新测试另在 classic 和 200% 缩放下通过，覆盖实际菜单关闭、取消、
TEMP 重排边界、宏／包含路径隔离与工程实例信息保留。本轮验证采用 Qt 离屏后台测试，未操作桌面鼠标。
发布标签为 `ela-v0.30.0`，沿用独立的 `ZeroSlack-Ela-win64` 目录与 ZIP，保留依赖许可证及 Ela 补丁。

## 0.29.41 删除欢迎页重复入口

删除欢迎页独立的工程／设置图标、占位列及侧栏可见状态切换逻辑，两个图标仅保留在
左侧栏内。欢迎页不再因开合结束时切换入口列而改变布局。
主窗口交互回归改为从侧栏进入设置。10 项相关 CTest 通过，覆盖 Ela 四档缩放的
侧栏与主窗口，以及 classic 侧栏和控件交互；Release 构建通过。
发布标签为 `ela-v0.29.41`，沿用独立的 `ZeroSlack-Ela-win64` 目录和 ZIP。

## 0.29.40 欢迎页快捷入口

左侧栏展开时，欢迎页隐藏重复的工程／设置图标及其占位列；收起侧栏后恢复。
复用导航停靠区的可见状态通知，快捷按钮与侧栏布局同步切换。
9 项侧栏与主窗口相关 CTest 通过，覆盖 Ela 的 100%／125%／150%／200% 缩放和 classic 侧栏。
本次发布标签为 `ela-v0.29.40`，沿用独立的 `ZeroSlack-Ela-win64` 目录和 ZIP。

## 0.29.39 正式发布范围

本次正式版本汇总 0.29.26–0.29.39 的控件迁移、Ela 导航侧栏、共享合成过渡及收尾闪动修正。
发布标签为 `ela-v0.29.39`，独立目录与 ZIP 均使用固定名称 `ZeroSlack-Ela-win64`，
位于 `E:/PinloomRoot/AppPackage/AppSuite/Apps`。包内 `build-info.json` 记录实际提交，
发布通道为 `formal` 且 `dirty=false`；经典包与 Ela 用户配置隔离规则保持不变。

Release 全量构建及最终 49 项相关 CTest 已通过；Windows 原生测试覆盖普通、最大化窗口
的左栏、右栏、底栏与区块共 96 次过渡。详细数据及验证边界见本文件末尾。
分发保留 Ela、Font Awesome、Qt 及其他依赖许可证、上游提交记录和 18 份 Ela 兼容补丁。
以下各阶段的“开发版”“未提交／未替换正式包”均为当时记录，由本次正式发布统一收尾。

## 第一批：基础控件（0.29.25）

- `ApplicationThemeManager` 增加 Ela 后端，启动前固定选择。
- `UiControls` 在欢迎页、设置中心、编辑器外观设置、工作区配置、文件导航、全局命令面板、
  Problems、Activity、工作区中心、上下文 Peek/停靠面板和临时编辑器工具栏中创建真实 Ela 控件，
  共接入 60 处创建位置。
- 八类控件：按钮、工具按钮、输入框、下拉框、复选框、整数/小数步进框、滑块。
  现有业务代码仍通过 Qt 基类连接信号和访问值。
- 六套主题映射到 Ela 色彩表；移除这些控件上的通用输入框/按钮 QSS 覆盖。
- 保留窗口管理、编辑器、文件/设计树模型、专业图画布、专用分析逻辑和现有图标。
  本轮不是将全部 UI 替换为 `ElaWindow`，也没有改写专业图。

## 第二批：树列表与标签（0.29.26 开发版）

新增接入 19 处创建位置：12 个 QTreeWidget、1 个 QTreeView，以及 6 处标签控件，
包括 Designer 创建的首个编辑器标签组。累计 79 处创建位置通过适配层接入。

- Files/Design、Problems、局部搜索、连接检查、热点及 RTL 风险列表保留 QTreeWidget
  的条目接口和既有模型，使用 ElaTreeView 的样式桥接。工作区中心使用 ElaTreeView。
- 导航、命令分类、语义面板、连接面板和文档标签采用 ElaTabBar。
  外层 UiTabWidget 继承 QTabWidget；不使用 ElaTabWidget 的页面关闭和拖出控制器。
- 文档关闭确认、页面生命周期、重排及跨组分屏继续由 TabManager 和
  EditorSplitController 管理。Qt 处理标签尺寸、溢出按钮与输入，Ela 负责绘制。
- 保留树条目的字体、语义颜色、勾选、换行、省略和键盘操作；行高随内容增加。
  标签关闭图标使用小尺寸线条。树、标签和滚动条样式销毁前解除引用。
- 移除通用树条目、标签 QSS 对 Ela 绘制的覆盖；工作区树使用即时滚轮响应。

本批仍保留原窗口壳、编辑器、专业图模型与画布。不表示全应用已经采用 Ela 控件。

## 第三批：专业面板与命令表单（0.29.27 开发版）

新增接入 117 处创建位置，累计 196 处，包括 RTL 框图/状态图、果核图、热点 Track/Matrix
及公共图形工具栏、上下文候选目标、搜索替换、连接/传播/重命名表单、Pinloom 操作区、
数字插入工具和外部修改冲突栏。新增 ElaRadioButton；保留 QButtonGroup 的互斥行为。
图形模型、画布、导出、编辑事务和撤销实现未改动。

单选按钮补齐禁用颜色、禁用时的悬停处理及样式销毁安全性。
数字输入框按范围、前后缀、输入区边距与 Ela 横向步进按钮计算宽度，修复框图 Depth
输入区被挤窄的问题。继续使用现有紧凑换行布局和滚动视口。

第三批结束时，通用 QMessageBox/QInputDialog、对话框标准按钮组、原生文件选择器及
特殊浮窗/右键环形菜单仍保持原组件。本批没有接入 ElaContentDialog 的模态遮罩、关闭动画或窗口管理。

## 第四批：对话框、查找栏与 Peek（0.29.28 开发版）

新增 `UiDialogs`，接入通用文本/整数/候选项输入、提示框及对话框按钮。工作区别名、
编译宏与源文件扩展名、跳转行、实例选择、文件操作提示和未保存文档决策使用真实 Ela 控件。
同步接入编辑器查找/替换栏、Peek 搜索与操作按钮，以及悬浮窗口的标题操作按钮。
本次迁移涉及 44 处调用；这一数字包含对话框调用，不与此前的控件创建位置数相加。

Qt 继续管理对话框模态、父子关系和关闭流程。输入区和按钮由 Ela 绘制，不接入
ElaContentDialog 的遮罩及异步关闭。保留默认取消、Escape、按钮角色、保存失败及
冲突文件禁用保存的行为；经典后端继续使用 Qt 的输入和提示对话框。
按钮组按角色连接信号，调用者保留返回的按钮指针；不依赖标准按钮枚举反查自定义按钮。

回归发现内嵌查找栏原有的 Enter 事件会继续传入编辑器，覆盖刚选中的匹配文本。
现由查找栏消费输入框的 Return/Enter，确保只查找或替换一次，替换仍使用原有撤销事务。
该缺陷在经典与 Ela 路径均有覆盖。Peek 保留代码字体、语义颜色和现有内容模型，
只移除会覆盖 Ela 控件绘制的局部 QSS。

原生文件选择器、启动阶段错误提示、右键环形菜单、列表/表格的专用委托，以及应用窗口壳
仍采用现有实现。本批未修改 Ela 上游源码，继续分发前十份兼容补丁及原有许可证。

## 第五批：菜单与列表、表格（0.29.29 开发版）

18 个业务文件的 37 处创建或子菜单调用通过 `UiControls` 接入，包括工程与路径菜单、
Files/Design 和文档标签右键菜单、专业面板 More/导出菜单、浮窗绑定菜单，以及命令候选、
全局面板、临时编辑器搜索、Pinloom、工作区配置、设置分类/快捷键和传播表单的列表或表格。

普通菜单使用 ElaMenu；弹出、关闭、助记符、子菜单导航和动作所有权继续由 Qt 处理，
不使用上游的菜单截图位移动画。Ela 绘制圆角、边框与悬停，Qt 绘制文本、图标、
快捷键和勾选标记。Ela 路径移除通用 QMenu QSS，经典后端保留原样式。

QListWidget/QTableWidget 保留条目模型，接入 ElaListView/ElaTableView 的样式工厂；
设置快捷键模型使用真实 ElaTableView。原有委托继续处理字体、语义颜色、勾选、换行、
单元格编辑和排序。行高随内容计算，列表左右留白在绘制和点击判定中只计算一次。
样式由应用持有，在菜单或视图销毁后延迟释放，避免销毁过程中重设样式。

编辑器右键环形菜单、专业图模型和画布、事务与撤销、原生文件选择器及窗口壳保持现有实现。
新增第 11 份兼容补丁，保留 Ela MIT、字体 OFL 和完整修改来源。

## 第六批：滚动与树展开（0.29.30 开发版）

本批接入交互行为：设置分类、设置内容页、快捷键表格和 Workspace Hub 使用 ElaScrollBar
的滚轮动画，时长为 160ms。滚动范围仍即时更新；连续滚轮累积目标，反向输入立即从当前
位置转向。触控板像素位移直接应用，不叠加第二层缓动。键盘定位、拖动滚动条、程序定位、
列表刷新及隐藏控件可以中止动画。编辑器和命令候选列表不启用这套滚动行为。

Files、Design 和 Workspace Hub 开启 QTreeView 提供的展开/折叠过渡，数据模型、
双击导航和语义内容保持原接口。其他专业结果树仍按原配置显示。

ElaTabBar 在保留 Qt 标签布局和输入的模式下，单独启用 160ms 横向滚动；按真实标签宽度、
隐藏状态和滚动按钮占用空间计算范围。滚轮移动标签视野，不切换当前文档；触控板直接
移动视野。关闭按钮随标签同步移动。点击、键盘切换、重排、增删标签和调整窗口尺寸时
停止或同步滚动位置。TabManager 与 EditorSplitController 继续管理关闭确认和分屏。

第六批结束时尚未接入 ElaAppBar/ElaWindow。验证与预览记录见本页末尾。

## 第七批：主标题栏（0.29.31 开发版）

主窗口使用真实 ElaAppBar 的标题布局、侧栏展开按钮和最小化／最大化／关闭按钮。
新增 `UiWindowChrome` 适配层，经典后端继续创建原标题控件；路径右键复制与文件定位、
侧栏开关及应用菜单快捷键保留。长标题可以收缩，窗口按钮按内容提示尺寸分配空间。

ElaAppBar 使用新增的外部窗口管理模式，不设置宿主窗口标志、边距或原生事件处理。
WindowSnapChrome 继续管理 Windows 分屏与缩放命中，WorkspaceChrome 管理拖动和几何；
Ela 更新最大化状态图标并响应原生最大化按钮的悬停。未接入 ElaWindow。

修正 Ela 上游关闭按钮在 QWidget::close() 后再次关闭 QWindow 的行为：现在只请求一次关闭，
尊重 MainWindow 的未保存确认和 QCloseEvent 拒绝结果，也不在可能已经销毁的窗口上继续操作。
关闭图标增加按下与键盘焦点反馈，悬停动画随控件销毁。新增第 13 份兼容补丁及对应许可记录。

## 第八批：侧栏入口与底栏按钮（0.29.32 开发版）

左侧工程／设置／收起按钮、右侧 ContextRail 功能按钮和悬浮窗批量隐藏入口，以及底栏
Problems／Activity 等按钮使用真实 ElaToolButton。保留现有线条图标，工程菜单仍为纯图标；
其菜单标记只从绘制参数中移除，菜单弹出和 Escape 关闭继续由 Qt 处理。

ContextRail 用 QWidgetAction 提供真实按钮，QAction 继续管理启用、显示、图标、提示与选中状态。
功能顺序、尾部入口、右键定位、重复点击收起、资源复用和延迟创建由现有控制器管理。
底栏共享状态数字绘制，根据文本、图标和计数实际尺寸预留空间；较大字体下底栏高度随之调整。
移除这三处旧按钮 QSS 对 Ela 绘制的覆盖；经典后端保留原样式。

本批未更改 Ela 上游源码，继续携带十三份兼容补丁、MIT 和字体 OFL。
没有替换专业图、编辑器、窗口／文档管理，也没有重新启用底栏高度动画。

## 第九批：滚动容器与只读文本（0.29.33 开发版）

5 处滚动容器创建入口使用真实 ElaScrollArea：设置分类页、侧栏堆叠、Peek、Pinloom 图片预览，
以及专业面板共用的可滚动布局。恢复 Qt 的按需滚动条策略；不启用鼠标拖拽滚动手势。
设置内容页沿用第六批的平滑滚轮，其余容器保持即时滚动与程序定位。

12 处只读文本创建入口使用 ElaPlainTextEdit：Activity、外部冲突与崩溃恢复对照、Pinloom 文本、
连接／传播／替换／RTL 风险差异、Peek 通用内容与长详情。调用方继续管理文档、换行、字体、
颜色和滚动位置；只读区不启用按像素计算的滚轮动画，避免与文本行单位混用。
普通文本菜单使用 Qt 生成的动作及禁用状态，ElaMenu 绘制外观，保留原生选择和复制。

新增第十四份兼容补丁：原组件可以停止强制文本调色板与全局焦点事件接管，保留宿主样式。
焦点标记使用单个可反向的自有动画；文本样式在控件及视口销毁后释放，NoFrame 不再绘制边框。
继续保留编辑器、专业图模型／画布及业务控制器；Ela MIT、字体 OFL 与补丁来源随包分发。

## 第十批：文字标签与工作区配置滚动（0.29.34 开发版）

25 个文件中的 85 处通用文字标签创建入口改用真实 ElaText，覆盖标题、说明、状态、
Peek 字段和详情、设置及专业面板提示。另将 21 处 QFormLayout 调用接入标签工厂，
其中 16 处生成 ElaText 字段名，5 处空标签行保持不创建标签。字段助记符仍绑定原输入控件。
保留纯图片／状态圆点标签、专用标题省略绘制、图像预览与经典窗口标题的原实现。

新增第十五份兼容补丁，允许宿主关闭 ElaText 在主题事件与绘制时强制重设文字颜色的行为。
适配层恢复原有排版和 QLabel 默认换行策略，保留文字选择、链接、语义颜色及禁用颜色。
调用方仍可设置字号、字重、代码字体与局部样式，编辑器及专业图模型／画布不在本批范围内。

工作区配置页原有的 QScrollArea 子类改为 ElaScrollArea，并把视口尺寸适配移到局部对象，
在视口完成调整后限制嵌套列表／表格的高度；对象随滚动容器销毁，不安装全局事件过滤器。
MIT、字体 OFL、上游来源与全部兼容补丁继续随独立 Ela 开发包分发。

## 第十一批：整个左侧栏由 Ela 承载（0.29.35 开发版）

真实 ElaNavigationBar 取代 Ela 后端的 NavigationViewport。工程／设置／收起按钮所在
工具区和 Files／Design 内容都挂入它的内容容器，由 Ela 分配上下布局、保持正文宽度并
裁切滑动。文件树模型、搜索、双击导航和设置页入口继续使用原业务接口。

开合调用 Ela 的 Maximal／Minimal 显示模式，使用其 255 ms OutCubic 宽度动画；应用层
不再创建或逐帧驱动该侧栏动画。第十六份上游兼容补丁增加自定义内容／头部接口及完成
通知，并把宽度动画改为单个有归属的可中断对象，连续反向切换从当前宽度开始。
这属于对 Ela 的公开接口扩展，不是原版 ElaNavigationBar 已具备任意文件树承载能力。

QDockWidget 保留为主窗口的停靠与布局保存槽。动画结束时同步一次 Qt 保存的停靠宽度，
避免最后一帧缓存导致宽度漂移；直接显示／隐藏和恢复布局也会同步 Ela 显示模式。
展开后释放到 200—400 逻辑像素范围，允许拖动分隔线。未采用 ElaWindow，也不改变
编辑器、右侧上下文面板、底栏或专业图模型。经典后端保留既有实现。

## 兼容处理

Ela 初始化发生在任何控件创建之前，初始化后恢复应用字体和原生窗口属性。
未接入全局递归事件过滤器；专业视图无需被逐个保护。

适配层释放上游固定宽高，保留响应式布局。按钮补齐 QIcon、助记符、键盘按压、
checked、焦点和默认按钮反馈。主题刷新重设按钮缓存色彩，支持同为浅色/深色的
不同 Catppuccin 主题切换。下拉框使用 Qt 的可立即关闭生命周期和 Ela 绘制，
避免上游动画期间不能关闭的问题。

销毁回归发现局部样式的释放顺序会影响 Qt 窗口关闭。工具按钮、步进框等在
释放样式前解除引用；下拉框与弹出列表共享的样式由 QApplication 托管，使用
deleteLater 在控件销毁后释放，避免析构期间重新设置弹出窗口样式。后者替代了
第一批补丁中的下拉框样式解除流程。基础控件保留可见状态销毁回归，下拉框另有
弹出列表销毁顺序和样式最终释放检查。

## 构建与运行

使用 Qt 6.10.2 / MinGW 13.1.0，新建构建目录，并设置：

```powershell
cmake -S . -B build/ela-migration -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DZEROSLACK_ENABLE_ELA=ON `
  -DZEROSLACK_ENABLE_SUITEUI=OFF `
  -DZEROSLACK_ENABLE_QLEMENTINE=OFF
cmake --build build/ela-migration --parallel 4
ctest --test-dir build/ela-migration --output-on-failure
```

Qt、MinGW、Ninja、Python 路径需按本机配置；首次编译所需 fmt 可使用本地缓存。
Ela 使用 Qt 私有头，CMake 强制检查 Qt 6.10.2。更换 Qt 版本需重新验证。

运行 `build/ela-migration/ZeroSlack-Ela.exe`，默认使用 Ela。
`--ui-style=classic` 在相同隔离配置中使用原控件，切换后端需重启。

通过 `scripts/package-ela.ps1` 打包到新的独立目录；普通 `package-release.ps1`
会拒绝 Ela 构建。配置位于 `ZeroSlack/ZeroSlack-Ela` 的 AppConfigLocation，
近期工作区、编辑器配置和会话存储与经典版隔离。用户显式打开同一工程时，
工程本地设置和源文件仍是同一份。

## 全屏侧栏开合性能修正（0.29.36 开发版）

Ela 仍控制整个侧栏的显示模式和动画。宽度驱动改为一次 `setFixedWidth`，避免
`QPropertyAnimation(maximumWidth)` 先写最大宽度、回调再写固定宽度产生重复布局请求。
该兼容变更单独保存为补丁 17，动画曲线、时长、反向操作和最终宽度恢复保持原契约。

编辑器的 resize 路径继续调用 Qt 原有排版和滚动条处理；只对可见区域附加显示去重。
记录文档、revision、首末行和字符范围；全部相同时复用诊断、语义高亮和行内附加信息。
滚动及显式发布仍即时刷新，高度变化、自动换行或文档变化导致范围改变时也重新发布。
0.29.36 当时没有使用截图替身、暂停输入、冻结更新或 Qt 私有编辑器接口；
0.29.37 进一步改为下述独立过渡显示层。

可见文本使用 `QTextLayout::setCacheEnabled(true)`，每个视图最多 256 个文本块。
缓存用 QTextCursor 跟随编辑定位，避免持有文档替换后失效的 QTextBlock。
离开视口或销毁视图时关闭缓存，并通过空的 beginLayout/endLayout 释放已整形数据；
文字、格式和文档 revision 保留，下次进入视口由 Qt 正常补齐布局。
单独关闭 cacheEnabled 只改变标志，不会立即释放缓存，因此没有仅依赖该标志回收。

回归同时修正经典侧栏在动画中立即收起时，将过渡宽度 200 写回保存宽度的问题：
尺寸约束恢复期间保留过渡标记，显示状态和最终宽度同步后才解除。

Qt 6.10.2 的宽度变化仍会触发文本重新排版，见
[QPlainTextEdit 源码](https://github.com/qt/qtbase/blob/v6.10.2/src/widgets/widgets/qplaintextedit.cpp#L1717)。
本次减少外围重复工作，不声称消除 Qt 排版和大面积软件绘制的全部成本。

## 侧栏过渡显示层重构（0.29.37 开发版）

用户反馈 0.29.36 在全屏时仍有卡顿。本机只读硬件查询确认输出为 3840 × 2160、
144 Hz；原有 QVariantAnimation 步进约 16 ms，而且每步仍修改真实编辑器宽度。
本轮把显示层和文档布局分离，不改动编辑器、文件树数据或专用图表的实现。

- ElaNavigationBar 继续拥有显示模式、目标宽度、255 ms 时长、OutCubic 曲线及中断代次。
  补丁 18 增加可选过渡呈现接口；过期完成回调不能覆盖新一轮状态。
- SidebarCompositor 在开合边界生成侧栏和工作区两张不缩放的临时画面。动画期间真实
  编辑区保持宽布局，QMainWindow 不再逐帧调整文档。侧栏恢复后同步停靠宽度。
- Windows 使用 DirectComposition 的独立合成动画；其系统合成节奏不由 Qt 的 16 ms
  动画计时器驱动。D3D 设备在后台预热，主线程不等待初始化；不支持或尚未就绪时，
  使用相同隔离布局的 raster 路径。只依赖 Windows 系统 DLL，不增加 Qt Quick 运行库。
- 普通按键、输入法、鼠标和滚轮先结束过渡；鼠标及滚轮按最终控件几何重新命中。
  Ctrl+1 可连续反向切换。关闭、失活、尺寸/DPI/主题变化也会收尾并释放画面。
- 快照像素有上限，结束立即释放 CPU 图像和 GPU 表面。窗口销毁时取消回调，
  不在 QMainWindow 布局已销毁后继续提交尺寸。

过渡画面是静态内容的平移；期间业务异步更新在真实控件中继续进行，结束后显示最新结果。
这不是实时渲染整个编辑器的架构迁移。DirectComposition 的报告刷新率只说明系统合成
配置，不代表本轮已测得每一帧的屏幕呈现时间。离屏测试也不能代替用户的实屏体验。

参考：[Microsoft DirectComposition](https://learn.microsoft.com/en-us/windows/win32/directcomp/why-use-directcomposition-)。

## 侧栏收尾闪动修正（0.29.38 开发版）

0.29.37 的收尾顺序是隐藏 QWidget 过渡层、销毁合成子窗口，然后重绘主窗口。
该子窗口在存活期间会裁切主窗口的原生绘制；先撤掉它会暴露尚未更新的画面。
另一个外观差异来自只捕获 Ela 的透明内容容器，遗漏侧栏自身的背景和圆角边框。

0.29.38 改为把 DirectComposition 顶层 visual 直接绑定到主窗口 HWND，并限制在工作区
矩形内；不再创建阻挡 Qt backing store 更新的子 HWND。真实布局、焦点和同步重绘
均在 visual 仍覆盖工作区时完成，刷新 GDI 批次后再以一次 Commit 撤掉 visual。
不增加延时、淡出或逐帧文档重排。raster 兼容路径保留，输入和反向切换协议不变。

侧栏快照改为绘制整个 ElaNavigationBar。收起状态下只在快照事务内临时展开其宽度，
绘制后还原原尺寸及约束；事务内禁用宿主布局，避免隐藏控件绘制造成编辑器往返重排。
CPU 图像、GPU 表面和 composition target 仍在动画结束后释放，Ela 上游补丁维持 18 份。

依据：[HWND composition target 的四层关系](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd)、
[Commit 的原子提交](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-commit)、
[GdiFlush](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush)。

## 共享面板过渡（0.29.39 开发版）

将 SidebarCompositor／NativeSidebarComposition 提取为 PanelCompositor／
NativePanelComposition。每个主窗口只创建一个 presenter；各层使用不缩放的图像、
平移和矩形裁切，继续采用 255 ms OutCubic、Windows DirectComposition 与 raster 回退。
Ela 仍拥有左栏显示模式和完成代次，其余面板的资源与可见状态仍由原控制器管理。

- 右侧 Context 栏：打开、Ctrl+2 隐藏／显示及栏标题关闭按钮接入横向滑入／滑出。
  移到左侧的 Context 停靠栏使用对应方向；浮动、标签化停靠、拖放和恢复布局即时处理。
- 底部工具抽屉：内容和调整高度的把手纵向滑动，Problems／Activity 按钮条固定。
  删除原未启用的逐帧高度动画。Ela 默认启用合成过渡，classic 仍即时应用。
- Context 内部区块：折叠／展开时，区块位置和裁切范围插值，真实视图一次到达最终布局；
  图表、文件内容、焦点与业务状态继续使用原控件。拖动高度、重排和删除先结束过渡。

同一面板反向切换沿当前进度继续，切换不同面板先完成前一过渡。收尾重绘、输入接管、
窗口销毁、CPU／GPU 资源释放沿用上一版修正。恢复状态不播放过渡，区块删除及视图
换宿主仍即时完成。快照总量限制为 96 MiB，区块捕获使用更小的分配预算；超限直接应用布局。
临时画面仍是静态快照，异步内容更新在过渡结束或用户输入接管后显示。

区块腾出的空白区域捕获 stack 自身的背景，不假定视口的 Window/Base 调色板等于最终
背景。DirectComposition 子图层按绘制顺序从后向前插入；在 referenceVisual 为空时，
`AddVisual(..., FALSE, nullptr)` 才表示置于所有兄弟图层之上，避免背景盖住滑动内容。
依据：[Microsoft AddVisual 的空 referenceVisual 语义](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-addvisual)。

PanelCompositor 的 QObject 接口通过共享库导出宏声明，保证跨 DLL 的类型化 signal
连接有效；测试检查 finished 信号的连接和投递，原生基准拒绝无效连接。classic 不创建
此合成器，也不为未使用的面板动画预热 D3D 设备。Ela 上游补丁仍为 18 份。

## 源码与许可

Ela 固定提交 `454cac2d57a47d3cc28577dc817793aec1881ca7`，源码、MIT 完整文本、
上游 README、字体 SIL OFL 1.1 以及可重建补丁均保存在 `thirdparty/elawidgettools`。
独立包包含对应许可证和补丁。详见 `THIRD-PARTY-NOTICES.md`。

## 第一批验证

2026-09-21 已完成实际应用、独立 DLL 与测试目标的 Release 构建。

| 检查 | 结果 | 本地证据 |
| --- | --- | --- |
| Ela 实例、键盘、图标、下拉框关闭、主题/编辑器字体隔离及可见控件销毁 | 通过，包含在下述 29 项内 | `build/ela-migration/ctest-final.log` |
| 设置、文件导航、CLI、面板、编辑器分屏、上下文与果核图，以及四档 DPI 布局/交互回归 | 29/29 通过，35.78 秒 | `build/ela-migration/ctest-final.log` |
| 欢迎页改用 Ela 工具按钮后的相关主窗复测 | 8/8 通过，16.93 秒；是上述测试的复测 | `build/ela-migration/welcome-ctest.log` |
| 0.29.25 发布构建后的版本、主窗、CLI 与源码规则检查 | 6/6 通过，13.12 秒 | `build/release-ela-0.29.25/verification.log` |
| Windows 原生设置页、下拉框 Escape 关闭、Light/Dark 切换 | 通过 | `build/ela-migration/native-evidence/settings-light.png` |
| Files 连续打开 `counter.sv`、`top.sv`，检查等宽字体、语义提示和最大化 | 显示正常，未崩溃 | `build/ela-migration/native-fixture/` |
| 包内程序与构建产物 SHA-256、Ela 许可及七份补丁 | 一致、齐全 | 包内 `SHA256SUMS.txt`、`licenses/ElaWidgetTools/` |

四档 DPI 为 100%、125%、150%、200%。高 DPI 审计沿用已接受的滚动视口验收：
普通控件不得重叠、越界或小于 minimumSizeHint；滚动内容允许裁切，但须可滚动到达。
本轮执行的是相关回归集，没有重跑全部历史测试，也没有新增帧率或延迟基准测量。

## 正式包与后续范围

初次原生验证使用独立预览包 `build/packages/ZeroSlack-Ela-0.29.24/ZeroSlack-Ela.exe`，
当时的 `build-info.json` 标记 `dirty: true`；该预览记录不代表正式发布提交。

2026-09-21 用户授权提交推送并打包正式包，版本更新为 `0.29.25`，Ela 发布标签为
`ela-v0.29.25`。本分支使用固定目录名 `ZeroSlack-Ela-win64`，正式发布位置为
`E:/PinloomRoot/AppPackage/AppSuite/Apps/ZeroSlack-Ela-win64/ZeroSlack-Ela.exe`，
归档为同级 `ZeroSlack-Ela-win64.zip`。经典正式包及其快捷方式保留。

`scripts/package-ela.ps1 -Formal` 要求源码已提交、工作区干净且生成版本匹配。
包内 `build-info.json` 记录正式提交、分支、`channel: formal`、`dirty: false` 及发布标签；
`SHA256SUMS.txt` 用于核对分发文件，完整 Ela 许可和七份补丁随包提供。

### 第二批验证与开发包

2026-09-21，0.29.26 开发构建的相关回归共 42 项通过：首轮 38 项通过；新增主窗断言
最初依赖已被分屏控制器改名的 Designer 对象名，修正为通过控制器取得初始标签组后，
其余四档 DPI 主窗测试复测通过。证据为 `build/ela-migration/stage2-regression.log`
及 `stage2-main-retest.log`。

原生检查另发现 Design 树自动收窄列时重复计入内容边距，造成文字过早省略。
修正为只在子元素布局中加一次边距，并加入宽列与自动列宽的文字像素完整性比较；
该回归在修正前失败、修正后通过。随后 Ela 相关 16 项复测全部通过，包含四档 DPI，
证据为 `stage2-column-before.txt` 和 `stage2-column-fix-retest.log`。

覆盖 Ela 基础控件、树模型字体/颜色、展开勾选、文件激活、长标签溢出、关闭按钮、
重排与键盘操作，以及真实 TabManager 的未保存取消关闭、分屏移动和合并。
同时检查经典控件回退、主窗与悬浮窗、工作区、局部搜索、连接、热点和 RTL 风险面板。
四档 DPI 仍为 100%、125%、150%、200%。没有新增帧率或延迟基准测量。

新增 `08-zeroslack-tree-tab-contracts.patch`，在上一批源码上应用后逐字节核对。
历史七份补丁和全部许可继续保留。验证证据为
`build/ela-migration/stage2-patch-verification.json`。

独立开发预览位于 `build/packages/ZeroSlack-Ela-0.29.26/ZeroSlack-Ela.exe`，
包内标记 `channel: preview`、`dirty: true`，不是正式发布提交。
Windows 原生已检查最大化、Files 打开文件、文档标签和 Design 层级，发现并修复了
上述自动列宽问题。首次复验被用户按 Escape 中止；继续迁移时已补做修复后原生复验，
确认 Design 折叠后名称完整显示，证据为 `native-evidence/stage2-design-column-fixed.png`。
此前运行的 `0.29.26-stage2` 预览为修复前构建。
0.29.25 正式包保持不变，本轮没有提交或推送。

### 第三批验证与开发包

2026-09-21，0.29.27 开发构建完成。相关回归首轮 49 项中 45 项通过；四档 DPI
布局检查发现 Depth 数字输入区不足。修正数字控件宽度计算后，包含这四项的
12 项复测全部通过。证据为 `stage3-regression.log` 和 `stage3-spin-retest.log`。

Windows 原生操作进一步发现：打开下拉列表、关闭列表后再关闭框图面板会崩溃。
调用栈定位到 ElaComboBox 析构时的 setStyle，Qt 在重新设置弹出窗口样式期间
访问了失效对象。现改为让共享样式存活到弹出列表及其子控件销毁之后，再延迟释放。
新增真实侧栏反复创建/关闭，以及可编辑、不可编辑下拉框的样式生命周期回归。
其中原生崩溃未在 offscreen 环境复现，原生重放与自动化生命周期断言分别记录。

修复后 30 项相关复测通过，覆盖 Ela 控件与四档 DPI、经典控件回退、上下文侧栏、
专业图和命令工作流。首轮有一项因测试程序尚未构建而未能运行，补建并单独复测通过；
证据为 `stage3-popup-regression.log` 和 `stage3-provider-retest.log`。
源码约束与版本文档检查另有三项通过。

原生桌面已验证框图嵌套、下拉列表 Escape 关闭、Depth 调整、面板关闭后重开，
以及热点 Track/Matrix 切换、矩阵单元格选择和带面板退出应用，未再次发生崩溃。
热点使用测试工程中具有条件/时序引用的 rst_n 信号；value 信号为空的查询结果
与 0.29.25 CLI 一致，未将其空面板作为显示通过的证据。截图保存在
`build/ela-migration/native-evidence/stage3-block-reopened.png`、
`stage3-hotspot-track.png`、`stage3-hotspot-matrix.png`，崩溃栈保存在
`build/ela-migration/stage3-native-attach.log`。

新增补丁 09（单选按钮）及 10（下拉框弹出窗口生命周期）。以已提交源码为基线
顺序应用补丁 08–10 后，14 个修改文件与工作区逐字节一致，见
`build/ela-migration/stage3-provenance-chain.json`。独立包继续包含 MIT、字体 OFL
及完整十份补丁。

修复后开发预览为 `build/packages/ZeroSlack-Ela-0.29.27-fixed/ZeroSlack-Ela.exe`，
标记 `channel: preview`、`dirty: true`。文件清单、程序版本及四个构建二进制的核对结果
见 `build/ela-migration/stage3-package-verification.json`。本批没有提交、推送或替换
0.29.25 正式包；未进行新的帧率/延迟基准测量，未重跑全部历史测试。

通用对话框和特殊浮窗控件已进入第四批。窗口壳保持现有实现，是否替换另行评估。
每次迁移按实际交互验证，不以控件类名替换数量作为完成标准。

### 第四批验证与开发包

2026-09-21，0.29.28 开发构建完成。新增对话框行为测试，覆盖文本/整数/候选项确认与
取消、弹出列表 Escape、按钮角色与禁用、未保存文档默认取消及保存失败。
选择对话框取消时返回初始值并设置 accepted=false，与 Qt 原行为一致。
查找栏测试检查 Enter 不修改文档、替换只执行一次、Escape 关闭和撤销恢复。

相关回归共 27 项。首轮 25 项通过；另外两项分别发现候选项取消返回值的适配差异，
以及 200% 缩放下工作区 Defines 表格比外层视口高 6 像素，无法完整滚入视野。
修正取消返回值，并在视口大小变化时约束内层列表/表格的最大高度后，10 项相关复测
全部通过，包含原先失败的两项及四档 DPI。27 项最终均有通过记录。
证据为 `build/ela-migration/stage4-regression-final.log`、
`stage4-dialog-dpi-retest.log`，布局矩形记录在 `stage4-hidpi-before/` 与
`stage4-hidpi-fixed/`。验收标准保持普通控件不重叠、不越界、不低于最小提示尺寸，
滚动内容逐项完整可达。

Windows 原生桌面使用独立包验证：跳转到第六行、查找 rst_n 并按 Enter、Escape 关闭
查找栏、双击符号打开 Peek、展开详情和源码跳转，以及嵌套 Add Define/Workspace
Configuration 对话框的输入与逐层取消。查找后源文档保持不变；带临时编辑器侧栏退出
应用未发生崩溃。原生截图为 `native-evidence/stage4-goto-dialog.png`、
`stage4-find-enter.png`、`stage4-peek-details.png` 和 `stage4-define-dialog.png`。
未保存决策和候选项弹出列表生命周期由上述自动化覆盖，未将其记为原生桌面验证。

独立预览为 `build/packages/ZeroSlack-Ela-0.29.28/ZeroSlack-Ela.exe`，
标记 `channel: preview`、`dirty: true`。已核对全部 60 个分发文件、四个构建二进制、
两项便携版本调用，以及 Ela 的完整十份补丁、MIT 和字体 OFL，见
`build/ela-migration/stage4-package-verification.json`。
本批未提交、推送或替换 0.29.25 正式包；没有重跑全部历史测试或新增流畅度基准。

菜单与剩余列表/表格已进入第五批；专业模型、画布和现有窗口生命周期继续保留。

### 第五批验证与开发包

2026-09-21，0.29.29 开发构建完成。26 项相关回归全部通过，另有三项源码约束及版本文档
检查通过。新增菜单/列表/表格行为测试覆盖经典后端和 Ela 四档 DPI：子菜单键盘展开与
Escape、助记符、禁用项、勾选、共享 QAction 生命周期、双行文字和字体/颜色、鼠标与
空格切换勾选、表格编辑、模型排序、真实工作区配置、命令列表激活及样式延迟释放。
同时运行四档布局、主窗与导航检查，以及设置中心、临时编辑器搜索、局部搜索、传播、
Pinloom、RTL 风险面板和图形导出回归。证据为 `build/ela-migration/stage5-regression.log`
和 `stage5-policy-checks.log`。布局沿用此前普通控件与滚动视口的验收标准。

Windows 原生桌面验证工程子菜单与快捷键显示、Files 右键菜单及 Escape 关闭；
工作区 Defines 新增临时宏、把 32 编辑为 64，再取消配置草稿；Ctrl+Space 的双行命令列表
和方向键选择；设置快捷键表格新增 `file.save → Ctrl+S` 并 Revert。临时宏没有写入工程，
带设置表格关闭应用后预览进程退出，原先的旧预览未被操作。原生截图与操作摘要分别为
`native-evidence/stage5-*.png` 和 `stage5-native-verification.json`。

新增 `11-zeroslack-menu-item-view-contracts.patch`。以已提交源码为基线依次应用补丁
08–11，26 个修改文件与工作区逐字节一致，见 `stage5-provenance-chain.json`。
MIT、字体 OFL、全部十一份补丁继续随独立包分发。

开发预览为 `build/packages/ZeroSlack-Ela-0.29.29/ZeroSlack-Ela.exe`，标记
`channel: preview`、`dirty: true`。已核对全部 61 个分发文件、四个构建二进制、便携版本调用
及许可证/补丁，见 `stage5-package-verification.json`。本批未提交、推送或替换
0.29.25 正式包；没有重跑全部历史测试或新增流畅度基准。

### 第六批验证与开发包

2026-09-21，0.29.30 开发构建完成。26 项相关检查全部通过，包含四档 DPI 的滚动、导航、
主窗和布局测试，以及经典路径、设置、Workspace Hub、菜单/表格、分屏和三项源码/版本
约束检查。新增 `ela_motion_test` 检查连续及反向滚轮、像素滚动、边界、键盘定位、隐藏、
列表清空、可见控件销毁、变宽/隐藏标签、滚动期间重排与关闭，以及树展开中断。
首轮发现纯像素事件在 Qt 列表中分配到错误方向，以及重排后选中标签未回到视野；
修正局部事件路由和选中标签定位后复测通过。键盘定位用例明确先选择末项，再按 Home
验证选择与滚动同时恢复。证据为 `build/ela-migration/stage6-motion-detail-final.txt`
及 `stage6-regression.log`。

Windows 原生桌面使用独立测试工程验证设置分类和内容区滚动、Files 分组折叠/展开与
双击打开、溢出标签的双向滚动及关闭、缩小窗口后的 Workspace Hub 列表底部可达，
以及放大窗口和退出。标签滚动前后活动文档保持一致，十二份测试源文件未被修改。
截图为 `native-evidence/stage6-*.png`，操作记录为 `stage6-native-verification.json`。
触控板像素事件由自动化覆盖，未进行物理触控板或新的帧率/延迟基准测试。

新增第 12 份兼容补丁。按顺序应用补丁 08–12 后，29 个修改文件与当前工作区逐字节一致，
见 `stage6-provenance-chain.json`。独立预览为
`build/packages/ZeroSlack-Ela-0.29.30/ZeroSlack-Ela.exe`，标记 `channel: preview`、
`dirty: true`。已核对 62 个分发文件、四个构建二进制、便携版本调用、MIT、字体 OFL
及全部十二份补丁，见 `stage6-package-verification.json`。本批未提交或推送，
0.29.25 正式包未被替换。

### 第七批验证与开发包

2026-09-21，0.29.31 开发构建完成。最终 24 项相关检查全部通过，耗时 13.63 秒，
证据为 `build/ela-migration/stage7-regression-final.log`。覆盖四档 DPI 的标题栏、主窗、
布局和文档导航，以及经典标题栏、滚动、对话框、RTL 工作台和源码／版本约束。
新增测试验证宿主窗口标志与边距不被 Ela 修改、关闭拒绝只触发一次、真实未保存文档
取消／放弃关闭、删除窗口期间的动画释放、侧栏开关、长标题、窗口状态图标、路径复制及主题切换。

初次原生检查发现 Ela 左侧布局把可收缩的标题标签压成零宽；此前自动化仅检查矩形
不越界，没有要求短标题具有足够宽度。现将伸展空间分配给标题布局，并补上短标题
完整宽度断言。隐藏标题栏中的默认应用小图标，窗口按钮继续使用现有线条图标，
最大化／还原状态仍由 Ela 更新。初次 `ZeroSlack-Ela-0.29.31` 预览包含上述问题，
修复后的可用预览是 `build/packages/ZeroSlack-Ela-0.29.31-fixed/ZeroSlack-Ela.exe`。

Windows 原生桌面已验证修复后的标题文字、打开带空格路径的测试文件、最大化／还原、
最小化后恢复、侧栏收起／展开，以及右下边缘拖动缩小窗口。对测试文件插入临时注释后，
点击标题栏关闭进入未保存确认；取消后窗口与注释均保留。随后撤销注释再关闭应用。
原生截图在 `native-evidence/stage7-*.png`，记录为 `stage7-native-verification.json`。
原生系统分屏布局弹窗、多显示器切换及新的帧率／延迟基准未在本批复测。

新增第 13 份兼容补丁，按顺序应用补丁 08–13 后，35 个修改文件与工作区逐字节一致，
见 `stage7-provenance-chain.json`。修复后的独立包包含 63 个清单文件、全部十三份补丁、
MIT 与字体 OFL；四个二进制与构建一致，便携版本调用通过，见
`stage7-package-verification.json`。包标记 `channel: preview`、`dirty: true`，
本批未提交、推送或替换 0.29.25 正式包。

### 第八批验证与开发包

2026-09-21，0.29.32 开发构建完成。32 项相关检查全部通过，耗时 26.58 秒，
见 `build/ela-migration/stage8-regression.log`。覆盖 100%、125%、150%、200% 下的
常驻入口、底栏、标题栏、主窗和布局，以及经典后端、上下文资源生命周期和源码／版本约束。
新增检查包括动作状态同步、禁用与隐藏、鼠标及键盘触发、右键坐标、移除按钮后的释放、
工程菜单 Escape 关闭、侧栏开关、1／999+ 计数、大字体布局和四种主题切换。
既有上下文三态回归改用实际按钮点击，确认首次打开、再次折叠、第三次复用原视图。

Windows 原生桌面验证了工程纯图标菜单及 Escape、设置页重复点击关闭、左栏开关、
右侧 Hub 打开／折叠／恢复和右键菜单、批量隐藏入口的图标与提示切换、Problems／Activity
展开及重复点击／Ctrl+J 收起。实际多个悬浮窗口的批量隐藏由上下文自动化覆盖，
本批原生检查仅核对该入口的状态切换。测试源文件校验和未变，测试进程均已退出。
截图保存为原始 JPEG，位于 `native-evidence/stage8-*.jpg`，详细记录为
`build/ela-migration/stage8-native-verification.json`。

原生检查发现既有小窗口约束问题：左栏收起、右侧 Hub 打开时，从 800×600 打开 Activity，
0.29.32 窗口增至 850×729。对照未修改的 0.29.31-fixed 包，同样操作增至 853×729。
两版均复现内容最小尺寸撑大宿主的现象，不能据此归因于本批 Ela 按钮迁移；该布局问题
保留为后续修复项，不计为本批已解决。对照证据是 `stage8-baseline-before-activity.jpg`
和 `stage8-baseline-activity.jpg`。本批没有新增帧率／延迟或物理触控板测量。

独立预览为 `build/packages/ZeroSlack-Ela-0.29.32/ZeroSlack-Ela.exe`，标记
`channel: preview`、`dirty: true`。63 个清单文件和四个构建二进制逐一校验通过；
便携版本调用、Ela MIT、字体 OFL、来源文档及十三份兼容补丁均已核对，见
`stage8-package-verification.json`。本批未新增上游补丁，未提交、推送或替换正式包。

### 第九批验证与开发包

2026-09-21，0.29.33 开发构建完成。45 项相关检查最终全部通过：组合回归见
`build/ela-migration/stage9-regression.log`；新增经典后端测试最初误把输入框颜色当作
最终颜色，按现有通用 QWidget QSS 修正断言后，5 项文本区检查重跑全部通过，见
`stage9-text-final-test.log`。覆盖 100%、125%、150%、200% 下的字体／文档／选区／滚动
保持、四种主题、复制菜单、焦点、动画中销毁、NoFrame、滚动区域可达性及 Activity
尾随／隐藏刷新；同时覆盖现有设置、Peek、Pinloom、差异／替换／连接面板、主窗口、
编辑器隔离、紧凑布局和版本／源码约束。

回归实际发现并修正了 ElaPlainTextEdit 构造时的透明背景 QSS 在切换主题后覆盖
宿主 Base 调色板的问题。保留调用方自定义颜色时，绘制和主题事件均不再强制重置。
字体检查使用 Qt 完成样式解析后的实际字体作基线，同时核对所选字体族与字号。

Windows 原生桌面使用独立预览及 `stage9-fixture/counter.sv` 验证：设置内容页能滚动至
透明度项；WIDTH 符号卡展开详情和 Escape 关闭正常；Activity 无选择时 Copy 禁用，
全选后启用，点击后菜单关闭且选区保留；纵向、横向滚动正常。带日志区的主窗口经
Alt+F4 关闭后确认测试进程退出，测试源文件哈希未变。复制得到的完整内容由 QtTest
核对，原生验证只检查操作和显示。未进行物理触控板或新增性能测量；专业差异等面板
以自动化覆盖为准，崩溃恢复和外部冲突对话框未在本批进行原生操作复测。
既有小窗口被内容最小高度撑大的问题仍属待修复项。

原始截图为 `build/ela-migration/native-evidence/stage9-*.jpg`；详细记录为
`stage9-native-verification.json`。补丁 08—14 从当前提交重放后与 40 个变更上游文件
逐字节一致，见 `stage9-provenance-chain.json`。

独立预览：`build/packages/ZeroSlack-Ela-0.29.33/ZeroSlack-Ela.exe`，标记 `channel: preview`、
`dirty: true`。64 个清单文件、4 个构建二进制、十四份 Ela 补丁、MIT／字体 OFL 与便携
版本调用均已核对，见 `stage9-package-verification.json`。本批未提交、推送或替换正式包。

### 第十批验证与开发包

2026-09-21，0.29.34 开发构建完成。49 项相关检查全部通过，耗时 16.73 秒，见
`build/ela-migration/stage10-regression.log`。新增检查覆盖 100%、125%、150%、200% 下
文字尺寸与换行、四种主题、选择和链接点击、语义／禁用颜色的实际绘制、字段助记符、
空标签行及生命周期；工作区配置页验证五个嵌套列表／表格完整滚入视口、反复调整尺寸，
以及存在待处理尺寸回调时销毁。既有设置、Peek、Pinloom、搜索替换、连接、RTL 风险、
专业图、主窗、编辑器隔离、紧凑布局和源码／版本检查也已通过。

完整构建发现工作区虚拟分组 UI 测试单独编译部分 UI 源码时缺少主题依赖，已改为链接
应用共享核心。标签测试按实际构造流程为每种字体创建独立控件；运行中对同一控件反复
setFont 后重置应用 QSS，两后端均出现恢复先前字体的 Qt 样式行为，未在本批改写该路径。
链接通过实际点击验证，表单键盘操作单独验证 Alt 助记符。初始测试记录保留在
`stage10-label-initial.log` 和 `stage10-label-setup-final.log`，最终结果以组合回归为准。

Windows 原生桌面使用独立开发包验证欢迎页名称／路径与设置文字，工作区配置页纵向
滚动从 0 到 310 后可见 Top module、Virtual Source Groups 和底部按钮；添加分组的
输入对话框提示正常，可用 Escape 取消，再取消工作区配置。WIDTH 符号卡保留警告色、
说明换行和代码标题，详情展开及 Escape 关闭正常。测试进程退出，测试源文件哈希未变。
窗口尺寸变化和四档 DPI 以自动化为准；原生边缘拖动未观察到尺寸变化，不计为通过项。
原始 JPEG 截图为 `native-evidence/stage10-*.jpg`，记录为 `stage10-native-verification.json`。
本批没有新增性能基准或物理触控板测量；既有小窗口被内容最小尺寸撑大问题仍未修复。

补丁 08—15 从当前提交重放后，44 个变更上游文件与工作区逐字节一致，见
`stage10-provenance-chain.json`。独立开发包为
`build/packages/ZeroSlack-Ela-0.29.34/ZeroSlack-Ela.exe`，标记 `channel: preview`、`dirty: true`。
65 个分发文件、4 个构建二进制、15 份兼容补丁、MIT／字体 OFL 和便携版本调用均已核对，
见 `stage10-package-verification.json`。本批未提交、推送或替换 0.29.25 正式包。

### 第十一批验证与开发包

2026-09-21，0.29.35 完整构建通过，43 项相关检查最终全部通过。组合回归见
`build/ela-migration/stage11-regression.log`；新测试最初将 Ela 的反向切换瞬时几何
连续性也要求于未迁移的经典路径，改为仅在 Ela 后端断言该契约后，五项侧栏检查
重跑通过，见 `stage11-sidebar-final.log`。其他经典交互、最终宽度与布局恢复继续检查。

新增测试覆盖四档 DPI 下的中间宽度、编辑区空间回收、稳定内容宽度、拖动至 340 后
反复开合、四次动画中反向切换、即时收起、直接显示／隐藏、宿主隐藏再显示、重新创建
窗口后恢复 355 宽度、动画中销毁、Files／Design 搜索保持、文件双击、标题按钮和
真实 MainWindow 的 Ctrl+1。直接检查 Ela 原生显示模式、内容替换的释放，以及 Ela
路径不创建应用自定义动画。既有标题栏、导航、主窗、上下文、布局、工程会话、RTL
工作台、设置及源码／版本约束回归也通过。

测试发现并修正了 Qt 停靠尺寸缓存导致重新展开少 1 像素，以及零宽停靠控件显示信号
无法可靠表示显隐的问题。Ela 展开结束后释放约束并同步一次停靠缓存；局部 Show／Hide
适配区分显式关闭与宿主隐藏，并兼容首次显示前恢复的宽度。

Windows 原生桌面使用独立开发包验证欢迎页、测试工程恢复、完整侧栏收起、顶栏按钮
展开、分隔线向右拖动、Ctrl+1 收起／展开后恢复新宽度、再次双击 top.sv 导航，均通过。
原生鼠标操作前已通知用户；检测到同时有桌面输入后暂停，取得当前可测试的答复后继续。
连续反向操作、四档 DPI 和精确像素断言以 QtTest 为准，本批未新增帧率测量。
测试窗口经 Alt+F4 关闭并确认进程退出；counter.sv 和 top.sv 的 SHA256 均未变化。
原始 JPEG 为 `native-evidence/stage11-*.jpg`，记录为 `stage11-native-verification.json`。

补丁 08—16 从当前提交重放后，与 48 个变更上游文件逐字节一致，见
`stage11-provenance-chain.json`。独立开发包为
`build/packages/ZeroSlack-Ela-0.29.35/ZeroSlack-Ela.exe`，标记 `channel: preview`、`dirty: true`。
66 个分发文件、4 个构建二进制、16 份兼容补丁、MIT／字体 OFL 和便携版本调用均通过
核验，见 `stage11-package-verification.json`。本批未提交、推送或替换 0.29.25 正式包。

### 0.29.36 性能修正验证

2026-09-21，完整构建通过。45 项相关 CTest 最终全通过，见
`build/ela-migration/stage12-regression-final.log`；补充共享视图检查后，可见区域专项
37 项断言通过，见 `stage12-shared-cache-test.log`。覆盖四档 DPI 的侧栏开合／反向／
恢复宽度、编辑器滚动／高度／换行、诊断与高亮保留和移除、缓存上限、共享文档、关闭
视图、替换文档，以及增量编辑、折叠、粘贴、模板和上下文工作区。首次组合回归的经典
立即收起宽度失败记录保留在 `stage12-regression.log`，已修正后通过。

`sidebar_animation_benchmark` 使用真实 MainWindow、5000 行代码、三个窗口尺寸，
每种尺寸六次收起和六次展开；Qt 事件循环驱动动画，使用临时配置和临时文件。
旧版使用 0.29.35 包的两个 DLL，与修正版交替运行三轮。结果如下（毫秒）：

| 窗口尺寸 | 编辑器 resize 均值：旧 → 新 | 编辑器 paint 均值：旧 → 新 | 宽度事件间隔中位数：旧 → 新 |
|---|---:|---:|---:|
| 1000 × 700 | 1.88 → 1.22 | 1.20 → 0.89 | 16.04 → 16.03 |
| 2560 × 1392 | 3.60 → 2.22 | 3.15 → 2.57 | 16.02 → 16.04 |
| 3840 × 2160 | 5.77 → 3.30 | 5.60 → 4.20 | 21.34 → 16.66 |

事件间隔列为三轮中位数的中位数；4K 的 P95 同口径从 43.34 降至 28.69 ms，仍有波动。
每轮 12 次开合的可见附加显示刷新，旧版为 137—186 次，新版为 0；滚动和新可见行仍
即时发布。原始文件为 `sidebar-perf-{baseline,final}-final-{1,2,3}.json`，汇总见
`stage12-perf-comparison.json` 和 `stage12-perf-summary.json`。

以上为 offscreen、DPR 1 的 CPU／窗口尺寸事件数据，不是 Windows 实际呈现帧率。
机器存在并行工作，不能据此承诺固定 60 FPS。嵌套事件耗时不叠加为总帧耗时。
早期 qWait 驱动测量和缓存探索记录保留供排查，不纳入上述最终对照。

Windows 原生桌面使用 0.29.36 独立包验证测试工程，最大化至 2560 × 1392 后点击
侧栏收起按钮、Ctrl+1 重新展开、Files 双击切换 counter.sv，最终布局和代码文字正常。
测试进程经 Alt+F4 退出，两份测试源文件 SHA256 不变。鼠标操作前后均已通知用户。
截图中的 Pinloom 测试程序错误弹窗属于并行应用，本轮没有操作；遮挡区域不参与验收。
原生检查仅覆盖交互和最终布局，没有测量 Windows 合成器帧率。
记录为 `stage12-native-verification.json`，图片为 `native-evidence/stage12-*.jpg`。

独立预览为 `build/packages/ZeroSlack-Ela-0.29.36/ZeroSlack-Ela.exe`；67 个清单文件、
四个二进制、17 份 Ela 补丁、许可和便携版本调用通过核验，见
`stage12-package-verification.json`。补丁 08—17 重放后与 48 个上游变更文件逐字节
一致，见 `stage12-provenance-chain.json`。本轮未提交、推送或替换 0.29.25 正式包。

### 0.29.37 过渡显示层验证

Release 全量构建完成后，45 项相关回归全部通过（26.95 秒），记录为
`build/ela-migration/stage13-regression-final.log`。包含 100%／125%／150%／200% 的
侧栏、导航、窗口、布局回归，以及编辑器分屏、文档状态、上下文与源码约束。
新增断言检查：过渡中真实编辑器尺寸不变，每次开合仅一次边界 resize，旧代次回调
无效，输入可立即接管，临时图像最终释放，恢复停靠宽度与动画中销毁窗口不崩溃。
随后补充鼠标按下／松开的完整转交测试，修正高 DPI 下的命中计算；最终 9 项
侧栏／窗口复测全部通过（12.07 秒），见 `stage13-pointer-final-tests.log`。

同一 5000 行真实 MainWindow 场景，每种尺寸连续开合 12 次。离屏 DPR 1 对照：

| 尺寸 | 0.29.36 编辑器 resize／paint 次数 | 0.29.37 次数 |
| --- | --- | --- |
| 1000 × 700 | 186／186 | 12／24 |
| 2560 × 1392 | 186／186 | 12／24 |
| 3840 × 2160 | 159／159 | 12／24 |

Windows 原生测试在 DPR 1.5 下验证普通窗口及 2560 × 1392 最大化窗口（约 4K 物理
像素）：均实际启用 `direct-composition`，各 12 次开合仍为 12 次 resize、24 次 paint。
系统合成统计约 144 Hz。最大化时准备画面中位数 36.97 ms、最大 44.81 ms；普通窗口
中位数 12.03 ms，首轮仍为 55.01 ms。后台预热避免在点击处理中创建 D3D 设备，
但快照、纹理分配和上传仍有一次性成本，不能声称首帧零延迟或稳定呈现 144 FPS。

原始数据为 `stage13-composited-offscreen.json` 和
`stage13-native-run/stage13-native-benchmark.json`；汇总为 `stage13-performance-summary.json`。
Computer Use 捕获了原生过渡中的工作区，见 `native-evidence/stage13-composition-in-motion.jpg`。
原生基准只打开自动生成的临时 SV 文件。最后通过鼠标收起、Ctrl+1 展开，确认最终
布局后 Alt+F4 关闭测试窗口，没有修改用户工程。最终截图为
`native-evidence/stage13-final-expanded.jpg`。
早期并行编译下的冷启动数据另存 `stage13-native-cold-under-build-load.json`，不混入本表。

Ela 补丁 08—18 重放后与 48 个上游变更文件逐字节一致，见 `stage13-provenance-chain.json`。
独立预览包为 `build/packages/ZeroSlack-Ela-0.29.37/ZeroSlack-Ela.exe`；正式包维持 0.29.25。
包内 68 个清单文件、四个二进制与构建目录一致；18 份 Ela 补丁、许可证和仅系统 PATH
下的 GUI／CLI 版本调用均通过校验，见 `stage13-package-verification.json`。
本轮未提交或推送。原生鼠标验证结束后测试窗口已关闭。

### 0.29.38 收尾修正验证

45 项相关 CTest 通过（26.39 秒），记录为 `build/ela-migration/stage14-regression.log`。
补充从零宽侧栏开始的像素检查并更新版本后，四档 DPI 侧栏、classic 侧栏和版本一致性
六项复测通过（12.25 秒），记录为 `stage14-final-tests.log`。

新增像素用例比较过渡层展开端点与真实 Ela 侧栏，覆盖背景、边框和内容，分别从展开与
完全收起状态生成快照。100%／125%／150%／200% 均逐像素一致，记录为
`stage14-pixels-{1,1.25,1.5,2}.txt`。同一测试换用 0.29.37 的 DLL 时失败，见
`stage14-pixels-old-version.txt`；该失败是旧版本负对照，不是当前版本回归失败。

Windows 原生测试使用隔离配置和临时 5000 行 SV 文件，在 DPR 1.5 下分别运行普通
1000 × 700 和最大化 2560 × 1392 窗口。两者均启用 `direct-composition`，各 12 次
开合为 12 次编辑器 resize、30 次 viewport paint，仍未逐帧修改编辑器尺寸。
准备画面中位数分别为 10.08 ms、33.12 ms，最大值为 57.67 ms、43.34 ms。
首帧准备仍有成本；系统合成统计约 144 Hz，不能解释为应用实测帧率。

随后通过鼠标收起、Ctrl+1 展开并检查最终布局，Alt+F4 关闭测试窗口。原始数据为
`stage14-native-run/stage13-native-benchmark.json`（基准程序沿用原输出文件名），汇总为
`stage14-native-verification.json`。截图为 `native-evidence/stage14-*.jpg`。
这些是原生交互及抽样截图，没有逐帧高速录制，不能据此声称测量了全部收尾帧。

独立预览包为 `build/packages/ZeroSlack-Ela-0.29.38/ZeroSlack-Ela.exe`。68 个清单文件、
四个二进制、18 份 Ela 补丁与许可证，以及仅系统 PATH 下的 GUI／CLI 版本调用全部
通过核验，见 `stage14-package-verification.json`。本轮未提交、推送或替换正式包。

### 0.29.39 共享过渡验证

Release 全量构建完成。最终 49 项相关 CTest 全部通过（28.45 秒），见
`build/ela-migration/stage15-regression-final.log`。新增 `ui_panel_motion_test` 在
100%／125%／150%／200% 检查底栏按钮位置、动画期间编辑器几何稳定、连续反向、
输入接管、恢复状态、跨面板接替、区块重排／删除、图层裁切及窗口销毁。
四档缩放下的空白区域背景像素检查通过，见 `stage15-background-final.log`；换用首轮
DLL 后同一用例得到黑色背景并失败，记录于 `stage15-background-old-version.txt`。
该负对照用于确认回归用例能识别本次原生测试发现的问题。

Windows 原生最终验证使用真实 MainWindow、临时配置、5000 行 SV 文件和真实
Workspace Hub。DPR 1.5 下，1000 × 700 与最大化 2560 × 1392 各运行左栏、右栏、
底栏、区块四种场景，每种 12 次，共 96 次，全部使用 `direct-composition`。
每组左栏／右栏／底栏均为 12 次编辑器 resize，区块场景为 0 次。
最大化窗口每次调用中的布局、捕获和纹理准备耗时中位数分别为 34.80、34.03、
42.86、6.10 ms，最大值分别为 37.17、37.03、46.43、6.75 ms。
该一次性成本仍存在；系统合成统计约 144 Hz，不表示应用逐帧实测帧率。

鼠标复核捕获了区块、底栏与右栏过渡中的可见内容及最终布局：标题保留、背景正常，
内容确实平移／裁切，底栏按钮条固定。以 Alt+F4 关闭隔离测试窗口。
原始数据为 `stage15-native-verified/panel-native-benchmark.json`，汇总为
`stage15-native-verification.json`，图片为 `native-evidence/stage15-*-in-motion.jpg`
和 `stage15-final-layout.jpg`。没有高速逐帧录制，不能据此声称检查了所有呈现帧。

首轮原生目录 `stage15-native-run` 保留背景／图层问题的排查数据；其基准跨 DLL
完成信号连接无效，等待了 watchdog，时长数据不用于性能结论。第二轮
`stage15-native-final` 已修正背景和连接，但仍有图层顺序问题，也不作为最终验收。
最终只采用 `stage15-native-verified`；基准现已检查连接有效性，图层顺序亦已修正。

独立预览包为 `build/packages/ZeroSlack-Ela-0.29.39/ZeroSlack-Ela.exe`。
68 个清单文件、四个二进制与构建目录一致，18 份 Ela 补丁与许可证及仅系统 PATH
下的 GUI／CLI 版本调用均通过核验，见 `stage15-package-verification.json`。
本轮未提交、推送或替换正式包。
