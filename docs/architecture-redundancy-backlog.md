# ZeroSlack 架构与冗余逻辑待办

## 0.31.25 发布（2026-10-06）

用户明确要求“push并打包正式包”，本次将已验收R3～R6及必要版本说明发布为0.31.25。发布前1,576文件与FINAL-IDENTITY逐一匹配，实际接受集合为`b594fb6d018d142f1bd8495eda13fd1a46527dfaaa277e1c09fb12e354f51948`。完整提交、暂存包验证、远端与本机坚果云替换结果见 `build/validation/20261006-convergence-release/RELEASE.md`；本条不代替发布完成证据。保留现有正式xIPs/SimDock嵌入组件，SuiteApp SDK仍关闭。下文旧发布/验收状态保留历史上下文。

## 自主多轮收敛（2026-10-06）

用户要求“自行迭代多轮，直到彻底收敛”；Goal 持续监督。起点为 R4 已验收 1,573 文件集合 `02157537246c9f1e8ff1a95135688cc602521b58e41f16c327789e6f1d501003`，含未提交 R3/R4。新任务、冻结配对基线及覆盖记录见 `build/coordination/20261006-zeroslack-convergence/`。逐批开发、整批验收，再做互补复查；不自动发布，不将“已审范围收敛”外推为不存在未知缺陷。

| 编号 | 分级 | 根因、证据与状态 |
| --- | --- | --- |
| AR-45 | 确定缺陷 / 中高 | R5 修复扫描到监听的交接：单 worker 核对新覆盖目录、成员包含类型、保留独立 semantic 回调；31/31 执行回归及统筹29项探针、5/5产品目标通过。R6 必需回归进一步确认冻结R5的双类型/隐藏原生注册边界，已限次清理重装并独立验收：81项真实junction/type组件、80项handoff、公开29项探针连续3次通过。保留早期失败和原R5接受范围，完整结论见 convergence/r5 与 r6/coordinator-review/ACCEPTANCE.md |
| AR-46 | 确定缺陷 / 高 | 已完成R6并独立验收。confirm() 与 confirm(list) 显式区分未提供范围和已知空范围，共用原事务；空工作区/关闭/排除/删除后的旧预览在编辑前拒绝。真实MainWindow/Ela原探针由40项/3失败变40项全过，新增实际产品contract162项通过，包含文本/revision/undo不变、未打开来源不被打开、重建预览与正常撤销。见 convergence/r6/coordinator-review/ACCEPTANCE.md |
| AR-47 | 确定缺陷 / 中高 | 已完成R6并独立验收。复用物理身份，在worker请求内按祖先链剪枝Windows junction回环，保留直接成员与无环逻辑别名。独立自环/无环各7项全过，81项组件覆盖间接环、间隙变化、快切/关闭和后续事件。同一回环夹具64条错误别名变1文件，扫描/安装中位数约3857.75→52.78 ms；旧版也能完成。见 convergence/r6/coordinator-review/ACCEPTANCE.md |

本次自主收敛已完成：R5/R6两批修复和独立验收后，FINAL-REVIEW-A/B 两个互补复查轮次未发现新增未闭环确定缺陷；报告和最终实际源码/运行身份见 `build/coordination/20261006-zeroslack-convergence/GOAL-COMPLETION.md`、`FINAL-IDENTITY.json`。保留R3/R4，上述为发布前接受状态；R3～R6随后纳入0.31.25发布，实际提交/远端/正式包完成结果见本文件开头的发布记录。普通工程测量有约12～14 ms局部增加，不宣称整体提速。范围外和未测边界仍保留，不作零未知保证。后文“待实施/未发布”等历史阶段叙述保留当时事实，当前状态以本节和对应最新验收/发布记录为准。

- 前轮迭代 R4（2026-10-06）：用户要求继续下一轮，现已完成开发与独立统一验收。基线是已验收但未提交的 R3（0.31.24，完整源码集合 `45224da21c221f5beda8f7697be3ecba6746c7773c9d09fa4bbc1f9c733934da`），并非只有 HEAD 的现场。具体任务、六维覆盖和冻结源/运行文件见 `build/coordination/20261006-zeroslack-iteration-r4/`。该轮 Goal 约定范围已闭环，不自动发布。

## R4：编辑能力与矩形选择生命周期（2026-10-06）

| 编号 | 分级 | 新证据、归属与状态 |
| --- | --- | --- |
| AR-34 补充 | 确定缺陷 / 高 | 已修复 palette/结构化命令之外，普通自定义 key dispatch、结构输入和矩形输入仍用 QTextCursor 绕开 Qt 的只读处理。冻结真实 MyCodeEditor 中 Tab、Enter、括号、矩形输入/Delete 均改变只读文本/revision/undo；普通字符及可写对照正常。现已复用 canApplyInsertion 补齐相关写入消费者及 IME，保留导航/复制/加载。已完成独立验收；旧已验收场景保留 |
| AR-44 | 确定缺陷 / 高 | 矩形选择保存裸行列，外来 SharedDocument 编辑既不结束选择也不重映射。真实主/辅助视图：框选 alpha/beta 首字母后，辅助插入 prefix\\n，回主输入 z 变成 zrefix/zlpha/beta。现由控制器标记自身编辑块，其余真实文本变更结束过期选区；清理视图局部撤销意图，保留共享文本 undo/redo，移除列粘贴冗余外层 edit block。已完成独立验收 |

基线 `baseline-probe/run.log` 为显式 Ela、Qt offscreen，10 检查 / 6 失败（5 项只读、1 项错误行）；另有正常只读普通输入、可写列编辑与 Undo、真实列选区激活对照。AR-39 原为外来 change 触发模板镜像，AR-44 是随后输入消费过期列坐标，两者分别跟踪，复用现有编辑来源事实。不是 R3 修改引入的回归，也不据本次补查重写过去测试范围。搜索/替换及 MultiCursor 的保护条件复核没有形成额外确定缺陷；限制见本轮覆盖记录。

R4 完整验收见 `build/coordination/20261006-zeroslack-iteration-r4/coordinator-review/ACCEPTANCE.md`：执行侧 23/23 回归通过；统筹独立 34 项黑盒探针从基线 13 失败变为全过，六组产品/控制器/共享文档测试（98/185/92/12/27/94 检查）全过，追加实际 MainWindow 模板/声明契约 89 项全过。11 个实施文件、1,573 个冻结源码文件与 46 个运行记录逐一核对。执行源码集合 `e9e482d31df4916fdf31dfd128e4fab5c8c3bdf19c9d6762c3b3682546d02db7`；当前 0.31.24 未提交。模拟 IME/Qt offscreen 不外推真实输入法驱动、画面帧率或其他平台；本轮无性能提升声明。

- 上一迭代 R3（2026-10-06）：0.31.24 / `65096e125bb176ca3d92d4bae1805328f6df22a8` 已完成 push、正式包替换及复验，用户随后要求“继续新一轮的迭代”。本批 AR-41/42/43 已完成根因修复、性能验证及整批独立验收，不自动发布。任务和冻结基线见 `build/coordination/20261006-zeroslack-iteration-r3/TASK.md`、`baseline-identity.json`；下文原 Goal 和发布授权属于历史阶段。

## R3：RTL 源码身份与诊断投影（2026-10-06）

| 编号 | 等级 / 优先级 | 根因、当前证据与验收落点 |
| --- | --- | --- |
| AR-41 | 确定缺陷 / 高 | `RtlActionCoordinator::captureRtlActionDocuments`、`InstancePairConnectionFacade` 和 `MultiSignalPropagationPlanner` 重建全量 semanticContents，保留大小写的绝对路径与 SemanticIndexSnapshot 的 Windows pathKey 协议不同。同一已保存源码被误标 unsaved/StaleSemanticSource，阻断正常编辑预览；现有共享 cachedFileSource 正确匹配。当前两组重新构建的真实 Slang 测试分别 27 checks / 19 failures、19 checks / 15 failures，日志已冻结。沿既有源查询/身份所有者统一三个消费者，保留实际修订、语法、语义、只读门禁；补真实 Qt 文档和 MainWindow 预览/确认/撤销、别名及负例。过去的 18/15 历史失败此轮才纳入闭环，不倒写原接受范围。已完成开发与统一验收 |
| AR-42 | 确定缺陷 / 中 | DiagnosticService 的 CurrentFile 空文件名落入 getDiagnostics 空字符串的全局通配语义；当前文件缺失时 Problems 显示其他文件且摘要错误。冻结 0.31.24 实际服务和面板在空目标及有目标→空目标时均显示 1 条其他文件错误，All Files 对照也为 1。由诊断查询所有者明确无目标与显式全局范围，补真实关闭最后 tab、未命名文档及工作区切换场景。已完成开发与统一验收 |
| AR-43 | 可选优化 / 中 | Problems 一次刷新重复生成 count、visible、current-file 三份报告，并清空重建所有树项；稳定输入也重复执行。Release/Ela、Qt offscreen、200 文件/1,000 条合成诊断，10 次同步 update 为 65.4–79.1 ms。复用不可变诊断输入和投影，避免不变数据的重复查询/排序/建树；正确处理 band、过滤、当前文件、工作区和显隐失效，保留选择与导航。记录同条件新旧、冷热/新发布/稳定刷新及残余代价，不外推屏幕帧率。已完成开发与统一验收 |

详细真实入口、现有保护、修复归属与验收条件在本轮 TASK；覆盖增量见 COORDINATOR-COVERAGE.md。Baseline probe 是实际生产查询与 ProblemsPanelCoordinator 加合成诊断，尚非真实 MainWindow 的关闭流程；RTL 两组使用实际 Slang 和文档测试适配器，真实 Qt 文档与产品入口现已补齐并通过；具体组合宿主与 MainWindow 边界见验收记录。未把测试替身结果当作全链路通过。源码集合为 `c39ee198f3904496d0a2956243dd14782d16794bb1f0a3bcdc47f94867135b66`。

R3 完整结论见 `build/coordination/20261006-zeroslack-iteration-r3/coordinator-review/ACCEPTANCE.md`：两个 RTL 工作流为 28/28、19/19；统筹六項定向复验通过，独立 Classic 29 项 / Ela 30 项各三进程通过。执行侧 11 项 GUI 与 38 个 CLI 用例通过并核对产物身份。诊断新发布在展开且保留选择时会提前做布局，合并 update 与事件处理后中位数约 94.5→58.0 ms；收起列表约 57.6→21.6 ms，范围为 Qt offscreen、200 文件/1,000 条合成诊断。

标注修正：本轮早期统筹 baseline-probe/coordinator-probe 只链接 Ela-enabled 构建，未激活 Ela 样式，实际为 Classic；原任务中的 Release/Ela 仅可指编译配置。原始材料保留，已补显式 Ela 的独立对照，不能把旧 Classic 计时冒充 Ela 运行。最终执行源码集合 `8dac6bf5ef8a4b522bc16d4a76cb2a3e159ed0b5d152864b9f1b4d398a47f95d`，当前仍为 0.31.24 未提交修改。
### 前轮 Goal 与发布记录

- 本次发布授权（2026-10-06）：用户明确要求 **“push 并打包正式包”**，将已验收 R1 六项及 R2 AR-39/40 收入 0.31.24。发布前 1,570 个源码文件与接受清单逐一核对；版本和发布说明作为单独增量记录。暂存验证、远端提交和坚果云替换的最终结果见 `build/validation/20261006-goal-release/RELEASE.md`，不以本条代替运行或发布完成证据。

- 持续优化授权（2026-10-05）：用户明确要求依照 `C:/Users/14971/.codex/skills/architecture-improvement/SKILL.md` 持续多轮架构优化并用 goal 模式监督；本次先完成六项，再按覆盖缺口完成 R2 两个根因，现两批开发与独立验收均完成。执行侧开发与自测，统筹在完整交付后统一验收，不进行执行中实时互验。完整目标审计见 R2 `coordinator-review/GOAL-COMPLETION.md`，不以全仓“零未知”为无限停止条件，也不自动发布。
- 本次发布前正式基线：0.31.23 / `ec4db54e4fca52f9375fa0aa505c18f41ad73922` 已 push、打包并替换本机坚果云，证据见 `build/validation/20261005-followup-ten-release/RELEASE.md`。之后 R1 六项接受（49 文件变更集合 e1923b6ecb79e823e8464ac3954be9c25657094a77e087e7993278b8a666c6ca）；R2 三交接受完整源码集合 59caa493244d1ce0f8f8c304d9fb53ed7ee4aed58ac28be42f18879726a0682e，AR-39/40 均已闭环，证据见 R2 coordinator-review/ACCEPTANCE.md。此前 Goal 验收现场包含未提交修改；0.31.24 的完整提交和包身份以本次发布记录为准。
- 记录日期：2026-10-04；最近更新：2026-10-06。
- 历史审查基线（AR-01 至 AR-11）：ZeroSlack 0.31.20，提交 `9198d9a3116226a4640e11e8fc72651c665366d4`。
- 历史审查基线（AR-12 至 AR-28 首次发现）：ZeroSlack 0.31.21，提交 `de7ce08b01d2f9e287f8643b204f87032074eb5a`。
- 历史接续静态审查基线：提交 `3f51c1bfdce116c1089a2fcd4c100ec212b28b53`（0.31.22）加已验收的 71 个实施文件，源码集合 `f9cc94bb281c0ca68cdcb3bacaf45e3ee554e599f86b48ac9b7bbf917a9d1c29`；后来已收入 0.31.23。该历史现场不是当前正在修改的全部源码。
- 当前实施来源（2026-10-05）：用户要求 **“先 push 并打包正式包，之后执行所有 6 项待修改项”**。0.31.23 已完成暂存包验证、push 及本机坚果云替换，六项 AR-34/35/36、AR-27 增补、AR-37/38 已完整交付并通过统筹验收，见任务目录 coordinator-review/ACCEPTANCE.md。发布记录目录为 `build/validation/20261005-followup-ten-release/`；六项任务目录为 `build/coordination/20261005-zeroslack-followup-six/`。之后的 Goal 授权覆盖后续开发与验收，不重复发布这一版。
- 上一阶段指示（2026-10-05）：**“记录；继续分析”。** 本轮仅阅读源码、测试断言、本机 Qt 文档与既有记录并更新本文，未修改产品、构建、运行测试、派发或发布。前批 10 项已经统筹验收通过，任务与验收仍见 `build/coordination/20261005-zeroslack-followup-ten/`，不将本轮新增场景倒写为其已覆盖范围。
- 发布状态：AR-01 至 AR-11 已随 0.31.21 发布；AR-12 至 AR-28 原约定实施范围已完成并通过验收，0.31.22 已 push 并替换本机坚果云正式包。发布记录为 `build/validation/20261005-all-backlog-release/RELEASE.md`。
- 本批完成：AR-01、AR-13、AR-19 的新调用链缺口，AR-16 的同根因优化，AR-22 的恢复风险，以及新增 AR-29 至 AR-33，共 10 项已开发并验收通过。发现时分级为 7 项确定缺陷、1 项待验证风险、2 项可选优化；AR-22 已经故障注入确认并修复。原验收记录及其所验证场景保留，不将新证据倒写为旧测试已经覆盖，也不把所有新发现称为本版引入的回归。
- 历史接续分析：当时新增 AR-34 至 AR-36 三项确定缺陷（源码推导），给 AR-27 增补同次上下文采集的可选复用优化；当时没有运行证据。后来均已获实施授权并进入 R1。原 AR-27 过滤缓存及失效修复的验收仍有效，R1 同次原文解析复用已单独验收，不覆盖历史证据边界。
- 历史第二次接续记录：新增 AR-37（排他创建，确定缺陷）和 AR-38（导航失败后的历史恢复，当时待验证）。浮动面板关闭及模板模式生命周期的静态保护被保留；后来的 R2 用真实新场景确认 AR-39，不将它倒写为旧审查已经运行复现。
- 首次统筹核对与返修证据：`build/coordination/20261005-zeroslack-all-backlog/coordinator-review/REVIEW-01.md`、`REWORK-01.md`；原探针冻结基线通过、本轮首交失败。此问题并入 AR-13，不另增同根因待办。
- 上一轮实施任务与验收条件：`build/coordination/20261005-zeroslack-all-backlog/TASK.md`。历史章节保留授权前的静态审查基线；其中“未运行复现”等描述发现时的证据。最新章节则按 0.31.22 当前源码重新记录。
- 当前规则：`C:/Users/14971/.codex/skills/architecture-improvement/SKILL.md` v1.0、`appsuite-scoped-execution/SKILL.md` v1.4，本轮已通知 ZeroSlack 执行侧读取应用。历史 v1.3 曾通知六个执行侧；规则通知本身不派发额外产品开发任务。
- 上轮执行及验收任务：`build/coordination/20261004-zeroslack-eleven-architecture/TASK.md`；发布记录：`build/validation/20261005-ela-architecture-release/RELEASE.md`。
- 证据边界：AR-01 至 AR-11 的“发现与影响”“代码依据”保留 0.31.20 基线的静态记录，其完成状态依据完整验收及发布记录。AR-12 起的初始发现依据 0.31.21 源码调用链，当时仅阅读测试替身与断言；授权后的实施已构建、回归及测量。“确定缺陷”表示源码存在完整可达的违约路径，初始静态记录不代表已经复现；实施后的实测结果与限制见本轮验收记录。
- 完整验收记录：AR-01 至 AR-11 见 `build/coordination/20261004-zeroslack-eleven-architecture/coordinator-review/ACCEPTANCE.md`，AR-12 至 AR-28 原实施范围见 `build/coordination/20261005-zeroslack-all-backlog/coordinator-review/ACCEPTANCE.md`；本批 10 项补充场景见 `build/coordination/20261005-zeroslack-followup-ten/coordinator-review/ACCEPTANCE.md`。逐项证据及源码/二进制身份见各目录的 `acceptance.json` 和 `accepted-source-runtime-identity.json`；各自证据只覆盖对应任务范围。

历史观察：主语义流水线已经收敛，部分调用方仍重复计算、维护状态或实现规则；当时 CLI 在取得统一快照后仍额外分析关系。AR-01 至 AR-11 已按上轮验收结果处理，不代表当前实现仍存在这些问题。后续发现从 AR-12 起单独记录，不能据其“新发现”推断由最近修改引入。

## Goal 持续优化覆盖与批次

方法采用 `architecture-improvement` v1.0 和 `appsuite-scoped-execution` v1.4；完整规则位于 skill，本文只保存 ZeroSlack 的范围、证据、条目和阶段，不另造一份重复通用规范。

| 批次 / 路径 | 当前状态 | 进入及完成条件 |
| --- | --- | --- |
| R1：AR-34/35/36、AR-27 增补、AR-37/38 | 六项完整验收通过 | 三次交付、两次 AR-36 返修已闭环；最终独立 8/8 回归。生产批量入口也复用共享身份绑定；独立 2,001 文件全量 222.0222→140.9148 ms，旧正式版 13.9828 ms，明确保留后台身份绑定代价。预览查询已通过，证据见 `build/coordination/20261005-zeroslack-followup-six/coordinator-review/ACCEPTANCE.md`。接受集合 e1923b6e…6ca；未发布 |
| R2：辅助编辑视图、模板联动、菜单/程序化撤销、声明成员身份 | 三交完整验收通过，AR-39/40 关闭 | 外部 ID 与声明/成员路径两次返修均闭环；独立 8 组配对对照、5/5 GUI/语义/关系目标、CLI 38 项通过；V2 48 条外部协议检查按未变范围继承。V1/V2/V3 源码、运行与证据保留。接受见 `build/coordination/20261006-zeroslack-goal-r2/coordinator-review/ACCEPTANCE.md`；未发布 |
| 覆盖补查：补全类型/排名与候选身份；其余 CLI 命令、集成适配和配置边界 | 本次约定主要调用链已按范围收拢 | COORDINATOR-COVERAGE.md 记录 12 条调用链的源码/运行结论与继承边界；类型/身份的具体缺陷已入 R2 修复。其余补查未取得需要新建高/中优先级条目的违约证据；平台/资料限制明确保留，不为了凑批次制造条目 |
| 历史已验收范围 | 原证据按范围保留 | 相关源码、依赖或配置变化时使受影响的覆盖项退回待审；没有新证据不重开。原两项基线断言失败和缺少资料不记为通过 |
| 其他五个应用内部、热点/果核图内部重构、缺失 SuiteApp SDK、被排除私有/版权夹具 | 范围外，沿用用户安排 | ZeroSlack 对它们的现有接入/共享协议仍按实际可达路径审查；不擅自恢复依赖或扩展到其他仓库 |

各轮沿职责与依赖、数据归属与复用、状态转换、异步生命周期、持久化、撤销与恢复记录真实场景，覆盖单位是调用链及状态组合。新增发现先分级、根因去重；对高/中优先级缺陷形成实施闭环，对风险补最小证据，对性能优化保留同条件前后数据。没有问题数量指标，也不把整个项目“零未知”作为无限停止条件。

goal 的完成条件：当前及后续改进批已按授权完成，声明范围中适用高风险场景有证据，纳入的高/中优先级问题已修复验收，或具有明确依据的受限/范围外记录；剩余覆盖及性能代价如实交付。仅启动执行或完成一次源码阅读不能将 goal 标记 complete；未运行/受限不能登记为验收通过。

## Goal R2 补查：共享文档与视图交互模式（2026-10-05）

基线为冻结的 0.31.23 / `ec4db54e4fca52f9375fa0aa505c18f41ad73922`。读取 `git show` 和已冻结源码，隔离探针链接经 SHA-256 校验的正式版 DLL/import library；未使用执行侧正在重建的 DLL。证据位于 `build/coordination/20261005-zeroslack-goal-r2-analysis/`，`probe-identity.json` 记录基线、实际二进制及探针身份。此阶段不是对未交付 R1 的中途验收。

| 调用链 | 维度与场景 | 状态与证据边界 |
| --- | --- | --- |
| 主编辑器 → 已打开的临时编辑器 → 同一 SharedDocument → 各自 contentsChange 消费者 | 数据归属、状态转换；失焦主视图保留模板联动，辅助视图普通输入 | 该范围已审；真实 TabManager 创建/注册辅助视图、同一 QTextDocument、焦点切换和键盘编辑已复现越界联动，见 AR-39 |
| 同一共享文档的两个模板控制器 | 职责、重入、撤销；两个公共模板模式入口同时激活再编辑 | 控制器边界已实测重复镜像；完整 MainWindow 菜单/用户模板路径尚待实施回归补齐。共享文档 undo 能恢复原文，不把额外写入误报为已证明的不可恢复损坏 |
| 单视图模板联动 → public undo/redo | 撤销与恢复；联动后程序化撤销和重做 | 本次探针均恢复预期文本，不登记缺陷；不能由此推出所有菜单、外部编辑和双视图组合都已覆盖 |
| 视图重绑/关闭、主 tab 切换、全局控制器打开 | 生命周期与既有退出保护 | 源码保护已核查：TabManager 重绑/关闭、主 tab 切换及 MainWindow 全局控制器打开会退出模式；辅助视图普通 FocusIn 没有 tab group，不能触发该保护 |
| 文件创建后开 tab/插入失败；补全排名及其余 CLI/适配边界 | 持久化/恢复与其他尚未闭合维度 | 部分已审，不能从本节实测外推为通过；按 Goal 批次继续补查 |

补查创建部分失败：`EditorCoordinator::createIncludeNewHeader` 在文件创建成功但开 tab 失败时明确提示 “Created include file but could not open it”，并返回失败；`openUserTemplateFile` 对开 tab 失败也返回失败，已存在文件可再次打开。持久文件并不承诺被编辑器 Undo 删除，不能仅据“留下文件”认定事务缺陷。输入检查与最终插入间的约束属于 R1 AR-34；待其完整交付再核对，不在开发中重复验收。

2026-10-06 接入及 CLI 覆盖补充（仍为 ec4db54e 静态核查，未为未变模块重复构建）：

| 模块/调用链 | 场景和结论 | 验证边界 |
| --- | --- | --- |
| NativeContextView → xIPs/SimDock provider | 宿主先检查 GUI 线程、Qt 版本、ABI、方法返回类型和 QWidget 所属；DLL 保留到进程结束避免回调悬空；重复激活幂等，错误态可重试，retired 态禁止再次激活；busy/modal 拒绝关闭并保留 Cancel/Stop | 所列宿主路径源码已审，未发现新的确定缺陷。正式包原生集成的 3/0 结果见 0.31.23 RELEASE.md；后续若涉及这些文件则需重新判断证据有效性，不覆盖外部应用内部行为 |
| XipsHostBridge → 导出完成回执 → references.json | 回执保留原 workspace，不借当前 workspace 改写来源；路径/联接检查，锁内读取，坏 JSON/未知 schema 拒绝覆盖，QSaveFile 检查完整写入/commit | 源码保护成立；不宣称已对任意不协作外部进程、所有系统平台组合做压力测试 |
| CLI context/symbol/impact → PreparedIndex → 输出 | context 取已捕获文本并核对行范围；查询只消费 prepared symbols/relationships；输出前共同校验源文件及依赖，新缓存只在成功时写入，失败返回错误 envelope | 原 AR-01/33 的冻结输入及预算实测证据保留；本次补查这些实际消费者，没有发现另一次源文本重读。impact 最多 8 层，不在缺少实测收益时另建图缓存 |
| GlobalControlCoordinator → GlobalControlPanel → 候选插入 | 冻结版继续核查：面板是 Qt Popup；Tab/Backtab 在 handleKey 中消费并切换候选类别；选择时先 hide，再派发。普通切 tab 点击会结束弹层，没有查到产品定义的“保持此弹层同时切编辑 tab”快捷键 | 仅支持所列正常交互的源码保护。Symbol 分支只有 revision 检查的事实保留；没有用测试直接改 active editor 来冒充真实用户路径，也未证明所有程序化异步切换组合均安全；不据缺少单个字段直接登记确定缺陷 |
| SuiteApp SDK provider → sourceSnippet | `suiteappintegration.cpp:155` 仍以空文本触发磁盘回退，属于 AR-36 相同输入契约的另一个消费者 | 该实现只有 SDK 构建启用才可达，本机 SDK 缺失且用户明确暂缓。记为 **AR-36 范围外消费者，待 SDK 恢复时迁移共享预览输入并验证**，不说已修复，不另外制造编号，也不擅自恢复 SDK 开发 |

### AR-39：视图局部模板控制器把其他视图的文档修改当作自身联动输入

**确定缺陷 / 高；本轮冻结版实测。进度：R2 完整验收通过，V3 实际产品 89 检查独立复跑通过。**

2026-10-06 首交接受边界：复用既有同步编辑事务与模式控制器，外来视图/无事务/UndoRedo 不能镜像，普通失焦退出、PopupFocusReason 保留合法菜单操作，workspace 切换撤销权限。真实 MainWindow 模板/编辑实例槽位、SharedDocument 双向交错、双控制器、公共/菜单/键盘撤销、外部事务/格式化、只读、辅助视图重绑/关闭及实际 workspace 切换通过；详细范围见 R2 REVIEW-01 及 89 项产品契约日志。以下保留发现时的负例与初始验收要求，不把它们改写成当时已验证。

可达场景：同一文件的临时编辑器已经打开，回主视图启用带重复槽位的模板编辑，再把焦点转入临时编辑器；辅助视图没有启用模板模式，仅改选中的一处文本。预期只有选中范围被修改，实际失焦主视图的槽位控制器同时修改另一处。探针中第一处 `rst_n → reset_n` 使未选中的第二处也变成 `reset_n`。继续用两个实际公共模板控制器入口激活相同槽位后，第二处变成 `reset_n_n`，证实两个观察者均写回共享文档的重入后果。后者是控制器级实测，不冒充已运行完整 MainWindow 菜单流程。

链路与现有保护：`MainWindow::ui.temporaryEditor.open` → `TemporaryEditorContextProvider` → `TemporaryEditorSession::ensureEditorFor/activateEntry` → `TabManager::createAuxiliaryView` → `SharedDocument::attachView`，辅助视图可编辑且与主视图共享文本；所有视图分别连接 `contentsChange`（`editorruntime.cpp:1328`、`:1726`）。`EditorTemplateSlotController::handleContentsChange`（`:637`）只检查本控制器 active、范围和本地重入标记，随后 `mirrorLinkedEdit`（`:775`）写回相同文档；没有编辑发起视图/交互所有权。`MyCodeEditor::focusOutEvent`（`:2291`）不退出模板模式，TabManager 的 FocusIn 只有命中 tab group 才切换活跃 tab（`:2214`），辅助视图不在其中。普通 tab 切换、重绑、关闭、全局控制器打开及键盘撤销的退出保护真实存在，但不覆盖这个输入序列。

建议归属：由编辑交互/共享文档边界明确一次可写交互的所有者及发起来源，模板控制器只对有效的自身编辑执行镜像；复用现有 mode controller、document identity 和视图注册/生命周期，不能只在一个临时窗口按钮回调里清模式，也不要新增另一个权威文档副本。选择失焦退出还是保留但限制写入时，应保证弹出菜单、补全、模板内部镜像、正常鼠标操作和批量外部编辑的语义。

验收：主/辅助视图交错输入只影响授权范围；两个视图不能重复镜像；主/辅助关闭、重绑和普通 tab 切换不残留写入权限；正常同视图联动、Tab/Shift+Tab/Escape、公共及菜单 Undo/Redo 仍成立；通过真实模板/编辑槽位入口和真实 SharedDocument 路径验证。外部格式化/事务修改不得被一个不属于它的模板会话扩写。保留基线负向探针和修改后的源码/DLL身份。与 AR-02 的共享语法解析、AR-19 的事务回滚分别有不同根因，不重开这两条。

### AR-40：模板与类型补全用名称代替声明成员身份

**确定缺陷 / 中；2026-10-06 冻结版实测。进度：R2 三交完整验收通过，声明/类型、外部 ID 及成员访问配对均已闭环。**

2026-10-06 最终接受：V3 把实际声明与具体路径绑定为同一事实，统一赋值、条件、timing、clock/reset 消费；单次 AST 遍历和局部声明复用替代三次独立采集。已绑定但无法解析时保守失败，旧无绑定事实兼容回退保持。独立 8 组零诊断实际 Slang 对照全部通过，包括六组原失败和两个正常语义；独立 5/5 GUI/语义/关系目标及 CLI 38 项通过。V1 的真实类型/报告适配、导航/预览/历史与 V2 的 48 条外部 CLI 协议证据按未变边界继承。下文保留历次发现和返修时的结论，不将其改写为当时已经通过。当前接受范围、快照既有证据合并策略及图绘制限制见 `coordinator-review/ACCEPTANCE.md`。

2026-10-06 第二交返修：V2 源码/运行/证据身份及 V1 冻结内容核对通过，独立 CLI 48 项断言、CLI 38 个 Qt 测试及 4/4 GUI/关系目标通过。共同语义 ID v2、完整 owner 链、相对路径和缓存 5 已消除首交的外部碰撞。不过 `SemanticValueReference` 与独立 paths 列表在 `resolveValueAccesses()` 按同名 root 重新配对，真实 Slang 零诊断输入 `p_a::item.left ^ p_b::item.right` 的赋值/条件均错误生成四种声明/路径组合。证据见 `coordinator-review/v2/member-access-pairing/results.json`；它是实际捕获→快照→关系构建，不是手写事实，CLI JSON 不暴露 accessPath 因而不能代替此验证。已按 `REVIEW-02.md` 要求共同事实中绑定实际声明和具体路径，统一赋值/条件/timing 消费、完整配对去重和有绑定输入的保守失败。timing 目前为源码同类风险，不称已实测。V2 的 1,698 份交付文件已冻结，第三交须使用新目录；同属 AR-40，不新增编号。

第二交冻结对照补充：准备第三交独立验收时，仅运行冻结 V2，`coordinator-review/pairing-contract/v2-ready/results.json` 的 8 组输入全部零诊断。6 组证实时序 sensitivity、clock/reset、同声明多成员及共享 include 嵌套别名也存在同类错配/漏路径；单成员不作为整值桥、标量整值直接转发两组正常。上述 timing 源码风险因此进一步取得运行证据，保留派发时的原结论。探针准备阶段自身的修正见同目录 README，不冒充产品失败，不对执行中的第三交进行验收。

2026-10-06 首交返修：实际 Slang、增量 owner/type key、模板大小写及 include 来源、MainWindow 候选/插入/撤销均通过；CLI 缓存也保存了新 ownerKey/typeKey。然而共同 `semanticStableIdentity()` 仍用 kind/owner.name/name 生成旧 v1 ID。两个模块内的同名 `state_t::IDLE` 在精确键不同的情况下导出相同 ID 和 URI；真实 `impact --symbol <top_a 的 exactId> --depth 1` 仅一个 seed，却包含另一模块的 IDLE 和关系。`symbol` 的关联边也使用碰撞 ID。证据见 R2 `coordinator-review/cli-identity/results.json`。这是原有同根因消费者的遗漏，不说是新引入回归，也不新开编号。已要求执行侧统一身份协议及版本/缓存策略、保留行移动和 workspace-relative 稳定性、补真实 CLI 回归后完整交付；不只对 impact 加文件过滤。首交及统筹通过/失败证据冻结在 `delivery-v1/`。

同根因消费者补充（2026-10-06）：实际 Slang 解析同文件两个合法模块，各自有同名 payload_t / state_t 及 item / state 变量；top_a 的 struct 成员、预期 enum 值和 visible 查询混入 top_b 的 beta / B_IDLE / B_RUN，实际 EditorInsertPaletteService 同样暴露。将 top_b 的类型改名后对照正常。证据 `build/coordination/20261005-zeroslack-goal-r2-analysis/type-identity/results.json` 保留源文本/摘要、真实记录、六个生产查询输出及冻结 DLL/探针身份；尚未运行 MainWindow 上下文采集和最终插入。归属/类型已有 stableKey 字段但实际采集为空，查询仅用 rawTypeText / owner.name 关联。同一源文件过滤无法区分这些声明。修复覆盖采集 → 快照/增量/序列化 → CompletionService → palette；合法别名/import/include/匿名类型与旧记录歧义须有回归。共享 Definition/SignalJourney/hotspot 调用边界一并审查，图内部重构仍排除。合并 AR-40，避免为每个查询重复列同根因。

真实入口是插入面板的 Instantiate：`EditorInsertPaletteService::appendSemanticTemplates` → `CompletionService::commandSymbolCompletionItem` → `moduleInstantiationTemplateForRecord` → `uniqueOrderedModuleMembers`。最后一层对 `record.name.toCaseFolded()` 去重，把语言中区分大小写的端口、参数错误合并。大小写宽松匹配是候选搜索的显示策略，不等于声明身份。

基线探针经实际 `SlangManager::extractSymbolRecords` 解析 `module case_child #(parameter WIDTH=1, parameter width=2)(input logic a, input logic A); endmodule`；索引正确包含 WIDTH、width、a、A 四条声明，生产用 CompletionService 的模板却只生成 `.WIDTH(WIDTH)` 和 `.a(a)`，丢弃另外两条。证据同 `20261005-zeroslack-goal-r2-analysis/probe.log` 与 `probe-identity.json`；这是实际解析器、索引和模板生成链路实测，尚未运行完整 MainWindow 插入交互。

同一归属契约的补充场景：`recordsForSelectedModule`（`completionservicecommand.cpp:220`）先按 owner.name 收集所有同名模块成员，在所选模块文件中没有记录时回退到全部 ownerRecords。探针分别由实际 Slang 解析 a.sv 的无端口 `same_child` 和 b.sv 的带 `other_port` 的 `same_child`，将解析记录装入真实 SemanticIndex；选择 a.sv 声明却生成 b.sv 的 `.other_port(other_port)`。这是索引消费者边界实测，完整项目分析/候选面板组合仍需回归补齐，不宣称解析器一定在每一种重复定义配置中发布两条声明。共享记录已有 `SemanticSymbolOwner::stableKey`（`include/zeroslack/semantic/semanticindex.h:69`），应先核实并复用，而不是仅去掉大小写折叠后继续跨声明借用成员。

归属与复用：修复实例化成员选择/去重的语义身份约束；复用已有语义 record/owner/source identity，保留精确大小写并约束所选声明。核查 include 中声明的参数/端口不能被简单“同文件”过滤掉；核查旧记录缺少 owner identity 时的保守兼容，避免为本问题引入第二套全局符号索引，也不要把用户命令缩写的不区分大小写策略一并改掉。

验收：真实源码中的大小写不同参数和端口全部按源顺序生成，槽位偏移正确；重复语义记录不多插一遍；普通模块、无参数模块、多个文件中同名模块的既有隔离行为保留；插入面板实际插入和一次 Undo/Redo 正常。新增测试用真实解析记录为主，不只用手写 record 掩盖上游语义。

## 接续静态审查：文件创建、导航恢复与界面生命周期（2026-10-05，第二轮）

承接 AR-34 至 AR-36 和 AR-27 增补，补查上轮明确留下的创建文件与失败恢复、导航历史、跨窗口生命周期。基线仍为文首的 0.31.22 已验收工作区；本次源码身份和 71 个实施文件对照另存 `build/coordination/20261005-zeroslack-postacceptance-static-review/source-identity-02.json`，不覆盖前一份证据。

| 条目 | 类别 / 优先级 | 当前结论 |
| --- | --- | --- |
| AR-37 | 确定缺陷 / 高 | 三条“新建”链使用先检查存在、后覆盖式写入；并发创建同名文件时可能覆盖其他写入者的内容 |
| AR-38 | 待验证风险 / 中 | 后退/前进在打开目标前就转移历史，忽略失败返回；目标临时不可读时丢失可重试位置，永久失效项的产品策略尚需明确 |

两项均来自源码推导，未运行复现，不据此声称发生频率或新增性能收益。文件创建的三处重复归为一个共同约束问题；不把它们拆成三项，也不将已验收的普通保存身份保护、文件树预检及配置冲突处理重新判为失败。

### 模块与六维覆盖增量

| 固定维度 | 本次具体调用链 / 状态组合 | 覆盖状态与边界 |
| --- | --- | --- |
| 职责与依赖 | 文件树命令 → WorkspaceFileOperationService；模板插入 → EditorCoordinator 创建头文件；用户模板命令 → MainWindow 初始化文件 | 该范围已审。三条入口各自实现“若不存在则创建”，没有统一的新建 I/O 语义；见 AR-37。模板、头文件内容规则仍属各自服务 |
| 数据归属与复用 | 不存在的目标路径与新文件所有权；back/forward 栈和当前导航位置；模板槽位的视图状态 | 文件创建、历史栈范围已审。模板槽位只补查当前视图切换、文档重绑、关闭和键盘撤销；辅助编辑视图与其他写入者交错尚未闭合 |
| 状态转换 | 新建预检通过 → 其他进程创建目标 → 本次提交；目标关闭/不可读 → 后退失败 → 目标恢复可读 | 该范围已审，分别形成 AR-37/38。没有把整次 apply 调用前的变化检查误当作提交时的排他保障 |
| 异步生命周期 | 浮动面板关闭 → provider 停用 → 延迟销毁；拖放定时回调与窗口代次；主窗口产品创建入口 | 所列范围已审。现有关闭许可、QObject 回调上下文和拖放代次保护有效；未证实跨窗口悬空调用。SDK 内部任务及全部重入排列未审 |
| 持久化 | QSaveFile 新建、QFile 模板初始化与本机 Qt 6.10.2 API 契约；已有保存及路径计划检查的边界 | 该范围已审，见 AR-37。普通覆盖保存和排他创建需要不同操作；没有提出用一个 NewOnly 标志直接替换所有保存 |
| 撤销与恢复 | 历史目标打开失败；模板 Slot Mode 的 Ctrl+Z 退出；面板拒绝关闭后保留原视图 | 导航列 AR-38 风险；键盘撤销及关闭保护路径已审。创建成功但后续开 tab/插入失败的全部恢复组合、辅助视图模板联动、菜单与程序化撤销尚属部分已审 |

本次没有列为新增问题的检查：

- 正式 `main` 只通过窗口工厂创建一个主窗口（`src/app/main.cpp:88`、`src/app/applicationwindow.cpp:6`）。浮动面板不是第二个 MainWindow；不能仅因存在全局服务就断言当前产品必然发生多个主窗口之间的状态覆盖。
- 浮动关闭从 `ContextFloatingWindow::closeEvent` 发出请求，经 `closeFloatingResource` → `closePeek` 先检查 provider 是否允许关闭，再取出、停用并延迟删除 view。拖放提交定时器有对象上下文和 `dockGeneration` 检查，滚动位置恢复回调绑定到 view（`src/ui/contextfloatingwindow.cpp:378`、`:388`、`:477`、`:517`；`src/ui/contextworkspacefloating.cpp:64`、`:195`；`src/ui/contextworkspacecontroller.cpp:1085`、`:1583`）。这仅支持所列路径，没有宣称全部销毁/重入组合无缺陷。
- `unregisterProvider` 当前只有测试调用，不能把其实现疑点直接写成现行产品动态卸载缺陷。Global Control 的 Symbol 分支没有 Templates 那样完整的身份字段，但面板为 Popup，普通 tab 点击会收回；尚未闭合“面板仍打开却换成另一编辑器”的真实产品路径，故本轮不列确定缺陷。
- 主 tab 切换和辅助视图重绑/关闭会退出交互模式，模板槽位键盘 Undo/Redo 先 clear；既有 linked-slot 测试也是键盘 Ctrl+Z（`src/documents/tabmanager.cpp:1166`、`:1197`、`:3110`；`src/editor/editortemplateslotcontroller.cpp:567`；`test_sv/completion_test.cpp:7301`）。不将这些保护误报为缺失，也不外推为所有其他写入来源均受保护。

后续优先缺口：辅助编辑视图与模板联动/程序化撤销的交错；补全候选身份与真正可达的面板上下文变化；其余补全类型推导与排序；新建文件后的开 tab/插入失败恢复。其他 CLI 边界、平台实际呈现仍未重新全面审查。其他五个应用、热点/果核图内部重构、缺失 SDK 及被排除的私有测试资料继续在范围外。

### AR-37：新建文件与覆盖保存共用写入语义，缺少排他创建

**确定缺陷；高优先级；未实施；源码推导。** 三条产品入口都明确要求目标尚不存在，但最后一段写入没有保留这一前置条件：

1. 文件树创建命令进入 `WorkspaceFileOperationService::apply`。它会重新生成计划、检查 revisionToken，`planCreate` 也拒绝已存在目标；随后却用 `QSaveFile(目标).open(WriteOnly)` 和 `commit()` 提交空文件。
2. 插入工具的 CreateHeader 进入 `EditorCoordinator::createIncludeNewHeader`，检查目标不存在，再用 QSaveFile 写模板正文并提交。
3. 打开全局/工作区用户模板的入口，在 `ensureUserTemplateJsonFile` 中检查不存在，再以 `QFile::WriteOnly | Truncate` 写入初始空模板 JSON。

可达交错：本进程完成最后一次“不存在”检查；另一个进程或同步工具创建同名可写文件；本进程随后打开/提交。前两条使用覆盖保存设备，第三条明确截断已有内容，因此会把对方的文件替换成空文件、头文件模板或空模板 JSON。既有重算计划可以拒绝**在重算前**到达的目标，不能关闭重算和实际创建/提交之间的窗口。进程内同步执行也不能排除另一个写入者。

本机 Qt 6.10.2 文档已核对：QSaveFile 的用途是暂存后提交整个文件，并不支持 NewOnly；QFile 的 NewOnly 才明确提供操作系统保证的“不存在才创建”。因此修复不能只是给 QSaveFile 加 NewOnly，也不能再补一次 exists。无需把普通文档保存改成“禁止覆盖”，两类业务语义应分开。

架构归属：在已有工作区文件操作/I/O 边界提供可复用的排他创建操作，携带初始字节并返回有类型的存在冲突/写入失败；文件树、创建头文件、模板文件初始化共同消费。模板内容生成及默认存放位置仍由原领域所有者决定，不能强迫全局模板经过只允许工作区内路径的计划器。若需要临时文件和失败清理，还应明确本次创建的所有权，避免清理其他写入者的新文件。正常覆盖保存保持原有内容与身份护栏。

依据：`src/navigation/navigationmanagerfileoperations.cpp:653`、`:790`；`src/workspace/workspacefileoperationservice.cpp:273`、`:321`、`:493`；`src/editor/editorcoordinator.cpp:728`、`:762`、`:775`；`src/app/mainwindow.cpp:3528`、`:3552`、`:3580`、`:3613`。API 依据为本机 `D:/Qt6/Docs/Qt-6.10.2/qtcore/qsavefile.html:149` 和 `qiodevicebase.html:88`。

现有 `test_sv/workspace_file_operation_test.cpp:246` 验证正常创建；`test_sv/gui_smoke_test.cpp:3439` 的“不覆盖”断言只覆盖调用前就已存在的文件。后续验证应在最后检查与实际创建/提交之间注入竞争，覆盖三个真实入口，确认对方内容保留并明确报告冲突，同时保留首次成功、预先已存在、写入失败的行为。这与 AR-30 已有配置的编辑基线冲突不同：本项要求取得原本不存在文件的创建权；也与 AR-34 的源编辑器只读约束不同。

### AR-38：导航历史先提交状态，后执行可能失败的跳转

**待验证风险；中优先级；未实施；源码推导。** 当前 `navigateBack` 先从 backStack 移除目标、把当前位置加到 forwardStack，再调用 `applyLocation`；前进方向完全对称。`applyLocation` 会因为工作区不匹配、目标文件打不开或定位失败返回 false，但调用方忽略该值。

源码能够闭合的后果：从 A 导航到 B 后关闭 A 的 tab，A 因临时缺失或不可读而无法重新打开；在 B 后退会消费 A 并将 B 放入前进栈，实际编辑器仍在 B。A 随后恢复可读，原后退项已经丢失，普通重试不能回到它。真实适配器确实把 `activateOpenFile || openFileInTab` 的失败传回；TabFileIo 会显示打开失败提示，所以本条不声称错误完全静默。

仍列风险的原因：代码后果明确，但产品尚未定义“永久删除的历史项应跳过还是保留”，不能直接把所有失败项都必须回滚当成既定规则。需要先明确临时不可用、永久失效及用户取消分别如何处理，再验证真实 Back/Forward 入口。

建议由既有 NavigationCommandCoordinator 持有一次历史跳转的候选和结果；成功后再提交双栈变化，或按明确的失效策略丢弃并继续查找。不要在 TabManager 新建另一套历史，也不要仅恢复一个栈而留下另一个栈的假当前位置。工作区切换时是否清空历史是另一项产品政策，本轮没有据此新增缺陷。

依据：`src/editor/editorcoordinator.cpp:495`；`src/navigation/navigationcommandcoordinator.cpp:69`、`:264`、`:280`、`:316`；`src/documents/tabfileio.cpp:66`。现有 `test_sv/gui_smoke_test.cpp:968` 验证同文件成功跳转后的实例上下文恢复，不覆盖打开失败及恢复可读后的重试；折叠视图的 Back 测试属于另一种历史，不能代替此链。后续需验证成功/失败/恢复后的两个栈、当前编辑器和实例上下文。

## 接续静态审查：插入工具、源码导航与预览（2026-10-05）

本轮优先补上历史覆盖表中的 completion/navigation/editor 消费者链，基于上方已验收工作区分析。71 个实施文件摘要仍与交付一致；本轮证据文件及 Git 基线对照见 `build/coordination/20261005-zeroslack-postacceptance-static-review/source-identity.json`。AR-34 至 AR-36 的关键消费者代码在本次十项实施中没有改动；这是补查发现，不据此推断为最新修改引入。

| 条目 | 类别 / 优先级 | 当前结论 |
| --- | --- | --- |
| AR-34 | 确定缺陷 / 高 | 插入工具的操作分支绕过统一可写性检查，只读编辑器仍能执行 include/import 写入及创建头文件 |
| AR-35 | 确定缺陷 / 中 | 导航继续使用独立的行文本词法规则，截断完整标识符，并把字符串中的 import 识别成导航目标 |
| AR-36 | 确定缺陷 / 中 | 预览把空的已打开文档当作不存在，并把旧语义位置与新缓冲区拼接；两个预览服务复制相同来源选择逻辑 |
| AR-27 增补 | 可选优化 / 中 | 过滤缓存已生效，但一次新的模板上下文采集中仍独立解析两次同一份原始文本；待评估共享语法输入 |

验证方式统一为**源码调用链推导**，没有运行这些新增场景。确定缺陷意味着条件、入口与违背的约束在代码中闭合，不等于已实测发生频率或完成修复。

### 模块与六维覆盖

模块及调用链：Global Control → EditorInsertPaletteService → MainWindow 分支派发 → MyCodeEditor / EditorCompletionWorkflow → PackageToolService、EditorCoordinator；编辑器 Ctrl+点击/预览 → SourceNavigationService / DefinitionService → SemanticIndex；DefinitionPreviewService / CodePreviewService → DocumentModel → DocumentRegistry，并补查语义失效调度和相关测试断言。

| 固定维度 | 本轮具体范围 | 覆盖状态与边界 |
| --- | --- | --- |
| 职责与依赖 | 插入计划、产品派发、编辑器写入、文件创建；导航词法识别与现有 TSDocument | 该范围已审。发现可写性检查分散、部分消费者绕过现有语法能力。没有重新审计所有命令路由及构建 ABI |
| 数据归属与复用 | 文档内容/存在状态、语义位置、当前修订；模板上下文原文与 probe 文本 | 该范围已审。预览丢失来源及版本契约；模板采集中原文重复解析。未全面审查补全排序和全部类型查询 |
| 状态转换 | 普通/只读编辑器、正常代码/字符串/完整特殊标识符、非空/清空/插行后的缓冲区 | 该范围已审。分别形成 AR-34/35/36。普通点击的 Tree-sitter 注释保护已存在，未将其误报为缺失 |
| 异步生命周期 | 文档编辑 → Dirty → 编辑空闲定时分析；旧语义快照与当前预览的衔接 | 该范围已审。预览未消费该状态/版本。没有本轮桌面交互或线程调度测量；跨窗口销毁后的所有单例使用仅部分已审 |
| 持久化 | create-header 写盘与随后插入入口；正常文档只读装配、保存仍转另存为 | 部分已审。确认创建副作用前缺少源编辑器可写性门禁；未把缓冲区可改夸大成绕过系统文件权限。新建文件与插入的全部部分失败恢复组合尚未闭合 |
| 撤销与恢复 | 结构化插入的 QTextCursor edit block、预览在目标变化后的拒绝/降级边界 | 部分已审。只读情况下不应产生修改或撤销步；高风险批量事务本轮未重审。导航前进/后退遇到目标打不开的产品策略尚未确认，不列确定缺陷 |

未审或仅部分已审：其余补全类型推导/排名、导航历史的失败策略、所有跨窗口单例销毁组合、其他 CLI 命令边界和平台实际动画呈现。其他五个应用、热点/果核图内部算法、缺失 SDK 及已排除版权/私有资料仍在范围外；共享代码预览的来源契约属于本轮范围。

### AR-34：插入分支未统一执行可写性约束

**确定缺陷；高优先级；未实施。** 普通 `insertCompletionText` 在入口拒绝 `isReadOnly()`；结构化 import/include/create-header 则通过另外三个方法进入工作流，没有同样的检查。

可达路径：打开只读源文件 → Ctrl+Space 的 Templates 中选择已有 include / 合法位置的 package import → `MainWindow` 检查文档修订、实例和模板目录后直接调用操作方法 → `MyCodeEditorState` 转发 → `applyStructuredInsertionPlan` 仅检查计划有效性，使用 `QTextCursor(editor->document())` 插入并返回成功。QPlainTextEdit 的只读交互状态没有成为这条程序化写入路径的前置条件。已有内存文本会被修改并产生撤销步。

同一派发中的 CreateHeader 还会先经 `EditorCoordinator::createIncludeNewHeader` 提交新头文件、打开新 tab，然后回到源编辑器插入 include；源文件只读、所在目录可写时，并没有在创建之前拒绝该操作。`PackageToolService::inlineRangeIsEditable` 只检查范围、注释/字符串语法位置，它不接收文档可写状态，不能替代权限门禁。

架构归属：在既有编辑器命令应用边界统一核对目标实例、修订和可写能力，再允许写入文档或创建关联文件；纯文本计划服务继续负责语法，不让它承担 UI 状态。普通插入、结构化插入和创建再插入消费同一约束，不能只给一个 palette 分支加判断。源文件保存仍有只读转 Save As 保护，本条不声称受保护的原磁盘文件必然被覆盖。

依据：`src/app/mainwindow.cpp:1578`、`:1622`、`:1659`；`src/editor/mycodeeditor.cpp:857`、`:1078`；`src/editor/editorruntimecommands.cpp:851`；`src/editor/editorcompletionworkflow.cpp:672`、`:706`、`:732`；`src/commands/packagetoolservice.cpp:259`；`src/editor/editorcoordinator.cpp:728`；`src/documents/tabmanager.cpp:2501`、`:2530`。已有 `test_sv/gui_smoke_test.cpp:3327` 覆盖正常插入和注入创建回调，`:11695` 的只读断言针对行操作，不能覆盖这三个结构化入口。未来应从真实 palette 及公共编辑器 API 各验证只读拒绝、内容/撤销栈不变、无创建副作用，并保留可写成功场景。

### AR-35：导航重复实现不完整的源码识别规则

**确定缺陷；中优先级；未实施。** 编辑器已公开现有 `TSDocument` 的精确标识符、注释和字符串查询；导航却把上下文压缩为一行 QString，再通过另一组 `isIdentifierStart/isIdentifierPart` 和 `indexOf("import")` 识别目标。

两个可达组合属于同一识别契约缺口：

1. `logic ready$next;` 及其使用处，点击 `$` 前后的部分，`SourceNavigationService::identifierAtColumn` 只把字母、数字和下划线当作标识符字符，得到 `ready` 或 `next`，后续定义查询以这个截断名称执行；转义标识符 `\ready.next ` 同样会被分段。仓库所带语法的 `simple_identifier` 明确接受后续 `$`，`TSDocument::identifierAt` 已能返回 simple/escaped_identifier 完整节点。
2. 合法字符串 `string msg = "import p::*;";` 中 Ctrl+点击 `p`，UI 的 `syntaxCommentAt` 不会拒绝字符串；`targetAtColumn` 先匹配 package import，跳过普通 `identifierAtColumn` 的字符串检查，于是把字符串中的 `p` 当作 package 导航目标。当对应 package 可解析时会尝试跳转。

架构归属：由编辑器已有语法文档提供精确 token/指令及所在上下文，导航消费该结果；保留真正 include 路径字符串的特殊语义。无语法上下文的行级接口若仍需要保留，应明确有限用途并复用公共 token 规则。需要区分注释、普通字符串、include 字符串、普通及转义标识符，不能只向一个字符集合补 `$`。

依据：`src/editor/editorsourcenavigation.cpp:505`；`src/editor/editorsourcenavigationquery.cpp:286`；`src/navigation/sourcenavigationservice.cpp:113`、`:164`、`:307`、`:312`、`:381`；`src/editor/editorcoordinator.cpp:854`；`src/editor/tsdocument.cpp:1042`、`:3072`；`src/editor/mycodeeditor.cpp:767`。测试 `test_sv/completion_test.cpp:7648` 验证普通 include 和普通名称，`test_sv/source_structural_navigation_test.cpp:93` 验证的是另一条结构化赋值导航的注释保护，不证明本链的特殊名称和 import 字符串处理正确。未来需把真实 UI 入口与共享查询都纳入上述组合。

### AR-36：预览丢失文本来源和位置版本的对应关系

**确定缺陷；中优先级；未实施。** `DefinitionPreviewService` 与 `CodePreviewService` 都实现了一份相同策略：向 DocumentModel 要 QString，仅在非空时使用；否则回退 SemanticIndex 缓存。`documentTextForFile` 返回的空文本不能区分“已打开且内容为空”和“没有该文档”，但现有 `documentForFile`/`editorForFile` 可以表达存在状态。

可达路径：先有语义分析结果，再把目标文件的打开缓冲区清空，尚未发布新语义结果时从其他文件触发定义预览 → 空缓冲区被当作未打开 → 展示旧缓存里的定义。若在目标定义之前插入多行，预览则选择新缓冲区，却仍按旧 SemanticSymbolRecord 的行列截取、突出显示，并标记 available。文本来源与坐标来源因而不是同一版本。CodePreview 的 preciseRange 也只检查文件名/行等字段，没有文本版本证据。

这一窗口由正常编辑链即可形成：DocumentModel 立即发布新文档状态；AnalysisScheduler 先标 Dirty，再定时刷新语义。失效逻辑主要取消过期在途请求，保留已发布快照；预览服务没有检查目标文件的语义状态或相应修订。不能把“后台有过期结果检查”当作“UI 可以任意混用旧位置和新文本”。

架构归属：复用文档模型已有存在/身份/修订信息，统一预览输入选择及片段截取；把来源、版本、内容（包括有效空文本）和坐标证据作为一组数据。对不匹配的位置重新解析或明确标注/拒绝过期预览。两个服务仍可分别负责标题和呈现，不能各保留一套有不同失效语义的来源回退规则。

依据：`src/navigation/definitionpreviewservice.cpp:81`、`:89`、`:113`；`src/navigation/codepreviewservice.cpp:112`、`:134`、`:155`；`src/documents/documentmodel.cpp:72`、`:104`；`src/documents/documentregistryqueries.cpp:55`、`:73`；`src/analysis/analysisschedulerdocuments.cpp:533`、`:653`；`src/analysis/workspacesymbolanalysiscontrollerlifecycle.cpp:152`。现有 `test_sv/gui_smoke_test.cpp:5408` 只在定义同一行追加 dirty marker，验证“优先非空未保存文本”，未覆盖清空、行移位及等待新语义时的版本对应。未来需使用真实 DocumentModel、已发布快照和编辑后的缓冲区验证这些组合。

### AR-27 增补：一次新上下文仍有可复用的原文解析

**可选优化；中优先级；尚未实测，原过滤缓存验收保留。** `EditorInsertPaletteService::query` 的 sameContext 缓存确实避免重复过滤时重算；一次新上下文中，`CodeTemplateContextAnalyzer::analyze` 会新建 TSDocument 解析全文，随后 `PackageToolService::analyzePackageImportSite` 再新建一个 TSDocument 解析相同原文，还会对加入 import probe 的预览文本另做解析。

前两次原文解析有共享输入的空间，编辑器本身也已经持有同修订的语法文档；第三次处理的是变换后的文本，承担合法性验证，不应简单删除。建议给既有分析服务提供按修订绑定的只读语法输入或一次上下文构建结果，保留必要的 probe/插入后验证，避免全局长期缓存。这里只确认重复工作，不能承诺可感知提速。

依据：`src/editor/editorinsertpaletteservice.cpp:469`、`:481`、`:487`；`src/completion/codetemplatecontextanalyzer.cpp:443`；`src/commands/packagetoolservice.cpp:357`；`src/editor/mycodeeditor.cpp:767`。`test_sv/insert_palette_catalog_test.cpp:405` 的 contextAnalyses 统计的是上下文构建次数，不是内部解析次数；其重复过滤与目录失效断言仍然有效。后续若实施，需分别计数原文/变换文本解析并测首开与过滤阶段，不把更改后的验证算作冗余。

建议处理次序：先 AR-34，再 AR-36 和 AR-35；AR-27 增补仅在上述共享输入归属明确后评估。当前没有派发开发。

## 本批 10 项最终验收（2026-10-05）

10 项按 TASK 约定范围验收通过，未发现需返修的阻断项。执行源码集合为 `f9cc94bb281c0ca68cdcb3bacaf45e3ee554e599f86b48ac9b7bbf917a9d1c29`，共 71 个实施文件；基线仍为 `3f51c1bf`，正式包未替换。

- 统筹独立复跑 GUI/CLI 构建、13 项关键回归和独立 CLI-only 1 项测试，全部通过。交付源码、产物和证据哈希核对一致。
- 执行侧 39 个受影响目标为 35 通过、2 个复现基线失败、2 个排除夹具限制；编辑器自包含 134/134 通过。两项旧失败的 18/15 个断言与重建基线逐条一致，不计为通过。本批没有声明全部 219 个测试目标通过。
- 30 份性能样本独立重算一致：两种大文件各 400 处批量编辑的总耗时中位数分别从 1900.384/2814.314 ms 降为 60.096/80.605 ms。语义装饰拥塞期间捕获从 160 次降为 1 次、实际启动从 160 次降为 2 次；最后后台排空中位数从 41.899 增至 46.741 ms，作为取舍保留。上述数据不外推整个应用倍率或桌面帧率。
- 真实适配器、核心和替身已统一核对；多进程恢复/配置、部分编辑失败、重入、撤销失败及重试、pending 重开和 CLI 中途修改都有专项证据。配置协作写入边界、进程内 pending 和外部竞争校验点的限制保留。

本批验收及完整限制见 `build/coordination/20261005-zeroslack-followup-ten/coordinator-review/ACCEPTANCE.md`。以下 0.31.22 章节保留实施前的发现依据与覆盖情况，其中的“当前/待实施/未实测”均指当时基线，不替代上面的最终状态。

## 0.31.22 继续审查：历史发现与覆盖（实施前）

以下证据均对应 `3f51c1bf`。验证方式是读取实际入口、共享实现、真实适配器、测试替身和有关历史差异，没有运行复现、性能测量或故障注入。七项确定缺陷指源码能够给出完整的条件和违约路径；发生频率尚未实测。接续分析本次补入 AR-01 的输入一致性缺口，以及 AR-32、AR-33 两个新条目；同一根因没有按多个触发场景重复计数。

### 当前发现总表

| 条目 | 分级 / 优先级 | 新证据及处理状态 |
| --- | --- | --- |
| AR-13 补充 | 确定缺陷 / 高 | 稳定文档身份未传到普通保存、恢复键和文件树操作收尾；重新打开，仍归原身份根因 |
| AR-19 补充 | 确定缺陷 / 高 | 因版本过期而完全未编辑的文件，也被当作本事务已修改文件恢复；重新打开，仍归事务恢复契约 |
| AR-29 | 确定缺陷 / 高 | 多个应用进程共用同一恢复记录，互相覆盖及清理；新根因，待实施授权 |
| AR-30 | 确定缺陷 / 高 | 工程配置缺少编辑基线冲突检查，设置的检查与提交也未共同串行；新根因，待实施授权 |
| AR-22 补充 | 待验证风险 / 中 | 失败后保留的待存会话没有参加重开恢复来源选择；待验证，不撤销原范围的验收 |
| AR-31 | 可选优化 / 中 | 批量编辑反复扫描和转换文本坐标，已求得的偏移被丢弃；待评估，未声称性能收益 |
| AR-16 补充 | 可选优化 / 中 | 语义装饰仍每轮提交全局线程池任务，缺少跨轮次的运行/待处理上限；并回原根因 |
| AR-01 补充 | 确定缺陷 / 高 | CLI 源码片段、清单和语义结果分次捕获，单次输出可能混用版本；重新打开原输入/缓存契约 |
| AR-32 | 确定缺陷 / 高 | RTL 高风险编辑撤销失败后，事务历史、工作流标记和面板归属脱节；新增恢复状态根因 |
| AR-33 | 确定缺陷 / 中 | CLI bundle 的固定头部及首个链接块未按完整输出扣预算；新增输出边界根因 |

### 模块、调用链与六维覆盖

本轮复用既有模块清单，优先深入上轮未闭合的状态组合。没有重读整个仓库，也不据本轮发现数量宣称全局审查穷尽。

| 固定维度 | 本轮具体核对范围 / 主要所有者 | 覆盖结论与剩余边界 |
| --- | --- | --- |
| 职责与依赖 | `app/main` 与窗口工厂；TabManager → ExternalDocumentSyncController / TabFileIo；命令 → rtleditcore → WorkspaceEditDocumentManager | 所列链已审。文档身份归属和回滚权限没有完整传到下游。未重新审计所有 CMake 边界、SDK 或外部 ABI |
| 数据归属与复用 | SharedDocument 的稳定 documentId 与显示路径；恢复记录键；IndexedWorkspaceTextEdit 与 Qt 坐标转换 | 所列链已审，见 AR-13/29/31。语义、编辑器、搜索各坐标类型不同，不主张直接跨域复用 Qt 私有类型 |
| 状态转换 | 别名重绑后普通保存/另存为；文件树重命名后关视图；预检后文件变化；会话保存失败后关闭、重开 | 前三组已审；会话恢复读取受 QSettings 缓存和失败类型影响，列 AR-22 风险。一般工作区切换已保留内存 UI，不把它误报为必然回退 |
| 异步生命周期 | 恢复队列提交门、取消、清理；多进程独立队列共享磁盘；主动编辑器刷新 → 装饰 worker | 所列链已审，见 AR-29/16。现有代次/存活/修订校验仍有效；未实测桌面帧率、线程池饱和或跨应用 IPC 负载 |
| 持久化 | 普通保存内容护栏；恢复快照保存/清理；工程配置对话框 → 项目存储；Settings Center 保存 | 所列链已审，见 AR-13/29/30。Pinloom 链接的锁内重载可供借鉴；各文件领域语义仍应归原服务。文件系统 syscall 间的所有外部竞争未穷尽 |
| 撤销与恢复 | PatchEngine 预检/逐文件应用/失败回滚；真实 restoreSnapshot；历史 restoreAtomically；失败会话的重开来源 | 所列调用链已审，见 AR-19/22；连续撤销的旧版本衔接修复仍保留。未运行中途改文件注入或新的端到端回归 |

接续分析的六维覆盖增量：

| 固定维度 | 本次补查的调用链 / 状态组合 | 结论与边界 |
| --- | --- | --- |
| 职责与依赖 | CLI inspect → shared worker → context/bundle；RTL 面板 → 工作流 → 事务服务 → 真实文档适配器 | 所列链已审，新增证据见 AR-01/32；工作流恢复策略与核心事务不一致，不以重复代码行数代替缺陷 |
| 数据归属与复用 | CLI 原始字节、语义逻辑文本、输入指纹、链接定位；工作流 undo 标记和面板 appliedOwner | 所列链已审。原始字节与解码文本不能直接比较摘要；同一命令仍须绑定同一批输入。跨工作区历史保留政策不据此判错 |
| 状态转换 | 应用成功 → 文件只读 → 撤销失败 → 恢复可写 → 重试/新预览/关闭工作区；输出预算边界 | 所列链已审，见 AR-32/33；未穷尽各 RTL 计划器的算法组合、所有面板状态 |
| 异步生命周期 | CLI 扫描与 worker 再捕获之间的外部修改；worker 完成时输入校验；持久面板在关闭工作区后的存活 | 已区分外部写入与本进程线程并发。worker 自身已有 stillMatchesDisk；CLI 第一份输入未纳入该检查。未执行时序注入 |
| 持久化 | 新建 CLI 缓存中的清单/依赖证据配对、cacheCurrent 标记、下一次调用失效 | 所列链已审；下一次调用能拒绝不一致缓存，不修复本次已经返回的混合结果。未将一般缓存写入竞争判为数据丢失 |
| 撤销与恢复 | 核心失败保留历史 → 服务代次不变 → 工作流 Failed → 面板丢失 owner；同类工作流的状态映射 | 所列链已审，见 AR-32；单文件只读场景可从真实适配器闭合，不只依赖故障替身。未运行桌面重现 |

当前目录覆盖：`app/documents/workspace/settings` 的上述状态链较深入；`commands/components/rtleditcore` 覆盖事务入口、适配器、坐标处理及高风险面板失败恢复；`editor/navigation` 覆盖身份和文件树边界；`semantic/analysis/ui` 补读装饰刷新、队列及面板生命周期；`cli` 本次深入输入捕获、context/bundle 组合输出和预算；`integrations` 核对现有锁内重载参考及 CLI 链接读取。`completion`、其余 CLI 命令的全部边界、各 RTL 计划器算法、平台动画呈现仍没有重新全面审阅，不能把旧验收泛化为这些范围在当前基线已重新通过。热点/果核图内部重构、其他五个应用和缺失 SDK/版权资料仍按既有安排列范围外。

### AR-13 补充：稳定身份停留在索引层，保存等消费者仍解析可变路径

**确定缺陷；高优先级；重新打开原条目。** 原反向索引移除、代表视图接替、Save As 和视图释放修复不否定；本次补到普通保存及操作收尾消费者。

可达普通保存路径：

1. 通过 `alias/unit.sv` 打开真实目录 A 的文件。SharedDocument 用 A 的物理路径作为稳定 documentId，同时保留 alias 显示路径。
2. 编辑该缓冲区，然后把目录别名指向 B。B 的同名文件内容与最初基线相同，且尚未在 ZeroSlack 中打开。
3. 普通保存仍把 alias 当作写入目标。`documentForFile(alias)` 查 B，未找到另一打开文档，因此冲突文档检查不拦截；`same(fileName, document->fileName())` 比较的又是同一条当前 alias。
4. `canOverwriteDocument` 只比较读到的逻辑内容摘要与已观察/已保存摘要。B 恰好同内容，且没有不可用/冲突状态时会放行；没有比较 B 的物理身份与稳定 documentId。
5. `TabFileIo` 按 alias 写 B；词法路径没改，后面不执行 renameDocument，文档仍绑定 A 却被标为已保存。不是仅导航显示不准，而是存在写错目标和错误清除未保存状态的路径。

相关消费者还有两处同源断层：恢复键从 `document->fileName()` 再解析物理路径，会跟着重绑后的 alias 走；文件树预检虽然收集了受影响文档，却不把这个集合交给收尾，收尾又从已发生变化的文件系统解析一次。例如通过指向 `rtl` 的目录联接打开文件，再从文件树将真实 `rtl` 改名，联接失效后收尾可能找不到原先受影响的视图。

依据：`src/documents/shareddocument.cpp:456`；`src/documents/tabmanager.cpp:2504`、`:2513`、`:2522`、`:2570`、`:423`、`:1813`、`:1865`；`src/documents/externaldocumentsynccontroller.cpp:241`、`:466`；`src/documents/tabfileio.cpp:98`；`src/editor/editorfileidentity.cpp:96`。现有 `test_sv/workspace_document_contract_test.cpp:172` 验证了重绑后 Save As 且另一个文档已打开，不等同于上述普通保存组合；文件树测试 `test_sv/workspace_file_operation_test.cpp:945` 使用的是普通目录路径。

修改归属建议：由既有文档所有者提供稳定的来源身份和可验证 I/O 目标，将显示路径变化、来源替换、明确 Save As 区分开；外部同步和恢复服务消费同一身份契约。路径操作携带预检中确认的文档/视图集合及有效性证据，收尾不依赖变更后的路径重新猜测。不能仅给 Ctrl+S 加一条特判，也不能静默改存到另一个路径。

后续验证应覆盖同内容/异内容的新目标、另一目标已打开/未打开、普通保存/另存为、恢复写入/清理，以及真实目录改名导致联接失效后的主视图和辅助视图收尾。此处描述的是当前源码推导，不声称这些组合已复现。

### AR-19 补充：拒绝编辑也触发无条件恢复，回滚缺少改动归属

**确定缺陷；高优先级；重新打开原条目。** 这是恢复契约的新增失败组合，已修复的单调修订与连续撤销保持有效。`PatchEngine` 对失败当前文件调用 restore 的分支在 `de7ce08` 已存在；不把它直接归为 0.31.22 新引入。

预检 A、B 后先应用 A；外部工具此时改了尚未打开的 B。轮到 B，真实适配器 `applyTextEdits` 的最新 snapshot 与 expectedVersion 不匹配，尚未编辑便返回 false。`PatchEngine` 仍先对 B 调用 `restoreSnapshot`；后者没有“当前应处于哪个版本”的参数，取得/加载 B 的新内容后将它替换成预检旧文本，再回滚 A。B 因此成为包含旧文本的未保存缓冲区。磁盘在这一步未被直接写回，但后续保存可能覆盖外部更新。

根因是 `bool applyTextEdits` 无法表达“未改动而拒绝”和“修改后失败”，恢复接口也不能证明当前状态确由本事务产生。`restoreAtomically` 另有先全量检查、再逐文件 restore 的阶段划分，同样没有把逐文件的预期当前状态带入写入接口。

依据：`components/rtleditcore/include/rtledit/workspace_document_manager.h:23`；`components/rtleditcore/src/rtledit/patch_engine.cpp:147`、`:170`；`src/workspace/workspaceeditdocumentmanager.cpp:115`、`:196`；`components/rtleditcore/src/rtledit/workspace_edit_transaction.cpp:314`、`:339`、`:360`。核心替身支持修改后返回 false，但不能据此证明真实适配器的“过期且未改”分支也应恢复（`components/rtleditcore/tests/patch_engine_tests.cpp:92`）。

建议在现有文档事务协议内表达应用结果、是否发生本事务改动、改后身份/版本和恢复前置条件。回滚只恢复本事务拥有的状态，外部新状态保留并报告冲突/残留；主线程同步执行不等于磁盘没有其他写入者。后续验证必须把外部修改放在预检与 B 实际应用之间，并检查失败后 B 缓冲区、磁盘与保存基线，而非只在整次调用前修改 B。

### AR-29：恢复记录缺少进程/编辑会话所有权

**确定缺陷；高优先级；新条目。** 与 AR-12 的界面线程开销、同一队列取消顺序分开：这里即使所有操作严格串行也会出错。

独立正式程序的入口和窗口工厂没有单实例拦截。两个进程打开同一工作区、同一文件时，恢复根目录相同，recoveryId 也只取决于工作区和物理文件身份：P 写自己的未保存文本，Q 再写自己的未保存文本，会覆盖同一个 JSON；随后 P 保存或放弃关闭，又会按相同 recoveryId 删除 Q 的记录。Q 的缓冲区仍未保存，崩溃后却可能失去恢复副本。

依据：`src/app/main.cpp:24`、`:88`；`src/app/applicationwindow.cpp:6`；`src/documents/crashrecoveryservice.cpp:541`、`:1217`、`:1245`、`:1272`、`:1290`；`src/documents/tabmanager.cpp:536`。AR-12 的取消/提交门使用队列内 QMutex，只保护本进程任务（`src/documents/documentrecoveryqueue.cpp:14`）；当前恢复测试的提交竞态也是同一门保护下的清理，见 `test_sv/crash_recovery_service_test.cpp:578`。

建议让现有恢复服务明确记录所有者和编辑会话代次，保留多个所有者的候选，保存/关闭只清理本会话有权清理的记录；重启后的遗留候选仍须能识别并恢复。仅加磁盘写锁不能解决串行覆盖问题；也不未经产品决策就禁止应用多开。验证应包含两个独立进程交替写入、某一进程正常保存/关闭、另一进程退出后的候选保留。

### AR-30：配置提交没有统一的基线冲突语义

**确定缺陷；高优先级；新条目。** AR-21 管草稿与层级，AR-28 管损坏/不兼容输入；本项针对仍然合法但已更新的配置。

普通工程配置对话框从 WorkspaceManager 拿一份配置；在对话框打开期间，另一个 ZeroSlack 进程或外部编辑器合法更新 `project.json`。当前对话框接受后，`WorkspaceConfigurationService::save` 只确认最新文件仍 usable，就用旧草稿重新序列化全部已知字段，没有检查读取时的内容版本。外部加入的 defines/includeDirs 等可被旧草稿覆盖。无需两个写入动作发生在同一个毫秒。

Settings Center 已有 expectedRevision，但“检查版本”和 `QSaveFile::commit` 分开，两进程都可以先通过同一旧版本检查，再各自提交；全局 QSettings 保存也在应用层版本检查之后写入整组自有字段。原子替换能保证单次文件完整，不能替代这一整段业务冲突判断。Pinloom 链接与 xIPs 回执已有锁内重读方式，可借鉴其顺序，不能把其中的业务合并规则原样搬给设置。

依据：`src/app/mainwindow.cpp:3672`；`src/workspace/workspacemanager.cpp:1126`；`include/zeroslack/semantic/workspaceconfigurationservice.h:83`；`src/settings/workspaceconfigurationservice.cpp:635`；`src/settings/settingscenterservice.cpp:303`、`:394`、`:463`；`src/settings/settingscenterpanel.cpp:393`。参考保护为 `src/integrations/pinloom/pinloomcodelinkstore.cpp:975`、`:982`，及 `src/integrations/xips/xipscontextprovider.cpp:84`。Settings Center 现有顺序冲突测试只覆盖保存调用前磁盘已经改变，不能证明同时提交安全。

建议由各现有存储服务持有读取基线，并把协作写入的加锁、锁内重读、版本判断和提交作为一个流程；合法新配置冲突应可见，保留用户草稿。外部编辑器不遵守应用锁的情况需要单独定义边界，不能声称一个 QLockFile 消除了所有文件系统竞争。验证优先覆盖“打开对话框→外部更新合法配置→接受”，再覆盖两个写入者和写入失败。

### AR-22 补充：待存会话与重新打开的恢复来源未统一

**待验证风险；中优先级；不新增编号。** 本轮确认 `pendingSaves` 保留失败快照，但 `restoreSession()` 始终从 stateService 读取，并未参与待存快照选择；关闭工作区又会移除 liveWorkspaceUi 和 activatedWorkspaceRoots，重开进入恢复路径。

需要验证的组合是：旧会话已落盘→新会话保存失败并耗尽重试→关闭、在同进程重开→恢复存储后再保存。界面是否读回旧会话、后续保存是否替换更晚的待存快照，取决于实际失败类型及 QSettings 同进程缓存行为，当前不直接断言已丢失。普通切换仍有 liveWorkspaceUi，因此不泛化为“切换就丢布局”；文件正文由文档保存/恢复保护，不属于这个会话问题。

依据：`src/workspace/workspacesessioncoordinator.cpp:183`、`:238`、`:262`、`:480`、`:520`；`src/workspace/workspacesessionstateservice.cpp:814`、`:870`。现有测试覆盖重试、清理取消和两个工作区各保留一份 pending，没有证明上述关闭后重开组合。后续先确认恢复来源与用户显式 Restore 的产品含义，再补失败恢复验证，不擅自改变已接受的退出尽力保存策略。

### AR-31：批量编辑丢弃坐标计算结果，多层再次全文扫描

**可选优化；中优先级；新条目。** 与 AR-20 已完成的按范围/文件捕获分开，本项发生在拿到同一文本之后。

`positionToOffset` 每次从文本开头扫描；每个 range 调两次。PatchEngine 已计算每个编辑的偏移，用于范围校验和排序，却在 `buildTextEditApplicationOrder` 输出时退回仅包含行列的 WorkspaceTextEdit。真实 Qt 适配器再次逐项求偏移，又为每个编辑构造、解码从文件开头到起止位置的 UTF-8 前缀；预计结果的 `applyTextEditsToString` 再扫描一遍。靠后的大量匹配时，坐标准备可接近编辑数 × 文本长度，另有反复临时字符串分配。

依据：`components/rtleditcore/src/rtledit/text_edit.cpp:7`、`:33`、`:62`、`:107`；`components/rtleditcore/src/rtledit/patch_engine.cpp:87`、`:138`；`src/workspace/workspaceeditdocumentmanager.cpp:127`、`:139`、`:157`。仓库已有搜索行首表和语义重映射行索引（`src/commands/searchservice.cpp:144`；`src/semantic/semanticsourceremap.cpp:55`），但它们使用 Qt/UTF-16 语义，不能直接依赖进无 Qt 的 rtleditcore。

建议由现有文本编辑核心按固定文本版本保留纯 C++ 行/字节索引或已验证偏移；Qt 适配器按同一版本批量做 UTF-8→UTF-16 映射。保留应用前的新鲜度检查、Unicode 边界校验、倒序编辑和同点插入合并规则，减少的是重复定位。当前没有新的毫秒数或收益比例；未来用相同文件和编辑集合分别测坐标准备、校验、实际应用，并检查非 ASCII、换行和输出一致性。

### AR-16 补充：语义装饰生产者仍未采用跨轮次有界提交

**可选优化；中优先级；并回 AR-16，不重复计数。** `scheduleActiveEditorPassiveRefresh` 已合并同一事件轮次的通知；`refreshActiveEditorSemanticDecorations` 也有取消标志、generation、QPointer、当前编辑器及修订校验，worker 内含取消点。不能将其描述成没有取消或能任意发布过期结果。

剩余差异是每次实际刷新仍新建 watcher、捕获文本/语义快照并提交全局 QtConcurrent；上一轮任务未完成时也继续提交，没有“一个运行任务加一个最新待处理输入”的跨轮次上限。旧任务即使启动后立刻取消，排队对象、捕获和 watcher 仍已创建；阻塞/饱和时可能持有多代输入。当前未证明常态卡顿或具体资源峰值。

依据：`src/app/mainwindow.cpp:835`、`:879`、`:1015`、`:1131`、`:1177`、`:1213`；`src/semantic/semanticdecorationservice.cpp:805`。建议在现有装饰任务所有者收敛运行/待处理/取消，参考已落实的按所有者有界队列；是否提取小型共同调度器应由真实复用对象决定，不额外搭全局任务框架。验证应区分同轮合并和多轮快速切换，并观察启动数、峰值保留输入和最终可见结果。

### AR-01 补充：CLI 的清单、源码片段与语义结果来自不同次捕获

**确定缺陷；高优先级；重新打开原条目。** 原来重复编译、关系未复用、外部 include 缓存失效的问题已处理；本次发现的是 CLI 消费者没有与共享 worker 绑定同一批输入，不重新报告已删除的第二次编译。

可达路径为一次需要重建缓存的 `context` 或 `bundle` 调用：

1. `inspectWorkspace` 读取源码字节 S0，保存 content、sha256 和 workspaceRevision；context 的行数/片段及 bundle 的片段使用这份内容。
2. 在 inspect 完成之后、worker 捕获之前，外部生成器或编辑器把一个源码文件改为 S1，然后保持不变。
3. `projectForWorkspace` 只传路径和配置，没有把 S0 交给 worker。`SemanticInputCapture` 再读磁盘，语义索引和 dependencyEvidence 来自 S1。
4. worker 的 `stillMatchesDisk` 能确认 S1 自身在分析期间未变，但并不知道 CLI 保留的 S0。序列化又把 S0 的 workspaceRevision/files 与 S1 的 symbols/relationships/diagnostics 放进同一个缓存。
5. `prepareIndex` 重建后直接标记 cacheCurrent=true；本次成功输出于是可能包含 S0 的代码及摘要、S1 的符号和位置。下次调用可能因 revision 不符重建，这不能修正本次已返回的不一致结果。

同根因消费者还有 `anchorsJson`：它按链接文件再次读磁盘，没有复用命令已捕获的源码。文件在前两次读取之后改变时，context/bundle 中的链接位置可能来自又一个版本。这里只要求同一源码的版本一致，不要求外部 Pinloom 元数据和源码采用同一业务版本号。

依据：`src/cli/zeroslackcli.cpp:321`、`:477`、`:539`、`:629`、`:723`、`:865`、`:1217`、`:1480`；`src/semantic/semanticanalysisinput.cpp:75`、`:85`；`src/analysis/incrementalsemanticanalysisworker.cpp:198`、`:606`。现有 CLI 测试在两次 execute 之间改变 include，验证下一次失效（`test_sv/zeroslack_cli_test.cpp:258`），以及静态输入下的 context/bundle；不覆盖一次 execute 内 S0/S1 分裂。

建议让现有语义输入所有者向 CLI 提供同一版本的语义结果与源码证据，清单、摘要、片段和链接定位统一消费；或者发现两次输入不一致时整体重取/明确失败。保留原始字节与逻辑文本的编码、BOM、换行契约，不能直接比较这两类摘要；也不能简单伪装成编辑器 override 而跳过磁盘新鲜度检查。顺带消除重复读取应在这个归属边界内完成，尚未测量耗时收益。后续验证需要把修改精确放在扫描与捕获之间，并检查本次输出、落盘缓存和下一次命中行为。

### AR-32：高风险编辑失败后的事务能力与界面状态相互矛盾

**确定缺陷；高优先级；新条目。** 与 AR-19 的底层恢复权限分开：这里即使底层历史正确保留，调用方也会失去恢复入口。与 AR-06 已完成的计划一致性校验相邻，但根因是工作流和面板重复持有、推导恢复状态，不能只补一个按钮。

可达的真实适配器场景：在一个文件内完成 RTL 重命名，保持文档打开且内容不变；将文件变成只读后，从高风险编辑面板撤销。

1. snapshot 的版本由文本修订、磁盘字节摘要和文档实例组成，不包含写权限。因此预检可以通过；`restoreSnapshot` 随后因只读返回 false，尚未改动该文件。回滚同样可因只读失败，核心返回 RestoreFailed 并保留历史。单文件未改动时，解除只读后底层的版本条件仍可满足，具备重试基础。
2. `RtlHighRiskEditWorkflow::undo` 对非 Conflict 的失败发布 Failed，却保留 workflowUndoAvailable=true；所以 canStartPreview=false，而 canUndoAppliedTransaction 又因状态不是 Applied 返回 false。
3. 面板 `undo` 看到 canUndo=false，清掉 appliedOwner。随后按钮状态不再提供撤销；新预览表面可点击，但工作流因仍持有 undo 标记拒绝。
4. 再点预览虽然会让工作流回到 Applied/InvalidState，面板只在 Applied 且 failure=None 时重建 owner，因此仍不能通过该面板撤销。关闭工作区的 reset 又只为仍有 owner 的工作流 retire；它不会重建 workflow 对象，无法清掉这个孤立标记。已审查的重开、预览、关闭路径均未闭合恢复。

此外，普通 UndoConflict 在工作流层仍保留 Applied，但面板先转成 Conflict，再只允许 Applied 面板状态启用 Undo；这也说明错误展示状态与操作能力被耦合。不能反向把所有失败都无条件开放撤销：发生真实残留或新事务替换时，应由事务所有者明确给出允许重试、需解决冲突、可放弃该工作流位置等能力。

依据：`src/workspace/workspaceeditdocumentmanager.cpp:83`、`:196`；`components/rtleditcore/src/rtledit/workspace_edit_transaction.cpp:191`、`:302`；`src/workspace/workspaceedittransactionservice.cpp:53`；`src/insights/rtlhighriskeditworkflow.cpp:608`、`:674`、`:696`、`:722`、`:812`；`src/insights/rtlhighriskeditpanel.cpp:826`、`:1163`、`:1366`、`:1413`、`:1519`、`:1628`；`src/semantic/semanticdockcoordinator.cpp:77`、`:177`。

同类实例连线和多信号传播对撤销失败均保留工作流 Applied（`src/commands/instancepairconnectionworkflow.cpp:759`；`src/commands/multisignalpropagationpanel.cpp:1142`），但这不代表它们所有界面组合已经验收。高风险面板现有测试替身 restoreSnapshot 永远成功（`test_sv/rtl_high_risk_edit_panel_test.cpp:103`）；核心的失败后重试测试也不等于该面板入口可重试。

建议在既有事务/工作流边界统一恢复能力与归属，面板只展示错误和消费操作能力；保留单步撤销产品语义、共享历史代次防串用以及真实残留处理。后续覆盖真实只读解除、失败但回滚成功、残留冲突、新事务占用、重试、显式放弃和关闭重开。当前没有运行 UI 复现；相关状态映射在 0.31.21 已存在，不认定为 0.31.22 引入。

### AR-33：CLI bundle 的输出预算没有覆盖完整候选块

**确定缺陷；中优先级；新条目。** 这是输出边界约束，与 AR-01 的源码版本一致性分开。结论仅针对 bundle 自身声明的 UTF-8/4 估算预算，不把估算差异、外层 JSON 元数据或其他模型的真实 tokenizer 当作错误。

两处同根因遗漏：固定 Markdown 头部直接包含完整 query，初始化 tokens 后没有检查是否已经超过内部 budget；足够长但合法的查询可以在没有追加任何符号前超额。首个 Pinloom 链接则先检查 tokens+anchorTokens，再单独检查 tokens+headingTokens；两项分别放得下，并不保证标题和链接一起放得下，后续追加前没有组合检查。

依据：`src/cli/zeroslackcli.cpp:1116`、`:1445`、`:1466`、`:1544`；命令行 `--query` 使用普通字符串输入，服务只要求非空。现有测试只用 query=q、budget=512 检查总估算（`test_sv/zeroslack_cli_test.cpp:427`），没有覆盖长查询固定开销和首个链接的边界组合。对照 `suiteContextData` 已先检查 payload 骨架，并对完整候选 payload 判断预算（同文件 `:2144`、`:2194`），不能把它误报为相同缺陷。

建议在 CLI 输出组装层对固定内容先计费，随后以完整块（首次标题加正文）做一次预算判断；最低可表达内容超过预算时明确拒绝或采用约定的截断表示。复用小型预算规则即可，Markdown 和 JSON 的计量范围仍分别明确，无需重建输出框架。后续验证恰好相等/超出一单位、长查询、首个链接及多字节文本。源码已足以证明分支遗漏，本轮未执行测试；这些代码在 0.31.21 已存在。

### 本轮排除的误报与下一步边界

- Qt 适配器并非自行忘记倒序编辑：PatchEngine 已通过 `buildTextEditApplicationOrder` 排序后调用它，不报告编辑顺序错误。
- 搜索产生事务位置时已将 UTF-16 行内前缀转成 UTF-8 字节列，不报告中文列坐标完全未转换。
- 事务历史有默认 32 条上限，不报告历史条数无限增长。跨工作区历史保留本身仍是产品策略，不据此直接判错。
- Pinloom 链接写入已有锁内重载；恢复队列已有进程内取消/提交门；语义装饰有过期结果保护。新问题不能覆盖掉这些真实保护。
- 历史对比表明，失败当前文件的无条件 restore、配置保存缺少内容基线、坐标重复扫描等路径并非本轮才出现。会话 pending 是上一轮新增机制，当前仅记录它与旧恢复路径组合的风险，尚未判定运行回归。
- CLI worker 已检查它自己捕获的输入是否仍与磁盘相符；外部 include 的下一次缓存失效也有已有证据。AR-01 补充针对本次调用跨两份输入的组合，不能改写成 worker 完全没有新鲜度校验。
- 高风险工作流有共享历史 generation 防串用；本次发现不是后一个事务会被前一个工作流任意撤销。核心保留历史、业务保留标记、面板丢弃 owner 是三个不同层次，必须分别描述。
- suite-context 明确只给 payload 计预算，并检查完整候选 payload；不能因外层 envelope 超过 maxTokens 就与 bundle 的组合遗漏混为一谈。

本次实施分组：文档/持久化组包含 AR-13/29/30；事务及恢复组包含 AR-19/32，AR-22 先确认风险；CLI 组包含 AR-01/33；性能组包含 AR-31/16，需以同输入基线评估收益。用户随后已明确“开始执行吧”，当前 10 项均已进入授权范围；发现分级与原证据保留，未验收前不标为完成。统筹拥有本记录，执行侧交付自己的实施及自测记录。

下一轮优先补当前缺口：各 RTL 计划器自身的输入/拒绝组合、completion 生命周期与复用、其余 CLI 查询边界和非身份类 UI 状态。跨工作区历史保留的产品政策仍待明确，不因保留本身判错。当前已沿撤销失败和 CLI 混合输入闭合新增证据，停止扩读这些已足够的路径；不以继续凑条目宣称全局审查穷尽。

## 历史固定清单覆盖记录（0.31.21，v1.3，全局遍历）

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

## 历史结论分级（0.31.21 首次发现）

优先级与结论类别独立：高优先级的性能优化也不自动等于功能故障。以下类别均与验证方式分开记录。

| 类别 | 条目 | 当前证据 |
| --- | --- | --- |
| 确定缺陷 | AR-18、AR-19、AR-21、AR-24、AR-25、AR-26、AR-28 | 文本/恢复/草稿契约、异步链接归属、链接存储保护、CLI 文件名协议和配置失败传播存在完整源码路径；未运行复现 |
| 待验证风险 | AR-22 | 自动会话保存失败传播缺口由源码确认；实际存储失败后的重开影响及所需切换策略待验证 |
| 可选优化 | AR-12、AR-13、AR-14、AR-15、AR-16、AR-17、AR-20、AR-23、AR-27 | 重复工作、同步路径、任务资源或无产品调用入口由源码确认；不声称已实测卡顿或改善收益 |

0.31.21 首次记录的 AR-12 至 AR-28 共 17 项：**7 项确定缺陷、1 项待验证风险、9 项可选优化**。当时新增的是 AR-24 至 AR-28（4 项缺陷、1 项优化），并扩充既有根因；没有把新增场景再次计数，也没有将静态确认写成运行复现。0.31.22 的新证据与当前分级见本文前部。

## 待办总览

| 编号 | 待修改项 | 优先级 | 状态 |
| --- | --- | --- | --- |
| AR-01 | CLI 统一分析、输入证据与组合输出一致性 | 高 | 原范围已发布；单次调用输入一致性补充已修复，本批验收通过 |
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
| AR-13 | 文档身份与索引、保存、恢复及路径操作消费者 | 高 | 原范围已发布；稳定身份进入 I/O 和路径操作消费者，本批验收通过 |
| AR-14 | 临时编辑器搜索仍同步全量查询和创建结果项 | 高 | 已完成，统筹验收通过 |
| AR-15 | 格式化各阶段缺少可复用的语法上下文 | 中 | 已完成，统筹验收通过 |
| AR-16 | 部分后台任务的取消、排队上限及资源隔离不一致 | 中 | 原范围已发布；装饰任务上限及合并完成，本批验收通过；排空阶段耗时边界见验收 |
| AR-17 | 文件载入、保存基线和外部同步重复读取同一文件 | 中 | 已完成，统筹验收通过 |
| AR-18 | 文档快照的文本契约在打开前后不一致 | 高 | 已完成，统筹验收通过 |
| AR-19 | 事务恢复权限、历史基线与真实文档修订契约 | 高 | 原范围已发布；事务修改回执及条件恢复贯通真实适配器，本批验收通过 |
| AR-20 | 搜索与替换的输入捕获未按范围和文件复用 | 高 | 已完成，统筹验收通过 |
| AR-21 | 设置草稿生命周期与工作区激活、生效状态耦合 | 高 | 已完成，统筹验收通过 |
| AR-22 | 会话保存失败处置、待存快照与恢复来源 | 中 | 重开丢失 pending 风险已确认并修复，本批验收通过；进程终止边界保留 |
| AR-23 | 无产品调用点的忽略目录接口重复维护配置提交规则 | 低 | 已完成，统筹验收通过 |
| AR-24 | Pinloom 异步创建回调使用可变的当前链接来源 | 高 | 已完成，统筹验收通过 |
| AR-25 | Pinloom 链接存储的加载失败与写入约束未闭环 | 高 | 已完成，统筹验收通过 |
| AR-26 | CLI 按文本行解析 Git 文件名，遗漏被转义路径的关联 | 中 | 已完成，统筹验收通过 |
| AR-27 | 模板面板输入复用及同次上下文采集 | 中 | 原过滤缓存及目录失效已验收；R1 同次解析复用也已验收，原文 2→1，30 进程证据见 R1 ACCEPTANCE |
| AR-28 | 工程配置加载失败被当作有效默认配置继续使用 | 高 | 已完成，统筹验收通过 |
| AR-29 | 多进程恢复记录缺少编辑会话所有权 | 高 | 会话隔离、租约及认领完成，两进程回归通过；本批验收通过 |
| AR-30 | 工程配置与设置提交的基线冲突语义不统一 | 高 | 草稿基线与协作锁内复查完成，两进程回归通过；本批验收通过 |
| AR-31 | 批量编辑重复扫描、转换同一文本坐标 | 中 | 索引及偏移复用完成，本批验收通过；两组 400 处编辑实测约 31.6/34.9 倍 |
| AR-32 | 高风险编辑的恢复能力、工作流状态与面板归属脱节 | 高 | 事务归属、重试及显式释放能力贯通面板，本批验收通过 |
| AR-33 | CLI bundle 固定内容及首次链接块遗漏组合预算 | 中 | 完整组合预算及边界回归完成，本批验收通过 |
| AR-34 | 结构化插入及关联文件创建绕过可写性约束 | 高 | R1 已完成，统筹独立验收通过 |
| AR-35 | 导航行级识别与共享语法的完整 token 契约不一致 | 中 | R1 已完成，统筹独立验收通过 |
| AR-36 | 预览文本来源、有效空内容与语义位置版本脱节 | 中 | R1 已完成，统筹独立验收通过 |
| AR-37 | 新建文件缺少排他创建语义 | 高 | R1 已完成，统筹独立验收通过 |
| AR-38 | 导航跳转失败已消耗历史 | 中 | R1 基线负例及修复后实际 TabManager 回归通过，已验收 |
| AR-39 | 共享文档的多个视图模板控制器越界及重复镜像 | 高 | R2 完整验收通过，V3 产品契约独立复跑通过 |
| AR-40 | 声明成员身份：实例化大小写误合并、跨声明借用及同名类型补全混用 | 中 | R2 完整验收通过；外部 ID 及声明/成员路径两次返修均闭环 |

AR-01 至 AR-11 及 AR-12 至 AR-28 原 TASK 的实施和验收记录均保留；前批 10 项后续缺口已随 0.31.23 发布。本次 Goal 的 R1 六项和 R2 AR-39/40 已完整验收，新增已纳入问题全部闭环；旧失败、缺失夹具及既定范围外事项不记为通过。目标完成审计见 R2 GOAL-COMPLETION.md，未自动发布。

以下保留历史发现、原执行顺序及验收说明；当前完成状态以本页待办总览及本批最终验收为准。

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

## 历史根因归并与当时处理顺序（0.31.21）

本轮没有发现统一语义 worker、共享文档语法、图报告所有者或域构建边界需要因新证据推翻。遗留集中在这些所有者与消费者的衔接，后续不宜按每个页面各补一条判断。

| 架构根因 | 关联条目 | 应复用的归属 |
| --- | --- | --- |
| 逻辑文本、磁盘字节、文档修订和历史恢复的契约不完整 | AR-17/18/19；AR-20 的输入捕获 | DocumentBuffer/文档登记、真实事务适配器、既有事务历史 |
| UI 当前状态承担草稿基线或异步操作身份 | AR-21/24 | 分层设置草稿、Pinloom 链接协调器；呈现状态与操作身份分别维护 |
| 读取失败、缺失、主动跳过和成功状态被弱化成空集合/bool/默认值 | AR-22/25/28 | 原会话/链接/配置所有者的有类型结果与通知；保留各自领域规则 |
| 不变输入的派生和 I/O 反复进入界面同步路径 | AR-12/13/14/15/20/27 | 文档、查询、格式化、模板目录各自的任务或会话输入，不共享失效条件不同的全局缓存 |
| 任务调度与外部协议边界不完整 | AR-16/26；AR-24 的副作用结果归属 | 原请求所有者、机器协议适配器；只读查询与副作用操作分别处理 |
| 旧入口保留第二套提交或交互规则 | AR-23 | 保留兼容用途时薄委托；确认无使用后删除，不因名字相似误删保护 |

当时的实施依赖建议：先处理会写错对象或覆盖旧资料的 AR-24/25/21，以及使整个工程分析配置失真的 AR-28；再一起设计 AR-18/19 的文档与历史契约，修复 AR-26 的明确协议错误；AR-22 明确失败策略后验证。性能条目按同一输入的基线测量安排，AR-23 随所有者收敛清理。这 17 项原范围已开发、验收并发布 0.31.22；本次新证据与当前处理顺序见文首的继续审查章节。

## 后续更新规则

本文件同时保存覆盖记录与待办，不把静态推断记为实测结论。继续审查优先补覆盖缺口，按根因合并发现；类别、优先级、覆盖状态与验证方式分别更新。执行时按实际范围重新核对调用链和复用边界，再更新交付依据。完成状态由统筹验收确定；源码行号以对应审查基线为准。
