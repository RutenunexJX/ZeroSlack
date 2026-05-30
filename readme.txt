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

--------------------------------------------------------------------------
当前状态 (Status)
--------------------------------------------------------------------------
- 版本：0.0.11/slang8（分支 tree_sitter_and_slang）。版本号见 version.h，运行时显示在窗口标题与状态栏
  右下角（构建时间见该标签 tooltip）。
- 进行中：**符号提取从 Tree-sitter 迁移到 Slang**（SlangManager::extractSymbols /
  extractWorkspaceSymbols → sym_list::setSymbolsForFile）。改动已通过 MinGW/Ninja 编译链接，
  尚未做完整运行实测。Tree-sitter 符号路径降级为遗留代码（仅「Tree-sitter 验证」按钮使用），
  语法高亮仍由 SVLexer 驱动。详见下文「符号分析系统」与「已知问题」。
- 已补并经无头测试验证（test_sv/dump_symbols.cpp 直接调用 SlangManager::extractSymbols 打印符号）：
  1) typedef struct/enum 于 typedef 站点产出 struct 类型符号（sym_packed_struct/sym_unpacked_struct）、
     struct 成员与枚举值（moduleScope = 类型别名）、enum/struct 变量 dataType = 类型别名；
  2) **module/interface 符号**改由 compilation.getDefinitions() 产出（此前 root.visit() 只遍历实例树，
     完全不产出 sym_module，导致模块作用域/补全全断）；
  3) **端口去重**：跳过端口背后的 net/var（PortSymbol.internalSymbol），端口不再既算 port 又算 logic/wire；
  4) 跳过顶层自动实例、按定义去重实例体（模块被多次例化时成员不重复）；
  5) function/task 形参与返回值/局部变量 moduleScope 改为所在子程序名（如 add_one），不再以模块名
     泄漏进 l/r/w 补全；scope-tree 路径不受影响，光标在子程序内仍可补全其局部符号；
  6) 内联匿名 enum/struct（无 typedef）在变量站点产出枚举值/成员，moduleScope = 变量名、dataType = 变量名，
     满足 ee / var.member 补全契约（emitEnumValues / emitStructMembers 复用于 typedef 站点与匿名变量站点）。
- 跳转修复（无头测试 test_sv/jump_test.cpp，offscreen 构造 MyCodeEditor，7 项全过）：
  a) findEndModuleLine 的 off-by-one：startLine 为 1-based，扫描需从 startLine-1 起，否则计不到本模块
     的 module 关键字、返回 -1，导致 getCurrentModuleScope 判为「无模块」（即已知问题「光标在模块内却显示无模块」）；
  b) canJumpToDefinition / jumpToDefinition：枚举值 / 结构体成员的 moduleScope 是「类型名」而非模块名，
     故这两类不按模块名作用域过滤（否则在模块内点击它们会被判不可跳转 / 跳不过去）；
  c) 本地跳转落点 off-by-one：下移 startLine-1、右移 startColumn-1，精确落在定义行/名字处；
  d) 跨文件跳转/符号导航/tooltip 行号 off-by-one：三处 startLine+1（mycodeeditor.cpp 跨文件 emit 与
     tooltip、mainwindow.cpp onSymbolNavigationRequested）在 1-based startLine 下都多跳一行；
     navigateToFileAndLine(L) 本身正确（Down×(L-1) 落 1-based 第 L 行），故改为直接传 startLine。
- 无头测试：test_sv/completion_test.cpp 驱动 CompletionManager 断言补全（含匿名 struct 成员、function 局部
  不泄漏）；test_sv/jump_test.cpp 断言跳转 9 项（跨模块隔离、enum/struct 可跳、本地落点行号、**跨文件**
  emit 出定义的 1-based startLine，用第二个文件 helper_mod.sv）。补全/跳转的“逻辑层”可脱离 GUI 自动测试
  与回归；仅弹窗渲染/鼠标 Ctrl+Click 等纯 UI 交互需 GUI 实测。
- GUI 版本号显示：version.h 定义 APP_VERSION；MainWindow 构造时写入窗口标题与状态栏常驻标签。
- 待验证/待补：注释感知（commentRegions）；单文件 elaboration 的跨文件解析；GUI 实测（弹窗交互）。


==========================================================================
核心功能 (Core Features)
==========================================================================

【编辑器基础】
- 多标签页文本编辑 (`TabManager`)
  - 新建 / 打开 / 保存 / 另存为
  - 未保存文件关闭时会弹出确认
- SystemVerilog 语法高亮 (`MyHighlighter`)
  - 基于专用词法分析器 `SVLexer`（sv_lexer.h/cpp、sv_token.h），完全移除高亮路径中的 `QRegularExpression`，避免正则回溯导致的 UI 卡顿。
  - 按行驱动：`highlightBlock` 内用 `SVLexer::nextToken()` 逐 token 推进，根据 token 类型（Keyword/Comment/Identifier/Operator/Number/String 等）调用 `setFormat`；多行块注释通过 `setState`/`getState` 跨块保持。
  - 关键字表：从资源文件 `config/keywords.txt` 加载（静态缓存），涵盖 Verilog/SystemVerilog 与预处理器关键字；Identifier 与表匹配时按关键字高亮，注释与字符串内不会误标。
  - 支持单行注释 `//`、块注释 `/* */`、双引号字符串（含 `\"` 转义）、数字、标识符、操作符（括号/分号等，TokenType::Operator）；Verilog 中单引号用于字面量（如 `1'b1`），不作为字符串高亮。
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
    - 以及扩展的：`i ` (interface), `p ` (parameter) 等；
    - enum 相关：`e `（枚举变量）, `ee `（枚举值）, `ne `（枚举类型，含 typedef enum 与匿名）；模块内补全，ne 在模块外仅显示全局 typedef enum。
    - struct 相关（严格作用域，仅在模块内补全）：`s ` (unpacked struct 变量), `sp ` (packed struct 变量), `ns ` (unpacked struct 类型), `nsp ` (packed struct 类型)
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

【基于作用域树的补全】
- CompletionManager::getCompletions(prefix, cursorFile, cursorLine)
  - 通过 ScopeManager::findScopeAt(cursorFile, cursorLine) 得到光标所在作用域；
  - 从该作用域起沿 parent 链向上，收集各层 symbols 中与 prefix 匹配的名称（内层已出现的不重复）；
  - 自然实现“局部变量 → task/function 内符号 → 模块内符号 → 全局”的补全顺序与词法遮蔽。
- cursorLine 与 SymbolInfo::startLine 一致，为 0-based 行号；若编辑器使用 1-based 需先减 1。

【Struct 相关命令的严格作用域（s / sp / ns / nsp）】
- 命令：`s `（unpacked struct 变量）、`sp `（packed struct 变量）、`ns `（unpacked struct 类型）、`nsp `（packed struct 类型）。
- 模块外（光标不在任何 module…endmodule 内）：
  - 补全列表为空，不弹出补全弹窗，避免全局命名空间污染。
- 模块内：
  - 通过 CompletionManager::getModuleContextSymbolsByType 聚合三类符号：
    1) 模块内部：严格在 [当前模块起始行, 下一模块起始行) 内的符号，防止多模块同文件时符号泄漏；
    2) Include：模块体内 `` `include "filename" `` 所引用文件中的符号；
    3) Import：模块体内 `import pkg::*;` / `import pkg::sym;` 所引用 package 中的符号。
  - 合并后按类型与前缀过滤、去重、排序。
- 全局符号（getGlobalSymbolsByType_Info）：
  - struct 变量仅当 symbol.moduleScope 为空时才视为全局（真正在 package/$unit 等定义），避免模块内 struct 变量泄漏到全局补全。
- **状态栏 struct 计数**：左下角“struct 变量 / struct 类型”仅按行范围统计（getModuleInternalSymbolsByType(..., useRelationshipFallback=false)），不使用关系引擎 fallback，避免键入 `s ` 再删除等操作后计数含入全局 struct 导致数字偏大。

【Enum 相关命令（e / ee / ne）】
- 命令：`e `（枚举变量）、`ee `（枚举值）、`ne `（枚举类型）。
- 枚举变量：含 `typedef enum { ... } name_t;` 后用类型名声明的变量（如 `name_t var;`），以及内联 `enum { A, B } var;` 声明的变量；补全列表括号内显示类型名或变量名（匿名枚举显示变量名，如 ON(power_switch)）。
- 枚举值：补全列表括号内显示来源类型（typedef 类型名或匿名时的枚举变量名）。
- 枚举类型：ne 补全来源为 sym_typedef（dataType=="enum"）与 sym_enum；模块内显示该模块内的 typedef enum 类型名，模块外显示全局 typedef enum。


==========================================================================
符号分析系统 (Symbol Analysis System)
==========================================================================

核心组件：`SymbolAnalyzer` + `sym_list`（符号数据库）+ `CompletionManager` + `ScopeManager`（作用域树）

- **解析架构（Slang 符号 + SVLexer 高亮）**
  - **符号数据（Slang，独家）**：大纲、补全、代码导航、作用域树与 CONTAINS 关系所依赖的符号数据**由** `SlangManager`（slangmanager.h/cpp）基于 `slang::ast::Compilation` 的 parse + elaboration **独家**提供。SymbolAnalyzer 对单文件调用 `SlangManager::extractSymbols(fileName, content)`，对整个工作区调用 `extractWorkspaceSymbols(filePaths)`（所有文件一起编译），得到 `QList<sym_list::SymbolInfo>` 后调用 `sym_list::setSymbolsForFile(fileName, list[, content])` 写入符号库，并在其中重建作用域树与 CONTAINS 关系。解析/elaboration 失败时返回空列表，不崩溃。
  - **语法高亮（SVLexer）**：高亮路径独立，由 `SVLexer`（sv_lexer.h/cpp、sv_token.h）逐 token 驱动，不依赖 Slang 或 Tree-sitter。
  - **Tree-sitter（SVTreeSitterParser）保留**：符号路径已不再使用 Tree-sitter；`SVTreeSitterParser` 仅保留给工具栏「Tree-sitter 验证」按钮（MainWindow::onDebug0 通过 parse(content) + getSymbols() 做验证输出，不写入符号库）。`sym_list::setContentIncremental` / `extractSymbolsAndContainsOnePass` 等旧的 Tree-sitter 符号入口现为**未调用的遗留代码**（保留以备回退/对照）。
  - **模块实例化关系（INSTANTIATES）**：由 `SlangManager::extractModuleInstantiations` 通过 Slang AST 的 InstanceSymbol 遍历产出（实例名/模块定义名/行号 1-based，100% 准确），SmartRelationshipBuilder 负责写入关系。其余关系类型（变量赋值/引用、task/function 调用、always、clock/reset 等）仍使用 SmartRelationshipBuilder 内原有正则逻辑。
- **Slang 符号提取明细（slangmanager.cpp::collectSymbolsFromRoot）**
  - 单文件 `extractSymbols` 用 `SyntaxTree::fromText` + `CompilationFlags::IgnoreUnknownModules`，使未定义模块不致中断；工作区 `extractWorkspaceSymbols` 用 `SyntaxTree::fromFiles` 把所有文件一起编译，类型 / package 的跨文件引用可正确解析。
  - 从 `RootSymbol` 用 `makeVisitor` 递归遍历，按 Slang 符号类型映射为 `SymbolInfo`：
    - `DefinitionSymbol`：Module→sym_module、Interface→sym_interface、Program→sym_module；
    - `InstanceSymbol`→sym_inst（dataType = 模块定义名）；
    - `VariableSymbol`：按 canonical type 判定 → reg / logic（ScalarType / IntegralType）、enum 变量（sym_enum_var）、packed/unpacked struct 变量（sym_packed_struct_var / sym_unpacked_struct_var）；Field→sym_struct_member；FormalArgument 按类型；
    - `NetSymbol`→sym_wire；`SubroutineSymbol`→sym_task / sym_function；
    - `PortSymbol`→按方向 sym_port_input / output / inout / ref；
    - `ParameterSymbol`→sym_parameter / sym_localparam（isLocalParam）；
    - `TypeAliasType`→sym_typedef（dataType 标记 "enum" / "struct"）；`EnumType`→sym_enum；`EnumValueSymbol`→sym_enum_value；`PackageSymbol`→sym_package。
  - 行号为 1-based（与 Qt/UI 一致，等同原 Tree-sitter 路径）。moduleScope 由 `getDeclaringDefinition()` 推出；包成员则取所在 package 名。
  - **已解决**：struct 成员 / 变量、enum、typedef 已由 Slang 产出，s/sp/e/ee/ne 相关补全与跳转可用；原 Tree-sitter 路径「`TYPE_NAME id;` 被误判为 wire」的 grammar 歧义，因 Slang 做语义判定而不再存在。
  - **typedef 站点产出（slangmanager.cpp）**：在 TypeAliasType visitor 中，对 typedef enum 额外遍历 EnumType::values() 产出 sym_enum_value（moduleScope = 别名，如 state_t）；对 typedef struct 额外产出 struct 类型符号（sym_packed_struct / sym_unpacked_struct）并遍历成员产出 sym_struct_member（moduleScope = 别名，如 test_s）。VariableSymbol visitor 对 enum/struct 变量写入 dataType = 声明类型别名。由此满足补全/跳转的数据契约：get{Struct,Enum}TypeForVariable 读 var.dataType、get{Struct,Enum}MemberCompletions 按 moduleScope == 类型名过滤。Field/EnumValue 的独立 handler 已移除（改在 typedef 站点产出，避免 moduleScope 取成模块名及重复）。
  - **当前缺口**：**内联匿名** enum/struct（无 typedef，如 `enum {A,B} v;`）的枚举值 / 成员当前不产出（仅 typedef 命名类型支持）；普通变量（reg/wire/logic）的 dataType（如 logic[7:0]）当前未填充（无消费者，仅 typedef/inst/enum 变量/struct 变量填 dataType）；commentRegions 在 setSymbolsForFile 路径不再维护（注释高亮仍由 SVLexer 保证）。上述均为编译通过、尚待运行实测。
- 支持解析的 SystemVerilog 符号包括但不限于：
  - `module` / `endmodule`
  - **有效模块判定**：仅当同时满足以下条件时才视为“有效模块”（用于补全、状态栏、getCurrentModuleScope 等）：
    1) 存在 `module` 声明；
    2) 存在与之配对的 `endmodule`（按深度匹配，支持嵌套 module）；
    3) 模块名为合法 SV 标识符：非空且符合 `[a-zA-Z_][a-zA-Z0-9_]*`（sym_list::isValidModuleName）。
    若缺少配对 endmodule 或模块名不合法，该段代码不会被判为“在模块内”。
  - `reg` / `wire` / `logic` 变量
  - `task` / `function`
  - 模块端口（ANSI 风格）：`input` / `output` / `inout` / `ref`，由 Slang 的 PortSymbol 按方向产出（dataType 当前未填充）。
  - parameter/localparam/typedef/enum、struct 成员与变量均已由 Slang 产出，s/sp/e/ee/ne 的补全 / 跳转可用（struct 类型名符号见上文「当前缺口」）。`interface`、实例化引脚（`.pin(sig)`）与 REFERENCES 关系由 SmartRelationshipBuilder 等负责；模块实例化（INSTANTIATES）由 SlangManager 基于 Slang AST 产出。
- 具备注释感知能力
  - 通过符号数据库中的注释范围表，避免解析注释中的符号
- **Struct 与注释**
  - typedef/enum 类型与变量、struct 成员与变量均已由 Slang 产出（sym_typedef、sym_enum、sym_enum_var、sym_enum_value、sym_struct_member、sym_packed_struct_var、sym_unpacked_struct_var）。注释感知（commentRegions）当前未在 Slang 路径维护；注释内容仍由 SVLexer 识别为 Comment，高亮路径不受影响。
  - **Packed / Unpacked 区分**：struct **变量**已通过 Slang 的 canonical type 区分 packed/unpacked（sym_packed_struct_var / sym_unpacked_struct_var）。struct **类型名**符号（sym_packed_struct / sym_unpacked_struct）尚未单独产出，目前以 sym_typedef(dataType="struct") 表示，ns/nsp 与类型名跳转依赖此形式，行为待验证。

【作用域树 (Scope Tree) — scope_tree.h】
符号管理采用分层作用域表，替代原先扁平的 QList + 字符串 moduleScope 匹配（O(N) 查找、无法正确表达嵌套与遮蔽）。

- 数据结构
  - ScopeNode：作用域类型（Global / Module / Task / Function / Block）、行范围（startLine, endLine）、
    parent/children 指针、本层符号 QHash<QString, SymbolInfo>（O(1) 查找）。
  - ScopeManager：按文件维护根节点；由 sym_list 在解析时构建并持有（getScopeManager()）。
- 解析方式（栈式）
  - 由 `sym_list::setSymbolsForFile` 调用 `rebuildScopeAndRelationshipsForFile(fileName)`：先取该文件全部符号并按 startLine（module 优先）排序，再按行号顺序维护 QStack<ScopeNode*> 与 QStack<int>（模块 id）：
    - 进入新符号前，若其 startLine 越过栈顶作用域 endLine 则出栈（闭合 module/task/function）；
    - 遇到 module 创建 Module 作用域并 push、记录模块 id；遇到 task / function 创建对应作用域并 push；
    - 遇到 reg / wire / logic / 端口 / parameter / typedef / enum / struct 成员变量等写入当前 scopeStack.top()->symbols，并对栈顶模块 addRelationship(..., CONTAINS)。
  - 作用域起止由 Slang 给出的 SymbolInfo.startLine / endLine 驱动；最后调用 buildSymbolRelationships(fileName) 构建其余关系。
- 接口
  - findScopeAt(fileName, line)：返回该行所在的最深层作用域。
  - resolveSymbol(name, startScope)：沿 parent 链向上查找符号，实现词法遮蔽（内层同名遮蔽外层）。
  - 在 clearSymbolsForFile 时会同步清除该文件的作用域树。

分析模式（SymbolAnalyzer 自持一个 SlangManager 实例 m_slangManager）：
- 打开标签分析：`analyzeOpenTabs`
  - 对当前打开的 SV 文件（`TabManager::getOpenSystemVerilogFiles()`），逐文件通过 `getPlainTextFromOpenFile` 取内容，调用 `m_slangManager->extractSymbols(fileName, content)` 后 `sym_list::setSymbolsForFile(fileName, list, content)`，不依赖编辑器对象。
- 工作区分析：`analyzeWorkspace` / `startAnalyzeWorkspaceAsync`
  - 通过 `WorkspaceManager` 拿到整个目录树中的 `.sv/.v/.vh/.svh/.vp/.svp` 文件
  - 调用 `m_slangManager->extractWorkspaceSymbols(svFiles)` 把所有文件一起编译得到全部符号，再按 fileName 分组（groupSymbolsByFile），逐文件 `setSymbolsForFile`；`startAnalyzeWorkspaceAsync` 在 QtConcurrent::run 后台编译、`onWorkspaceAnalysisFinished` 回主线程写回。
- 单文件分析：`analyzeFile`（读磁盘）/ `analyzeFileContent`（直接用内容）
  - 供文件变化回调 (`fileChanged`) 与增量分析调用，均走 extractSymbols + setSymbolsForFile。

增量分析：
- `MainWindow::scheduleOpenFileAnalysis(fileName, delayMs)` 按 fileName 去抖，超时后从 TabManager 取内容调用 `analyzeFileContent`；`cancelScheduledOpenFileAnalysis(fileName)` 可取消已调度分析。
  - 触发与延时：行数变化时 500ms；无工作区时根据**当前行**是否含显著关键字（`module`/`endmodule`/`reg`/`wire`/`logic`/`task`/`endtask`/`function`/`endfunction`）决定 1s 或 3s，避免每次按键都触发全量分析。


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
- 导航树按符号类型分组显示；struct 与 enum 在 UI 中可区分：Packed/Unpacked 结构体类型与变量、结构体成员；类型定义（typedef）、枚举变量、枚举值（getSymbolTypeDisplayName / getSymbolIcon，NavigationManager::symbolTypes 含 sym_typedef、sym_enum_var、sym_enum_value 等）。
- 支持两种跳转方式：
  - 符号导航：由 `NavigationManager::symbolNavigationRequested` 触发
  - 文件+行号导航：`MainWindow::navigateToFileAndLine`
- 行号栏点击：快速把光标跳转到某一行

【定义跳转（Ctrl+Click）】
- 在 `MyCodeEditor` 中：
  - 按住 Ctrl 并将鼠标移动到标识符或 `` `include `` 路径上时：
    - 光标变成手型（仅当当前作用域内存在该符号定义，或位于可跳转的 include/package 上时）
    - 标识符或路径高亮为蓝色下划线
    - 可选地弹出 Tooltip 展示定义位置等信息
  - Ctrl+左键跳转优先级：
    1) **Include**：若点击在 `` `include "filename" `` 的路径字符串上，通过 `tryJumpToIncludeAtPosition` / `openIncludeFile` 打开被包含文件。
    2) **Package**：若点击在 `import pkg::*;` / `import pkg::sym;` 的 package 名上，跳转到该 package 定义（`getPackageNameFromImport` + `jumpToDefinition`）。
    3) **符号定义**：否则按符号名跳转（`getWordAtPosition` + `jumpToDefinition`）。
  - 符号跳转规则：
    - **作用域限定**：光标在某个模块内时（sym_list::getCurrentModuleScope 非空），
      只考虑**当前模块**的符号；不会跳到其他模块的同名端口或变量（例如两个模块都有 clk_main 时，只跳本模块的）。
    - 若当前模块内**没有**该符号定义（其他模块有），则不视为可跳转、不跳转（canJumpToDefinition 与 jumpToDefinition 均按当前模块过滤）。
    - 可跳转定义类型包含：module/interface/package/task/function、端口、reg/wire/logic/parameter/localparam，**struct 变量与成员**，以及 **enum 类型、枚举变量、枚举值**；这些均已由 Slang 产出，可跳转。struct **类型名**（sym_packed_struct / sym_unpacked_struct）当前以 sym_typedef 形式存在，类型名跳转行为待验证（见上文「当前缺口」）。
    - 优先跳当前文件中的定义；端口类型优先级高于 reg/wire/logic。
    - 再考虑其他文件中、且仍在当前模块作用域内的定义（若有）。
  - **Struct 相关跳转**：
    - **成员跳转**：在 `var.member` 表达式中 Ctrl+点击成员名（如 member0），根据变量名解析出 struct 类型，跳转到该 struct 内该成员的定义位置；结构体成员的 moduleScope 为结构体类型名，跳转时按类型过滤、不按模块名过滤。
    - **变量跳转**：Ctrl+点击 struct 变量名，跳转到其声明（packed/unpacked struct 变量已纳入 isSymbolDefinition 与 definitionTypePriority）。
    - **类型名跳转**：在声明语句（如 `test_s test_s_var;` 或 `test_sp test_sp_var;`）中 Ctrl+点击类型名，跳转到 `typedef struct [packed] { ... } type_name;` 中别名位置。Slang 当前以 sym_typedef(dataType="struct") 表示该类型名，尚未单独产出 sym_packed_struct / sym_unpacked_struct，该跳转行为待验证。definitionTypePriority 中 sym_packed_struct / sym_unpacked_struct 显式优先级 6，与 parameter/localparam 一致。
  - **Enum 相关跳转**（与 struct 类似的 3 类）：
    - **枚举值跳转**：Ctrl+点击枚举值名（如 STATE_IDLE、ON），跳转到该枚举值在 enum 体中的定义行（sym_enum_value 已纳入 isSymbolDefinition 与 definitionTypePriority）。
    - **枚举类型跳转**：Ctrl+点击 typedef enum 类型名（如 fsm_state_t），跳转到 `typedef enum { ... } type_name;` 中类型名位置（sym_typedef 表示枚举类型）。
    - **枚举变量跳转**：Ctrl+点击枚举变量名（如 fsm_state、power_switch），跳转到其声明行（sym_enum_var 已纳入 isSymbolDefinition 与 definitionTypePriority）。
  - **跳转后鼠标跟随**：本地跳转（当前文件内）与跨文件跳转（navigateToFileAndLine）完成后均调用 `moveMouseToCursor()`，将鼠标指针移动到新光标位置。
  - 跳转过程会复用 `NavigationManager` 的符号导航接口


==========================================================================
符号关系系统 (Symbol Relationship System)
==========================================================================

核心组件：
- `SymbolRelationshipEngine`
- `SlangManager`（slangmanager.h/cpp）：基于 Slang 的语义分析。现负责**全部符号提取**（extractSymbols / extractWorkspaceSymbols，供 SymbolAnalyzer 使用），以及模块/接口/程序实例化（extractModuleInstantiations，供 SmartRelationshipBuilder 写入 INSTANTIATES 关系）。
- `SmartRelationshipBuilder`
- `RelationshipProgressDialog`

功能概览：
- 在工作区分析完成后，进一步分析符号之间的关系，例如：
  - **模块实例化关系（INSTANTIATES）**：由 `SlangManager::extractModuleInstantiations` 通过 Slang AST（Compilation + InstanceSymbol 遍历）产出，行号与实例/模块名 100% 准确；SmartRelationshipBuilder 仅负责将结果映射为 addRelationshipWithContext(..., INSTANTIATES)。
  - 实例引脚到模块端口的 REFERENCES（`.pin(sig)` → 对应 module 的 port 定义，供跳转到定义）
  - 变量赋值 / 驱动关系
  - 任务 / 函数调用关系
- 结果会回写到：
  - `SymbolRelationshipEngine` 中（作为统一关系存储）
  - `CompletionManager` 中（用于关系感知型补全）

分析流程（简化）：
1. 打开 Workspace（批量）
   - 先触发符号批量分析 (`SymbolAnalyzer::analyzeWorkspace`)
   - 然后由 `SmartRelationshipBuilder` 对所有 SV 文件进行逐个关系分析（`analyzeMultipleFiles`），内部对每个文件在模块实例化环节调用 SlangManager；结果经 `relationshipBatchWatcher` 回主线程 `beginUpdate`/`endUpdate` 写回引擎。
2. 单文件关系分析（编辑/保存时）
   - `MainWindow::requestSingleFileRelationshipAnalysis(fileName, content)`：仅当 `hasSignificantChanges` 通过时才触发；在后台 `QtConcurrent::run` 中调用 `SmartRelationshipBuilder::computeRelationships`，结果经 `relationshipSingleFileWatcher` 回主线程写回。
   - 触发时机：符号分析超时后（`scheduleOpenFileAnalysis` 回调里）、保存后、文件监视到变更后；编辑器中 `onTextChanged` 启动 `relationshipAnalysisDebounceTimer`（约 2s），停止输入后到时再请求单文件关系分析，避免连续按键重复触发。
3. `RelationshipProgressDialog`
   - 显示两阶段进度：阶段 1 符号分析（`setSymbolAnalysisProgress`）、阶段 2 关系分析（`updateProgress`）
   - 展示当前正在处理的文件、已完成数量、进度条等；支持“取消”分析，触发 `analysisCancelled`
4. 分析结果
   - `analysisCompleted(fileName, relationshipsFound)` 按文件汇报；所有文件处理后自动关闭进度对话框并在状态栏给出汇总

其它：
- MainWindow 持有 `SlangManager` 实例，创建 SmartRelationshipBuilder 时传入；CompletionManager 通过 setSlangManager 获得同一实例，以便其内部的 SmartRelationshipBuilder 也使用 Slang 做实例化分析。
- 所有新增/清空关系会通知 `CompletionManager` 刷新内部缓存
- 导航面板可以基于最新的关系数据刷新视图


==========================================================================
构建与运行 (Build & Run)
==========================================================================

【依赖】
- Qt 5/6（建议在与你当前 `.pro` 文件兼容的版本上构建）
- 支持 C++20 的编译器（Slang 要求 C++20；项目使用 `std::unique_ptr` 等）
- Slang：thirdparty/slang 子项目，用于 SlangManager 的语义分析（**符号提取** + 模块实例化）；CMake 已配置 slang::slang 链接与 C++20
- Tree-sitter：thirdparty/tree_sitter 与 thirdparty/tree_sitter_systemverilog，现仅供 SVTreeSitterParser 的「Tree-sitter 验证」按钮使用（不再参与符号库）；构建时仍需能编译 C 源（lib.c、parser.c）

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

【核心问题诊断】（已由下方阶段 A～F 对应解决）

当前系统曾存在以下主要性能瓶颈，已通过异步化、轻量化与缓存策略逐一处理：

1. UI 线程阻塞
   - SymbolAnalyzer::analyzeWorkspace 虽已做批处理，若仍在主线程通过
     QApplication::processEvents() 运行，在处理大型工程时会导致界面响应延迟。
   - 目标：将所有解析逻辑从 UI 线程剥离，用 QtConcurrent::run 等包装扫描与
     分析循环，禁止在后台线程调用 processEvents() 或创建 QWidget。

2. 重量级对象开销
   - 若工作区分析通过 createBackgroundEditor 频繁创建 MyCodeEditor (QWidget)
     来读取文件，每个文件一个编辑器实例，内存与 CPU 初始化开销巨大。
   - 目标：废弃“临时编辑器”方式，改为 analyzeFileContent(const QString& content)
     等接口，直接对 QString 提取符号（现由 SlangManager::extractSymbols 解析、setSymbolsForFile 写回），
     ​避免创建 MyCodeEditor 实例；多线程访问 sym_list 时需保证单例线程安全或后台独立
     临时表再合并。

3. 高频同步 IO 与无效重算
   - 文件保存或变更时触发的全量分析会带来频繁磁盘读取和冗余符号表/关系表构建。
   - 目标：利用 hasSignificantChanges 等逻辑跳过“仅注释/空白”变更；在关系
     引擎侧引入 beginUpdate()/endUpdate() 批量提交，在 endUpdate 前禁止
     invalidateCache()，避免每添加一条关系就清空全局缓存导致的 O(N^2) 行为。

【已完成阶段】

[x] 阶段 A — 彻底的异步化重构 (MainWindow & SymbolAnalyzer)（已完成）
  - 使用 QtConcurrent::run 包装整个工作区扫描与分析循环。
  - 禁止在后台线程调用 QApplication::processEvents() 或创建任何 QWidget。
  - 通过 batchProgress 等信号异步回传进度，仅在主线程更新 progressDialog。

[x] 阶段 B — 解析器轻量化 (SymbolAnalyzer)（已完成）
  - 废弃 createBackgroundEditor；新增 analyzeFileContent(fileName, content)，
    直接对 QString 做解析。
  - 符号提取迁移至 Slang：analyzeFileContent / analyzeFile / analyzeOpenTabs 调用 SlangManager::extractSymbols，
    工作区调用 extractWorkspaceSymbols，结果经 sym_list::setSymbolsForFile 写回（全量重算），
    不再走 setContentIncremental / SVTreeSitterParser，不再创建 MyCodeEditor。
  - sym_list::getInstance() 使用静态 QMutex 保证多线程下单例创建安全；
    getAllSymbols() 使用 QReadLocker，持写锁时通过 s_holdingWriteLock 避免死锁。

[x] 阶段 C — 语义级去抖与增量策略 (SmartRelationshipBuilder / SymbolRelationshipEngine)（已完成）
  - 利用已有 hasSignificantChanges：仅当结构/定义变更时才触发关系重构。
  - 在 SymbolRelationshipEngine 中引入 beginUpdate() 与 endUpdate()，在
    endUpdate 之前不调用 invalidateCache()，批量提交后按需失效缓存。

[x] 阶段 D — 语法高亮性能与正确性 (MyHighlighter)（已完成）
  - **Lexer 化重构**：完全移除 MyHighlighter 内的 QRegularExpression，改用专用词法分析器 SVLexer
    （sv_lexer.h/cpp、sv_token.h）。highlightBlock 仅调用 SVLexer::nextToken() 按 token 高亮，
    避免正则回溯与多遍匹配带来的主线程卡顿。
  - 关键字仍从 config/keywords.txt 静态加载（loadKeywordsOnce + QMutex），Lexer 输出 Identifier，
    Highlighter 用关键字表判断后应用关键字格式；注释/字符串由 Lexer 直接识别为 Comment/String，
    故注释内关键字不会误标。
  - 关键字表已补全为常用 Verilog/SystemVerilog 与预处理器关键字（见 config/keywords.txt）。

[x] 阶段 E — 作用域树 (Scope Tree) 符号管理（已完成）
  - 新增 scope_tree.h：ScopeNode（Global/Module/Task/Function/Block）、ScopeManager
    （findScopeAt、resolveSymbol）；按文件维护作用域树，O(1) 层内查找与正确词法遮蔽。
  - sym_list：setSymbolsForFile 写入 Slang 符号后调用 rebuildScopeAndRelationshipsForFile，
    按 startLine 排序后栈式构建作用域树；clearSymbolsForFile 时同步 clearFile 作用域树；
    getScopeManager() 惰性创建并返回 ScopeManager。结构符号（module/task/function/typedef/enum/struct 成员变量）均由 Slang 产出。
  - CompletionManager：新增 getCompletions(prefix, cursorFile, cursorLine)，基于
    findScopeAt + 沿 parent 链收集符号，供“按光标所在作用域”的补全使用。
  - **Struct 补全作用域**：struct 相关命令（s/sp/ns/nsp）已实现严格作用域——模块外不补全，模块内使用 getModuleContextSymbolsByType（模块内 + include + import），且 getModuleInternalSymbolsByType 按“下一模块起始行”严格边界，避免跨模块泄漏；getGlobalSymbolsByType_Info 中 struct 变量仅 moduleScope 为空时视为全局。状态栏 struct 计数调用 getModuleInternalSymbolsByType(..., useRelationshipFallback=false)，仅按行范围统计，不含关系引擎 fallback。

[x] 阶段 F — 作用域背景持久光标缓存 (MyCodeEditor)（已完成）
  - 问题：highlighCurrentLine 每次光标移动都查 sym_list，分析滞后导致背景“回弹”。
  - 方案：用 QTextCursor 缓存作用域选区（m_scopeSelections），Qt 随文档自动更新光标位置；
    仅在分析完成时从数据库刷新缓存（updateScopeBackgrounds）。
  - 实现：删除 getScopeBackgroundSelections()；新增 updateScopeBackgrounds() 与成员
    m_scopeSelections；refreshScopeAndCurrentLineHighlight() 先 updateScopeBackgrounds 再
    highlighCurrentLine；highlighCurrentLine 仅过滤 997/998、追加缓存与当前行 998、setExtraSelections，
    不再查库。编辑时背景随文本移动，分析完成后刷新至正确语义块。

【冗余清理与架构对齐 (Redundancy Cleanup & Architecture Alignment)】— 已完成

以下清理项已落实，代码库与“异步/数据驱动”架构对齐，当前无已知遗漏冗余。

已完成的清理：
  - SymbolAnalyzer：已删除 analyzeEditor、analyzeOpenTabs 内 getEditorAt 循环；
    解析统一走 analyzeFileContent(fileName, content)，符号经 SlangManager 提取后由 sym_list::setSymbolsForFile 写回。
  - sym_list：已移除无调用者的 setCodeEditor / setCodeEditorIncremental；Slang 路径的写入入口为
    setSymbolsForFile(fileName, list[, content])（setContentIncremental 保留为遗留）。持写锁分析路径中，findSymbolIdByName / getSymbolById
    在 s_holdingWriteLock 为 true 时不再加读锁，与 findSymbolsByFileName / getAllSymbols 一致，避免同一线程死锁。
  - CompletionManager：getModuleInternalVariables / getGlobalSymbolCompletions 等已统一
    使用 matchesAbbreviation；结果截断统一在 CompletionModel 出口（MaxCompletionItems），
    已删除各子方法内重复的 MaxCompletionListSize 截断及该常量。
  - 符号作用域：除原有扁平 symbolDatabase + moduleScope 外，增加 ScopeManager 作用域树，
    补全可选用 getCompletions(prefix, cursorFile, cursorLine) 实现按行作用域与遮蔽。
  - SymbolRelationshipEngine：已删除类外冗余 relationshipTypeToString，已精简
    getModuleInstances 内空调试分支。
  - MainWindow / MyCodeEditor：已删除 onDebugPrintSymbolIds、disLineNumber 空函数；
    延后符号分析已迁移至 MainWindow::scheduleOpenFileAnalysis，SymbolAnalyzer 不再持有
    基于 MyCodeEditor 的定时器。
  - 调试逻辑已全部移除：代码中不再包含 qDebug 输出、调试信号（如 debugScopeInfo、debugStructMemberCompletion）
    及对应槽/连接；状态栏不再显示“当前模块 / logic / struct 计数”（已随调试信号一并移除）；发布构建无需再通过宏关闭调试输出。
  - SmartRelationshipBuilder：已移除空占位 analyzeInterfaceRelationships 及其调用；
    interface 分析待后续统一扩展接口实现。模块实例化（INSTANTIATES）已改为使用 SlangManager
    + Slang AST（extractModuleInstantiations），不再使用 QRegularExpression；其余关系类型仍用正则。
  - MyCodeEditor：作用域背景改为持久光标缓存；删除 getScopeBackgroundSelections()，新增
    updateScopeBackgrounds() 与 m_scopeSelections，highlighCurrentLine 仅用缓存与当前行重绘，
    避免每次光标移动查库导致背景回弹。
  - mycodeeditor.cpp：已去除重复 #include（如 QScrollBar）。

架构一致性（后续修改时请保持）：
  - 信号安全：SymbolRelationshipEngine::addRelationship 须保持 Qt::QueuedConnection，
    禁止在后台线程直接触发 UI 刷新。
  - 写锁保护：sym_list 的增量解析仍受 QMutex / QReadWriteLock 保护，防止多线程崩溃。
  - 符号解析：仅使用 Slang（SlangManager::extractSymbols / extractWorkspaceSymbols），写回经 sym_list::setSymbolsForFile。
    setContentIncremental / SVTreeSitterParser 符号路径为遗留代码（仅 Tree-sitter 验证按钮使用），SVSymbolParser 已移除。
    语法高亮由 SVLexer 驱动；hasSignificantChanges 等改为简单字符串/词边界判断。

若发现新的冗余，可参考本节原则处理并更新本段说明。

==========================================================================
已知问题 (Known Issues)
==========================================================================

- **UI 卡顿 (UI Lag)**：以下操作时界面可能出现短暂卡顿或响应延迟，属已知现象，后续会通过进一步异步化与分批更新优化：
  - 打开工作区：选择目录并加载工作区时，会触发批量文件扫描与符号分析，主线程可能参与进度更新或数据回写，导致界面短暂不流畅。
  - 从导航页打开文件：在导航面板中点击文件或符号进行跳转/打开时，若需加载大文件或触发符号/关系查询，会有可感知的延迟。
  - 符号分析进行中：打开标签分析、工作区分析或单文件分析执行时，若与 UI 刷新、导航树更新、状态栏更新等叠加，可能出现卡顿。当前分析已采用 QtConcurrent 等异步方式，但结果回主线程写回符号库/关系引擎、刷新导航与补全缓存时仍可能造成主线程短时繁忙。

- **符号解析（Slang 迁移中）**：符号解析已迁移至 Slang（SlangManager::extractSymbols / extractWorkspaceSymbols），
  写回经 sym_list::setSymbolsForFile。该迁移为**未提交的进行中改动**，已编译通过并用无头测试
  （test_sv/dump_symbols.cpp）验证符号字段，GUI 实测待做；遗留的 Tree-sitter 符号路径
  （setContentIncremental / extractSymbolsAndContainsOnePass）保留但已不被调用。
  已修复并验证（见 collectSymbols）：
  - module/interface 改由 compilation.getDefinitions() 产出（修复 root.visit() 不产出 sym_module 的致命缺陷）；
  - 端口去重（跳过 PortSymbol.internalSymbol）；跳过顶层自动实例；按定义去重实例体（多次例化不重复成员）；
  - typedef enum/struct 于 typedef 站点产出值/成员/类型符号，moduleScope = 类型别名，满足补全/跳转契约。
  以下为已知/待验证的缺口：
  - **内联匿名 enum/struct**（已修复）：无 typedef 的内联匿名类型（如 `enum {A,B} v;`、`struct {...} s;`）
    在变量站点产出枚举值/成员，moduleScope = 变量名、变量 dataType = 变量名，满足 ee / var.member 补全契约。
    多个变量共用同一匿名类型时会按各自变量名各产出一份（轻微重复，可接受）。
  - **function/task 局部符号**（已修复）：fillSymbolInfo 在计算 moduleScope 时优先向上查找最近的
    SubroutineSymbol，将形参/返回值/局部变量的 moduleScope 设为所在 task/function 名（而非模块名），
    从而不再泄漏进 `l `/`r `/`w ` 模块级补全（该路径按 moduleScope == 模块名过滤）；scope-tree 路径
    不依赖 moduleScope，光标在子程序内仍可补全其局部符号。
  - **变量 dataType**：reg/wire/logic 等变量的 dataType（如 logic[7:0]）当前未填充（无消费者）；enum/struct
    变量已填 dataType = 类型别名，inst 填模块名，typedef 填 enum/struct 标记。
  - **注释感知**：commentRegions 在 setSymbolsForFile 路径不再维护；注释高亮仍由 SVLexer 保证。
  - **单文件 elaboration**：extractSymbols 对单文件做 elaboration，跨文件的类型/package 引用可能解析失败，
    导致部分符号缺失（单文件 vs 全工程权衡）；工作区分析用 fromFiles 整体编译可缓解。
  - **编译依赖**：新增引用 slang 头（CompilationUnitSymbols / MemberSymbols / VariableSymbols / PortSymbols /
    ParameterSymbols / SubroutineSymbols / AllTypes 等），需确认在当前 slang 版本下可编译链接。

- **Tree-sitter（SVTreeSitterParser）**：仅保留给工具栏「Tree-sitter 验证」按钮（parse(content) + getSymbols()），
  不再参与符号库写入；原「`TYPE_NAME id;` 被解析为 wire」的 grammar 歧义已因改用 Slang 语义判定而消失。

- **Module 识别 (Module Recognition)**：当前 module 识别仍存在已知问题与局限。有效模块
  的判定已统一为“必须有 module + 配对 endmodule + 合法模块名”（见上文“有效模块判定”），
  但以下情况可能仍会出错或未覆盖：
  - 宏展开、条件编译（`ifdef/endif`）内的 module/endmodule 边界可能未正确解析；
  - 跨文件的 module（例如 module 在 include 文件中）边界依赖当前文件的文本范围；
  - 注释/字符串内出现的 `module`/`endmodule` 已尽量排除，极端嵌套或格式异常时可能误判；
  - 其他与具体工程代码风格相关的边界情况。
  若出现“光标在模块内但状态栏显示无模块”、补全作用域错误或跳转目标不准，可优先检查
  该文件是否满足“成对 module/endmodule + 合法模块名”，并排查上述场景。后续会持续改进
  module 识别的鲁棒性。

- **状态栏「当前模块」**：使用符号库中 module 的 startLine/endLine 与 sym_list::getCachedFileContent
  的缓存内容做“光标是否在模块内”判定（CompletionManager::findModuleAtPosition），不再仅依赖磁盘读取与 findEndModulePosition 正则。当前符号由 Slang（SlangManager）产出。若仍显示「无模块」，
  ​可能原因包括：（1）缓存内容与编辑器当前内容不一致，cursorPosition 在“缓存 + position 转行号”
  时产生偏差；（2）需改为传入编辑器当前缓冲区内容做 position-to-line，使行号与光标所在文档一致。
  建议后续：getCurrentModule 或 findModuleAtPosition 支持可选“当前文档内容”参数，优先用其做
  position 转 cursorLine，无再回退到缓存/磁盘。

- **作用域树 (Scope Tree)**：现由 setSymbolsForFile → rebuildScopeAndRelationshipsForFile 基于 Slang 符号的
  startLine/endLine 重建。getCompletions(prefix, cursorFile, cursorLine) 基于 findScopeAt 的按作用域补全与
  词法遮蔽已实现框架；struct 相关命令（s/sp/ns/nsp）的严格作用域（模块外不补全、模块内聚合
  internal+include+import、全局仅 moduleScope 为空）已修复。注意作用域闭合依赖 Slang 给出的 endLine，
  若 endLine 缺失或不准会影响出栈与遮蔽；若发现按作用域补全或跳转异常，可优先排查 endLine 与 getCurrentModule 边界。

- **作用域背景 (Scope Background)**：左侧条带与编辑器内 module/logic 的背景由符号分析驱动。
  已采用“持久光标缓存”：编辑时 highlighCurrentLine 仅使用缓存的 m_scopeSelections（Qt 会随
  文档自动更新光标位置），背景随文本移动无回弹；分析完成后 refreshScopeAndCurrentLineHighlight
  调用 updateScopeBackgrounds 从数据库刷新缓存。若分析尚未完成则沿用上一轮缓存。


==========================================================================
备注 (Notes)
==========================================================================

- 本文件是面向“阅读/维护代码的人”的说明文档，侧重介绍目前仓库中已经实现的架构与能力。
- 如果你在阅读代码时发现 README 与实际实现不一致，以**代码实现**为准，再回过头来更新本文件即可。
