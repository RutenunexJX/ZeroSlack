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
- 工作区分析：`analyzeWorkspace` / `startAnalyzeWorkspaceAsync`
  - 通过 `WorkspaceManager` 拿到整个目录树中的 `.sv/.v/.vh/.svh/.vp/.svp` 文件
  - 使用 QFile+QTextStream 读入内容，每个文件送入 `setContentIncremental`（阶段 B 轻量化，不创建 MyCodeEditor）
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

【核心问题诊断】

当前系统存在以下三个主要性能瓶颈，可能导致大工程下界面卡顿：

1. UI 线程阻塞
   - SymbolAnalyzer::analyzeWorkspace 虽已做批处理，若仍在主线程通过
     QApplication::processEvents() 运行，在处理大型工程时会导致界面响应延迟。
   - 目标：将所有解析逻辑从 UI 线程剥离，用 QtConcurrent::run 等包装扫描与
     分析循环，禁止在后台线程调用 processEvents() 或创建 QWidget。

2. 重量级对象开销
   - 若工作区分析通过 createBackgroundEditor 频繁创建 MyCodeEditor (QWidget)
     来读取文件，每个文件一个编辑器实例，内存与 CPU 初始化开销巨大。
   - 目标：废弃“临时编辑器”方式，改为 analyzeFileContent(const QString& content)
     等接口，直接对 QString 或轻量级 QTextDocument 做正则解析，避免创建
     MyCodeEditor 实例；多线程访问 sym_list 时需保证单例线程安全或后台独立
     临时表再合并。

3. 高频同步 IO 与无效重算
   - 文件保存或变更时触发的全量分析会带来频繁磁盘读取和冗余符号表/关系表构建。
   - 目标：利用 hasSignificantChanges 等逻辑跳过“仅注释/空白”变更；在关系
     引擎侧引入 beginUpdate()/endUpdate() 批量提交，在 endUpdate 前禁止
     invalidateCache()，避免每添加一条关系就清空全局缓存导致的 O(N^2) 行为。

【代办】

[x] 阶段 A — 彻底的异步化重构 (MainWindow & SymbolAnalyzer)（已完成）
  - 使用 QtConcurrent::run 包装整个工作区扫描与分析循环。
  - 禁止在后台线程调用 QApplication::processEvents() 或创建任何 QWidget。
  - 通过 batchProgress 等信号异步回传进度，仅在主线程更新 progressDialog。

[x] 阶段 B — 解析器轻量化 (SymbolAnalyzer)（已完成）
  - 废弃 createBackgroundEditor；新增 analyzeFileContent(fileName, content)，
    直接对 QString 调用 sym_list::setContentIncremental 进行正则解析。
  - analyzeWorkspace / analyzeFile 改为 QFile+QTextStream 读内容后调用
    setContentIncremental，不再创建 MyCodeEditor。
  - sym_list::getInstance() 使用静态 QMutex 保证多线程下单例创建安全；
    getAllSymbols() 使用 QReadLocker，持写锁时通过 s_holdingWriteLock 避免死锁。

[x] 阶段 C — 语义级去抖与增量策略 (SmartRelationshipBuilder / SymbolRelationshipEngine)（已完成）
  - 利用已有 hasSignificantChanges：仅当结构/定义变更时才触发关系重构。
  - 在 SymbolRelationshipEngine 中引入 beginUpdate() 与 endUpdate()，在
    endUpdate 之前不调用 invalidateCache()，批量提交后按需失效缓存。

[x] 阶段 D — 语法高亮性能优化 (MyHighlighter)（已完成）
  - 将多个关键字的多个正则合并为一个大的正则（如 \b(module|endmodule|reg|...)\b），
    并在 Qt 6.4+ 下调用 QRegularExpression::optimize()。
  - keywords.txt 改为静态缓存单例（loadKeywordsOnce + getKeywordPattern），
    避免每次实例化 MyHighlighter 都读文件；多线程下用 QMutex 保护。

==========================================================================
备注 (Notes)
==========================================================================

- 本文件是面向“阅读/维护代码的人”的说明文档，侧重介绍目前仓库中已经实现的架构与能力。
- 如果你在阅读代码时发现 README 与实际实现不一致，以**代码实现**为准，再回过头来更新本文件即可。
