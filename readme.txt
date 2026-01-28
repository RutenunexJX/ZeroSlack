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
未来计划 (Plan / TODO)
==========================================================================

（以下是对代码中现有 Plan 的整理与扩展）

已完成：
1. 使用 `QCompleter + 自定义 QAbstractItemModel` 重写补全框架（`CompletionModel`）
2. 使用智能指针管理 `MainWindow` 中的各类 Manager，减少手动 `delete`
3. 引入增量符号分析，只重新分析有改动的文本片段/文件
4. 分离职责：
   - 输入/模式处理：`ModeManager`
   - 文本编辑：`MyCodeEditor`
   - 符号解析：`SymbolAnalyzer` + `sym_list`
   - 补全逻辑：`CompletionManager`
   - 导航：`NavigationManager` + `NavigationWidget`
   - 关系分析：`SymbolRelationshipEngine` + `SmartRelationshipBuilder`

计划中 / 待完成：
1. UI/交互层面进一步解耦 `QTabWidget`：
   - 逐步向 `QTabBar + QStackedWidget` 结构迁移，提高灵活性
2. 完善配置系统：
   - 将命令前缀、快捷键和模式行为放入可编辑配置（文件或 UI）
3. 补全文档与示例工程：
   - 提供典型 SystemVerilog 工程样例，展示导航与关系分析效果


==========================================================================
备注 (Notes)
==========================================================================

- 本文件是面向“阅读/维护代码的人”的说明文档，侧重介绍目前仓库中已经实现的架构与能力。
- 如果你在阅读代码时发现 README 与实际实现不一致，以**代码实现**为准，再回过头来更新本文件即可。
