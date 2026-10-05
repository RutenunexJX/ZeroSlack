# ZeroSlack 架构与冗余逻辑待办

- 记录日期：2026-10-04；最近更新：2026-10-05。
- 历史审查基线（AR-01 至 AR-11）：ZeroSlack 0.31.20，提交 `9198d9a3116226a4640e11e8fc72651c665366d4`。
- 新一轮审查基线（AR-12 起）：ZeroSlack 0.31.21，提交 `de7ce08b01d2f9e287f8643b204f87032074eb5a`。
- 用户最新指示（2026-10-05）：**执行所有待办，即 AR-12 至 AR-28 共 17 项。**
- 当前状态（2026-10-05）：**AR-01 至 AR-11 已随 0.31.21 发布；AR-12 至 AR-28 共 17 项全部完成，并通过统筹最终验收。** AR-13 首交的别名视图索引回归已返修闭环。用户确认全部完成后发布，统筹正在准备 0.31.22。
- 首次统筹核对与返修证据：`build/coordination/20261005-zeroslack-all-backlog/coordinator-review/REVIEW-01.md`、`REWORK-01.md`；原探针冻结基线通过、本轮首交失败。此问题并入 AR-13，不另增同根因待办。
- 本轮实施任务与验收条件：`build/coordination/20261005-zeroslack-all-backlog/TASK.md`。以下覆盖与发现保留授权前的静态审查基线；“未运行复现”等仅描述发现时的证据，实施结果另行记录，不提前标为解决。
- 规则：`C:/Users/14971/.codex/skills/appsuite-scoped-execution/SKILL.md` v1.3，已通知六个执行侧。规则通知不派发开发任务。
- 上轮执行及验收任务：`build/coordination/20261004-zeroslack-eleven-architecture/TASK.md`；发布记录：`build/validation/20261005-ela-architecture-release/RELEASE.md`。
- 证据边界：AR-01 至 AR-11 的“发现与影响”“代码依据”保留 0.31.20 基线的静态记录，其完成状态依据完整验收及发布记录。AR-12 起的初始发现依据 0.31.21 源码调用链，当时仅阅读测试替身与断言；授权后的实施已构建、回归及测量。“确定缺陷”表示源码存在完整可达的违约路径，初始静态记录不代表已经复现；实施后的实测结果与限制见本轮验收记录。
- 完整验收记录：`build/coordination/20261004-zeroslack-eleven-architecture/coordinator-review/ACCEPTANCE.md`；逐项证据及最终源码/二进制身份见同目录的 `acceptance.json` 和 `accepted-source-runtime-identity.json`。

历史观察：主语义流水线已经收敛，部分调用方仍重复计算、维护状态或实现规则；当时 CLI 在取得统一快照后仍额外分析关系。AR-01 至 AR-11 已按上轮验收结果处理，不代表当前实现仍存在这些问题。后续发现从 AR-12 起单独记录，不能据其“新发现”推断由最近修改引入。

## 固定清单覆盖记录（v1.3，全局遍历）

**范围与结论**

本轮先清点目录、构建目标、状态所有者，再沿入口、消费者、失败返回和恢复路径检查六个固定维度。源码仍为 `de7ce08`，因此沿用同基线已审路径并补读缺口；AR-01 至 AR-11 的运行证据只属于原验收范围。

目录清点包含 `src` 下 14 个一级目录、596 个文件，`include/zeroslack` 下 41 个文件，以及 `components/rtleditcore` 的 16 个源文件/头文件和 14 个 CMake 辅助文件。**清点数量不是逐行审阅数量，也不是正确率或覆盖率。** 本轮完成的是全局所有者、依赖和下表所列跨层路径的静态遍历，不宣称读完所有实现语句或穷尽所有输入组合。

### 模块与所有者

| 范围 | 主要所有者与数据 | 本轮状态及边界 |
| --- | --- | --- |
| 应用组装、构建边界、退出 | `app`、CMake；MainWindow 组装；Documents/Semantic/UI 分别构建 | 所列范围已审：入口、可选 SDK 分支、目标依赖、关闭前检查、析构先取消后等待；平台驱动和 SDK 内部范围外 |
| 工作区、扫描、导航、会话 | `workspace`、`navigation`；WorkspaceManager/ProjectModel；WorkspaceSessionCoordinator | 所列范围已审：激活/扫描代次、Design 后台派生、文件修改预检/重规划/提交/关视图/刷新、会话失败；外部修改恰好插入文件系统调用间隙仍待验证 |
| 文档、视图、语法 | `documents`、`editor`；DocumentBuffer/SharedDocument、TabManager、EditorSyntaxState | 所列范围已审：多视图、保存与 Save As、外部冲突、路径别名、逻辑文本/原始字节、真实修订恢复；所有编码及盘符失效组合未穷尽 |
| 语义分析、查询 | `analysis`、`semantic`；调度器、SymbolAnalyzer、SemanticIndex、EffectiveValueService | 所列范围已审：输入证据、工作区/文档切换、取消、统一发布、观察者重入、快照消费者；SystemVerilog 各语法语义正确性仍需专项 |
| 补全、悬停、模板 | `completion`、`editor`；编辑模式、候选/悬停所有者、UserTemplateService | 所列范围已审：内容变更/滚动关闭预览、销毁退订、声明确认重验证、模板面板查询；发现 AR-27；不把未发现产品调用的旧 Tab 入口当成当前输入路径 |
| 命令、搜索、跨文件事务 | `commands`、`insights`、rtleditcore；工作流与真实文档适配器 | 所列范围已审：公共预览确认边界、局部搜索捕获、多文件失败回滚、连续撤销及再操作；全部 RTL 计划器的算法输入组合未逐一证明 |
| 设置、工程配置 | `settings`；SettingsCenterService、设置草稿、WorkspaceConfigurationService | 所列范围已审：切换草稿、两层设置冲突重载、正常保存失败、损坏/未知版本工程配置、旧写入口；同时进入提交的多进程竞争仍待验证 |
| 停靠、悬浮、图形 | `ui`、`insights`；ContextWorkspaceController、PanelLayoutController、LiveInsightSession | 所列范围已审：复用/关闭/绑定、动画中断收尾、图请求代次和销毁、导出原子提交；多显示器/DPI/GPU 的运行表现未重测 |
| 原生组件、IPC、CLI | `integrations`、`cli`；NativeContextView、PinloomHostClient/CodeLinkStore、CLI 服务 | 所列范围已审：组件契约、xIPs 宿主落盘、Pinloom 查询/创建/链接存储、CLI 配置/缓存/错误出口/Git 文件名协议；外部应用内部和可选 SDK 范围外 |

热点图、果核图内部算法重构依用户安排列为范围外；不据此跳过其共享会话和文档边界。缺失 SuiteApp SDK、版权测试资料不补回。其他五个应用只接收规则通知，不进入本轮源码审查。

### 六个固定维度及证据

| 维度 | 已核对的具体路径/状态 | 当前结论与依据 | 尚未覆盖 |
| --- | --- | --- | --- |
| 职责与依赖 | GUI/CLI 域构建、启动、可选集成、关闭先取消后等待 | `cmake/DomainTargets.cmake:42`；`cmake/CliTargets.cmake:1`；`src/app/main.cpp:22`；`src/app/mainwindow.cpp:382`；`src/analysis/analysisscheduler.cpp:26`。未重开 AR-07 | 外部二进制 ABI、平台驱动；非主机平台的完整行为 |
| 数据归属与复用 | 文档字节/逻辑文本；历史修订；草稿和基线配对；模板面板捕获输入；遗留写入口 | AR-13/17/18/19/20/21/23/27；`src/app/mainwindow.cpp:1560`；`src/editor/editorinsertpaletteservice.cpp:408` | 所有语义查询/编码组合；旧公共 API 的仓库外调用者 |
| 状态转换 | 工作区与文档切换、文件重命名/删除、侧栏复用期间创建链接、配置加载失败 | AR-21/24/28；`src/navigation/navigationmanagerfileoperations.cpp:715`、`:761`、`:784`、`:912`；`src/workspace/workspacefileoperationservice.cpp:265` | 文件系统调用间隙的外部替换；目录子孙变化、硬链接、嵌套工作区交错 |
| 异步生命周期 | 语义/图形发布；Design 代次；悬停销毁和确认；Pinloom 只读与有副作用回调 | `src/semantic/symbolanalyzerpublication.cpp:193`、`:253`；`src/insights/liveinsightsession.cpp:221`、`:288`；`src/navigation/navigationmanagerdata.cpp:147`；`src/editor/editorsourcenavigation.cpp:449`、`:482`；AR-16/24 | Pinloom 已处理但响应丢失时的跨应用结果核对；多进程/网络盘调度实况 |
| 持久化 | 文档/配置/会话、Pinloom 链接加载与写入、xIPs 回执、图导出、CLI 缓存及协议 | AR-22/25/26/28；`src/integrations/xips/xipscontextprovider.cpp:73`；`src/insights/graphexportservice.cpp:477`；`src/cli/zeroslackcli.cpp:648`、`:723`、`:2342` | 掉电/磁盘故障实测；设置“检查版本→提交”间的并发写窗口 |
| 撤销与恢复 | 多文件应用/恢复中途失败、回滚后的历史、关闭重开导致的修订变化、恢复替身差异 | AR-18/19；`components/rtleditcore/src/rtledit/patch_engine.cpp:147`；`src/workspace/workspaceeditdocumentmanager.cpp:207`；`src/commands/scopedreplaceworkflow.cpp:448` | 回滚仍有残留时的交互恢复；跨工作区历史的产品策略；不把关闭重开的冲突自动定为新缺陷 |

### 已有保护、降级项与后续缺口

- 普通文件保存失败会保留未保存状态，尝试保留恢复快照，并由 DocumentReviewCoordinator 发出保存/恢复错误通知。不能将会话保存风险扩大为“源码保存失败也静默成功”。
- 正常工程配置对话框走 `setWorkspaceConfiguration → applyWorkspaceConfiguration`，先检查保存结果再更新模型，并在失败时提示。另一个旧接口的错误处理不能代表这个正常入口失效。
- 语义发布核对 generation、workspace epoch、项目身份及快照版本；事实与索引的静默提交处于共同保护内，通知后再次检查是否仍为当前结果。图形回调有取消、代次和目标销毁保护。这些已核对分支没有新的违约证据，不因“需要更全面”而重开旧项。
- `setIgnoredDirectories` 漏查保存结果，但在仓库产品源码中仅有声明和定义，没有发现调用；降为 AR-23 的冗余接口优化，不能作为用户可达故障报告。删除前仍须确认公共 ABI 兼容范围。
- 本轮沿源码补查了上述首轮缺口。多文件回滚后的修订基线并入 AR-19；两层设置的冲突重载并入 AR-21；Pinloom 副作用和存储、CLI 文件名协议分别记录 AR-24/25/26。继续追同一根因不重复计数。

### 本轮补查的保护与排除依据

- 文件树修改先检查未保存/锁定文档，保存后重新规划，再由 `WorkspaceFileOperationService::apply` 重建计划核对源版本；成功后关闭受影响视图并刷新文件集。关闭视图是现有明确流程，不能误报为漏做编辑器路径迁移。物理别名和未创建路径由 `EditorFileIdentity::lookupKey` 解析，外部同步保留身份相同但显示路径改变时的指纹（`src/editor/editorfileidentity.cpp:96`；`src/documents/externaldocumentsynccontroller.cpp:43`）。
- Design 派生使用一个私有线程池任务和一个待处理请求，完成时核对工作区、代次、快照、top 与文件集合；隐藏会失效旧请求，析构断开 watcher 后等待。它不等同于 AR-16 中每次输入都追加任务的 Pinloom 查询（`src/navigation/navigationmanager.cpp:35`、`:40`、`:50`；`src/navigation/navigationmanagerdata.cpp:147`）。
- 悬停遇内容变更/滚动会关闭；编辑器退出时解除候选与语法连接；声明提交再次检查文档、物理文件身份、修订及语义快照。普通同步候选不能凭名称就当作异步任务缺少取消（`src/editor/editorsourcenavigation.cpp:449`、`:482`；`src/editor/editorruntime.cpp:1174`、`:3920`、`:4077`）。
- 图导出先在内存编码、限制尺寸，源由 QPointer 守护，落盘检查完整写入与 QSaveFile 提交。停靠转换在关闭/缩放/DPI/输入中断时收尾，原生合成不可用时有现存后备路径。本轮没有发现足以重开 AR-09/10/11 的新证据（`src/insights/graphexportservice.cpp:170`、`:477`、`:525`；`src/ui/contextdocktransition.cpp:245`、`:257`、`:303`）。
- xIPs 宿主写回先取得 QLockFile，拒绝覆盖损坏引用文件，再原子保存；这与 Pinloom 链接存储的 AR-25 不同，不将两种实现混报为一个普遍故障（`src/integrations/xips/xipscontextprovider.cpp:73`）。
- Pinloom IPC 连接重试发生在发送请求之前，发送后没有自动重复创建。协议名、requestId、响应大小均有检查。因此本轮不报告“自动重试必然重复创建”；请求已被远端执行但响应超时，属于尚需跨应用核对的结果不确定场景（`src/integrations/pinloom/pinloomhostclient.cpp:69`）。
- CLI 的损坏缓存按未命中处理；缺失/过期且禁止刷新、缓存写失败、未知命令、查询错误有显式退出结果。AR-26/28 是这些既有保护之外的具体输入路径，不重新报告已关闭的 CLI 重复语义分析问题。
- 阅读了真实适配器及现有测试替身：Pinloom 创建测试同步返回，关闭测试延迟的是查询；设置冲突测试直接覆盖单层 Apply，均不能证明本轮发现的交错场景正确。本轮未执行这些测试。

**上述全局模块边界遍历已完成；上表未审组合仍明确保留。** 不把目录清点或静态推导称为完整运行验收，不用新增条目数量衡量审查充分性。该静态审查阶段未实施条目，也未运行产品测试、性能测量或故障注入；其后用户已授权全部 17 项实施与必要验证。

## 结论分级

优先级与结论类别独立：高优先级的性能优化也不自动等于功能故障。以下类别均与验证方式分开记录。

| 类别 | 条目 | 当前证据 |
| --- | --- | --- |
| 确定缺陷 | AR-18、AR-19、AR-21、AR-24、AR-25、AR-26、AR-28 | 文本/恢复/草稿契约、异步链接归属、链接存储保护、CLI 文件名协议和配置失败传播存在完整源码路径；未运行复现 |
| 待验证风险 | AR-22 | 自动会话保存失败传播缺口由源码确认；实际存储失败后的重开影响及所需切换策略待验证 |
| 可选优化 | AR-12、AR-13、AR-14、AR-15、AR-16、AR-17、AR-20、AR-23、AR-27 | 重复工作、同步路径、任务资源或无产品调用入口由源码确认；不声称已实测卡顿或改善收益 |

当前 AR-12 至 AR-28 共 17 项：**7 项确定缺陷、1 项待验证风险、9 项可选优化**。本轮新增的是 AR-24 至 AR-28（4 项缺陷、1 项优化），并扩充既有根因；没有把新增场景再次计数，也没有将静态确认写成运行复现。

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
| AR-12 | 恢复快照仍在界面线程同步持久化 | 高 | 已完成，统筹验收通过 |
| AR-13 | 编辑变更通过全量视图扫描及物理路径查询定位关联视图 | 高 | 已完成，统筹验收通过；别名返修已闭环 |
| AR-14 | 临时编辑器搜索仍同步全量查询和创建结果项 | 高 | 已完成，统筹验收通过 |
| AR-15 | 格式化各阶段缺少可复用的语法上下文 | 中 | 已完成，统筹验收通过 |
| AR-16 | 部分后台任务的取消、排队上限及资源隔离不一致 | 中 | 已完成，统筹验收通过 |
| AR-17 | 文件载入、保存基线和外部同步重复读取同一文件 | 中 | 已完成，统筹验收通过 |
| AR-18 | 文档快照的文本契约在打开前后不一致 | 高 | 已完成，统筹验收通过 |
| AR-19 | 连续撤销的历史基线与真实文档修订规则不一致 | 高 | 已完成，统筹验收通过 |
| AR-20 | 搜索与替换的输入捕获未按范围和文件复用 | 高 | 已完成，统筹验收通过 |
| AR-21 | 设置草稿生命周期与工作区激活、生效状态耦合 | 高 | 已完成，统筹验收通过 |
| AR-22 | 自动会话保存失败缺少调用方处置与可见结果 | 中 | 已完成，统筹验收通过 |
| AR-23 | 无产品调用点的忽略目录接口重复维护配置提交规则 | 低 | 已完成，统筹验收通过 |
| AR-24 | Pinloom 异步创建回调使用可变的当前链接来源 | 高 | 已完成，统筹验收通过 |
| AR-25 | Pinloom 链接存储的加载失败与写入约束未闭环 | 高 | 已完成，统筹验收通过 |
| AR-26 | CLI 按文本行解析 Git 文件名，遗漏被转义路径的关联 | 中 | 已完成，统筹验收通过 |
| AR-27 | 模板面板筛选重复解析不变文档并读取模板文件 | 中 | 已完成，统筹验收通过 |
| AR-28 | 工程配置加载失败被当作有效默认配置继续使用 | 高 | 已完成，统筹验收通过 |

AR-01 至 AR-11 的执行范围已闭环。AR-12 至 AR-28 也已按本轮 TASK 全部闭环；后续原始用户回复“全部 17 项完成后发布”已核实，由统筹发布。

继续审查新增的 AR-18、AR-19 涉及文本与撤销正确性，建议执行时优先确认；AR-20 涉及输入准备的重复开销，AR-21 涉及设置草稿保留。AR-18 可与 AR-17 共用文档载入边界设计，但两者分别处理文本契约和重复 I/O，不合并成单纯的缓存优化。

## AR-12 至 AR-28 最终验收说明

- 完整实现及逐项证据：`build/coordination/20261005-zeroslack-all-backlog/DELIVERY.md`；统筹最终记录：同目录 `coordinator-review/ACCEPTANCE.md` 与 `acceptance.json`。
- 原探针确认首次交付的别名关闭索引残留；返修后独立复跑通过，9 项受影响回归通过。文档身份在登记后固定，路径查询仍解析当前目标；更名、代表视图、最后释放和重绑定后的另存为一致。
- 最终 AR-13 十个交替样本的同步工作中位数 1917.316→5.070 ms（60 视图、100 次编辑及刷新）。其余 12 场景保留首次交付的原始运行身份和已复核数据，不改称最终发布包的计时，也不外推桌面帧率。
- 最新注册集观察为 199/218 通过；12 个已有失败目标、7 个缺失排除夹具目标仍是限制。公共 GUI 冒烟 654 checks / 0 failed，独立 CLI 2/2 通过。自动会话保存持续失败时，退出最后尝试后仍允许退出，不能保证未落盘会话保留；源文件保存走独立保护流程。
- 已验收实施源码集合：`f559b9231188acd9c8ef01c1968f53b6c63e17718f442d3cf52c75ad116a0237`。发布版本资料重建另记身份；原始发现和覆盖缺口继续作为历史/范围边界保存，不据此宣称全仓没有问题。

## AR-01 至 AR-11 最终验收说明

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

## AR-12：恢复快照仍在界面线程同步持久化

**发现与影响**

文档文本修订、脏状态通知会直接进入 `TabManager::writeRecoverySnapshot`，普通更新每累计 16 个修订触发一次；切换标签时，对上一个未保存文档强制写入，强制路径会绕过相同修订的去重。服务同步完成全文提取、UTF-8 转换、摘要、JSON 和 `QSaveFile::commit`，恢复持久化的耗时仍进入界面调用链。这里只确认同步路径，不断言已造成某种频率或时长的卡顿。

**建议归属与收敛方向**

由现有文档与恢复服务管理不可变修订快照、相同修订去重和有界合并写入。Qt 文本文档的读取应遵守线程归属；后台持久化须保持保存、关闭、删除和恢复记录清理的顺序，避免迟到写入重新生成已清理记录。

**代码依据（0.31.21）**

`src/documents/tabmanager.h:51`；`src/documents/tabmanager.cpp:431`、`:2703`、`:3009`；`src/documents/crashrecoveryservice.cpp:588`。

## AR-13：编辑变更通过全量视图扫描及物理路径查询定位关联视图

**发现与影响**

`documentChangeApplied` 进入 `DocumentSessionState::applyChange` 后，通过 `DocumentRegistry::editorsForDocumentId` 扫描全部登记视图，并对候选文档调用 `EditorFileIdentity::lookupKey`。该键生成会解析物理路径，在 Windows 上包含文件句柄查询。已有修订去重能跳过同一变更的重复通知，但首次处理仍扫描所有视图；光标单独移动使用另一条轻量路径，不属于此问题。

**建议归属与收敛方向**

复用 `SharedDocument` 已有的视图归属，或由现有登记器维护文档到视图的反向关联；在打开、关闭及身份变更时更新。保留别名、符号链接与路径变更语义，避免在每次内容编辑中重复做物理路径探测。

**代码依据（0.31.21）**

`src/documents/documentmodelconnections.cpp:21`；`src/documents/documentsessionversions.cpp:44`；`src/documents/documentregistry.cpp:200`；`src/editor/editorfileidentity.cpp:28`、`:153`。

## AR-14：临时编辑器搜索仍同步全量查询和创建结果项

**发现与影响**

临时编辑器搜索框的 `textChanged` 直接调用查询：遍历候选、模糊评分、合并、全量排序后，弹窗清空并为全部结果建立 `QListWidgetItem`。弹窗高度限制不会限制建立的结果项数量。目录构建已经异步且有界，本项指查询和结果呈现阶段，不重复 AR-08 已完成的公共候选交互收敛。

**建议归属与收敛方向**

继续复用现有不可变候选目录与搜索所有者，让查询具备合并和取消能力；结果采用模型或逐步呈现，避免每次输入都同步创建全部控件项。保留完整结果的可达性，不用静默截断掩盖成本。

**代码依据（0.31.21）**

`src/ui/temporaryeditorcontextview.cpp:84`、`:204`；`src/app/mainwindowcontextworkspace.cpp:131`；`src/ui/temporaryeditorsearchprovider.cpp:416`；`src/ui/temporaryeditorsearchpopup.cpp:89`。

## AR-15：格式化各阶段缺少可复用的语法上下文

**发现与影响**

格式化命令在界面线程完成全文处理。结构缩进、语法错误范围、赋值对齐、保守范围与不可变 token 校验分别建立语法文档；部分阶段消费相同输入，部分消费变换后的文本，却没有按文本版本共享树、行和 token 的任务上下文。此处不凭静态代码指定固定解析次数或耗时。

**建议归属与收敛方向**

在格式化服务内建立单次任务上下文，按实际文本版本复用解析结果；对已经改变的文本必须重新解析，保留变更前后语义保护。重计算如移到后台，应用结果前必须核对文档修订。

**代码依据（0.31.21）**

`src/editor/editorruntimecommands.cpp:886`；`src/editor/formatterservice.cpp:1829`、`:2079`；`src/editor/structuredwhitespaceformatter.cpp:468`、`:3073`、`:3186`、`:3260`、`:3278`。

## AR-16：部分后台任务的取消、排队上限及资源隔离不一致

**发现与影响**

Ghost annotation 的旧任务只在服务调用前后查看取消标记，内部全文计算没有取消接口。Pinloom 搜索每次输入均发起一个默认线程池任务；代次校验只丢弃旧结果，不停止旧任务的连接、重试和等待。两类工作使用默认线程池，存在无效工作与交互计算争用资源的可能；未实测争用影响。模块图与状态图当前已有内部取消，本项不将其误报为旧问题未解决。

**建议归属与收敛方向**

沿既有任务所有者统一“运行中任务＋可替换待处理请求”的约束与内部取消传播，区分阻塞 I/O 和交互计算。搜索等只读请求可合并；打开、创建等有副作用操作须保留独立执行语义，不统一粗暴取消，也不另建庞大的第二套调度框架。

**代码依据（0.31.21）**

`src/editor/editorruntimeannotations.cpp:712`；`src/editor/ghostannotationservice.h`、`src/editor/ghostannotationservice.cpp:553`；`src/integrations/pinloom/pinloomcontextview.cpp:694`、`:731`、`:812`；`src/integrations/pinloom/pinloomhostclient.cpp:69`、`:683`。

## AR-17：文件载入、保存基线和外部同步重复读取同一文件

**发现与影响**

首次打开文件时，TabFileIo 读取全文；DocumentBuffer 为保存基线再次读取原始字节；外部同步登记读取磁盘快照，随后又主动处理一次文件变化并再次读取。文件加载结果没有跨这些所有者传递，文本、摘要与状态重复取得。外部同步仍有指纹比较及覆盖前冲突保护，不能仅凭多次读取推断静默覆盖或数据丢失。

**建议归属与收敛方向**

由加载边界产生包含原始字节、解码文本、对应摘要及文件状态的不可变结果，文档基线与同步登记复用它；明确哪些外部变化重验证仍有必要。保持原始字节指纹与逻辑文本指纹的区别，不能直接互换。

**代码依据（0.31.21）**

`src/documents/tabmanager.cpp:2413`；`src/documents/tabfileio.cpp:56`；`src/documents/documentbuffer.cpp:10`、`:95`、`:180`；`src/documents/externaldocumentsynccontroller.cpp:43`、`:462`、`:605`。

## AR-18：文档快照的文本契约在打开前后不一致

**发现与影响**

- `WorkspaceEditDocumentManager::snapshot` 对已打开文件返回编辑器的逻辑文本，对未打开文件返回未经解码和换行规范化的磁盘字节，二者都被放进同一个 `WorkspaceDocumentSnapshot::text`。
- 批量应用和恢复为取得编辑器会调用 `openFileInTab`；正常文件打开通过 `QFile::Text` 与 `QTextStream`，文本表示在事务途中发生变化。以 Windows CRLF 文件为例，预览和历史的旧快照可以包含 CRLF，而编辑器文本使用 LF。
- `restoreSnapshot` 将历史文本经 `QTextCursor` 写回后，与原历史字符串逐字比较。保留 CRLF 的旧快照与规范化后的编辑器文本不一致，形成撤销/回滚被判为失败的代码路径。此处是静态推导，未运行复现，不据此声称已经丢失文件内容。

**建议归属与收敛方向**

在文档边界统一解码、逻辑换行与编辑坐标契约，区分可编辑逻辑文本、原始磁盘字节及编码/换行元数据。事务应消费一致的文档快照；打开视图不应改变其含义。复用已有 `DocumentBuffer` 和文档登记器，进一步解除批量编辑对可见标签创建的依赖。不能通过删除比较来掩盖内容差异。

**代码依据（0.31.21）**

`src/workspace/workspaceeditdocumentmanager.cpp:85`、`:133`、`:227`；`src/documents/tabfileio.cpp:56`；`src/app/mainwindow.cpp:1418`；`components/rtleditcore/src/rtledit/workspace_edit_transaction.cpp:390`、`:433`。Qt 文档逻辑换行的现有断言见 `test_sv/editor_line_operation_test.cpp:454`，本轮仅阅读。

## AR-19：连续撤销的历史基线与真实文档修订规则不一致

**发现与影响**

- 事务历史通过“版本和文本均相等”判断是否可以撤销。一次撤销成功后，只更新移到 redo 栈的当前条目，没有更新 undo 栈中更早条目对相同文档的预期版本。
- 真实 `WorkspaceEditDocumentManager::restoreSnapshot` 使用文本编辑恢复内容，不恢复旧修订号；共享语法文档因此产生新修订。连续两次操作同一文件，撤销第二次后，文本虽回到第一次操作的结果，第一次历史条目仍持有先前版本，再撤销会进入版本冲突分支。
- Scoped Replace 使用独立事务服务，撤销一次后仍根据 `canUndo()` 保持继续撤销入口，因此该差异存在产品可达路径。核心测试替身和 scoped-replace 替身恢复时直接将版本赋回旧值，与实际编辑器规则不同；这些测试不能单独证明真实多级撤销正确。
- 全局遍历补充：多文件恢复中途失败时，`restoreAtomically` 会把已恢复文件再写回操作前内容，但失败返回不更新保留在历史栈中的预期版本。即使文本回滚成功，真实修订也可能已前进，再次尝试仍会冲突；这是同一基线衔接根因，不新增条目。应用失败的 `PatchEngine` 也会恢复当前文件和此前成功文件，应纳入同一恢复结果契约。
- 关闭再打开、普通保存改变磁盘指纹、用户编辑和外部改写也可能产生版本不匹配；其中有些是必要保护。源码尚不足以判定这些情况下都应自动继续撤销，不能一律按缺陷移除检查。跨工作区历史是否保留，需要在实施时明确产品策略。

**建议归属与收敛方向**

由既有事务历史所有者定义恢复结果和后续历史基线的衔接规则，保留文档修订的单调性。仅为已验证由本次历史操作恢复的状态更新相关基线，仍拒绝用户编辑或外部磁盘变化；不能粗暴移除版本检查。后续实施时使测试替身遵循真实恢复语义，并覆盖同一文件的连续事务及失败回滚后的历史一致性。

**代码依据（0.31.21）**

`components/rtleditcore/src/rtledit/workspace_edit_transaction.cpp:10`、`:173`、`:195`、`:288`、`:334`；`src/workspace/workspaceeditdocumentmanager.cpp:207`；`src/editor/editorsyntaxstate.cpp:113`；`src/commands/scopedreplaceworkflow.cpp:448`；`src/commands/scopedsearchpanel.cpp:1081`；`components/rtleditcore/tests/support/mock_workspace_document_manager.cpp:81`；`test_sv/scoped_replace_workflow_test.cpp:151`。

补充依据：`components/rtleditcore/src/rtledit/workspace_edit_transaction.cpp:306`、`:327`；`components/rtleditcore/src/rtledit/patch_engine.cpp:147`；`src/workspace/workspaceedittransactionservice.cpp:53`、`:94`。本轮只阅读失败/残留返回与历史保留分支，未做故障注入。

## AR-20：搜索与替换的输入捕获未按范围和文件复用

**发现与影响**

- Scoped Search 的上下文提供器没有范围参数。每次搜索先调用 `MainWindow::scopedSearchContext`，遍历工作区文件、提取已打开文档全文，并同步读取全部尚未打开的工程源文件，然后才由查询服务处理当前文件、模块、语法块或工作区范围。局部搜索仍承担整个工程的输入准备成本。
- 构建预览和确认替换也重新调用该提供器。另在 `ScopedReplaceWorkflow::preparePreview` 内，每个编辑命中都调用 `documentManager->snapshot`；`capturedIdentities` 仅防止重复保存最终记录，没有跳过重复取快照。实际快照会读磁盘计算摘要，未打开文件还会再读一次全文，同一文件的很多匹配会重复做整文件 I/O。
- 搜索按钮/Enter 才触发这条路径，不能误报为每次搜索框输入都读全工程。本项也区别于 AR-14 的临时编辑器候选搜索，且不凭代码推断具体卡顿时长。

**建议归属与收敛方向**

让输入捕获接收明确的范围与文件集合，当前文件或局部范围只取得必要文档；工作区范围按不可变输入批次组织读取。单次预览按文档身份捕获一次内容和版本，确认应用时重新验证必要文件。复用现有文档与工作区快照边界，保持预览到提交之间的独立重验证，避免跨阶段缓存造成陈旧输入。

**代码依据（0.31.21）**

`src/app/mainwindow.cpp:1271`、`:1295`、`:1409`；`src/commands/scopedsearchpanel.cpp:251`、`:306`、`:371`、`:605`、`:671`；`src/commands/scopedreplaceworkflow.cpp:180`、`:213`、`:228`；`src/workspace/workspaceeditdocumentmanager.cpp:94`、`:110`。

## AR-21：设置草稿生命周期与工作区激活、生效状态耦合

**发现与影响**

- 切换工作区的 `workspaceActivated` 信号调用 `refreshSettingsCenterWorkspace`，随后 `setWorkspaceRoot/reload` 直接覆盖 `globalDraft` 和 `workspaceDraft`。比如修改全局字体大小但未点 Apply，再切换工作区，全局草稿也会丢失；自动切换入口没有使用已有的 `isScopeDirty` 判断。
- 正常顶栏切换在收到激活信号后，`MainWindow::activateWorkspace` 的成功分支又调用一次刷新；同一工作区会再 `reload` 并应用设置。自动更新、用户主动重新加载和丢弃草稿共用同一操作，既产生重复工作，也没有清楚表达草稿保留策略。
- 持久化已有 SettingsCenterService，运行时外观也已有不写回全局设置的适配器；不将本项误报为旧的持久化归属问题再次发生。
- 全局遍历补充一条确定缺陷：立即生效的全局字段遇冲突后，`applyImmediateField` 重新加载两层设置，只对 `globalDraft` 做增量重基，却把整个 `loadedSnapshot` 换成新快照，留下旧的 `workspaceDraft`。这破坏了“草稿与读取它时的版本成对保存”的约束，因此优先级上调为高。
- 可达顺序：A 面板加载 G0/W0；另一个实例先更新工作区为 W1、再更新全局为 G1；A 改主题或悬浮透明度，触发全局冲突重载。此时 A 持有新工作区版本 W1，却仍持有 W0 草稿。切到 Workspace 不重新加载草稿，点击 Apply 会使用 W1 的版本号保存 W0，冲突检查通过，从而覆盖对方的工作区修改。即使 A 没改过工作区，旧草稿与新基线之差也会使 Apply 可用。此顺序由源码推导，未实测多实例。

**建议归属与收敛方向**

保留全局草稿的独立生命周期，为每个工作区明确草稿保存或切换处置规则。每层草稿、基线值和版本必须成对更新：重载一层时不能只推进另一层版本；需要重载两层时分别重基本地差异。区分“更新当前有效设置”“切换工作区草稿”“用户明确重新加载/撤销修改”，由单一激活通知驱动运行时生效，避免重复调用。优先保留草稿，不为每次普通工作区切换新增不必要的确认弹窗。

**代码依据（0.31.21）**

`src/app/mainwindow.cpp:587`、`:2255`、`:4446`；`src/settings/settingscenterpanel.cpp:145`、`:206`、`:220`；`src/workspace/workspacemanager.cpp:672`、`:1268`。

补充依据：`src/settings/settingscenterpanel.cpp:247`、`:289`、`:1023`、`:1077`、`:1101`、`:1136`；`src/settings/settingscenterschema.cpp:60`、`:83`；`src/settings/settingscenterservice.cpp:375`、`:394`；`src/app/mainwindow.cpp:4311`。`test_sv/settings_center_panel_test.cpp:392` 的现有冲突测试走单层直接 Apply，未经过全局立即字段重载两层的顺序，本轮未运行。

## AR-22：自动会话保存失败缺少调用方处置与可见结果

**类别：待验证风险；验证方式：源码推导。**

`WorkspaceSessionStateService::save` 返回包含原因的保存结果；自动路径 `saveSession(false)` 不展示失败消息，只发出 `sessionSaveFinished(root, false)`。产品源码未发现该信号的消费者。`saveBeforeWorkspaceTransition` 返回的 bool 在打开、切换、关闭工作区和主窗口退出时没有被调用方处理；自动定时保存也没有保留失败后的重试状态。

条件为本地会话存储写入失败：当前内存 UI 在切换时还有 `liveWorkspaceUi` 保留，因此不能断言切换立即丢失全部布局；但关闭工作区会删除该内存记录，进程退出也不会保留它，下次启动可能恢复旧会话。源码文件保存另有未保存文档处理及错误通知，不属于此风险。`false` 还可能表示用户已清理会话而主动抑制保存，不能简单改成所有 false 都阻止退出。

建议在现有会话协调器区分“跳过、成功、失败”及失败原因，明确失败如何通知、是否保留待保存状态，以及允许继续切换/退出的策略；复用现有通知服务与保存调度，不增加默认弹窗或无限重试。实施前需验证实际存储故障与再次打开的表现。

依据：`src/workspace/workspacesessionstateservice.cpp:815`；`src/workspace/workspacesessioncoordinator.cpp:142`、`:385`、`:394`、`:427`、`:488`；`src/app/mainwindow.cpp:4537`。`sessionSaveFinished` 在产品源码中仅有声明及发送点；测试中的 QSignalSpy 不属于产品错误处理。

## AR-23：无产品调用点的忽略目录接口重复维护配置提交规则

**类别：可选优化；验证方式：源码及调用点检索。**

`WorkspaceManager::setIgnoredDirectories` 独立更新模型、文件列表、工作区条目与 watcher，再保存工程配置且忽略保存返回值；同一类修改的正常入口 `setWorkspaceConfiguration → applyWorkspaceConfiguration` 则先检查保存成功再发布模型变化。两条路径重复维护持久化与状态更新规则。

本轮对 `src` 的调用点检索只找到旧接口声明和定义，没有产品调用者；正常工程配置对话框走受保护的新入口。因此记录为接口收敛机会，不报告为“忽略目录保存失败仍向用户显示成功”的已确认运行故障。确认公共 ABI/外部调用兼容范围后，可移除旧入口或令其薄委托给现有配置提交路径，避免再实现一套规则。

依据：`src/workspace/workspacemanager.h:78`；`src/workspace/workspacemanager.cpp:544`、`:590`、`:1134`；`src/app/mainwindow.cpp:3654`；`src/settings/workspaceconfigurationservice.cpp:594`。

全局遍历的同类收敛候选：`UserTemplateService::setRecords/addOrUpdateRecord/removeRecord` 及自定义缩写的同名入口，产品目录检索只见声明、实现和内部互调，调用者在测试中；实际模板编辑入口是打开 JSON 文档。模板服务的旧 writer 使用 `QFile::Truncate` 且不检查 `write` 返回值，不能因为发现危险写法就报告成当前用户可达的数据丢失。`EditorCompletionWorkflow::handleInlineAbbreviationTab` 同样只发现定义/声明，不能据其函数名宣称当前每次 Tab 都会读取模板文件。删除或收敛这些接口前确认外部 ABI 和兼容用途，不因此重新实现整套模板编辑功能。

同类依据：`src/completion/usertemplateservice.cpp:610`、`:759`、`:780`、`:801`；`src/completion/customabbreviationservice.cpp:284`；`src/editor/editorcompletionworkflow.cpp:534`；`src/app/mainwindow.cpp:3533`。本项总览名称仍保留原忽略目录入口，以上作为同一类遗留入口清点补充，不重复计数。

## AR-24：Pinloom 异步创建回调使用可变的当前链接来源

**类别：确定缺陷；优先级：高；验证方式：完整源码调用链，未运行复现。**

**触发与影响**

1. 在代码选择 A 上打开链接面板并点击 Create Anchor。`createSourceAnchor` 将 A 发给 Pinloom，但完成 lambda 只捕获 `QPointer self`。
2. 请求未完成时，对同一工作区的代码选择 B 再次执行链接动作。资源仍为 `pinloom:library`；控制器复用原视图，`activateView → restoreState → setLinkSource` 将 `activeLinkSource` 改为 B。此路径不需要销毁视图，禁用 Create 按钮也没有阻止重新激活。
3. A 的请求返回后，回调调用 `finishLink(entry)`，后者读取当前 `activeLinkSource` 即 B。存储只校验 B 属于当前工作区及来源结构有效，没有“这个 entry 是为 A 创建”的请求关联信息，因而把 A 的锚点写到 B 的本地绑定。

QPointer 解决对象存活，不解决同一对象承载新操作后的结果归属。查询已有 search/resolve generation 不能覆盖创建链。这里不声称远端自动重试重复创建，也不将其并入 AR-16 的性能任务排队问题。

**建议归属与收敛方向**

由 Pinloom 链接操作所有者持有一次操作的不可变来源、工作区、请求标识和结果。落盘使用原操作来源，UI 更新另核对当前呈现代次；切换/关闭后要保留明确的完成或未关联结果，不能简单丢掉已执行的远端副作用。复用现有协调器和存储，不让可变 UI 字段充当事务身份。

**代码依据**

`src/integrations/pinloom/pinloomcontextview.cpp:420`、`:506`、`:1045`、`:1077`；`src/integrations/pinloom/pinloomcontextprovider.cpp:31`、`:159`；`src/integrations/pinloom/pinloomcodelinkcoordinator.cpp:76`、`:126`；`src/ui/contextresource.cpp:14`；`src/ui/contextworkspacecontroller.cpp:718`、`:800`；`src/integrations/pinloom/pinloomcodelinkstore.cpp:903`。

现有测试 `test_sv/pinloom_context_provider_test.cpp:316` 对创建立即返回，`:851` 延迟的是查询销毁场景。未来验证需覆盖 A 创建→B 重新激活→A 返回，以及视图关闭/工作区切换后的结果归属；本轮未执行。

## AR-25：Pinloom 链接存储的加载失败与写入约束未闭环

**类别：确定缺陷；优先级：高；验证方式：源码读写协议推导，未运行复现。**

**触发与影响**

- `setWorkspaceRoot` 清空内存后调用 `load`；加载失败仅保存错误字符串。`addLink/save` 不检查该加载状态，允许在空记录上追加并用 QSaveFile 替换原文件。已有链接文件损坏、版本不支持或超限时，随后新增链接可能覆盖原有内容。QSaveFile 保证写入替换完整，不能判断“空内存是否代表成功读到空文件”。
- 读取端限制总文件 8 MiB、4096 个来源锚点、每锚点 128 个链接；新增和写出端没有对应限制。因此当前程序本身就能写出下次拒绝加载的文件，例如同一来源累计 129 个不同链接。这不依赖未来版本或外部手工损坏。再次加载失败后继续新增，会进入上一条覆盖路径。
- `loadFailureReason` 在产品源码中的消费者是 suite catalog 的读取诊断，当前链接写入协调器没有将错误变成禁止覆盖条件。普通写入失败会撤回刚追加的内存链接，这项保护仍存在，不能误报为完全没有回滚。

**建议归属与收敛方向**

由 PinloomCodeLinkStore 明确区分缺失、成功加载、不可解析、不兼容、读取失败；失败状态保留原文件并拒绝以空集合覆盖。写入前应用与读取一致的数量/体积约束。保存操作应带加载基线并处理外部更新，避免把长期驻留内存直接当最新全量数据；并发写冲突的策略需另行验证。可参考同仓库 xIPs 宿主对坏文件保留和锁定读改写的做法，但保留各自领域数据，不复制另一套业务存储。

**代码依据**

`src/integrations/pinloom/pinloomcodelinkstore.cpp:25`、`:872`、`:903`、`:1012`、`:1157`、`:1178`、`:1208`、`:1218`、`:1294`；`src/integrations/pinloom/pinloomcodelinkcoordinator.cpp:37`；对照 `src/integrations/xips/xipscontextprovider.cpp:84`、`:89`。本轮只阅读测试与失败分支，没有改写真实链接数据。未来验证应以隔离夹具覆盖损坏/未知版本/数量和字节边界，并证明失败前后原文件字节保留。

## AR-26：CLI 按文本行解析 Git 文件名，遗漏被转义路径的关联

**类别：确定缺陷；优先级：中；验证方式：源码及本机 Git 协议文档，未运行复现。**

`gitChangedFiles` 使用 `git diff --name-only` 与 `git ls-files --others --exclude-standard`，输出经 UTF-8 解码后按换行分割，没有 `-z`，也没有 Git 引号/转义解码。Git 默认会对中文等特殊路径加引号和转义；`QDir::cleanPath/fromNativeSeparators` 不能替代该协议解码。

`changedData` 将这些字符串直接放入 changedKeys，与语义快照中未转义的相对路径比较。中文文件发生变更时，文件列表可能返回转义后的显示串，对应符号和链接锚点无法匹配，却仍通过普通成功出口返回。这属于 CLI 边界缺陷，不影响编辑器正常打开同名文件，也不涉及已修复的 CLI 重复分析。

建议两个 Git 子命令统一使用 NUL 分隔的文件名协议，先按字节边界拆分再解码；复用同一个路径协议适配函数。保留已有超时、错误码和参数数组调用，不换成 shell 字符串命令。

依据：`src/cli/zeroslackcli.cpp:1121`、`:1149`、`:1153`、`:1396`、`:2474`。协议交叉核对来自本机 Git 随附的 `C:/Program Files/Git/mingw64/share/doc/git-doc/git-diff.html`（`-z` 说明约 830 行）、`git-ls-files.html`（约 784 行）及 `git-config.html`（`core.quotePath` 默认值约 3202 行）；未创建仓库或运行 CLI 测试。未来用中文目录、中文文件名和可用平台的特殊字符夹具同时核对 files/symbols/anchors。

## AR-27：模板面板筛选重复解析不变文档并读取模板文件

**类别：可选优化；优先级：中；验证方式：源码热路径与复用边界，未实测耗时。**

当前产品入口是 Global Control 的 Templates 页：搜索框 `textChanged → GlobalControlCoordinator::refresh → EditorInsertPaletteService::query/templateItems` 同步执行。每次输入过滤文字都会：

- 对打开面板时已捕获、过滤期间不变的整篇文档重新创建 TSDocument，提取时钟/复位上下文；
- 重新展开内置模板候选；通过 `UserTemplateService::catalog → records → reload` 再读取、解析、校验全局和工作区模板 JSON；
- 最后才按搜索文字过滤并重建结果列表。

筛选条件变化不要求上述文档事实和模板定义同时重算。此处与 AR-14 的临时编辑器搜索、AR-15 的格式化阶段分别有不同入口和所有者，不将不同页面的问题混作同一条；也不把没有产品调用的旧 `handleInlineAbbreviationTab` 当作当前触发点。

建议在当前面板会话中复用按文档修订/光标上下文建立的模板事实，UserTemplateService 按全局文件、工作区文件和内容版本维护目录结果；明确工作区切换、文件改变、手动 Reload 的失效规则。过滤仅处理候选，不引入跨修订复用语法树；保留已有符号替换路径的修订重验证，并分别明确模板插入所需的上下文有效性检查。优化收益需未来用相同模板集与文档条件测量。

依据：`src/ui/globalcontrolpanel.cpp:57`；`src/commands/globalcontrolcoordinator.cpp:19`、`:120`、`:146`；`src/app/mainwindow.cpp:1560`、`:1591`；`src/editor/editorinsertpaletteservice.cpp:408`、`:480`；`src/completion/codetemplatecontextanalyzer.cpp:443`；`src/completion/usertemplateservice.cpp:322`、`:698`、`:717`、`:722`。

## AR-28：工程配置加载失败被当作有效默认配置继续使用

**类别：确定缺陷；优先级：高；验证方式：GUI/CLI 两条源码消费者链，未运行复现。**

`WorkspaceConfigurationService::loadWithResult` 先构造默认配置；现存工程 JSON 损坏或 schema/version 不支持时，只设失败 message 后返回，configuration 仍是有根目录的默认值。文件打不开时还可能落入 legacy/default 分支，没有把“不存在”和“存在但读取失败”区分开。

GUI 的 `loadConfigurationForWorkspace → load` 丢弃 loaded/message，随后按默认值激活和扫描。CLI 虽使用 loadWithResult，却仅检查 `configuration.isValid()`；该方法只要求 workspaceRoot 非空，因此也继续构建并可返回成功。若原工程依赖 defines/includeDirs/top/虚拟源分组，这些设置会缺席，而消费者没有明确说明正在使用降级配置。这会使后续语义结果基于错误配置，不能只当作通知文案优化。

建议由现有配置服务提供可判别的读取状态：不存在时允许默认值；损坏、不兼容和 I/O 失败保留诊断及原文件，GUI 明确受限/降级状态，CLI 返回可识别错误。不得用 `loaded == false` 一概拒绝新工程，也不将非法旧内容自动保存成默认值。保持当前正常配置保存“先成功、再发布模型”的既有保护。

依据：`include/zeroslack/semantic/workspaceconfigurationservice.h:29`、`:40`；`src/settings/workspaceconfigurationservice.cpp:398`、`:414`、`:501`、`:507`、`:587`；`src/workspace/workspacemanager.cpp:1125`、`:1229`；`src/cli/zeroslackcli.cpp:332`、`:337`、`:2391`。未来验证要分别覆盖文件缺失、非法 JSON、未知版本、读权限失败，检查 GUI 状态和 CLI exit/envelope/cache，而不是只检查返回了非空根目录。

## 根因归并与后续处理顺序

本轮没有发现统一语义 worker、共享文档语法、图报告所有者或域构建边界需要因新证据推翻。遗留集中在这些所有者与消费者的衔接，后续不宜按每个页面各补一条判断。

| 架构根因 | 关联条目 | 应复用的归属 |
| --- | --- | --- |
| 逻辑文本、磁盘字节、文档修订和历史恢复的契约不完整 | AR-17/18/19；AR-20 的输入捕获 | DocumentBuffer/文档登记、真实事务适配器、既有事务历史 |
| UI 当前状态承担草稿基线或异步操作身份 | AR-21/24 | 分层设置草稿、Pinloom 链接协调器；呈现状态与操作身份分别维护 |
| 读取失败、缺失、主动跳过和成功状态被弱化成空集合/bool/默认值 | AR-22/25/28 | 原会话/链接/配置所有者的有类型结果与通知；保留各自领域规则 |
| 不变输入的派生和 I/O 反复进入界面同步路径 | AR-12/13/14/15/20/27 | 文档、查询、格式化、模板目录各自的任务或会话输入，不共享失效条件不同的全局缓存 |
| 任务调度与外部协议边界不完整 | AR-16/26；AR-24 的副作用结果归属 | 原请求所有者、机器协议适配器；只读查询与副作用操作分别处理 |
| 旧入口保留第二套提交或交互规则 | AR-23 | 保留兼容用途时薄委托；确认无使用后删除，不因名字相似误删保护 |

实施依赖建议：先处理会写错对象或覆盖旧资料的 AR-24/25/21，以及使整个工程分析配置失真的 AR-28；再一起设计 AR-18/19 的文档与历史契约，修复 AR-26 的明确协议错误；AR-22 明确失败策略后验证。性能条目按同一输入的基线测量安排，AR-23 随所有者收敛清理。当前全部 17 项已完成开发与验收，正式发布由统筹按用户后续确认处理。

## 后续更新规则

本文件同时保存覆盖记录与待办，不把静态推断记为实测结论。继续审查优先补覆盖缺口，按根因合并发现；类别、优先级、覆盖状态与验证方式分别更新。执行时按实际范围重新核对调用链和复用边界，再更新交付依据。完成状态由统筹验收确定；源码行号以对应审查基线为准。
