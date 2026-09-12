# Context 放置模型第 1 期验收记录

实施版本：0.25.11。对照：`bb8b0ece`（0.25.10），Qt 6.10.2 / MinGW 13.1.0 Shared Release。
第 2 步实施文件将基线门槛修订为 106/107：只接受两份未纳入版本管理的旧 `.zs` 样本缺失引起的工作区持久化测试失败。没有生成样本、修改导入代码或跳过测试。

## 重构前故障诊断与断言修订依据

- 临时编辑器 Back：实测通知次数为 1，前后 stableKey 均为 `temporaryEditor:primary`，当前位置返回第 2 行。公共标题为 `● context_fixture.sv ·1`，与源编辑器 tab 标题相同；旧断言比较的 `displayText()` 为 `context_fixture.sv : 2`。证据：`build/evidence/context-placement-diagnosis.log`。因此排除通知未触发和 stableKey 拒绝更新，根因是旧标题断言未同步 0.25.9 的源 tab 标题设计。保留文档与行号断言，标题改为与源 tab 相等，并增加通知次数、列号、公共资源中的位置与 stableKey 检查。
- Pinloom：README.md:31、CHANGELOG.md:111（0.22.2）及用户手册.md:34/175 一致规定 rail 打开侧栏。旧测试误从 Peek 取 view 并在失败后解引用空指针。修订为验证唯一非持久侧栏、无 Peek，以及连续三次点击重用同一 view；后续显式 Peek 测试先通过 unpin 将这个真实 view 移入浮层，继续保留全部后续功能断言，并补充空指针失败退出。
- 初版文件漏列第三项失败：workspace_persistence_test 的旧样本导入和只读校验，27/29 checks。第 2 步文件已纠正这一点并明确接受该限制。

## 验收结果

路径均相对于仓库 `E:/ZeroSlack/ZeroSlack`。证据位于忽略目录 `build/evidence/`。原生桌面手工验证未测，没有生成替代截图。

| # | 检查项 | 结果与证据 |
| --- | --- | --- |
| 1 | 调试输出清理 | `temporary_editor_context_provider_test.cpp:160` 后直接执行原有断言；只删定位用的 Back evidence 输出，框架错误输出保留。 |
| 2 | 修订基线 | 106/107，289.85 秒；`context-placement-step2-baseline-ctest.log`。唯一失败为 workspace_persistence_test，恰好两条指定旧样本断言，其余 27 条通过。 |
| 3 | 旧枚举零残留 | `rg` 搜索 src、test_sv、docs、主文档及 CMakeLists.txt，0 匹配；忽略目录内保留的旧构建证据不属于源码。 |
| 4 | 三维类型 / QtCore | `src/ui/contextplacement.h:7`；只 include QMetaType、QString，均为 QtCore。提供默认值、相等/不等和名称函数。 |
| 5 | 三种等价映射 | `mainwindow.cpp:1362` 为 Floating/Transient/Global；`pinloomcodelinkcoordinator.cpp:131` 为 Docked/Transient/Global；`mainwindowcontextworkspace.cpp:694` 为 Docked/Kept/Global。 |
| 6 | 拒绝五种组合 | `contextworkspacecontroller.cpp:313`；`verifyUnsupportedPlacements` 在已有 1 个浮层和 2 个侧栏资源时验证五种组合全部失败，原因含完整 placement 名称；创建/激活次数、资源、持久化状态及四类信号均不变。 |
| 7 | 分支计数 | 9 → 4；见下方统计方法和 `context-placement-static-verification.json`。 |
| 8 | 单一 rail 方法 | `contextworkspacecontroller.cpp:953`，activateRailProvider 共 41 行（含签名与括号）；优先复用已有实例，活动实例收起，否则按默认位置新建。 |
| 9 | 默认位置常量 | `contextworkspacecontroller.cpp:945`，defaultPlacementFor 返回 Docked/Transient/Global；只有说明后续记忆接入位置的注释，没有新字段或设置。 |
| 10 | 保留第 1 步 rail 断言 | `pinloom_context_provider_test.cpp:366` 起的三次点击和同一 view/搜索次数断言原样保留，测试通过；静态检查将旧值按规定替换后逐条比较。 |
| 11 | 全部调用点 | 见下方文件/行号清单；openResource 和 resourceOpened 签名同时更新。 |
| 12 | 状态结构不变 | contextworkspacestate.h、contextresource.h 和 workspacesessionstateservice.cpp 与第 2 步前快照逐字相同；kVersion=3。 |
| 13 | 双向跨构建验证 | 新构建导入旧状态：72/72 checks；保留的旧构建导入新状态：55/55 checks；`context-placement-cross-build.log`，详见下文。 |
| 14 | 五项独立性质 | verifyPlacementMapping、verifyUnsupportedPlacements、verifyRailThreeStates、verifyRailFocusExisting、verifyStateCompatibility 均通过。后者为第 1 步新增并原样保留。 |
| 15 | 指定回归 | 10/10，0 失败，20.58 秒；`context-placement-regression-ctest.log`，逐项见下表。 |
| 16 | 最终全量 | 106/107，1 失败，254.35 秒；`context-placement-final-ctest.log`。与 `context-placement-step2-baseline-ctest.log` 的 107 项结果逐项相同，失败仍仅为两条指定旧样本断言；比对结果 `context-placement-final-comparison.json`。 |
| 17 | 断言零放宽 | 第 2 步前的 55 条 Context、21 条临时编辑器、55 条 Pinloom 静态 check 表达式全部保留，仅做规定的类型值替换；包括仅跨构建运行的条件断言。脚本及 JSON 见下文。 |
| 18 | 缺失样本相关文件不变 | workspace_persistence_test.cpp 与快照逐字相同，两个 `.zs` 仍不存在；CMake 唯一变更是显式列入新头文件，旧测试注册及配置变量不变。 |
| 19 | 未实现后续功能 | diff 审核确认无新增配置/状态字段、窗口、绑定逻辑、多实例或堆叠容器；binding 只出现在类型、名称/相等及拒绝校验中。 |
| 20 | PeekHost 仍为子控件 | contextpeekhost.h/.cpp 与快照逐字相同；对这两个文件和新类型头搜索 Qt::Window、Qt::Tool、WA_TranslucentBackground、setWindowOpacity，0 匹配。 |
| 21 | 手工逐 provider rail | **未测**：当前环境没有可用的原生桌面操作接口，无截图。自动化覆盖 mock/Pinloom 的打开、收起、复用，并回归 liveInsights、RTL workbench；不等同于逐一点击所有真实 provider。 |
| 22 | 手工 pin 保留滚动/撤销 | **未测**，无截图。自动化验证 pin/unpin 移动同一 QWidget 和共享 QTextDocument，且已有共享编辑内容保留；未手工验证滚动值和撤销操作。 |
| 23 | 文档 / 版本 | VERSION、CHANGELOG、plan、goal、VERSIONING、package README 均同步 0.25.11；README 仅改版本标记以满足现有 version_documentation_guard，用户手册未改。 |

## 分支统计与等价性边界

统计条件决策点，而非字段出现次数：每个 if、switch 或三元表达式算 1 处，复合 if 内的短路项不重复计数。原函数为 7 个 if + 2 个三元；原行号 335、349、356、377、383、404、431、453、459。

当前四处分别为能力映射（三元，281/284）、组合拒绝（复合 if，313）、surface 分派（三元，333）、persistence 分派（switch，458）。整个控制器中放置字段共访问 6 次，未用派生布尔变量或查表隐藏判断。创建、激活、复用、通知分别由私有方法承接，能力模型本身未改。

`openResource` 先校验工作区、provider、支持组合和能力。新资源的三个落点不变；同一资源已有实例时保留原规则：浮层请求复用已有侧栏；临时侧栏请求复用已有 Peek；Kept 请求通过原 pinPeek 移动已有 Peek。既有侧栏资源重开时也保留原 transient 标记语义，本期不增加隐式转换规则。

验证脚本：`build/evidence/context-placement-static-check.ps1`。结果：`context-placement-static-verification.json`。它比对受保护源码、CMake 唯一允许的改动，并在只归一化规定枚举替换与空白后核对每条既有 check。完整构建日志：`context-placement-build.log`。

## 双向状态实测

第 1 步已保留 `context-placement-old-runtime/context_workspace_test.exe` 与旧核心 DLL；哈希记录为 `context-placement-old-runtime-hashes.json`。使用真实 WorkspaceSessionStateService 保存/读取 INI 内的现有 JSON，不新增序列化器或状态格式。

两次执行均以仓库根目录为工作目录，Qt offscreen；按以下顺序执行：

1. 新测试程序 `--state-import context-placement-old-state.ini context-placement-new-state.ini`，72/72 checks。
2. 旧测试程序 `--state-import context-placement-new-state.ini context-placement-old-roundtrip-state.ini`，55/55 checks。

输入/输出路径均在 build/evidence。程序逐字段核对两个固定资源、provider 状态、活动资源、Peek 宽/高、侧栏宽度、侧栏/rail 可见性及 valid；恢复后再 capture 亦逐字段相等，Context 状态版本保持 3。新旧核心库均从各自测试程序所在目录加载。

## 指定回归与调用点

| CTest | 结果 |
| --- | --- |
| context_workspace_test | 通过 |
| context_workspace_125pct_dpi_test | 通过 |
| context_workspace_150pct_dpi_test | 通过 |
| context_workspace_200pct_dpi_test | 通过 |
| temporary_editor_context_provider_test | 通过 |
| temporary_editor_context_search_test | 通过 |
| pinloom_context_provider_test | 通过 |
| live_insights_context_provider_test | 通过 |
| rtl_insight_workbench_test | 通过 |
| gui_smoke_test | 通过 |

旧调用点的替换位置：

- src/app/mainwindow.cpp:1362、4289。
- src/app/mainwindowcontextworkspace.cpp:162、690、694。
- src/integrations/pinloom/pinloomcodelinkcoordinator.cpp:130、131，三元选择保持原语义。
- src/ui/contextworkspacecontroller.h:48、67；cpp:535（pin 通知）、810（恢复）；rail 入口改为 119 → 953 → defaultPlacementFor。
- test_sv/context_workspace_test.cpp:532、650、682、718、730、733、743、819、925。
- test_sv/gui_smoke_test.cpp:14551。
- test_sv/pinloom_context_provider_test.cpp:758、772。
- test_sv/temporary_editor_context_provider_test.cpp:116、152。

## 双开调用观察

`mainwindowcontextworkspace.cpp:688` 的两次打开原样保留为 Floating/Transient/Global，再 Docked/Kept/Global。它不是无条件冗余：若资源已在 Peek，第二次调用会直接 pin 已有 view，不重新 activate；第一次调用负责应用本次资源状态。若资源本来在侧栏，则两次都会复用并激活同一 view。将其缩成一次需要另行调整激活契约，本期不改。

## 改动文件

| 文件 | 摘要 |
| --- | --- |
| src/ui/contextplacement.h | 新增 QtCore 三维值类型和名称函数。 |
| src/ui/contextworkspacecontroller.h | 替换公开参数/信号类型，声明 surface、复用和 rail 私有方法。 |
| src/ui/contextworkspacecontroller.cpp | 集中校验/能力映射，分离落点和持久性路径，统一 rail 切换。 |
| src/app/mainwindow.cpp | 按原语义替换两个浮层调用。 |
| src/app/mainwindowcontextworkspace.cpp | 替换三处调用，保留先预览再固定。 |
| src/integrations/pinloom/pinloomcodelinkcoordinator.cpp | 保留 link selection 与其他操作的放置区别。 |
| test_sv/context_workspace_test.cpp | 状态兼容驱动、四项新增放置/rail 测试及原调用点替换。 |
| test_sv/temporary_editor_context_provider_test.cpp | 修订过时标题断言并加强通知/身份/位置验证，清理定位输出。 |
| test_sv/pinloom_context_provider_test.cpp | 修订过时 rail 断言、加入实例复用和空指针保护。 |
| test_sv/gui_smoke_test.cpp | 更新原 Peek 调用的类型。 |
| CMakeLists.txt | 显式列入新头文件。 |
| VERSION | 更新产品版本至 0.25.11。 |
| CHANGELOG.md | 记录两步修复和重构。 |
| README.md | 只同步现有版本标记，不修改能力描述。 |
| plan.md | 记录第 1 期边界和后续三期方向。 |
| goal.md | 同步版本及行为等价目标。 |
| VERSIONING.md | 同步受控版本。 |
| packaging/ZeroSlack-PACKAGE-README.txt | 同步包内说明版本。 |
| docs/context-placement-review.md | 记录两步验收和限制。 |

## 差异与已知限制

- 第 2 步文件的基线描述与实测相符；第 1 步文件中的 8 处条件和漏列失败已在续文件中纠正。测试位置随新增用例偏移，以本报告实际行号为准。
- workspace_persistence_test 的两条 legacy fixture 断言因样本未纳入版本管理而无法评估，产品方已决定不提交样本。没有伪造兼容性结论，没有跳过测试。
- 本期不支持 Floating+Kept 或任何 DocumentBound；真浮层单实例、多实例与绑定、侧栏竖栈分别留待后续三期。
- 原生桌面手工验证未测；自动化覆盖的范围与限制见清单 21/22。
