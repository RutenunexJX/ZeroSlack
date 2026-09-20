# 按钮与复选框样式整合及 A/C 复核

日期：2026-09-20。实施基线：`main` / `d7ae782` / `v0.29.20`，既有回归 112 项。
对应 `E:/ZeroSlack/AppSuite-UI-现代化方案.md` 的 A1 及后续 A/C 对照。下文保留各阶段历史结果。
当前状态（2026-09-21）：SuiteUi 已独立提取，用户已授权三个应用的正式包默认启用；
ZeroSlack 从 0.29.23 起使用 SuiteUi 控件，`--ui-style=classic` 保留回退，
`--no-ui-animations` 关闭控件过渡。具体范围见 [SuiteUi 发布决定](suite.md)。

## 实施结果

- 普通按钮、工具按钮与复选框的外观状态由 `src/ui/insightcontrolstyle.h` 集中定义。全局与局部辅助函数不再各自维护按钮/复选框状态规则。
- `applyToolbarButton`、`applySegmentedCheckBox` 只设置角色、按字体和样式计算最小高度。删除无调用方的两个局部 QSS 生成接口；其他面板、标题、搜索框的主题辅助函数保留。
- 设置页 Apply 使用主操作角色；Revert 保持普通操作。主操作的按下、选中及禁用状态分别定义。
- `RoundedIcons::Style` 只接管 `QCheckBox` 的指示器绘制：勾选、部分选中横线及禁用状态。列表/树的勾选指示器没有改为该绘制路径。
- 焦点只改变固定宽度边框的颜色，避免获得焦点时文字和尺寸跳变。输入、键盘、焦点顺序、信号和无障碍继续由原生 QWidget 控件负责。
- 标题栏、侧栏按钮保留专属规则。未改编辑器字体、专用图渲染器、圆环菜单、浮窗窗口机制和业务数据。
- 采用即时状态反馈。本轮没有新增动画，也没有用截图推断动态流畅度。
- 后续完整窗口对照发现侧栏按钮的固定上限小于 `minimumSizeHint`：移除重复的 30 px 限制及 36 px 上限，保留最小尺寸，由布局采用样式尺寸。六主题、四档 DPI 的主窗口回归增加对应断言。
- Workspace Hub 的预览/刷新按钮改用现有 `RoundedIcons` 矢量图标；去掉固定宽度，保留 32 px 最小宽度及紧凑尺寸策略，避免文本符号缺失或裁切。

基线 `src/` 的 `setStyleSheet` 调用点为 **15 个文件、36 处调用**；实施后为 **15 个文件、34 处调用**。收益是消除两套局部状态规则和对应实例的主题连接，而非达到任意调用次数指标。Qlementine 对照中的 167/259 是运行时 `(scope, selector)` 作用点，不能直接和源码调用数量相减。

## 自动验证

Qt 6.10.2 / MinGW 13.1 / Release / Windows，测试平台 `offscreen`。日志、截图和测量保存在忽略目录 `build/evidence/control-style-20260920/`。

| 覆盖 | 方法与断言 |
| --- | --- |
| 六主题 × 四档 DPI × 六种控件角色 | 新增 `control_style_interaction_test`，覆盖普通、工具条、主操作、工具按钮、普通复选框、分段筛选复选框，共 144 组状态序列 |
| 鼠标/键盘 | 实际鼠标进出、按下、移出取消、12 次连续点击；Space、Tab/Shift+Tab 和对话框默认按钮 Enter；检查信号次数与尺寸稳定 |
| 中断与禁用 | 按下后禁用或隐藏再释放不误触发；禁用不响应 Space；勾选标记不能仅用底色代替，部分选中必须与勾选不同 |
| 完整应用入口 | 实例化 MainWindow，经欢迎页设置按钮打开实际设置页；通过覆盖开关和编辑复选框修改草稿，再通过 Revert 恢复；六主题检查 Apply 可用性和字体 |
| 实际浮窗组件 | 将同一个 Kernel 面板移入 ContextFloatingWindow，再取出并还原停靠；通过实际筛选控件验证节点数和视图缩放，循环六主题 |
| 既有布局与专业功能 | 四档 `high_dpi_layout_test`、原主题与编辑器字体测试，以及全量现有回归；滚动区域沿用“显示区域不越界且控件完整可达”的已批准口径 |

原 `insight_visual_style_test` 中两个“局部 QSS 包含字符串/换主题时字符串变化”的断言已改为实际渲染的选中态差异及主题往返像素比较；其余断言保留。新增交互测试还检查指示器内部的勾选色像素，避免仅改变背景也被判为通过。

A1 初次全量 CTest **116/116 通过**（96.33 秒）。加入上述 Hub 和侧栏尺寸修复后的最终构建退出码 **0**，`FAILED:` 数量 **0**；全量 CTest 再次 **116/116 通过**（并行 4，97.71 秒），允许失败集合为空。后一次日志位于 `build/evidence/qlementine-integration-20260920/product-build-2.log` 与 `product-ctest.log`。

四档布局测量均为 0 个失败、0 个未覆盖辅助控件；每档按界面、控件标识和文字匹配到 233 个可唯一对应的字体记录，变化数均为 0。布局截图各 19 张；每档交互测试覆盖六种主题的状态序列、完整主窗口设置页和 Kernel 浮窗/停靠往返。

首轮曾发现 220 px Hotspot 面板因复选框外框增加而多换行，已用边框内的 padding 补偿恢复原有紧凑尺寸，`compact_layout_test` 原断言未经放宽且已通过。调试期间一次链接因测试进程仍占用 DLL 失败；等待测试结束后重新构建成功，最终验收仅引用成功构建后的上述结果。

复核入口：`final-build-result.txt`、`final-build.log`、`final-ctest.log`；`before/<scale>/` 与 `after/<scale>/` 为四档布局截图，后者还包含控件状态、完整主窗口和浮窗截图。截图均为离屏渲染。

## 完整窗口 A/C 对照

固定 Qlementine `209e549c415f1d828883f9f21a633eb535f9d67d` / 1.5.0.0。A 使用当前产品样式；C 在独立程序中全局使用 Qlementine 的绘制与尺寸，应用适配器管理控件初始化、焦点和保护区域。实验不接入产品 CMake。

| 职责 | C 的实际处理 |
| --- | --- |
| 控件初始化 | 保留 Qt 控件输入和焦点策略，跳过上游 QWidget polish 的外部焦点框、组合框视图替换及事件过滤器；焦点直接画在控件内 |
| 弹出菜单 | 保留列表式组合框；禁用 Qt 的触发项闪烁与菜单淡出，避免库内动画开关关闭后仍延迟处理下一次选择 |
| 主操作与图标 | 现有 primary 角色只映射绘制参数，不改变默认按钮语义；图标沿用现有矢量图形，由库处理前景色 |
| QSS 职责 | 实验路由器移交标准控件规则并保留移交记录；标题栏、侧栏及浮窗表面保留专属规则。路由器是实验工具，生产迁移需显式所有权清单 |
| 编辑器与专用视图 | 在保护根及所有子控件设置原有样式，根恢复原 QSS；不能仅给根设置 style 并假定子控件继承 |

最终矩阵：六主题 × 四档缩放 × A/C（C 分动画开、关）共 **72 个独立进程，72/72 通过**。证据位于 `build/evidence/qlementine-integration-20260920/matrix-fixed/`；`gallery.html` 可切换主题、DPI 与界面，`analysis.json` 为测量汇总。

- 六种控件角色执行悬停、按下、移出取消、13 次鼠标/键盘激活、按下后禁用、勾选/部分选中、焦点与尺寸检查；组合框每进程连续展开和选择 12 次。
- 从真实 MainWindow 欢迎页入口打开设置，编辑覆盖开关并 Revert；每进程将同一个 Kernel 面板在实际浮窗组件与停靠容器间往返 8 次，销毁旧浮窗，验证节点数和视图变换。
- Hub 通过实际鼠标选择验证选中态；预览与刷新按钮检查图标、尺寸和信号。
- A/C 动画关闭的 **24 对**样本中，编辑器/gutter、主题往返后的编辑器、圆环菜单三类完整截图均逐像素一致。现有 Maple Mono 和后建 Iosevka 编辑器的 QWidget/document 字体一致；已匹配 UI 控件字体无差异。
- 框图节点/边及嵌套可读性、状态图节点/边、热点图轨道/矩阵的数量与 A 一致。业务渲染器没有替换。
- 本轮记录的可见按钮、组合框和数值输入框没有低于最小尺寸或超出根窗口的记录。此检查不等于所有标签、表格、窄窗口和原生屏幕的完整布局验收。

鼠标悬停使用 QtTest 的 QWindow 事件路径，规避 QWidget 便利接口经平台鼠标坐标造成的离屏高 DPI 误差。窗口截图的逻辑尺寸和 DPI 保存在证据中；不能将离屏窗口当作真实物理屏幕。

## 动画清理缺陷与修复

补充源码检查发现上游 `WidgetAnimationManager::stopAll()` 在 `std::for_each` 中删除当前 `unordered_map` 元素，随后递增已失效迭代器。普通 Release 的 16 次运行中开关动画未崩溃；使用**未修改的上游源文件**与 `_GLIBCXX_DEBUG` 单独编译可稳定触发 `attempt to increment a singular iterator`，退出码 3。后者是独立缺陷复现，不是产品回归失败。

隔离实验增加 **1 个上游实现补丁**：循环从 `begin()` 取键并删除，删除后重新获取迭代器。上游 checkout 保持干净；本地副本保留 SPDX 及 MIT 原文，通过显式对象文件替换静态库中的同名实现。修复后的检查版验证映射全部清空并退出 0；Release 再执行 16 次开关，退出 0。随后用修复后的实验程序重新运行上述完整 72 组矩阵。原始失败日志和未修复矩阵均保留。

动画采样只验证状态过渡确实存在、连续反向操作以及隐藏/禁用时不误触发。默认颜色过渡来自上游 192 ms / OutCubic，应用内联焦点为即时反馈。已测试动画关闭与启动时开启；运行中开关有独立复现与修复验证。离屏采样不提供原生帧率、帧间隔或主观流畅度结论。

## 结论与剩余边界

本轮证据支持继续采用 **C 的受控产品试点**，进入可选样式入口与原生桌面验收；无需先完成全仓自建样式层。C 的保护边界和交互可以成立，但其维护内容包括控件初始化策略、QSS 职责、主题/图标映射和上述上游补丁。不能将其描述为直接安装库即可完成现代化。

本轮只实测了固定上游及一个补丁的构建和重验，**尚未实测升级到另一个上游版本的成本**。下次更新版本时必须重放补丁或确认上游已修复、复验矩阵，并记录耗时及新冲突；不能从当前文件数量外推升级工时。

Qlementine 的 MIT 原文随实验保留；旧证据中的字体元数据仍适用（8 份 Inter 的 OFL-1.1、4 份 Roboto Mono 的 Apache-2.0）。正式分发前仍需补齐随包版权、字体许可全文及 Qt 发行材料；本轮不生成含 Qlementine 的正式包。

独立实验结束时，原生 Windows 观感、DWM 毛玻璃、Snap、跨屏动态 DPI、连续拖放、帧节奏以及全部自定义字体尚未验证。下述产品预览缩小了原生验证缺口；默认产品仍使用 A1，SuiteUi 仍以第二个应用验证为提取条件。

## 可选产品入口与原生桌面复核

日期：2026-09-20。工作区基线：`e409383` / `0.29.21`。本节是未发布预览的实施与验证记录，
不是新正式版本，也不替代前述独立评审。正式安装目录未更新。

### 构建、配置与样式边界

- `ZEROSLACK_ENABLE_QLEMENTINE` 默认 `OFF`。启用后编译固定版本的静态 Qlementine，新增
  `zeroslack_preview` 目标，输出 `ZeroSlack-Qlementine-Preview.exe`；普通入口仍默认 classic。
- 预览默认 Qlementine；`--ui-style=classic` 可作对照，`--no-ui-animations` 关闭库内控件动画。
  后端只在启动时选择；运行期间可切换主题和动画开关，不能切换后端。
- `ApplicationThemeManager` 对外仅暴露 Qt/应用类型。适配器接管普通控件绘制、尺寸和主题映射；
  QWidget 继续负责输入、信号、弹出列表和无障碍。跳过上游 QWidget polish，焦点画在控件内部。
- 显式 `chromeStyleSheet` 保留主框架、标题栏、入口栏、菜单和标签角色规则。部分专业面板及
  Workspace Hub 的局部 QSS 仍保留；本次没有声称完成全仓标准控件 QSS 清零。
- 编辑器、圆环菜单、`InsightGraphView` 的保护根与所有子控件使用 classic 样式；新增子控件、
  换主题和重挂载后重新同步。框图、果核图、热点轨道/矩阵及状态图仍使用现有专业渲染器。
- 现有 `RoundedIcons` 负责图标颜色，关闭 Qlementine 的统一图标重着色；沿用现有 UI/编辑器字体。
- 预览使用独立应用名称、INI 设置路径和工作区会话文件，不注册 SuiteRuntime。
  选择打开的源文件及工作区 `.zeroslack` 仍属于该工程，不能把配置隔离理解为源文件沙盒。

直接 vendored 预览构建需要 `-DZEROSLACK_ENABLE_QLEMENTINE=ON -DZEROSLACK_ENABLE_SUITEUI=OFF`，随后正常执行
`cmake --build` 和 CTest。打包脚本：`scripts/package-qlementine-preview.ps1`。
默认输出 `build/previews/ZeroSlack-Qlementine-0.29.21/`；更新现有预览必须加 `-UpdatePreview`，
并且目录中的预览标记必须匹配。脚本只复制明确的应用/DLL、Qt 插件及许可，不更新正式包。

### 原生检查发现的三项缺陷

| 缺陷与原因 | 修复及验证 |
| --- | --- |
| 最近工程读取了正式版配置。旧的 `QSettings(org, app)` 构造函数使用 NativeFormat，不遵循预览设置的 defaultFormat | 最近工程、编辑器外观、自定义缩写和列号工具改为显式使用 defaultFormat/UserScope。临时 INI 回归验证读写；预览重启后只出现本轮 native-fixture |
| 欢迎页蓝色笔画变为单色。库的统一图标重着色覆盖了自带图标引擎 | 禁止统一重着色；主窗口回归检查强调色像素，原生欢迎页重新显示蓝色笔画 |
| Dark 下禁用 Apply 和已勾选复选框的标记不可见。禁用背景与前景均映射为 `#64748b` | 禁用主色背景改用 `panelSubtle`；六主题验证有字/无字及勾选/部分选中的像素差；重建后的原生深色设置页确认文字和标记可见 |

所有源码修改使用 `apply_patch`。依赖复制、打包和哈希清单生成属于机械操作。

### 验证结果与边界

最终构建退出码 **0**，日志中 `FAILED:` 为 **0**；全量 CTest **124/124 通过**，并行 4，
耗时 **111.48 秒**。原有 116 项保留，新增四档 Qlementine 控件与四档生命周期测试。
新增覆盖配置隔离、禁用文字/标记、主题往返、保护树重挂载、组合框连续选择和动画开关；
动画采样只证明存在状态过渡，不提供 DWM 帧率结论。

原生检查使用独立预览 EXE、Windows SendInput 和 Windows.Graphics.Capture，测试工程为
`build/evidence/qlementine-product-20260920/native-fixture/`。只对该临时工程执行打开操作。
观察到普通主窗口为 800×600、最大化捕获为 1920×1032；未独立测量系统缩放，
不能把该次操作算作四档 DPI 真机矩阵。

| 原生项目 | 结果 |
| --- | --- |
| 最大化、最小化、激活恢复、正常关闭与重启 | 已执行，无崩溃；重启读取预览自己的主题及最近工程 |
| 工程目录/别名对话框、Files 双击打开文件 | 已执行，编辑器保持等宽字体，语法颜色与行号显示正常 |
| 浅色切深色、设置下拉列表、覆盖开关、Tab/Space、Revert | 已执行，选择与恢复生效；最终包复核禁用标记和文字 |
| Kernel 侧栏拖出、浮窗标题栏移动、Pin 返回侧栏 | 已执行，面板内容保留；此次目标为单节点，不能证明复杂图的完整操作 |
| 浮窗边缘缩放、Windows 边缘 Snap | 本轮未形成可确认结果；未计为通过 |
| 跨屏 DPI、断开显示器、六主题全部原生对照、复杂多窗口连续拖放 | 未覆盖 |
| DWM 材质系统回退、帧节奏与主观动画质量 | 未验收 |

证据位于 `build/evidence/qlementine-product-20260920/`：
`disabled-content-build.log`、`disabled-content-ctest.log`、`package-disabled-content.log`；
`native-dark-settings.png` 为原生深色设置页捕获，其他测试截图仍是离屏图。
本轮原生截图和输入观察不能替代其余待验证项，清单继续维护在 `unverified-manual-checks.md`。

### 依赖与分发

vendored 源码位于 `thirdparty/qlementine/`，仍固定 `209e549c415f1d828883f9f21a633eb535f9d67d`。
140 个上游文件的字节哈希核对仅 `WidgetAnimationManager.cpp` 与原版不同，对应已记录的
`stopAll` 修复；其余附加文件为来源、补丁、哈希与许可记录。局部 `.gitattributes` 禁止换行转换。
预览包含 Qlementine MIT、8 份 Inter 的 OFL、4 份旧 Roboto Mono 的 Apache-2.0 许可全文与
字体元数据，以及 Qt/MinGW 分发材料和 `SHA256SUMS.txt`。升级到其他上游版本的成本仍未实测。

## 动效场景规格（2026-09-20）

本表是 README 与 suite.md 的唯一动效数字来源。状态表示本行完整的交互规格是否验证，
不把存在动画对象等同于验证了时长和原生流畅度。本轮不为 classic 新增动画。

| 场景与路径 | 时长、曲线 | 中断与输入 | 减少动画边界 | 实现状态与证据 |
| --- | --- | --- | --- | --- |
| classic 按钮、工具按钮、复选框、分段选中反馈 | 即时（0 ms），无插值 | 新状态直接替换，输入不等待 | 已即时，不增加额外开关 | 已实现并实测；`control_style_interaction_test::states` 在真实控件上比较悬停/按下图像、激活计数及取消，classic 基线 116/116；未测原生呈现延迟 |
| SuiteUi 0.1.0/0.1.1 与 vendored 控件状态色、勾选指示器 | 默认 192 ms，OutCubic；内部焦点即时 | Qt 先处理输入；上游反向过渡从上次目标重新起步，不能宣称从当前颜色连续反转；隐藏停止，关闭动画清空动画对象 | ZeroSlack `--no-ui-animations`；RegMap `QT_REDUCE_MOTION`/系统偏好各自映射 SDK 开关 | 已实现未实测；过渡存在性及开关生命周期已覆盖，完整的默认时长/反向连续性及原生帧节奏尚未全部实测 |
| SuiteUi 0.1.1 显式过渡时长接口 | 非负毫秒，OutCubic；测试取 0/600 ms，负数明确拒绝 | 改时长后下一次绘制重设该控件的过渡；保留原输入语义 | `setAnimationsEnabled(false)` 优先，立即呈现最终状态 | 已实现并实测；`transitionDurationChangesRenderingAndReducedMotionWins` 验证中间图像区别于两端，600 ms 用例在 565 ms 采样时像素已量化为终值；这不是计时器或帧率精度结论 |
| ZeroSlack 左侧栏宽度 | 180 ms，OutCubic | 停止旧过渡，从当前实际宽度转向新目标；完成后才隐藏 | `setExpanded(..., false)` 即时；尚未接系统偏好或控件动画 CLI 开关 | 已实现未实测；`navigationpanecoordinator.cpp`，原生帧节奏与中断时序未形成量化验收 |
| ZeroSlack 底栏生产路径 | 即时（0 ms），无插值 | 连续反向点击立即落实最后状态 | 构造器主动禁用高度动画，与 SDK 开关无关；未启用的内部动画分支为 140 ms / OutCubic | 已实现并实测；`panel_layout_controller_test::verifyClickGeometryStability` 检查默认禁用、展开高度和按钮/顶层几何不变，基线通过；未测屏幕呈现延迟 |
| Files/Design/Pinloom 条目底纹过渡候选 | 100 ms，OutCubic | 从当前颜色重定向，不重启固定起点，不阻塞选中 | 未来开关关闭后即时；当前底纹仍即时 | 仅规格未实现；不得把现有静态 hover 规则计作过渡通过 |
| 信息浮窗淡入/关闭候选 | 打开 100 ms、关闭 60 ms，OutCubic | 交互区立即可用，重复开关从当前透明度反向；关闭取消挂起任务 | 未来开关关闭后即时；不得淡化文字与内容独立造成透明度叠乘 | 仅规格未实现；原生材质与此淡入规格分开验收 |
| 圆环菜单及子项条候选 | 80 ms，OutCubic | 立即命中，不延迟关闭；从当前透明度反向 | 未来开关关闭后即时；禁止恢复矩形阴影 | 仅规格未实现；当前菜单行为保持原样 |
| Activity 数字提示、复制成功指示候选 | 提亮 120 ms / OutCubic；成功勾号保留 800 ms 后即时恢复 | 新事件重置提示期限，不能排队积累闪烁 | 关闭装饰动画后仍直接显示语义结果；不丢计数与成功反馈 | 仅规格未实现；没有计作已覆盖 |

本次规格共 9 行：已实现并实测 3、已实现未实测 2、仅规格未实现 4。
离屏图像采样一律 `nativeFramePacingMeasured: false`。上述 565 ms 是在颜色量化后的
首个满足图像相等断言的采样时间，不是 600 ms 配置的误差上限，更不是 DWM 呈现间隔。
原始数据保存在 `build/evidence/plan-remainder-20260920/a-sdk011/controls.log`。

减少动画行为的既有覆盖：ZeroSlack `animatedControlsAndPopups` 连续切换 16 次，核对
激活计数和 popup 关闭；RegMap `appliesWorkbookTheme` 校验环境覆盖，真实控件矩阵分别
运行开/关路径。SDK 新增用例进一步断言关闭动画后按下/松开图像立即等于终态。
这些分层测试不表示 ZeroSlack CLI 控制了导航栏，或 RegMap 实时监听了系统偏好变化。

## 其余控件的状态与验收规则

本节规范当前应用所有的控件，不扩大 SuiteUi 绘制范围，不改变默认外观。状态优先级为
禁用优先，按下/选中优先于悬停，键盘焦点叠加在可用状态；只读不是禁用。下表中“未覆盖”
表示缺少对应会失败的断言，不能以规则字符串存在或截图已生成代替交互和视觉验收。

尺寸使用语义量：`s = density.baseSpacing`，`h = density.controlHeight`，
`hc = density.compactControlHeight`，`r = density.smallRadius`，
`f = focus.width`，`fm = QFontMetrics(该文字角色字体)`。
ZeroSlack 对应 `InsightDensityTokens/InsightFocusTokens/UiTypography::Role`；
RegMap、Pinloom 分别映射自身的 metrics/字体角色，不能强制共用另一应用的字体。
控制框最低高度为 `max(h或hc, fm.height + 上下内边距 + 边框)`；布局使用 `sizeHint`/
`minimumSizeHint`，不把 token 的内容区高度误当成含边框的总高度。几何验收取当前
`availableGeometry()`；滚动容器检查显示区域不越界且每一项可完整滚入视口。
既有 QSS 仍含字面尺寸，本节是其语义归属规则，不声称已把所有常量重构为 token。

| 控件 | 普通 / 悬停 / 按下 / 选中 / 焦点 / 禁用与特有状态 | 尺寸、密度规则 | 绘制和行为所有权 | 已有会失败的断言与缺口 |
| --- | --- | --- | --- | --- |
| QLineEdit | input 背景/正文；hover 边框；按下定位光标；选区 selected/inverse；focus 描边；disabled 背景/文字；只读允许复制，错误附语义提示，placeholder 用次级色 | 高度用上述公式，横向内边距 `2s`；宽度至少容纳最小语义值，长路径允许水平浏览，不压缩字体 | ZeroSlack `insightvisualstyle.cpp` 的输入 QSS 画边框/颜色；Qt 编辑、选区、验证器和输入法；SDK fallback 不接管 | `high_dpi_layout_test.cpp::audit` 的 outside/undersized/intersections 计入失败；SDK `fallbackSurfacesKeepPixelsAndMetrics` 检查 sizeHint 和整图相等。选区/只读/错误状态完整视觉组合 **未覆盖** |
| QComboBox 与 popup | 输入表面；hover 边框；按下展开；选中当前项；focus 描边；disabled 不展开；popup hover/selected 区分，Escape 不提交 | 主体高度同输入框；箭头命中宽度至少 `hc` 的语义紧凑目标；popup 行高取 `max(hc, fm.height + 2s)`，总高受屏幕可用区限制，溢出滚动 | 主体/列表色由应用 QSS；展开、键盘提交、滚动和无障碍由 Qt；SDK 不替换 combo view | ZeroSlack `qlementine_lifecycle_test.cpp::animatedControlsAndPopups` 16 次 Up/Down/Return 后断言 popup 隐藏和 currentIndex；SDK fallback 像素/尺寸相等。Escape 取消与屏幕边缘 popup 几何 **未覆盖** |
| QAbstractItemView 列表/表格 | panel/正文；hover 表面；按下命中当前模型索引；选区 selected/inverse；focus 与选择独立；disabled 保留内容但不编辑；编辑、空状态和多选由业务模型定义 | 行高至少 `max(hc, fm.height + 2s)`，专业表格可使用自身 row metric；列宽允许滚动，禁止为适配主题改模型或压缩文字 | QSS 负责普通状态色，Qt delegate/model/selectionModel 负责内容和编辑；专业表格保护根保留原 style；SDK 不绘制 | RegMap `suiteui_controls_test.cpp::realControlsAndThemeLifecycle` 检查列数/字体、双行批量编辑及 undoDepth；ZeroSlack high-DPI audit 验证可达性。全体 selected+disabled、hover+editing 视觉组合 **未覆盖** |
| QScrollBar | groove/handle；hover 强化 handle；pressed 拖动；无独立选中态；键盘焦点仅对可聚焦实现有效；无范围/disabled 不滚动 | 厚度来自应用 scrollbar metric（现有值保持）；handle 最小长度用 `hc`，实际长度遵循 pageStep/range；命中区不得小于绘制区 | 应用 QSS 负责 groove/handle 外观；Qt 负责范围、页步、滚轮与拖动；SDK fallback | ZeroSlack high-DPI audit 逐项改变滚动值并将 unreachable 计为失败；RegMap GUI 测试第 3159/3161/3170/3172 行断言滚动位置保持。handle 按下/禁用/焦点视觉与鼠标拖动 **未覆盖** |
| QTabBar | tab 普通表面；hover 表面；按下不抢先提交关闭；selected 强调；focus 标识；disabled 不切换；溢出保留滚动按钮、拖动及关闭入口 | 高度 `max(hc, fm.height + 2s)`；标签可读宽度由 fm 的最少可辨识字符加图标/关闭按钮与间距推导；不足时滚动，不将标签缩为不可读 | 应用 tab QSS 与 EditorSplitController 拥有外观/分组，Qt 提供 tab 选择/滚动；SDK fallback | `compact_layout_test.cpp::editorTabs` 检查实际绘出的标签至少可辨识字符、tab 宽度和滚动按钮最小尺寸；旧像素断言未删改。disabled/focus/拖动中的组合视觉 **未覆盖** |
| QMenu 与菜单项 | menu 表面；hover 使用选择色；按下触发 action；勾选项保留 checked 指示；键盘选中态；disabled 灰色不可触发；separator/submenu 不当作普通动作 | 行高 `max(hc, fm.height + 2s)`；图标列、标签、快捷键、子菜单箭头各自计入 sizeHint；popup 受屏幕可用区约束 | 标准菜单颜色/间距归应用 QSS，Qt 负责 QAction、子菜单与键盘；圆环菜单是独立专业交互，不能用此表替换 | `context_workspace_test.cpp::verifyNoActiveDocumentBinding` 断言 disabled/action tooltip、启用以及 trigger 后复用同一 view。完整菜单渲染、子菜单边界、Escape/快捷键操作 **未覆盖** |
| QToolTip | 提示表面/文字；无可交互 hover/pressed/selected/focus；描述 disabled 原因仍可保留文本；富文本需由内容所有者转义 | `fm` + `s` 内边距，长文本换行宽度取屏幕可用区与角色最大行宽的较小值；不固定设备像素尺寸 | 应用 QSS/QToolTip palette 负责外观，Qt 负责出现/消失；SDK 构造和换主题不得改全局 tooltip palette | SDK `rendererDoesNotChangeApplicationTypographyOrPalette` 四轮断言 QToolTip palette 不变；Context 菜单断言禁用解释非空。真实弹出框的换行/边缘位置/延时 **未覆盖** |
| QSplitter handle | 边界 token；hover 强化；pressed 保持抓取反馈；无选中态；支持键盘的应用提供 focus；disabled 不改变比例；双击复位若存在由应用定义 | 厚度来自 splitter handle metric；hit rect 包含绘制 rect；两侧最小尺寸从内容 minimumSizeHint 导出，不能侵占按钮命中区 | 应用 QSS 定义表面，Qt splitter 分配尺寸；应用决定可聚焦性/键盘步进；SDK fallback | RegMap `structuresCompetitionShellResponsively` 断言方向及两侧尺寸；同文件第 3019–3023 行检查真实 handle 和 focusPolicy。连续鼠标拖动、键盘步进及焦点视觉 **未覆盖** |
| QLabel 文字角色 | body/title/metadata/technical/status 由角色决定；普通标签无 hover/pressed/selected；可选中文字遵循 selection token；链接有独立 focus；disabled 用 disabledText；状态不能只靠颜色 | 字体由 `UiTypography::Role`/应用角色导出，标题权重用 typography token；行距由 fm 与 role spacing，长提示换行或可复制省略，禁止固定高度截断 | 应用字体与 QSS/role 属性负责；Qt QLabel 排版、链接和选区；SDK 不修改应用字体/palette | RegMap `structuresCompetitionShellResponsively` 检查 title/path/state 实际内容和响应式显隐；ZeroSlack `compact_layout_test.cpp::untitledNames` 检查显示标题/tooltip URI。所有文字角色对比度、长译文、链接焦点视觉 **未覆盖** |

覆盖结论限于上述明确断言：9 类均有部分既有验证，但没有一类被宣称为所有状态组合
已完整覆盖。QSS 字符串测试只证明规则存在；离屏像素相等只证明 SDK 未改变该样本，
两者都不代替原生显示、键盘可用性和屏幕边缘验收。本轮既有断言未删改。
