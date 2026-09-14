# Insight 侧栏交互设计

基线 87caec95 / 0.28.1。本文记录已拍板的设计，供实施与验收对照。
五条已全部在 0.29.0 实施，逐条实施记录见文末。

## 问题

侧栏里 5 个 insight section（Kernel / Module / State / Hotspot / Wave）显示的不是图，而是一张状态卡：
kind 选择网格 + 状态 chip + 一行 summary + Follow/Pin/Full View（`src/insights/liveinsightscontextview.cpp:400`）。
那行 summary 由 `src/app/mainwindowcontextworkspace.cpp:553` 硬编码拼成，只是把已选中的东西重念一遍
（`clk · inputs and outputs`），不携带任何新信息。真正的图只在 Full View 的中央标签页
（`LiveInsightToolPage` → `RtlInsightWorkbench`）里。而"给某个符号看某张图"的唯一入口是编辑器右键的四条
`insight.*` action（`src/commands/actionregistry.cpp:882`）——侧栏自己没有选目标的能力，只能被动跟随光标。

结论：section 是"关于某视图的说明卡"而不是该视图本身；同一功能被劈到 rail、卡片、Full View、老 dock 四个面，
右键菜单完全绕开侧栏另走一路。

## 已拍板的五条

### 1. section 渲染真图，尺寸由用户调整

section 正文换成真正的图组件，不再是 summary QLabel。窄宽度下画不下**不做**降级成列表——由用户自行调整
section 高度（`ContextDockHost::setSectionHeight` 与边界拖动已具备）和侧栏宽度（dock 分隔线，宽度已持久化）。
图按可用尺寸 fit，不锁定最小宽度到画不下就撑破容器。Full View 保留，负责大图。

### 2. rail 保持 5 个入口不变

不塌缩成单个 Insights 入口。section 内部的 kind 切换网格与 5 个 rail 图标的重复关系保持现状。

### 3. 重选目标用 slot mode 式闪动拾取

新增一个编辑器交互模式，复用既有机制，不发明新的绘制通道：

- 模式注册进 `EditorModeController`（现有 `EditorModeId` 已有 TemplateSlots / SignalSelection 两个先例）。
- 候选用 overlay annotation 表达：新增 `EditorAnnotationKind::InsightTarget`，
  `placement = Overlay`，靠 `phaseVisible` + 500 ms 定时器闪动——与 `EditorTemplateSlotController`
  的 `ensureBlinkTimer` / `publishVisibleAnnotations` 同一套（`src/editor/editortemplateslotcontroller.cpp:327`）。
- 候选只在**可见区**枚举，用既有 `TSDocument::identifiersInRange(startChar, endChar)`
  （`src/editor/tsdocument.h:759`，wave manifest 服务已在用），随滚动增量发布，不扫全文件。
- **两级候选判定**，避免为每个可见标识符跑重服务：
  - 一级（同步、决定闪不闪）：语法 + taxonomy。Kernel / Hotspot / State 取 signal 与 port
    标识符（与 `resolveSignalSelectionCandidate` 的
    `SymbolTaxonomy::isSignalDeclaration || isPortDeclaration` 同一判据）；Module 取 module 声明名与实例
    类型名；Wave 取 scope 头（module / always 块）。
  - 二级（选中后）：调真正的服务确认。例如 State 调
    `StateTransitionGraphService::buildStateTransitionGraph`，被拒时按
    `StateTransitionGraphNotFoundReason` 原样显示原因并**保持模式不退出**，让用户接着选下一个。
    禁止静默把不合格目标当成合格。
- 键鼠：`Tab` / `Shift+Tab` 在候选间移动，`Enter` 选定，`Esc` 退出（与 slot mode 一致）；鼠标左键直接选定。
- 进入方式：section 标题条上的 scope chip 点击进入；该 section 是本次拾取的请求方，选定后目标钉在它身上。
- 跨文件：本模式只处理当前编辑器可见区。目标在别的文件时走右键（第 4 条）或 Full View，不在本模式内跳文件。

### 4. 右键 action 改为重定向侧栏 section

四条 `insight.*` 右键 action 的落点从"另开 dock / 中央标签页"改成：把对应 kind 的 section 重定向到该符号并钉住；
section 不存在则创建。侧栏不可见时按既有 `setDockVisible(true)` 路径一并显示。
`Open current tab in main area` / Full View 仍然是取得大图的方式。

### 5. 状态与空态

- 状态 chip 不再占正文：降级为标题条上的小指示（进行中 / 陈旧 / 错误），正文始终显示 last-valid 内容
  （`snapshot.hasLastValid` 已具备），不再用 "Waiting" 覆盖已有结果。
- 空态不写 "Follow the editor or select source context…"，改为列出 3–5 个可点候选目标（当前模块、其 FSM、
  最近访问过的信号），点击即成为该 section 的目标。
- Wave 的 "RTL-derived symbolic values; not a simulation result." 从正文移到标题条 provenance 徽章，
  不占两行正文。

## 实施顺序

1 → 5 → 4 → 3。第 1 条不动架构即可消除"信息没用"，值得单独一版；第 3 条引入新编辑器模式，最后做。

## 明确不做

- 不把 5 个 rail 入口合并。
- 不做窄宽度下的图→列表降级。
- 不新增原生 dock，不改浮层几何解析。
- 不为让候选闪动而全文件扫描或对每个标识符跑语义服务。

## 验收要点（实施时逐条给证据）

- section 内是真图组件实例，不是 QLabel；拖动 section 边界与 dock 分隔线后图重新 fit，视图实例不重建
  （provider `created` 计数不变）。
- 拾取模式：候选仅来自可见区；滚动后候选集跟随；`Tab`/`Shift+Tab`/`Enter`/`Esc` 行为与 slot mode 一致；
  二级判定拒绝时给出具体原因且模式不退出。
- 右键 action 执行后，对应 section 的目标等于所选符号且处于钉住态，未新开中央标签页。
- 空态候选可点且点击后成为目标；正文在 stale 时仍显示上次有效内容。
- 既有基线 106/107 不变；`context_workspace_test` 四缩放全过。

## 第 1 步实施记录（0.29.0）

- `LiveInsightsContextView` 在固定 kind 且宿主提供了 `ToolContextSource` 时，把正文换成真正的
  `LiveInsightToolPage`（与 Full View 同一套渲染），summary QLabel 隐藏保留；多 kind 视图（Peek 用的
  五合一概览板）不变，仍是摘要板。没有宿主上下文来源时（单元测试、无 MainWindow）也保持摘要卡，
  不静默创建一个渲染不出东西的 surface。
- 上下文用拉取模型：宿主装一次 `std::function<LiveInsightToolContext()>`，刷新时机沿用既有
  `LiveInsightSession` 快照信号，因此 Follow Editor 关闭时 surface 与摘要卡一起冻结，没有新增推送通道。
- surface 懒创建：section 折叠或隐藏时只记账不构造 workbench，`showEvent` 时补渲染。
- **既有缺陷修复**：`compact_layout_test` 新增的 surface 审计暴露出 `insightWorkbenchSurface.2`
  在 220/270/340/480 四个宽度下一律要 934 px 高，而 section 只有 640 px。探测结果：
  `rtlInsightsGraphPanel` 是 `QStackedWidget` 的**非当前页**，它内部的 `CompactToolbar` 在宽度还是
  100 px 时算出换行高度 694 px 并 `setFixedHeight` 钉死，而 QStackedLayout 的最小高度取所有页的最大值。
  修法：`InsightViewSurface` 对 dock 和 stack 改用 `QSizePolicy::Ignored` 纵向策略并 `setMinimumHeight(0)`，
  与它原本就有的横向 `Ignored` + `setMinimumWidth(0)` 对称。surface 最小高度 1022 → 103。
  该缺陷在中央标签页里不可见（标签页足够高），只有嵌进侧栏才暴露。
- 覆盖：`live_insights_context_provider_test::fixedKindSectionRendersRealSurface`（无宿主保持摘要卡、
  有宿主创建 surface 且摘要隐藏、快照刷新复用同一 surface 实例、Follow 关闭后不再拉取上下文）；
  `compact_layout_test::contextWidths` 增加 withSurface 维度，四个宽度下 overlap/outside/clipped 仍全为 0。

## 第 5 步实施记录（0.29.0）

- **状态上标题条**：`ContextDockHost` 的 section header 增加一个状态标签，内容来自 view 的动态属性
  `contextStatusText` / `contextStatusTooltip`，通过 `QEvent::DynamicPropertyChange` 事件过滤器实时刷新
  （顺带让 `contextDisplayTitle` 的变化也能立即反映到标题）。属性为空的 view 不显示任何 chip，
  所以非 insight 的 section 完全不受影响。这条通道是通用的，第 4 步的重定向可以直接复用。
- **正文瘦身**（只对固定 kind 的 section）：隐藏正文里的 section 名、`Open Full View`（标题条已有
  "Open in main area"）、单 kind 卡片（按钮 + 状态 chip），并把 Follow Editor 与 Pin 合并成一行；
  `RtlInsightWorkbench::setCompactChrome` / `LiveInsightToolPage::setCompactChrome` 关掉 workbench 自带的
  标题行与 Detach 按钮（section 本身就能拖出去变浮层，两套语义并存只会让人困惑）。Wave 的 Refresh 保留，
  因为没有别的地方提供手动刷新。多 kind 概览视图不受影响，相关既有断言一条未改。
- **空态**：`contextHasTarget` 按 kind 判定（Kernel/Hotspot 要信号，Module/State 要模块，Wave 要文件），
  没有目标时正文显示可点候选而不是一句话，也不去构造一个只会画空框的 workbench。候选 =
  「当前没在跟随的编辑器目标」+「该 section 最近三个目标」。点选 → 写入 target override、关闭 Follow
  Editor、重渲染；重新勾选 Follow 即回到跟随。
  **边界**：设计里写的"当前模块的 FSM"需要语义枚举，留到第 3 步和两级候选判定一起做。
- 效果（`build/evidence/insight-surface-step5/`，与 `insight-surface-0.29.0/` 对比）：480 px 宽下正文内容区
  起点从约 440 px 抬到约 330 px；340 px 宽下从约 580 px 抬到约 365 px。
- 覆盖：`live_insights_context_provider_test` 的 `fixedKindSectionPublishesStatusAndTrimsChrome`
  （卡片/全视图入口隐藏、Follow 与 Pin 仍在、provenance 进 tooltip）与
  `emptyFixedKindSectionOffersReachableTargets`（失去目标后出空态、候选恰好一条、点选后钉住且关闭 Follow）；
  `context_workspace_test::verifySectionStatusFromView`（无发布不显示、发布后显示、清空后隐藏），四缩放各 188 checks；
  `compact_layout_test::contextWidths` 四宽度 × 有无 surface 仍全部 0/0/0。

## 第 4 步实施记录（0.29.0）

- 入口链路原本就是 右键 action → `EditorCoordinator` → `SemanticPanelRefreshCoordinator::liveInsightOpenHandler`
  → `MainWindow::openLiveInsightFromSourceAction`，所以本步只改最后一环：
  - 去掉「先 Floating 再 Docked」的双开，改为单次 Docked/Kept 打开 + `setDockVisible(true)` +
    `focusResource`，因此侧栏被隐藏或 rail 被藏起来时右键同样能把它带回来。
  - 用第 5 步建好的 `applyTargetCandidate` 把 section 钉到该符号（含 member access path），
    并把资源初始状态里的 `followEditor` 由 true 改为 false——从源码选中的符号是目标，不是订阅。
  - **不再调用 `openLiveInsightFullView`**，中央标签页只剩 section 标题条上的入口这一条路径。
  - 取视图新增 `ContextWorkspaceController::viewForResource`，dock 与浮层都能找到，
    因此 section 被拖成浮层后右键依然命中同一个实例。
- **既有断言的语义替换**（不是放宽）：`gui_smoke_test` 里
  「State Transition command opens the full state canvas」「kernel hotspot module and state commands share
  Live Insights」「Wave command opens the right provider and full tool page」原本要求中央标签页存在，
  与本期语义直接冲突，改为要求「section 被钉住（followEditor 为 false）、surface 存在、
  且对应 `live-insight:*` 标签页**不存在**」。原来只对标签页做的渲染断言改为对**侧栏 surface** 做：
  state 图 `graphModeForTest()=="state-transition"` 且节点数 > 0，module 图节点数 > 1 且嵌套堆叠可读——
  比原断言更强，因为它证明侧栏里画出了真图而不只是标签页被创建。
  另新增「Section header still opens the full Wave tool page」：点 section 标题条的 `contextDockFullView`
  按钮后标签页出现且 wave coordinator 存在，`detachToWindow` 的既有断言接在其后，覆盖未丢。
- 同类替换还有一处：`expose_signal_to_top_gui_test` 的
  「context Insight executes through Registry into Live Insights」原来按 objectName
  `liveInsightToolPage.kernel` 找中央标签页，改为找侧栏 surface `liveInsightSurface_kernel`，
  并**新增**两条原来没有的断言：中央标签页不存在、该 section 处于钉住状态（`followEditor()` 为 false）。
- 实测证据 `build/evidence/insight-step4-review/state.png`：在真实 fixture 工作区里右键 `state_d` 后，
  侧栏 section 直接显示 FSM（Signal/Current/Next 已填、转移表 C0 IDLE→RUN、C1 RUN→IDLE）。
  同时暴露一个**待定问题**：多 section 并存时默认高度偏小，图画布被工具栏和表格挤到很小
  （见 block.png）。按第 1 条"尺寸由用户自行调整"这不算缺陷，但给 insight section 一个更大的
  默认/建议高度会明显改善第一印象，留待用户决定是否单独做一版。

## 待做

无。第 3 步与 section 建议高度都已实施，记录见下。

## section 建议高度实施记录（0.29.0）

- **设计文件原来的前提是错的，先纠正**：这里曾写「`ContextViewCapabilities` 目前只有
  `minimumWidth/preferredWidth/maximumWidth`，补 `preferredHeight`」。实际上 `src/ui/contextresource.h:20`
  的 `ContextViewCapabilities` 一直有 `minimumHeight/preferredHeight/maximumHeight`，而且 `preferredHeight`
  已经被 `preferredSize()` 占用——peek 高度（`contextworkspacecontroller.cpp:899/1256`）与浮窗初始尺寸
  （`contextworkspacefloating.cpp:26`）都读它。复用它会把浮窗/peek 尺寸和侧栏 section 高度耦死。
- 因此新增的是**显式 opt-in 的第二个字段** `preferredSectionHeight`，默认 **0 = 保持 dock 自己均分 viewport
  的既有行为**，只有 `LiveInsightsContextProvider` 声明它（`suggestedSectionHeight()` = 560）。
  temporaryeditor / pinloom / workspacehub 三个 provider 一行未改，行为逐字节不变。
- 落点在 `ContextWorkspaceController::addDockedResource`（它手里已经有 capabilities），
  `addResource` 成功后调既有的 `setSectionHeight`，因此仍然走 `qBound(minimumSectionHeight, h, 8192)`，
  不绕过夹取。
- **踩到的坑**：状态恢复时 `contextworkspacecontroller.cpp` 对每个持久化 section 无条件调
  `setSectionHeight(key, section.height)`，而 0.28.x 存下来的 `height` 可能是 **0**（从未拖动过），
  会把刚设好的建议初值打回"均分"。改为只在 `section.height > 0` 时应用；用户真正拖过的高度照旧优先。
- **数值是测出来的，不是拍的**（`compact_layout_test::insightSectionHeights`，三个 insight section 并存，
  测焦点 section 的图画布 `insightCanvasView`/`rtlInsightsGraphView` 高度）：

  | 缩放 | 屏幕 | 均分：section/正文/画布 | 建议高度：section/正文/画布 | 画布占正文 |
  | --- | --- | --- | --- | --- |
  | 1 | 800×800 | 0 / 233 / 80 | 560 / 527 / 374 | 70% |
  | 1.25 | 640×640 | 0 / 180 / 30 | 560 / 445 / 292 | 65% |
  | 1.5 | 533×533 | 0 / 144 / 12 | 560 / 338 / 185 | 54% |
  | 2 | 400×400 | 0 / 100 / **0** | 560 / 205 / 52 | 25% |

  断言分两层：**每个缩放**都要求「画布高度严格大于均分时」且「画布 > 0」；「画布 ≥ 正文 40%」只在
  `屏幕高度 ≥ 建议高度` 时要求。理由：200% 下整块屏幕只有 400 逻辑像素，比一个 section 还矮，
  此时限制画布的是固定的 ~150 px chrome 而不是建议值，断 40% 等于断言屏幕更大。这是**缩小断言适用范围**
  而不是放宽阈值——该缩放仍被前两条覆盖，且实测值写在报告里。
- **可见的行为变化（不是缺陷，但要知情）**：`ContextDockHost::arrangeSections` 在总高超出 viewport 时
  压缩而不是滚动，且把焦点 section 排在压缩顺序最后。所以多 section 并存时变成「焦点 section 拿到大块高度、
  其余压到最小高度」，而不是原来的「人人均分、人人画不下」。证据：`build/evidence/insight-step-a/`
  的 `insight_sections_split.png` 与 `insight_sections_suggested.png`——前者三个 section 里一张图都看不见，
  后者焦点 section 里工具栏与画布都在。
- 覆盖：`context_workspace_test::verifySectionPreferredHeight`（provider 不声明时 section 高度仍为 0；
  声明时等于建议值；用户拖小后仍可更小且该值被 captureState 记录；恢复时 >0 的持久化高度优先、
  ==0 的不抹掉建议值）。

## 第 3 步实施记录（0.29.0）

- **编辑器模式**：`EditorModeId::InsightTargetPick` + `EditorModeOwner::InsightTargeting`，
  描述符 priority 92、`escapeBehavior = Cancel`、inputs 含 Escape/Tab/Backtab/Enter/Navigation/Mouse。
  `editor_mode_controller_test` 里的 `expectedModes` 穷举表**已扩展**（没有改 size 断言）。
- **控制器** `EditorInsightTargetPickController`（`src/editor/`），形状照
  `EditorTemplateSlotController` 与 `EditorSignalSelectionController`：`bind/shutdown/start/clear/
  handleKeyPress/handleMousePress/publishVisibleAnnotations`，由 `MyCodeEditorState` 持有，
  在既有的那组 bind/shutdown 旁边挂接。
- **候选只来自可见区**：发布入口挂在 `editorruntimeannotations.cpp` 已有的绘制路径上，
  与 `templateSlots` / `columnMode` 用同一个 `query.firstVisibleLine/lastVisibleLine`，所以滚动自动跟随。
  标识符用 `TSDocument::identifiersInRange(startChar, endChar)` 枚举。
  **注意该 API 按名字去重**（`tsdocument.cpp` 的 `seenNames`），所以一个名字在可见区里只闪第一处出现，
  不是每个 occurrence 都闪——这是沿用既有 API 的结果，如需逐 occurrence 闪动要另加接口。
- **一级判定（决定闪不闪）不许对每个标识符跑语义服务**：`MyCodeEditorState::insightTargetNameSet`
  每次发布**最多查一次** `SemanticIndex`（Signal 类查 `getSymbolRecords(fileName)` 后按
  `isSignalDeclaration || isPortDeclaration` 过滤；Module 类查
  `getSymbolRecordsByDeclarationKind(Module)`，模块名同时覆盖声明处与实例的类型名），
  结果按（类别, 文件名, 语义快照 revision）缓存，之后每个标识符只做哈希查找。
  刻意**没有**复用 `resolveSignalSelectionCandidate`：它对单点调 `DefinitionService::resolveDefinition`，
  逐可见标识符调用等于每个符号一次语义往返。
- **Wave 的 scope 候选**是 scope 头而不是标识符：对可见区每一行行首调
  `TSDocument::alwaysScopeTarget` / `moduleScopeTarget`，只保留 `startLine` 本身也在可见区内的结果，
  按 `startChar` 去重。所以头部滚出屏幕的 always 块不是候选——与"只在可见区拾取"一致。
- **闪动**：新增 `EditorAnnotationKind::InsightTarget`，`placement = Overlay`，`phaseVisible` 由 500 ms
  `QTimer` 翻转，与 slot mode 同一套。**两处必须同步改否则静默失效**：
  `EditorAnnotation::isValid()` 的 `textRequired`（本 kind 不带 text，否则注解会被当无效丢弃）与
  `AnnotationLayer::defaultPriority()`（否则优先级掉到 1）。绘制走
  `paintInsightTargetAnnotation`，几何与 slot mode 共用抽出来的 `overlayHighlightRect`，
  颜色取 `InsightVisualStyle::theme().semantic.kernel/port`，**与 slot mode 的 read/accent 不同色**，
  两个闪动模式不会看成同一个。
- **键鼠**：`Tab`/`Shift+Tab` 按 `startChar` 顺序循环（索引以控制器自己的为准，不从光标反推——
  slot mode 在这点上踩过坑），`Enter` 选定，`Esc` 退出，左键点在候选上直接选定；
  点在别处不消费事件，模式继续。Tab 的所有权与 slot mode 一样在 `handleKeyPress` 里前置，
  免得被补全/源码导航抢走。
- **二级判定**：校验器由调用方注入。State 走
  `StateTransitionGraphService::buildStateTransitionGraph`，失败时把
  `notFoundReasonDisplayName` 原文作为原因显示，**模式保持活跃、候选集不变**，用户可以接着选下一个。
  Kernel/Hotspot/Module/Wave **今天没有二级判定**（代码注释写明），不假装做了。
- **落点**：复用 `applyTargetCandidate`。**`TargetCandidate` 必须扩 scope 字段**——它原本只有
  module/signal/accessPath，而 Wave 的目标是 scope（`scopeLabel/scopeStartPosition/scopeEndPosition/
  scopeStartLineZeroBased`）；不扩的话 Wave 的拾取结果会被 `effectiveContext()` 静默丢弃，
  用户选了等于没选。新字段同时纳入 `operator==`，否则 recentTargets 会把不同 scope 去重成一个。
- **入口**：沿用第 5 步那条通用属性通道再往前一步——view 发布 `contextScopeText`/`contextScopeTooltip`，
  `ContextDockHost` 在 section 标题条渲染一个可点的 `contextSectionScope` 按钮，点击时
  `QMetaObject::invokeMethod(view, "requestScopePick")`。host 保持通用：属性为空的 view 不显示 chip，
  没有该槽的 view 点了也只是无事发生。空态里另有一个「Pick in editor…」按钮走同一条路。
  **已知边界**：section 被拖成浮层后没有 section 标题条，因此浮层里只有空态按钮与右键菜单这两个入口；
  没有为此改浮层几何或新增浮层 header 机制。
- **跨文件不在本模式内处理**（设计原文如此）：拾取只看当前编辑器的可见区，目标在别的文件时走右键或 Full View。
- 覆盖：新测试 `editor_insight_target_pick_test`（可见区限定 + 滚动跟随 + 三个类别的一级判定 +
  注解有效性与 active 唯一性 + 500 ms 相位翻转 + Tab/Shift+Tab/Enter/Esc + 二级拒绝时原因原文且不退出）；
  `live_insights_context_provider_test::pickedTargetReachesTheSurfaceAndTheSectionHeader`
  （chip 可见且显示当前 scope、点击触发宿主请求、回填后 surface 的 context 里 scope 字段真的变了、
  Follow 被关掉、无宿主的 section 不显示 chip）。

### 第 3 步的两处补充记录

- **文件名匹配**：一级判定按当前文件取信号/端口记录时，`SemanticIndex::getSymbolRecords(fileName)` 是按
  **字面文件名**索引的，而编辑器的 `identity.current()` 与记录里的 `location.fileName` 未必是同一种拼写
  （`resolveSignalSelectionCandidate` 用 `EditorFileIdentity::same` 比较这两者，就是这个原因）。
  因此精确查询返回空时再用 `EditorFileIdentity::same` 扫一遍快照兜底，结果同样按快照 revision 缓存，
  不会每帧重扫。
- **`editorruntime.cpp` 有 5000 行的架构护栏**（`controller_boundary_test`）。本期的编辑器模式方法一度把它
  顶到 5046 行。**没有抬高阈值**，而是把这组方法搬到 `editorruntimecommands.cpp`（它本来就持有模式相关的
  命令实现），`editorruntime.cpp` 回到 4959 行，护栏原样通过。
- **gui_smoke 覆盖到哪一步**：`gui_smoke_test` 里新增四条断言，验证真实 MainWindow 链路——标题条 chip 显示
  被钉住的目标、点击后编辑器进入拾取模式、拾取枚举的行区间等于编辑器当前可见区间、取消后模式消失。
  **不在这里断言"闪了哪些符号"**：该夹具打开的文档在语义快照里没有记录，候选本来就应该是空的；
  候选内容由 `editor_insight_target_pick_test` 用已知快照覆盖。
- 证据：`build/evidence/insight-step3-review/pick_mode_blink.png`（真实 `MyCodeEditor` 上的闪动叠层：
  `clk` / `rst_n` / `done` / `state_q` / `other_q` 被高亮，当前候选 `clk` 填充更重；`pick_child`、
  `child_inst` 与关键字不是候选。注意同名标识符只闪可见区里的第一处，这是 `identifiersInRange` 去重的结果）；
  `pick_mode.png`（真实工作区里点 chip 后编辑器进入拾取模式，该文档无语义记录所以没有候选）。

## 0.29.0 收尾后的测试基线

- 全量 CTest **107/108**（本期新增 1 个测试目标 `editor_insight_target_pick_test`，所以从 106/107 变成
  107/108）。唯一失败仍是既知的 `workspace_persistence_test`，失败范围仍恰好是
  `real new and huge_prj legacy fixtures import` 与
  `real legacy fixtures remain byte and timestamp identical` 两条（27/29 通过），原因是两个 fixture 未纳入
  版本管理，用户已决定不提交。
- `context_workspace_test` 四个缩放各 **197 checks**（第 5 步时是 188，本期 +9 条 section 高度断言）。
- `compact_layout_test::contextWidths` 四宽度 × 有无 surface 共 8 组，overlap/outside/clipped 全为 **0/0/0**
  （`build/evidence/insight-step-a/context_widths.txt`）。
- `editor_insight_target_pick_test` **17/17**。
- **环境自伤提醒**：中途有一次 reconfigure 用了 `PATH="...:$PATH"`，把 Git 的 `usr/bin` 烤进了
  `ZEROSLACK_TEST_PATH`，`wave_simulation_configuration_test` 随即整体失败（MSYS 的 `tar` 接管了
  `tar C:\...`）。用纯 Windows PATH 重新 configure 后恢复。核查命令：
  `grep -c Git build/<dir>/CTestTestfile.cmake` 必须为 0。
