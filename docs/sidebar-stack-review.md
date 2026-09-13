# Context 侧栏第 4 期验收记录

基线 b904b8ae / 0.26.0；版本 0.27.0。两步功能实现完成，自动化结果如下；原生桌面手工验收未测。

## 实施前记录：必须调整的既有断言

- context_workspace_test 的 verifyRailThreeStates、主测试中的 active pinned provider、transient sidebar，以及 pinloom_context_provider_test 的第二次 rail 点击，原来要求整个 dock 不可见。本期 4.3 明确要求只折叠当前 section，因此修改为 dock 可见、目标 section 折叠，并增加其它 section 不受影响的独立检查。这不是保留旧语义的例外，而是本期规定的语义替换。
- 两条 kVersion==5 断言升级为 6。sameState 增加 dockSections 比较；直接构造的状态显式设置 section 默认状态，真实旧输入则检验只展开旧活动项的迁移。旧字段比较保持精确，不增加容差。
- 旧测试中通过内容 view 直接更改父容器布局的假设若不适用于新容器，将单独记录，不删除既有资源身份、几何和 provider 状态检查。

## 边界

第 1 步完整回归发现 `test_sv/compact_layout_test.cpp:untitledNames` 直接查找 Context 内的 QTabWidget 并读取 tabText/tabToolTip。竖栈替换后该类型不再存在。改为从 sectionHeader 读取具名 QLabel 文本和 header tooltip；所有 UUID 排除、与编辑器标题相等、复制视图/编辑后标题更新、四个窄宽度几何审计断言保持不变，不修改测试阈值。

竖栈在现有 Context QDockWidget 内实现，不新增原生 dock，不修改 overlay 或浮层几何解析器。v5→v6 单向升级，旧 0.26.0 读端会忽略整个 v6 Context 状态。拖回使用客户区 Qt 拖动手柄，不使用原生标题栏或平台消息。

## 实施过程与检查点

基线完整运行 106/107，96.67 秒。第 1 步首次全量发现 compact_layout_test 依赖旧 tab 结构；更新其结构定位后还发现嵌套图表布局在窄宽度下未完成重排。容器改用可用高度分配默认空间、标准 section 内部布局，并在 resize 的下一轮事件重新激活嵌套布局。原有 220/270/340/480 宽度下的重叠/越界/裁切审计均保留，最终十二组计数全部为 0。

第 1 步完整构建退出码 0；指定十项加 compact_layout_test 为 11/11，23.30 秒；四个 Context 缩放各 163 checks；全量 106/107，92.07 秒，107 项结果逐项与基线一致。通过后保存 `build/evidence/sidebar-stack-step1-source/`，才加入第 2 步拖动代码。

第 2 步及版本同步后的完整构建退出码 0，补充构建确认同样为 0。日志为 `sidebar-stack-final-build.log` 与 `sidebar-stack-final-build-check.log`。最终指定十项加 compact_layout_test 为 11/11，24.91 秒；四缩放各 175 checks，实际 v5 跨构建导入 177 checks。收尾还断开容器销毁前的焦点回调，并用 QPointer 保护拖动期间关闭窗口的生命周期。

## 29 项验收清单

源码路径相对于仓库根目录；日志、JSON 和源码快照位于 `build/evidence/`。

| # | 检查项 | 结果与证据 |
|---|---|---|
| 1 | 正常基线 | 106/107；`sidebar-stack-baseline-ctest.log`，96.67 秒。仅 workspace_persistence_test 两条既定 fixture 断言失败，其余 27 条通过。editor_incremental_test 基线通过，未改阈值。 |
| 2 | 第 1 步检查点 | 构建通过；指定 10/10，加窄布局为 11/11；四缩放各 163/163；全量 106/107。`sidebar-stack-step1-regression.log`、`sidebar-stack-step1-ctest.log`、`sidebar-stack-step1-comparison.json`。 |
| 3 | 单 dock 内竖栈 | `src/ui/contextworkspacecontroller.cpp:130` 为唯一既有 Context dock；`src/ui/contextdockhost.cpp:46` 在其中使用 QScrollArea/section。静态检查确认 dock 数量未增加，相关四个实现文件搜索 splitDockWidget/tabifyDockWidget 均无匹配。 |
| 4 | API 签名保留 | 对基线 contextdockhost.h 的 public 方法和 signals 逐项去空白比较，所有签名仍存在。结果见 `sidebar-stack-static-verification.json`；仅增加 section/拖动接口。 |
| 5 | 聚焦和激活语义 | `src/ui/contextdockhost.cpp:185` 跟踪唯一聚焦 key，192 行展开、滚动到标题条并设置焦点；不隐藏其它 section。verifySidebarStack 同时验证三内容区可见及另一个 section 不受 rail 影响。 |
| 6 | 折叠/改高/重排 | `src/ui/contextdockhost.cpp:233` 折叠，247 行设置请求高度，259 行重排，344 行处理真实标题/边界鼠标事件。verifySidebarStack、verifySidebarCompression 覆盖折叠往返、拖动重排信号、边界拖动高度。 |
| 7 | rail 仅折叠目标 | `src/ui/contextworkspacecontroller.cpp:1124` 保留单一三态方法，1141 行只折叠目标 section。stack_rail 验证其它 section 展开且 dock 可见；原 overlay/native 分支保留。 |
| 8 | 整条侧栏隐藏 | 既有 QDockWidget 关闭功能和 toggleViewAction 继续可用，stack_hide_whole 实测隐藏整个 dock。原“空 dock/clearResources 时隐藏”位于控制器 683/713/777 行，语义不变。 |
| 9 | v6 独立 UI 状态 | `src/ui/contextworkspacestate.h:18` 新门槛，125 行平行 dockSections 列表；资源编码仍来自 provider，未写入 collapsed/height。控制器 860 行独立采集 section 状态；序列化 348 行另写 JSON 数组。 |
| 10 | v5 迁移 | `src/workspace/workspacesessionstateservice.cpp:639` 读取 v6，旧版分支生成只展开 activePinnedResourceKey 的列表。verifyV5SidebarMigration 输入三个资源，实测折叠状态 true/false/true，默认高度 0，所有 v5 字段精确相等。 |
| 11 | 真正旧构建输入 | 开工保存实际 0.26.0 EXE/DLL 到 sidebar-stack-v5-runtime，由其 --state-export 生成 sidebar-stack-v5-state.ini（151 checks）；最终构建 --state-import 读取后输出 sidebar-stack-v6-state.ini，177/177。旧字段精确比较，新增 section 按迁移规则比较。 |
| 12 | 双向同视图搬运 | verifySidebarDragOut/verifySidebarDragBack 比较 `floating->view() == view`、`host->viewForResource(key) == original` 和 provider.created 计数；拖出删除原 section，拖回 native 无资源且不可见。 |
| 13 | Qt 客户区拖回 | `src/ui/contextfloatingwindow.cpp:109` 处理客户区手柄 MouseMove 并执行 QDrag；`src/ui/contextdockhost.cpp:423` dropEvent 处理 MIME。无 WM_EXITSIZEMOVE/WM_NC/nativeEvent 代码，无原生标题栏拦截。 |
| 14 | 不可分离限制 | `src/ui/contextdockhost.cpp:386` 禁用手柄并提示；`src/ui/contextworkspacefloating.cpp:161` 再次按 capability 验证。stack_drag_opt_out 检验强行调用失败、原因非空、实例数量不变。不按 provider id 判断。 |
| 15 | 三种插入位置 | verifySidebarDragBack：第二项上半部→索引 1；下半部→索引 2；空白→末尾。共用生产 acceptFloatingDrop/insertionIndex 路径。 |
| 16 | 14 条独立性质 | 全部实现，逐条映射见下节，全部进入四个缩放变体。新增跨工作区拒绝、空侧栏显示和取消恢复等检查。 |
| 17 | 四个缩放 | 默认 175/175、125% 175/175、150% 175/175、200% 175/175；`sidebar-stack-final-regression.log`。 |
| 18 | 指定回归 | 十项逐项 Passed，另加 compact_layout_test Passed，合计 11/11，24.91 秒。temporary_editor_context_provider_test 21 checks，pinloom_context_provider_test 56 checks。 |
| 19 | 最终全量 | 106/107，92.45 秒（-j6），107 项状态逐项与基线相同，唯一失败及两条 fixture 断言不变。基线 sidebar-stack-baseline-ctest.log；最终 sidebar-stack-final-ctest.log；对照 sidebar-stack-final-comparison.json。 |
| 20 | 既有断言不放宽 | 静态检查保留 142 个既有 Context check（版本号按 5→6 归一）；4 个 rail 旧断言改为规定的 section 折叠语义，逐条见前文；Pinloom 保留 55 条、对应 rail 1 条更新，见 sidebar-stack-pinloom-assertions.json。compact_layout_test 仅替换 tab 定位/文本访问，所有几何阈值保留。 |
| 21 | overlay 零改动 | contextpeekhost.cpp 与基线 SHA256 相同：19AE0B17E4DB4BDC1E8684A1C1A7B1FD24E3E2323CAF73DB01D78688F17016AE；git diff 为空。 |
| 22 | 几何解析器不变 | 静态脚本逐字比较状态头文件中从几何常量到解析器的整个代码段，结果相同。未改阈值或常量。 |
| 23 | persistence 测试与 fixture | workspace_persistence_test.cpp 哈希相同，CMakeLists.txt 全文相同，两个 .zs 仍不存在；未改 fixture、注册或 legacy 变量。 |
| 24 | 临时编辑器行为 | provider 测试源码哈希相同、21 checks 通过；搜索测试通过；既有 overlay↔dock 同 QWidget 搬运和 resize 语义检查保留，compact 标题/复制视图/编辑联动审计通过。侧栏外观及 rail 折叠按本期统一变化，编辑内容功能未变。 |
| 25 | 手工竖栈与重启 | 未测，无手工截图。待执行：三个 section 同时显示，折叠/改高/重排后重启对照。自动化覆盖真实鼠标事件、几何/信号、生产序列化往返；未覆盖真实桌面观感与完整进程重启。 |
| 26 | 手工拖出/拖回 | 未测，无手工截图。待执行：标题条拖出，客户区手柄拖回指定插入线。自动化覆盖标题鼠标事件、生产拖回处理器、三种落点、同视图身份和取消状态；未运行真实 Windows QDrag 嵌套事件循环的完整手势，不据此断言手感已验收。 |
| 27 | 手工旧工作区迁移 | 未测，无手工截图。待执行：0.26.0 保存三资源活动项，0.27.0 启动后观察。自动化实际旧 EXE 输出导入成功，构造 v5 三资源只展开活动项；未以原生桌面完整启动观察旧工作区。 |
| 28 | 手工 temporaryEditor 对比 | 未测，无手工截图。待执行：对照 0.26.0 的 Peek、resize、Pin、Unpin、共享文档编辑。自动化覆盖原 provider、搜索、overlay 及窄宽度审计；未测真实键鼠操作手感。 |
| 29 | 文档与版本 | VERSION、CHANGELOG.md、plan.md、goal.md、VERSIONING.md、packaging/ZeroSlack-PACKAGE-README.txt、README.md、用户手册.md 和本记录同步 0.27.0。没有新增源文件，CMake 不需要变更。 |

指定十项：context_workspace_test、context_workspace_125pct_dpi_test、context_workspace_150pct_dpi_test、context_workspace_200pct_dpi_test、temporary_editor_context_provider_test、temporary_editor_context_search_test、pinloom_context_provider_test、live_insights_context_provider_test、rtl_insight_workbench_test、gui_smoke_test。

唯一既定全量失败为 workspace_persistence_test 的 `real new and huge_prj legacy fixtures import`、`real legacy fixtures remain byte and timestamp identical`，其余 27 条通过。不得将 106/107 称为全量全部通过。

## 14 条独立性质映射

均位于 test_sv/context_workspace_test.cpp。

| # | 性质 | 函数/检查 |
|---|---|---|
| 1 | 多 section 同时显示 | verifySidebarStack / stack_visible |
| 2 | 折叠展开精确往返 | verifySidebarStack / stack_collapse、stack_expand |
| 3 | 全部折叠合法 | verifySidebarStack / stack_all_collapsed |
| 4 | 鼠标重排、信号、持久顺序 | verifySidebarStack / stack_reorder |
| 5 | rail 只折叠目标 | verifySidebarStack / stack_rail |
| 6 | v6 高度/折叠往返 | verifySidebarStack / stack_v6_roundtrip |
| 7 | v5 只展开活动项 | verifyV5SidebarMigration / stack_v5_migration、stack_v5_live |
| 8 | 聚焦最后压缩 | verifySidebarCompression / stack_compression，附 stack_resize 边界拖动检查 |
| 9 | 拖出同 QWidget、其它状态不变 | verifySidebarDragOut / stack_drag_out_identity |
| 10 | 拖出落点完全可见 | verifySidebarDragOut / stack_drag_out_geometry |
| 11 | 不可分离禁止拖出 | verifySidebarDragOut / stack_drag_opt_out |
| 12 | 上半/下半/空白插入 | verifySidebarDragBack / stack_drop_upper、stack_drop_lower、stack_drop_blank |
| 13 | 拖回同 QWidget | verifySidebarDragBack / stack_drop_identity，比较创建计数和原视图指针 |
| 14 | 拖回无重复资源 | verifySidebarDragBack / stack_drop_identity，floatingWindows 为空、原 native 无资源 |

## 文件清单

| 文件 | 一句话摘要 |
|---|---|
| src/ui/contextdockhost.h | 保留旧 API 并声明 section 布局、拖动、插入定位接口。 |
| src/ui/contextdockhost.cpp | 替换 tab 容器，管理折叠/布局/焦点/排序、落点指示及 Qt drop。 |
| src/ui/contextfloatingwindow.h | 增加客户区拖动手柄、事件过滤与开始/结束信号。 |
| src/ui/contextfloatingwindow.cpp | 从客户区手柄创建 QDrag，保留原生窗口几何/opacity 行为。 |
| src/ui/contextworkspacecontroller.h | 增加按 key 与全局坐标拖出接口。 |
| src/ui/contextworkspacecontroller.cpp | 连接拖动与布局保存，采集/恢复 section 状态并调整 rail 语义。 |
| src/ui/contextworkspacefloating.cpp | 校验可分离能力、搬运视图并解析落点几何，管理临时显示的 drop 目标。 |
| src/ui/contextworkspacestate.h | v6 与平行 DockSection 状态结构。 |
| src/workspace/workspacesessionstateservice.cpp | section JSON 与旧版本确定性迁移。 |
| test_sv/context_workspace_test.cpp | 新增 14 性质及跨构建兼容、无重复、取消/作用域检查。 |
| test_sv/compact_layout_test.cpp | 用 section 标题取代旧 tab 定位，保留所有窄宽度和名称审计。 |
| test_sv/pinloom_context_provider_test.cpp | 按新 rail 语义检查只折叠 Pinloom section。 |
| VERSION | 版本升级 0.27.0。 |
| VERSIONING.md | 更新当前版本基线。 |
| CHANGELOG.md | 新增竖栈、拖动和单向迁移英文条目。 |
| README.md | 更新 Context 多 section 与拖动能力，移除顶部 tab 描述。 |
| plan.md | 记录四期实现完成及验证边界。 |
| goal.md | 同步当前目标与尚未完成的原生验证。 |
| packaging/ZeroSlack-PACKAGE-README.txt | 更新包说明、版本及 v6 降级后果。 |
| 用户手册.md | 补充折叠/改高/重排、客户区拖回与旧工作区迁移说明。 |
| docs/sidebar-stack-review.md | 本验收记录与四期限制汇总。 |

## 与实施文件描述不符之处及实现选择

- 实施文件 1.1 的行号为近似位置；基线 Context dock 构造实际在 controller.cpp:130，原空 dock 隐藏为 672/702/766（最终为 683/713/777）。只有一个 Context dock 的事实一致。
- 既有 compact_layout_test 也依赖旧 tab 容器，实施文件未列为必须调整项；按第 6.11 条先记录，再替换结构访问，并保留更强的原几何审计。这不是用户批准的偏离。
- 采用 QScrollArea 加自有 section 布局与下边界手柄，未采用可选 QSplitter。每个展开 section 至少保留内容显式 minimumHeight 或 48 逻辑像素，加 28 标题和 5 边界；非聚焦先压缩，仍不足时滚动。0 高度表示默认分配，显式高度为请求值，受空间压缩时不覆盖已记忆请求高度。
- 拖出使用标题条/手柄的 Qt 鼠标释放位置，拖回使用 QDrag/dropEvent。拖出不会模拟原生标题栏拖动。移除 section 后暂时保持其它展开 section 的已分配高度；改高、折叠或调整容器大小后重新分配。
- 原生浮层继续隐藏/清空空宿主以供复用，拖回释放资源归属并隐藏窗口，原 QWidget 被搬运，没有销毁重建。

## 四期整体已知限制

- 原生桌面交互第 25–28 项未测；此前第 3 期的真实 frameMargins 四边值、多显示器/DPI 移动、连续两次重启外框测量仍未测。offscreen 精确几何、四缩放及生产状态往返不能替代这些测量。没有生成冒充手工验收的截图。
- 真正的 0.26.0 导出状态覆盖实际旧写端，但其原生浮层字段多为默认值；构造 v5 输入另外覆盖非默认原生几何、global 实例、kept、collapsed、文档布局和 LRU 顺序，逐字段精确比较。
- 布局绑定仍按工作区相对文档路径，改名/另存为不自动迁移旧记录；未保存文档不参与绑定。文档布局保留上限 32，浮层状态恢复上限 16，含已存在的隐藏 native 实例；交互新开实例不设上限。
- 文档切换过程中的局部恢复跳过结果尚未显示到 Activity，这是第 3 期已有边界；restoreState 返回跳过数量和原因。单浮层的 rail 隐藏状态不单独持久化，全局收起持久化。
- provider 返回固定 activationResource 时“新建”仍复用同一 stableKey；不同实例需要不同资源。侧栏 DocumentBound 仍拒绝；内容不随活动文件自动替换。
- 本次不新增 provider、不改变图表内部功能，不提供跨应用/跨工作区拖入，不把原生标题栏移动解释为收进侧栏。
- 本次仅本地提交；未 push、未替换正式包。正式包仍为此前发布的 0.25.12。
