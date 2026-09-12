# Context 浮层第 3 期验收记录

基线：8ddc7e97 / 0.25.12。目标：0.26.0。自动化结果见下表；原生桌面验证未测。

## 实施前核实与断言调整依据

- 第 2 期 single_floating_instance 检查 overlay/native 互斥，与本期原生多实例目标直接冲突。保留 overlay 替换旧 view 的要求，改为原生窗口并存、旧 overlay 释放、所有 stableKey 唯一、同资源重开不创建视图的更强检查。
- kVersion=4 的既有断言需升为 5；补充真实 0.25.12 输出的 v4 输入检查和所有新字段往返，而不是跳过旧字段。
- 原生浮层从 v4 仅恢复几何升级为 v5 恢复资源及几何。原测试中 restoreState 后资源为空的假设需按此修订；所有精确几何、指针和原 overlay 断言仍保留。
- 既有图标选择有 providerId 名称映射（contextworkspacecontroller.cpp:47–79），属于本期前已有的图标映射；不扩展为窗口或绑定决策。
- 文档切换和文档关闭是不同事件。TabManager 提供 activeDocumentChanged 与 tabClosed；仅 setActiveDocument(path) 无法区分非活动文档关闭。用户已明确允许增加 documentClosed(path) 通知，控制器不依赖 TabManager。
- LiveInsights 的 Pin 回调原来调用 pinPeek()（mainwindowcontextworkspace.cpp:310），单实例下成立，多实例下可能固定错误窗口；改为按回调 resource.stableKey() 指定实例。
- provider.activationResource 的 stableKey 如果固定，菜单“新建”仍应聚焦已有同资源实例，不能伪造资源身份违反同资源唯一规则；多个窗口需要多个不同资源。
- 第 2 步原生浮层允许 Kept/Global，以使 Kept/DocumentBound 切为 Global 时保留持久性维度；overlay 仍只接受原有 Transient/Global。原“unsupported”夹具使用不可分离 provider 且无活动文档，因此其原拒绝断言仍成立，另加原生 Kept 与绑定成功检查。

## 兼容与验证边界

v4 → v5 是单向升级：旧 0.25.12 读端将整块忽略 v5 Context 状态。v4 标量几何继续保存，含义为最近一次原生浮层几何。
不修改 contextpeekhost.cpp 或 resolvedFloatingGeometry 函数及其常量。原生边框自纠正纳入第 1 步，并保留 offscreen 不等价于真机显示器/DPI验证的限制。

新增 `kept` 字段保存持久性维度，避免 Kept 文档实例重新打开后退化为 Transient；新增 `documentFloatingOrder` 明确保存从旧到新的 LRU 顺序。两者均属于 v5，不改变 v4 读取门槛。全局实例与文档实例分别保存，资源不重复写入两处。

第 1 步检查点在第 2 步开始前完成，源码快照分别存于 `build/evidence/floating-multi-baseline-source/` 与 `floating-multi-step1-source/`。检查点完整构建通过，十项回归 10/10（20.71 秒），全量 106/107（247.36 秒）。修复本期测试发现的 cascade 边缘重叠、MRU 字符串引用失效后才进入文档绑定实施。

最终复查同时发现 `resetPeekToProviderPreferredSize()` 仍引用当前浮层，改为只查询 overlay 自身资源。新增 `multi_overlay_reset` 检查关闭另一原生窗口后，overlay 的尺寸重置与视图身份仍保持原有语义。`ContextPeekHost` 实现没有改动。

## 31 项验收

表内源码路径相对于仓库根目录；证据统一位于 `build/evidence/`。第 18 项的独立性质逐条映射见下一节。

| # | 检查项 | 结果与证据 |
|---|---|---|
| 1 | 基线 | 106/107，245.84 秒；`floating-multi-baseline-ctest.log`。仅 `workspace_persistence_test` 的两条既定 fixture 断言失败，其余 27 条通过。 |
| 2 | 第 1 步检查点 | 完整构建通过；十项 10/10；全量 106/107。`floating-multi-step1-checkpoint-build.log`、`floating-multi-step1-regression.log`、`floating-multi-step1-ctest.log`、`floating-multi-step1-comparison.json`。 |
| 3 | 原生多实例、单份 overlay | `src/ui/contextworkspacecontroller.h:101` 为窗口容器；`src/ui/contextworkspacefloating.cpp:49` 分配/复用窗口；控制器构造函数只创建一次 ContextPeekHost。 |
| 4 | cascade 与可见范围 | `src/ui/contextworkspacefloating.cpp:20`；`verifyMultipleFloatingWindows` 验证三个不同外框、不同 QWidget，偏移后的窗口完全位于 availableGeometry 内。 |
| 5 | rail 左键三态 | `src/ui/contextworkspacecontroller.cpp:1106`，单一 `activateRailProvider` 方法，32 行（含签名、花括号）。`verifyRailThreeStates`、`verifyMultipleFloatingWindows` 覆盖默认打开、MRU 聚焦、再次隐藏。 |
| 6 | rail 右键分层 | `src/ui/contextrail.cpp:20` 发 id 与全局坐标；`src/ui/contextworkspacefloating.cpp:253` 构建菜单。静态检查对 rail 两文件搜索 `instance\|binding\|DocumentBound` 无匹配。 |
| 7 | 一键收起 | `src/ui/contextworkspacefloating.cpp:162` 仅改可见性，171 行维护 toggle。`multi_collapse`、`multi_expand`、`collapse_overlay`、`expand_overlay` 比较所有状态字段及 `closed.isEmpty()`。 |
| 8 | v5 与旧字段 | `src/ui/contextworkspacestate.h:17` 新版本门槛；原六个 floating 标量保留。序列化写端 `src/workspace/workspacesessionstateservice.cpp:347`，读端 625 行按 v5 门槛读取新增字段。 |
| 9 | 恢复上限 | `verifyFloatingRestoreLimit`：输入 20，恢复 16，跳过 4，warnings 非空；同样覆盖 collapsed 状态和生产序列化往返。 |
| 10 | 原生边框自纠正 | `src/ui/contextfloatingwindow.cpp:119` 保存请求外框，抑制 show 期间中间几何写回；show 后不等则再次 apply，再 remember。`verifyNativeFrameCorrection` 在 setView 返回后立即精确比较外框。 |
| 11 | 真机 frameMargins | 未测，没有四边实测值。offscreen 的结果不能证明 Windows 窗口管理器建立边框的真实时序。 |
| 12 | 文档通知边界 | `src/ui/contextworkspacedocuments.cpp:82` 接收相对路径；96 行接收用户批准的 documentClosed。`src/app/mainwindowcontextworkspace.cpp:145` 转发切换，149 行转发最后一个同路径编辑视图关闭。控制器无 TabManager/DocumentModel include。与原“唯一通知”要求的差异已获用户批准。 |
| 13 | 绑定仅用于原生 Floating | `src/ui/contextworkspacecontroller.cpp:352` 拒绝 Docked/DocumentBound；浮层分流后继续拒绝 overlay 的新组合。`verifyDocumentBindingVisibility`、`verifyUnsupportedPlacements` 覆盖成功与拒绝路径。 |
| 14 | 无活动文档明确拒绝 | `verifyNoActiveDocumentBinding` 验证返回 false、原因非空、createView 为 0；菜单绑定项禁用并带提示。 |
| 15 | 文档布局 32 条 LRU | `verifyDocumentLayoutEviction`：归档 33 条后保留 32、淘汰 0；重新访问 1 再归档 33 后淘汰 2，明确验证最近使用顺序；32 条完整状态往返。 |
| 16 | 移动不重建视图 | `verifyDetachableRoutingAndMovement` 的 `view() == original`、`viewForResource(...) == original` 和 `created == created_before` 覆盖 overlay→dock→native→dock→overlay；`multi_collect` 覆盖批量搬运；`verifyDocumentBindingVisibility` 覆盖双向绑定同指针及创建计数。 |
| 17 | 真实 v4 输入兼容 | 保留基线 EXE/DLL 在 `floating-multi-v4-runtime/`，由 0.25.12 输出 `floating-multi-v4-state.ini`（104 checks），新构建用 `--state-import` 导入并输出 v5。`floating-multi-cross-build.log` 记录结果；额外 `verifyV4ContextInput` 验证所有旧浮层字段非默认值，且 v4 中混入的 v5 键不被读取。 |
| 18 | 15 条独立新性质 | 全部实现；逐条测试映射见下表，均进入默认/125%/150%/200% 四个变体。 |
| 19 | 四个缩放 | 默认 151/151、125% 151/151、150% 151/151、200% 151/151。日志 `floating-multi-final-regression.log`。 |
| 20 | 十项指定回归 | 10/10，20.92 秒。四个 Context 变体各 151 checks；temporary_editor_context_provider_test 21 checks，pinloom_context_provider_test 56 checks。十个名称与结果见下节。 |
| 21 | 最终全量与基线对照 | 106/107，93.84 秒（-j6），107 项状态逐项与基线相同；唯一失败测试及其两条断言未变。基线 `floating-multi-baseline-ctest.log`；最终 `floating-multi-final-ctest.log`；逐项对照 `floating-multi-final-comparison.json`。 |
| 22 | 既有断言未放宽 | `floating-multi-static-verification.json`：99 条旧断言保留（仅版本 4→5 归一化）；1 条旧单实例互斥断言以更强多实例和 overlay 替换检查取代。未加容差、fixture 特判或新的 DPI 条件跳过。 |
| 23 | overlay 实现零改动 | 基线与最终 `src/ui/contextpeekhost.cpp` SHA256 相同；git diff 为空。静态检查 JSON 保存哈希。 |
| 24 | 几何解析器未改 | 基线与最终状态头文件中从几何常量到 resolvedFloatingGeometry 的完整代码段逐字相同，静态检查通过。 |
| 25 | fixture 与注册未动 | workspace_persistence_test.cpp、Pinloom/temporaryEditor provider 测试哈希相同；CMake diff 仅添加两个 cpp。两个 .zs 仍不存在，legacy 变量及 CTest 注册未变。 |
| 26 | 未提前实施第 4 期 | 审查改动文件及 CMake diff，未修改 ContextDockHost/QTabWidget 结构，未添加标题条拖出/收进事件处理。 |
| 27 | 手工多窗口/多显示器 | 未测，无截图。待执行：三个窗口移到不同坐标/显示器，收起展开比较逐个外框。自动化覆盖三个 native Tool 对象、cascade、隐藏/恢复全字段；未覆盖真实多显示器移动及 DPI 切换。 |
| 28 | 手工 tab 切换绑定 | 未测，无截图。待执行：A 绑定窗口，切 B 再切 A。自动化覆盖 path 通知后的可见性、同资源同视图及精确几何；未覆盖实际桌面点击 tab 的交互。 |
| 29 | 手工关闭再打开文档 | 未测，无截图。待执行：A 的两个绑定窗口调整位置，关闭 A 后重开。自动化覆盖 documentClosed 释放 QWidget、归档、重开恢复两实例及 Kept；未验证真实 tab 关闭与重开全过程。 |
| 30 | 手工连续重启两次 | 未测，无截图和外框实测值。待执行：记录三次启动的 x/y/w/h 及 frameMargins。自动化覆盖精确几何恢复、生产序列化往返和 setView 返回时外框不变量；未覆盖 Windows 原生边框时序及连续进程重启。 |
| 31 | 文档与版本 | VERSION、CHANGELOG.md、plan.md、goal.md、VERSIONING.md、packaging/ZeroSlack-PACKAGE-README.txt、README.md、用户手册.md 和本记录已同步 0.26.0；两个新增实现文件已显式列入 CMake。 |

## 15 条独立性质映射

全部位于 `test_sv/context_workspace_test.cpp`。函数合并共享 fixture，不合并独立断言。

| # | 性质 | 测试函数/检查 |
|---|---|---|
| 1 | 三实例并存 | verifyMultipleFloatingWindows / multi_instances、multi_cascade |
| 2 | 同资源不重复 | verifyMultipleFloatingWindows / multi_unique_resource |
| 3 | rail 三态与 MRU | verifyRailThreeStates、verifyMultipleFloatingWindows / multi_rail_focus、multi_rail_hide、multi_rail_restore |
| 4 | 一键收起往返 | verifyMultipleFloatingWindows / multi_collapse、multi_expand；verifySingleFloatingInstance / collapse_overlay、expand_overlay |
| 5 | 恢复上限 | verifyFloatingRestoreLimit |
| 6 | 几何自纠正不变量 | verifyNativeFrameCorrection |
| 7 | v4 字段兼容 | verifyV4ContextInput、verifyStateCompatibility 的实际跨构建输入 |
| 8 | rail 右键信号 | verifyRailContextMenuSignal |
| 9 | 绑定可见性 | verifyDocumentBindingVisibility |
| 10 | 绑定不改内容 | verifyDocumentBindingVisibility 中 resource、view 和 created 的相等断言 |
| 11 | 无活动文档拒绝 | verifyNoActiveDocumentBinding |
| 12 | 收起优先于绑定 | verifyDocumentBindingVisibility 中 collapsed 后切回文档的隐藏断言 |
| 13 | 每文档布局往返 | verifyDocumentLayoutRoundtrip |
| 14 | 32 条记录确定性淘汰 | verifyDocumentLayoutEviction |
| 15 | 绑定切换不重建 | verifyDocumentBindingVisibility、verifyNoActiveDocumentBinding 的菜单切换检查 |

十项指定回归：`context_workspace_test`、`context_workspace_125pct_dpi_test`、`context_workspace_150pct_dpi_test`、`context_workspace_200pct_dpi_test`、`temporary_editor_context_provider_test`、`temporary_editor_context_search_test`、`pinloom_context_provider_test`、`live_insights_context_provider_test`、`rtl_insight_workbench_test`、`gui_smoke_test`。以上每项均 Passed，合计 10/10。真实 v4 跨构建导入模式增加两条检查，最终为 153/153；完整构建退出码为 0。

全量唯一允许的失败是 `workspace_persistence_test`：`real new and huge_prj legacy fixtures import`、`real legacy fixtures remain byte and timestamp identical`。不得把 106/107 表述成全量全部通过。

## 改动文件

| 文件 | 摘要 |
|---|---|
| src/ui/contextworkspacecontroller.h | 声明多实例、MRU、收起、绑定与文档布局接口/状态。 |
| src/ui/contextworkspacecontroller.cpp | 统一资源查重、移动、聚焦、状态恢复与 rail 左键行为，修正按实例操作回调。 |
| src/ui/contextworkspacefloating.cpp | 新增原生实例容器操作、cascade、独立收起、16 上限恢复及右键菜单。 |
| src/ui/contextworkspacedocuments.cpp | 新增路径绑定、可见性、文档关闭释放/重开恢复与 32 文档 LRU。 |
| src/ui/contextfloatingwindow.cpp | 显示后重新应用请求外框，防止中间尺寸覆盖目标。 |
| src/ui/contextrail.h | 新增仅携带 provider id 与坐标的右键信号。 |
| src/ui/contextrail.cpp | 映射右键 action，保持 provider 项位于收起控件之前。 |
| src/ui/contextworkspacestate.h | 升级 v5，增加实例、收起、文档布局及显式 LRU 字段。 |
| src/workspace/workspacesessionstateservice.cpp | 新增实例/文档布局 JSON 读写，按版本放行新字段。 |
| src/app/mainwindowcontextworkspace.cpp | 转发文档切换/关闭路径，LiveInsights Pin 操作使用具体资源。 |
| test_sv/context_workspace_test.cpp | 扩充上述 15 性质、跨构建兼容与 overlay 共存回归。 |
| CMakeLists.txt | 显式增加两个控制器实现文件。 |
| VERSION | 产品版本 0.26.0。 |
| CHANGELOG.md | 英文新增功能与兼容说明。 |
| README.md | 当前版本与多浮层/绑定/收起/菜单能力。 |
| VERSIONING.md | 当前开发版本基线。 |
| packaging/ZeroSlack-PACKAGE-README.txt | 包文档版本、能力及 v5 升级后果。 |
| plan.md | 第 3 期实施结果与第 4 期边界。 |
| goal.md | 当前目标状态及原生验证限制。 |
| 用户手册.md | 菜单、绑定、关闭恢复、数量限制的使用说明。 |
| docs/floating-multi-review.md | 本次验收、证据、差异及限制。 |

## 已知限制与差异

- 原实施文件只列 Floating/Kept/DocumentBound 为新增组合；实现同时允许原生 Floating/Kept/Global，使绑定切换保持正交语义。不可分离 overlay 不开放这些组合。控制器通过用户批准的额外关闭通知区分关闭与切换。
- 原生桌面验收 27–30 和真实 frameMargins 未测，不能据 offscreen 断言宣称已消除所有 Windows 显示器问题。自纠正在 show 后先保留请求几何，再重新应用，之后才记录外框；现有解析函数、阈值和 overlay 源码不变。
- v4 真正导出文件里的原生几何为基线测试默认值；另外用非默认旧字段构造 v4 JSON 精确验证，二者共同覆盖真实旧写端与非默认值兼容。
- 交互新建无数量上限；恢复最多 16 个原生资源，计算包含当前已存在的原生窗口，含隐藏的其他文档窗口。达到上限时文档自动恢复也会跳过，布局记录保留；restoreState 的跳过数量和原因进入返回结果，切换文档过程的局部恢复结果暂未展示到 Activity。
- 文档归档最多 32 条，以明确 LRU 淘汰；工作区外已保存文件仍可采用含 `..` 的相对路径，未保存文件不参与绑定。改名/另存为不自动迁移旧路径归档。
- 一键收起持久化；某一窗口通过 rail 左键单独隐藏不单独持久化。展开仍遵守绑定可见性与单窗口隐藏状态。
- 空原生宿主可以复用；关闭资源会释放 view，但保留空宿主对象供后续使用。`floatingWindows()` 仅返回实际承载资源的窗口。
- provider 的 activationResource 若固定，菜单新建会定位已有同资源；创建不同资源需 provider 提供不同 stableKey。未伪造 id。
- 本次提交不执行 push、不替换正式包；正式包仍为此前发布的 0.25.12。
