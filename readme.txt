==========================================================================
项目简介 (Project Overview)
==========================================================================

ZeroSlack 是一个面向 SystemVerilog 的轻量级代码编辑器 / 浏览器，基于 Qt 开发。
它在普通文本编辑器的基础上，重点增强了以下能力：

- SystemVerilog 语法高亮与多标签编辑
- 模块/变量/任务/函数等符号的实时解析与索引
- 自定义命令驱动的智能补全系统
- 工作区级别的批量符号分析与关系分析
- Ctrl+单击 跳转到定义、导航面板浏览符号

适合用作：浏览/理解中大型 SystemVerilog 工程、快速跳转与补全、做一些“静态 IDE”级别的体验。


==========================================================================
核心功能 (Core Features)
==========================================================================

【编辑器基础】
- 多标签页文本编辑 (`TabManager`)
  - 新建 / 打开 / 保存 / 另存为
  - 未保存文件关闭时会弹出确认
- SystemVerilog 语法高亮 (`MyHighlighter`)
- 行号栏 (`LineNumberWidget`)
  - 显示行号
  - 点击行号可将光标跳转到对应行
- 基本编辑操作
  - Copy / Cut / Paste / Undo / Redo


【三种工作模式 (`ModeManager`)】
- Normal Mode（普通模式）
  - 标准文本编辑
  - 智能补全始终可用
- Command Mode（命令模式）
  - 通过特定前缀进入，针对不同符号类型给出补全
  - 示例前缀（在行首输入）：
    - `r `：reg 变量
    - `w `：wire 变量
    - `l `：logic 变量
    - `m `：module
    - `t `：task
    - `f `：function
    - 以及扩展的：`i ` (interface), `s ` (struct type), `e ` (enum type), `p ` (parameter) 等
- Alternate Mode（替代模式 / 命令行模式）
  - 仅接受命令，不编辑正文
  - 支持命令：`save` / `save_as` / `open` / `new` / `copy` / `paste` / `cut` /
    `undo` / `redo` / `select_all` / `comment` 等
  - 命令输入时会通过同一套补全弹窗展示和选择

【模式切换】
- 通过对 Shift 键的双击检测在 Normal / Alternate 模式间切换
- 不同模式下 Tab 外观颜色可区分状态
- 按键事件统一由 `ModeManager` 处理，`MainWindow` 和 `MyCodeEditor` 都会转发按键


==========================================================================
智能补全系统 (Advanced Autocompletion)
==========================================================================

核心组件：`CompletionManager` + `CompletionModel` + `QCompleter`

- 支持缩写与模糊匹配
  - 例如：`vti` 可以匹配 `var_temp_in_tempModule`
- 评分规则
  - 更偏向前缀匹配
  - 更偏向单词边界 / 连续字符匹配
- 上下文感知
  - 根据当前模式（Normal / Command / Alternate）和符号类型调整候选列表
  - 在 Command Mode 下，列表会分组显示不同类型符号
- 视口自适应
  - 弹出框大小会跟随内容动态调整
  - 能根据窗口边界调整出现位置

在 `MyCodeEditor` 中：
- 文本变化会启动一个 0ms 的定时器，集中触发补全逻辑
- 会根据光标所在模块、在注释内与否等条件筛选候选
- 命令模式下会对整行命令区域进行高亮（深色背景 + 白字）


==========================================================================
符号分析系统 (Symbol Analysis System)
==========================================================================

核心组件：`SymbolAnalyzer` + `sym_list`（符号数据库）+ `CompletionManager`

- 支持解析的 SystemVerilog 符号包括但不限于：
  - `module` / `endmodule`
  - `reg` / `wire` / `logic` 变量
  - `task` / `function`
  - `interface` / `struct` / `enum` / `parameter` 等扩展类型
- 具备注释感知能力
  - 通过符号数据库中的注释范围表，避免解析注释中的符号

分析模式：
- 打开标签分析：`analyzeOpenTabs`
  - 对当前所有打开的编辑器进行分析
  - 使用 `setCodeEditor` 将每个编辑器的内容送入符号解析器
- 工作区分析：`analyzeWorkspace`
  - 通过 `WorkspaceManager` 拿到整个目录树中的 `.sv/.v/.vh/.svh/.vp/.svp` 文件
  - 使用隐藏的后台 `MyCodeEditor` 加载并解析，每个文件单独送入 `setCodeEditorIncremental`
- 单文件分析：`analyzeFile`
  - 供文件变化回调 (`fileChanged`) 调用

增量分析：
- `scheduleIncrementalAnalysis` / `scheduleSignificantAnalysis`
  - 根据是否包含关键字（如 `module` / `task` / `function` 等）决定延时
  - 避免每次按键都触发全量分析，减轻性能压力


==========================================================================
工作区与导航 (Workspace & Navigation)
==========================================================================

【Workspace (`WorkspaceManager`)】
- 支持选择一个目录作为“工作区”
- 递归扫描 SystemVerilog 文件
- 监听文件变动并自动重新分析（符号 + 关系）

【导航系统 (`NavigationManager` + `NavigationWidget`)】
- 主窗口左侧有“导航 Dock 窗口”
  - 可以通过某些快捷键或模式切换来显示/隐藏
- 能根据当前文件/当前符号更新导航视图
- 支持两种跳转方式：
  - 符号导航：由 `NavigationManager::symbolNavigationRequested` 触发
  - 文件+行号导航：`MainWindow::navigateToFileAndLine`
- 行号栏点击：快速把光标跳转到某一行

【定义跳转（Ctrl+Click）】
- 在 `MyCodeEditor` 中：
  - 按住 Ctrl 并将鼠标移动到标识符上时：
    - 光标变成手型
    - 标识符高亮为蓝色下划线
    - 可选地弹出 Tooltip 展示定义位置等信息
  - Ctrl+左键可跳转到符号定义
    - 优先跳当前文件中的定义
    - 再考虑其他文件中的模块定义
  - 跳转过程会复用 `NavigationManager` 的符号导航接口


==========================================================================
符号关系系统 (Symbol Relationship System)
==========================================================================

核心组件：
- `SymbolRelationshipEngine`
- `SmartRelationshipBuilder`
- `RelationshipProgressDialog`

功能概览：
- 在工作区分析完成后，进一步分析符号之间的关系，例如：
  - 模块实例化关系
  - 变量赋值 / 驱动关系
  - 任务 / 函数调用关系
- 结果会回写到：
  - `SymbolRelationshipEngine` 中（作为统一关系存储）
  - `CompletionManager` 中（用于关系感知型补全）

分析流程（简化）：
1. 打开 Workspace
   - 先触发符号批量分析 (`SymbolAnalyzer::analyzeWorkspace`)
   - 然后由 `SmartRelationshipBuilder` 对所有 SV 文件进行逐个关系分析
2. `RelationshipProgressDialog`
   - 显示两阶段进度：
     - 阶段 1：符号分析
     - 阶段 2：关系分析
   - 展示当前正在处理的文件、已完成数量、进度条等
   - 支持“取消”分析，触发 `analysisCancelled`
3. 分析结果
   - `analysisCompleted(fileName, relationshipsFound)` 按文件汇报
   - 所有文件处理后，会自动关闭进度对话框并在状态栏给出汇总

其它：
- 所有新增/清空关系会通知 `CompletionManager` 刷新内部缓存
- 导航面板可以基于最新的关系数据刷新视图


==========================================================================
构建与运行 (Build & Run)
==========================================================================

【依赖】
- Qt 5/6（建议在与你当前 `.pro` 文件兼容的版本上构建）
- 支持 C++11 及以上的编译器（项目大量使用 `std::unique_ptr` 等）

【构建步骤（命令行示例）】
1. 打开 Qt 提供的命令行环境（如：`Qt x.y.z (MSVC/MinGW) Command Prompt`）
2. 进入工程目录：
   - `cd /path/to/ZeroSlack`
3. 生成 Makefile：
   - `qmake demo.pro`
4. 编译：
   - `make` 或在 Windows 上使用 `nmake` / `jom`
5. 运行生成的可执行文件：
   - `./demo`（或对应平台下的 `.exe`）

【在 Qt Creator 中打开】
1. 打开 Qt Creator
2. 选择“打开项目”，选中 `demo.pro`
3. 按向导配置 Kit 后，直接“构建并运行”


==========================================================================
性能优化方案 (Performance Optimization Plan)
==========================================================================

【合理性分析】

本方案从异步与并发、扫描与数据流、索引与缓存、UI 与内存四个维度提出优化，
与当前代码结构一一对应，具备可实施性：

1. 异步与并发
   - 代码中 `SmartRelationshipBuilder::analyzeFile` 在 MainWindow 的文件变化/
     工作区分析回调中同步调用，大文件或批量文件时易阻塞主线程。迁移至
     QtConcurrent::run 或独立分析线程，并用 QFutureWatcher 回传结果，可显著
     改善界面响应。
   - `MyCodeEditor::onTextChanged` 已对接 SymbolAnalyzer 的 scheduleIncrementalAnalysis，
     但未对关系分析做去抖或取消：连续输入时若触发关系分析，仍可能堆积任务。
     在 onTextChanged 中增加对关系分析任务的延迟/取消逻辑是合理的。

2. 扫描与数据流
   - SymbolAnalyzer 与 sym_list 对同一文本存在多遍扫描（module/assign/变量/任务等），
     且 sym_list 中大量使用 QRegExp。在一次遍历中合并符号提取与基础关系（如
     CONTAINS）可减少重复匹配与内存访问。
   - SmartRelationshipBuilder 与 syminfo 中广泛使用已弃用的 QRegExp；Qt5/6 推荐
     QRegularExpression，其 JIT 与优化匹配对长文本更友好，迁移具有明确收益。
   - WorkspaceManager::scanDirectory 使用 QDirIterator 逐文件收集路径，得到的是
     QStringList，并非“一次性将数百个文件内容全部加载进内存”；若优化目标是大
     目录下的“后续分析阶段”一次性加载过多文件内容，则应在 SymbolAnalyzer::
     analyzeWorkspace 或调用方做流式/分批加载与解析，避免同时打开大量编辑器
     或一次性读入全部文件内容。

3. 索引与缓存
   - sym_list 已维护 symbolNameIndex（QHash<QString, QList<int>>），findSymbolsByName
     已是 O(1) 索引+小列表遍历。可进一步在 sym_list 中提供 findSymbolIdByName，
     直接返回首个 symbolId，避免调用方（CompletionManager、SmartRelationshipBuilder）
     反复构建 QList<SymbolInfo> 仅为了取 ID，降低分配与拷贝。
   - SymbolRelationshipEngine 已有 queryCache（QHash）与 cacheValid；当前在每次
     addRelationship/removeRelationship/removeAllRelationships 等写操作时调用
     invalidateCache()，导致 queryCache.clear()。方案中“仅在文件真正保存
     (fileSaved) 时失效相关缓存”需细化：若仍保持“任意关系变更即失效”，则
     可改为按 (symbolId, file) 或按文件粒度失效，避免全局清空，减少重复计算。
   - analyzeFileIncremental（SmartRelationshipBuilder）当前实现仍调用 analyzeFile
     全量重算；SymbolAnalyzer/sym_list 侧已有按行增量（setCodeEditorIncremental、
     detectChangedLines）。关系侧可改为仅对变更行或受影响模块做关系更新，而非
     整文件 analyzeFile。

4. UI 与内存
   - CompletionModel 使用 QList<CompletionItem> 且无分页；候选过多时（如 >500）
     会一次性填充模型并展示，易造成弹窗卡顿。对候选数量做上限或分页加载、
     延迟渲染，可提升补全弹窗响应。
   - NavigationWidget 通过 updateFileHierarchy/updateModuleHierarchy/updateSymbolHierarchy
     全量刷新三棵树（populateFileTree/populateModuleTree/populateSymbolTree）。
     仅当当前文件或当前符号相关数据变化时做局部更新（如只刷新当前文件节点、
     或只更新受影响的模块/符号节点），可减少大工程下的 UI 卡顿。

【详细实施方案】

一、异步化与并发 (Concurrency)

  [x] 迁移至后台线程分析（已实现）
      - SmartRelationshipBuilder 新增 computeRelationships(fileName, content, fileSymbols)，
        在后台线程中仅计算关系并返回 QVector<RelationshipToAdd>，不写引擎。
      - MainWindow 使用 QtConcurrent::run + QFutureWatcher 提交单文件/批量关系分析任务，
        在 finished() 中于主线程将结果写回 SymbolRelationshipEngine 并更新状态栏与进度对话框。
      - 单文件（新标签、保存、fileChanged）与工作区批量分析均改为异步，支持取消未完成任务。
      - sym_list 的 findSymbolsByName / findSymbolsByFileName / getSymbolById 已加读锁，供后台安全读取。

  [x] 任务去抖 (Debouncing)
      - 在 MyCodeEditor::onTextChanged 中，除现有 SymbolAnalyzer 的
        scheduleIncrementalAnalysis 定时器外，增加对“关系分析”的延迟触发
        或取消逻辑。
      - 若存在“单文件关系分析”的定时器，在连续输入时重置该定时器，并在
        新分析启动时取消上一次未完成的关系分析任务（若有 QFuture 则
        cancel/waitForFinished 或置取消标志）。
      - 已实现：MyCodeEditor 使用 relationshipAnalysisDebounceTimer（2 秒），
        连续输入时重置；定时到时调用 MainWindow::requestSingleFileRelationshipAnalysis，
        其内部会 cancel 未完成的 QFuture 再提交新任务；tabCreated/fileSaved/fileChanged
        均改为调用该接口，统一取消逻辑。

  [x] 单文件关系分析稳定性
      - main.cpp 中在创建 MainWindow 前调用
        qRegisterMetaType<SymbolRelationshipEngine::RelationType>(...)，使
        relationshipAdded 信号在跨线程/队列传递时可用，避免
        "Cannot queue arguments of type 'RelationType'" 导致崩溃。
      - requestSingleFileRelationshipAnalysis 串行化：当 fileSaved、fileChanged、
        去抖定时器同时触发时，先 cancel 当前 future 并 waitForFinished() 再
        setFuture(newFuture)，避免快速连续 setFuture 导致崩溃；addRelationship
        时跳过 fromId/toId < 0 的无效条目。

二、扫描算法与数据流优化 (Efficiency)

  [x] 合并符号与关系扫描（已实现）
      - sym_list 新增 extractSymbolsAndContainsOnePass(text)：在一次按位置
        顺序的遍历中，同时匹配 module/endmodule/reg/wire/logic/task/function，
        提取符号并立即建立 CONTAINS 关系（维护当前模块栈，遇 module 入栈、
        endmodule 出栈，非模块符号添加时若栈非空则 addRelationship(CONTAINS)）。
      - 使用 findNextStructuralMatch(text, startPos, structRanges) 从 startPos
        起找下一个“结构”匹配（多正则取最早且不在注释/struct 内），减少对同一
        文本的多遍扫描；logic 仍排除 struct 内部。
      - setCodeEditor / setCodeEditorIncremental（首次）改为：buildCommentRegions
        → extractSymbolsAndContainsOnePass → getAdditionalSymbols → buildSymbolRelationships；
        行级增量仍使用原有 analyzeSpecificLines 等逻辑。

  [x] 正则表达式预编译与迁移（已实现）
      - SmartRelationshipBuilder：AnalysisPatterns 内 QRegExp 已全部替换为
        QRegularExpression，类初始化时构造一次、重复使用。
      - sym_list、MyCodeEditor、CompletionManager、MyHighlighter、SymbolAnalyzer
        中解析/匹配路径已改为 QRegularExpression；捕获组与 API 已逐处适配。

  [ ] 工作区与批量分析的流式/分批读取
      - 在 WorkspaceManager 或调用方（如 SymbolAnalyzer::analyzeWorkspace）：
        不对整个目录一次性“加载所有文件内容到内存”，改为按批次（如每批
        50～100 个文件）读取并分析，分析完一批再处理下一批，或使用队列+
        工作线程流式消费，以控制内存峰值与主线程占用。

三、索引与缓存 (Indexing/Caching)

  [ ] 符号 ID 快速查找
      - sym_list 已具备 symbolNameIndex；在 sym_list 中新增 findSymbolIdByName(
        const QString& symbolName)，利用 symbolNameIndex 直接返回首个匹配
        的 symbolId（若存在），否则返回 -1。
      - CompletionManager::findSymbolIdByName 与 SmartRelationshipBuilder::
        findSymbolIdByName 改为在内部调用 sym_list::findSymbolIdByName（或
        等价地使用 sym_list 的索引），避免先取 QList<SymbolInfo> 再取
        first().symbolId，减少临时列表分配。

  [ ] 关系引擎查询缓存策略
      - 当前 SymbolRelationshipEngine 在每次 add/removeRelationship 等写操作
        时调用 invalidateCache() 并 queryCache.clear()。
      - 改为按“影响范围”失效：例如仅当某 symbolId 或某文件对应的关系被
        修改时，清除与该 symbolId（或该文件相关 symbolId）相关的
        queryCache 条目，而不是全局 clear；或在明确“文件保存”时再失效
        与该文件相关的缓存条目，具体策略可与 fileSaved 事件绑定。

  [ ] 增量分析优化
      - SmartRelationshipBuilder::analyzeFileIncremental 当前委托给 analyzeFile，
        未真正按变更行增量。改进为：根据 changedLines 或 SymbolAnalyzer/sym_list
        提供的变更信息，只对受影响模块/块重新执行关系提取与更新，并调用
        SymbolRelationshipEngine 的增量更新接口（若有）或先移除该文件旧关系
        再仅写入受影响部分，避免全文件重扫。

四、UI 渲染与内存 (UI/UX Performance)

  [ ] 补全列表延迟渲染与数量限制
      - 在 CompletionModel 或填充补全列表的上层逻辑中：当候选数量超过阈值
        （如 500）时，采用分页加载或只取前 N 条用于显示，并可选地提供
        “显示更多”或滚动加载；或限制单次展示的最大条数，其余仅按需加载。
      - 避免单次 setCompletions(…) 传入过大的 QList，防止补全弹窗首次打开
        时卡顿。

  [ ] 导航树局部更新
      - 在 NavigationManager/NavigationWidget 中，区分“当前活跃文件/当前
        符号”与“全局树数据”。仅当当前文件或与当前符号相关的节点发生
        变化时，刷新对应子树（如只调用 populateModuleTree 中与当前文件相关
        的部分，或只更新 symbolTree 中受影响的类型/节点），而不是每次
        数据变更都全量调用 updateFileHierarchy/updateModuleHierarchy/
        updateSymbolHierarchy 并重绘整棵树。

==========================================================================
性能信息打印 (Performance Logging)
==========================================================================

为方便对比优化前后性能，在以下路径使用 QElapsedTimer + qDebug 输出耗时与规模：

【sym_list】
- setCodeEditor：输出 file / chars / lines、commentRegions / onePass / additional /
  buildRels 各阶段微秒、总微秒、本文件符号数。标签：[Perf] setCodeEditor
- setCodeEditorIncremental(first)：首次全量分析，格式同上。标签：[Perf] setCodeEditorIncremental(first)
- setCodeEditorIncremental(delta)：行级增量，输出 file / changedLines、analyzeLines /
  buildRels / total 微秒。标签：[Perf] setCodeEditorIncremental(delta)

【SymbolAnalyzer】
- analyzeOpenTabs：输出 files / totalMs / symbols。标签：[Perf] analyzeOpenTabs
- analyzeWorkspace：输出 path / files / totalMs / symbols / avgMsPerFile。标签：[Perf] analyzeWorkspace
- analyzeFile：输出 file / ms / symbols。标签：[Perf] analyzeFile
- analyzeEditor：输出 incremental|full、file / ms / symbols。标签：[Perf] analyzeEditor

运行程序时在控制台（或 Qt Creator 的“应用程序输出”）中搜索 “[Perf]” 即可查看上述日志，
便于对比单文件/工作区分析及各阶段耗时是否随优化而改善。

【性能结论（参考 debug.md 类日志）】
- 合格。commentRegions / onePass / additional 已较快；主要耗时在 buildRels（关系构建）。
- 工作区首次分析约 10–12s / 28 文件、约 800 符号属可接受；单文件大文件（如 8 万字符）单次 1.5–2.2s 属预期。
- 后续可优化：关系引擎按文件/增量失效缓存、补全列表分页等，可进一步降低 buildRels 与 UI 卡顿。

==========================================================================
仅当内容影响符号时才分析 (Skip Analysis on Comment/Whitespace-Only Changes)
==========================================================================

当文本变更“明显不涉及符号”时（仅注释、空格、空行等），不再触发符号/关系分析，避免无意义重算。

【实现】
- sym_list 维护“符号相关”规范化哈希：calculateSymbolRelevantHash(content)。
  规范化方式：去掉 /* */ 块注释、整行 // 注释、空白行，再将空白压缩为单空格后做哈希。
- needsAnalysis(fileName, content)：用 symbolRelevantHash 比较，仅当“符号相关”内容变化才返回 true。
- setCodeEditor：若当前文件的 symbolRelevantHash 与已存一致，直接 return，不清符号、不分析。
- setCodeEditorIncremental：沿用 needsAnalysis，未变则整函数提前返回；分析结束后更新
  fileStates[fileName].symbolRelevantHash。

【效果】
- 不输入任何内容只保存、或只输入/删除几个空格后保存，不会触发分析。
- 仅改注释（整行 // 或 /* */ 块）、仅改空行或空白，也不会触发分析。
- 只有真正影响符号的修改（如增删改 module/reg/wire/task/function 等）才会触发分析。

==========================================================================
备注 (Notes)
==========================================================================

- 本文件是面向“阅读/维护代码的人”的说明文档，侧重介绍目前仓库中已经实现的架构与能力。
- 如果你在阅读代码时发现 README 与实际实现不一致，以**代码实现**为准，再回过头来更新本文件即可。
