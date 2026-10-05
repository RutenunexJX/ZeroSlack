# ZeroSlack 架构与冗余逻辑待办

- 记录日期：2026-10-04。
- 审查基线：ZeroSlack 0.31.20，提交 `9198d9a3116226a4640e11e8fc72651c665366d4`。
- 用户最新指示（2026-10-04）：解决这 11 项，goal 模式监督。
- 当前状态（2026-10-05）：**AR-01 至 AR-11 已完成开发及统筹完整验收。** 首轮验收的 4 处返修已闭环；本轮未提交、推送或替换正式包。
- 当前执行及验收任务：`build/coordination/20261004-zeroslack-eleven-architecture/TASK.md`。
- 证据边界：下方“发现与影响”“代码依据”保留 0.31.20 基线的静态审查记录；最终状态依据源码审查、GUI/独立 CLI 构建、相关回归、原生 Windows 停靠记录和前后耗时实测。
- 完整验收记录：`build/coordination/20261004-zeroslack-eleven-architecture/coordinator-review/ACCEPTANCE.md`；逐项证据及最终源码/二进制身份见同目录的 `acceptance.json` 和 `accepted-source-runtime-identity.json`。

初始观察：主语义流水线已经收敛，部分调用方仍重复计算、维护状态或实现规则；当时 CLI 在取得统一快照后仍额外分析关系。以下基线发现已按本轮验收结果处理，不代表当前实现仍存在这些问题。

## 待办总览

| 编号 | 待修改项 | 优先级 | 状态 |
| --- | --- | --- | --- |
| AR-01 | CLI 重复分析及独立缓存有效性判断 | 高 | 已完成，统筹验收通过 |
| AR-02 | 同一文档的多个编辑视图重复解析 | 高 | 已完成，统筹验收通过；性能边界见下文 |
| AR-03 | 图形刷新调度与实际计算归属分离 | 高 | 已完成，统筹验收通过 |
| AR-04 | 旧兼容机制收敛不彻底 | 中 | 已完成，统筹验收通过 |
| AR-05 | 相同动作的执行路由重复及主窗口职责过多 | 中 | 已完成，统筹验收通过 |
| AR-06 | 高风险编辑的一致性校验重复散落 | 中 | 已完成，统筹验收通过 |
| AR-07 | 模块依赖主要靠约定，构建边界较弱 | 中后 | 已完成，统筹验收通过 |
| AR-08 | 候选弹窗的基础交互重复 | 中 | 已完成，统筹验收通过 |
| AR-09 | 自动换行布局与 ElaFlowLayout 能力重叠 | 中 | 已完成，统筹验收通过 |
| AR-10 | 原生 Ela 动画之外遗留的旧动画分支 | 中 | 已完成，统筹验收通过 |
| AR-11 | 动画策略和开关归属分散 | 高 | 已完成，统筹验收通过 |

执行顺序由实际依赖决定：先明确语义、文档、查询与 UI 边界，收敛实际所有者，再完成构建边界和统一回归。11 项必须逐项交付，不能把未完成项静默移出本轮。

## 最终验收说明

- CLI 消费一次统一分析及其依赖证据；相同 QTextDocument 共享一个真实 TSDocument；模块图、状态图和目标验证由既有 LiveInsightSession 构建和复用报告。
- 旧队列、租约、重复语义存储及旧动画后备实现已清理；共同命令、编辑计划校验、候选交互和换行布局收敛到各自公共所有者。Documents、Semantic 与 UI 建立实际构建边界，独立 CLI 无 Widgets/Ela 递归依赖。
- 首轮完整验收发现的同步状态验证、模块深度丢失、性能测量边界及重复动画收尾均已闭环。普通模块图保持深度 2，专用面板保持完整深度；取消、过期结果和关闭/重开回归通过。
- 本轮受影响 CTest 25 项、独立 CLI 2 项、GUI 自包含 654 项检查通过；使用首轮未受影响回归及原生 Windows 交互证据。统筹另行核对最终源码/二进制，复跑图形/目标/DPI 4 项、CLI 2 项、两类外部 include 失效与递归依赖检查，并重算全部 24 个文档测量样本。
- 性能边界：在相同解析、高亮、折叠及呈现完成条件下，三视图最大事件间隔中位数在连续编辑中由 51.20 降至 25.51 ms，多行注释变更由 179.34 降至 60.57 ms。单视图事件响应处于基线附近，后台全部完成时间增加约 2.4%–2.6%；该差异明确保留，不声称所有耗时都下降，也不将离屏事件数据当成桌面帧率。
- 版权夹具和缺失的可选 SuiteApp SDK 沿用用户允许的覆盖边界；未伪造或补回。没有遗留未完成的本轮架构条目，已知性能差异和覆盖范围保存在验收报告中。

## AR-01：CLI 重复分析及独立缓存有效性判断

**发现与影响**

- CLI 先调用 `SymbolAnalyzer::analyzeProject` 取得统一快照，随后又创建 `SlangManager`，重新提取并计算关系，没有直接复用主流水线已经生成的关系。
- 第二轮关系提取接口没有传递工程的 `topModule`；单独读取输入也使输出一致性需要额外维护。
- CLI 的缓存版本主要依据工作区扫描文件和配置生成。仅修改工作区外的 include 文件时，这个版本可能保持不变，继续命中旧缓存。

**建议归属与收敛方向**

统一语义请求提供快照和输入依赖证据；CLI 负责序列化及机器接口。复用语义输入的有效性验证，避免另建编译与依赖判定规则。

**代码依据**

- `src/cli/zeroslackcli.cpp`：`inspectWorkspace`（323 行）、版本生成（366 行）、`buildSemanticIndex` 内首次分析（512 行）、额外关系分析（547 行）、`prepareIndex` 缓存判断（724 行）。
- `src/analysis/incrementalsemanticanalysisworker.cpp`：统一编译及关系构建已经使用同一份捕获输入。
- `src/semantic/slangmanagerrelationships.cpp`：`extractWorkspaceRelationshipInfo` 重新建立输入捕获与编译上下文。

## AR-02：同一文档的多个编辑视图重复解析

**发现与影响**

- 分屏共享 `QTextDocument` 和高亮器，但每个编辑器仍拥有独立 `TSDocument`、解析定时器及文本缓存。
- 每个视图监听共享文档的 `contentsChange`，分别处理文本增量和语法更新，增加重复工作与状态同步负担。
- 这是代码路径上确认的重复；不据此宣称耗时会严格按视图数量成倍增加。

**建议归属与收敛方向**

将文档语法基线、文本修订和增量事件归于文档。光标、折叠、选择、可见区域及临时交互投影继续归于视图；保留这些视图差异，避免直接把所有编辑器状态合并。

**代码依据**

- `src/editor/mycodeeditor.cpp`：每个编辑器创建 `MyCodeEditorState`（382 行）。
- `src/editor/editorsyntaxstate.cpp`：每个状态持有解析定时器（31 行）、创建 `TSDocument`（64 行）；高亮器另有按文档共享的登记机制。
- `src/editor/editorruntime.cpp`：`attachDocumentConnection`（1347 行）、本地文本缓存更新（1714 行）、`syntax.applyDocumentChange`（1747 行）。

## AR-03：图形刷新调度与实际计算归属分离

**发现与影响**

- `LiveInsightSession` 当前注册的后台构建器主要复制上下文、生成摘要。
- 模块图和状态图的实际报告构建仍由 Presenter 在界面调用链中同步执行。
- 会话状态通知进入视图刷新，Workbench 再做上下文去重；调度、去重和实际构建分别维护。外层异步状态不能代表实际图计算已经离开界面线程。

**建议归属与收敛方向**

由一个会话拥有真实报告的构建、取消、版本校验和复用，界面负责应用结果。先明确公共生命周期，不将此项自动扩展为热点图、果核图内部算法重构。

**代码依据**

- `src/app/mainwindowcontextworkspace.cpp`：`summaryBuilder`（146 行）、会话构建器注册（175 行）。
- `src/insights/liveinsightscontextview.cpp`：`refreshSnapshot`（838 行）进入 `renderSurface`。
- `src/insights/rtlinsightworkbench.cpp`：`setContext` 内去重（115 行）、`refresh` 内调用实际表面（144 行）。
- `src/insights/rtlinsightspresenter.cpp`：`showFsmGraph`、`showModuleBlockDiagram`（331 行）、`showStateTransitionGraphForSignal`。

## AR-04：旧兼容机制收敛不彻底

**发现与影响**

- 关系分析旧队列仍随调度器创建，普通产品流程已不再向其排队；部分旧接口仍供兼容调用或测试使用。
- `lastContentByFile` 仍维护内容，仓库内未找到其查询接口的调用者。
- `WorkerLease` 的计数、锁和等待机制仍存在，仓库内未找到获取租约的调用。
- `SemanticIndex` 仍保留旧可变存储与新快照之间的多套查询分支，增加维护和误用风险。

**建议归属与收敛方向**

核对实际兼容用途后收缩或隔离旧机制。保留有用途的只读快照投影与同步接口适配；不能仅因名称包含 legacy 或 compatibility 就删除，也不将旧代码的存在解释为正常运行时必然重复构建两份关系图。

**代码依据**

- `src/analysis/analysisschedulerwiring.cpp`：`setupRelationshipAnalysis`（5 行）。
- `src/semantic/relationshipanalysisqueue.cpp`：`lastContent`（15 行）、`rememberRequestedContent`（20 行）。
- `src/semantic/smartrelationshipbuilder.cpp`：租约状态（9 行）、`acquireWorkerLease`（66 行）。
- `src/semantic/semanticindexstore.cpp`：旧写入入口（555 行）、双模式查询（609 行）。
- `src/semantic/relationshipanalysisworker.cpp`：现有只读投影适配器，应与多余调度和状态区分。

## AR-05：相同动作的执行路由重复及主窗口职责过多

**发现与影响**

- 删除行、合并行、上下移动行等相同执行路由，分别存在于 `MainWindow` 与 `CommandLayerCoordinator`。
- 目标编辑器选择、可用性判断及失败处理需要多处维护，动作登记统一尚未带来执行规则统一。
- 主窗口仍承担较多恢复审阅、外部冲突处理和具体命令逻辑。

**建议归属与收敛方向**

相同动作由一个执行所有者处理，菜单与命令面板提交调用。保留不同入口确有必要的参数和交互差异。恢复、冲突等业务交给对应协调器，主窗口负责组装与顶层协调；仅拆分 `.cpp` 文件不能完成职责收敛。

**代码依据**

- `src/app/mainwindow.cpp`：`executeActionRoute`（3065 行），行操作分派（3365 行）。
- `src/commands/commandlayercoordinator.cpp`：`registerActionExecutionRoutes`（834 行），相同行操作绑定（986 行）。
- `src/commands/commandlayercommandregistry.cpp`：路由及 fallback 分派（189 行）。

## AR-06：高风险编辑的一致性校验重复散落

**发现与影响**

- 实例连线工作流和多信号传播面板分别实现 `sameSemanticObject`、`sameProvenance`、文本编辑及计划比较。
- 面板也承担预览与待执行计划是否一致的判断。底层事务虽已共享，这些共同约束尚未归到共同所有者。
- 计划字段增加或规则变化时，存在只更新其中一处的维护风险。

**建议归属与收敛方向**

在已有编辑事务边界提供统一的准备结果与一致性校验接口；UI 展示校验结果，工作流管理业务状态。收敛过程中保留现有版本、预览、来源和事务一致性约束。

**代码依据**

- `src/commands/instancepairconnectionworkflow.cpp`：`sameProvenance`（95 行）、`samePlan`（120 行）、提案验证（1139 行）。
- `src/commands/multisignalpropagationpanel.cpp`：`sameProvenance`（124 行）、`sameWorkspacePlan`（149 行）、`renderProposal` 中一致性判断（853 行）。
- `components/rtleditcore/include/rtledit/edit_plan.h` 与 `src/workspace/workspaceedittransactionservice.cpp`：已有共享计划和事务边界。

## AR-07：模块依赖主要靠约定，构建边界较弱

**发现与影响**

- 语义、编辑器、UI 和集成代码集中在 `zeroslack_core`；模块头文件路径全局开放。
- CLI 通过该目标依赖 Widgets/Ela，查询服务还大量通过全局实例绑定语义索引。
- 目录划分没有充分限制跨层取数据，容易让新增功能各自维护缓存和状态。

**建议归属与收敛方向**

先明确实际依赖方向与数据所有者，再逐步拆出可独立使用的语义查询、文档和界面边界。沿真实调用关系减少全局依赖，复用现有快照与上下文注入能力，避免为了拆分而增加无用途的抽象层。

**代码依据**

- `cmake/source_layout.cmake`：模块目录及全局 `include_directories`。
- `CMakeLists.txt`：`zeroslack_core`（747 行）、Ela 公共依赖（834 行）、CLI 依赖（875 行）、Widgets 等公共依赖（920 行）。
- `src/semantic/semanticruntimewiring.cpp`：`configureQueryServices`（30 行）。

## AR-08：候选弹窗的基础交互重复

全局控制面板、命令选择器分别实现上下选择、Enter 执行、Esc 关闭、结果列表和弹窗定位；临时编辑器搜索也存在可复用交互。提取真正相同的交互和定位能力，保留不同的数据模型、异步结果版本、焦点策略、分类切换及子窗口约束，不强制合并成一个搜索业务。

依据：`src/ui/globalcontrolpanel.cpp:205`、`src/commands/commandlayercoordinator.cpp:2029`、`src/ui/temporaryeditorsearchpopup.cpp:198`。

## AR-09：自动换行布局与 ElaFlowLayout 能力重叠

`CompactFlowLayout` 自行实现换行、尺寸和排布；仓库内已有 `ElaFlowLayout`。比较并保留隐藏控件、伸缩、输入框最小宽度、heightForWidth 和字体缩放等实际要求，将公共布局能力收敛到一个所有者。允许必要的 Ela 本地修正及薄适配，不能仅改名或把完整算法再复制一份。规则矩阵工具箱的 QGridLayout 不在强制替换范围，不默认增加 400 ms 重排动画。

依据：`src/ui/compactlayout.h:31`、`thirdparty/elawidgettools/ElaFlowLayout.h:11`、`thirdparty/elawidgettools/private/ElaFlowLayoutPrivate.cpp`。

## AR-10：原生 Ela 动画之外遗留的旧动画分支

当前正常路径已使用 ElaNavigationBar / ElaDrawerArea；左栏仍保留自写宽度动画，右栏和底栏保留 PanelCompositor 后备实现。按实际调用可达性清理或隔离旧后端机制，保留停靠、悬浮、实时伸缩等仍有用的职责。不能将旧代码存在误报为两套动画同时运行。

依据：`src/ui/navigationpanecoordinator.cpp:80`、`src/ui/contextworkspacecontroller.cpp:1949`、`src/ui/panellayoutcontroller.cpp:1054`。

## AR-11：动画策略和开关归属分散

左右侧栏读取应用级动画开关；底栏使用自己的 animationsEnabledValue；右栏内部折叠区直接使用调用参数。统一这些路径和公共页面切换的策略归属，初始化/恢复及调用者请求立即应用时仍应无动画；复用 Ela 的动画执行与中断收敛，不自建第二个动画引擎。

依据：`src/ui/contextworkspacecontroller.cpp:1915`、`src/ui/panellayoutcontroller.cpp:1060`、`src/ui/contextdockhost.cpp:528`、`src/ui/uicontrols.cpp:452`。

## 后续更新规则

本文件保存待办，不把静态推断记为实测结论。执行中按实际范围重新核对相关调用链和复用边界，再更新条目状态与交付依据。完成状态由统筹验收确定；源码行号以本文件记录的审查基线为准。
