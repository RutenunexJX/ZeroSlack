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

- **双轨解析架构（Tree-sitter + Slang）**
  - **UI 与符号**：大纲、补全、代码导航的符号数据**由** `SVTreeSitterParser`（sv_treesitter_parser.h/cpp）**独家**提供。sym_list::setContentIncremental 首次与非首次均走 extractSymbolsAndContainsOnePassImpl → SVTreeSitterParser::parseSymbols()，解析结果写入符号库并驱动作用域树与 CONTAINS 关系。语法高亮由 SVLexer 驱动。**不改动** sv_treesitter_parser，继续用于高亮与简单 outline。
  - **语义分析（Slang）**：`SlangManager`（slangmanager.h/cpp）基于 `slang::ast::Compilation` 对单文件做解析与 elaboration，目前用于**模块实例化关系**（INSTANTIATES）。SmartRelationshipBuilder 在分析「谁实例化了谁」时调用 SlangManager::extractModuleInstantiations，遍历 Slang AST 中的 InstanceSymbol，提取实例名、模块定义名与行号（1-based），不再使用正则，保证 100% 准确。解析/elaboration 失败时返回空列表，不崩溃。其余关系类型（变量赋值/引用、task/function 调用、always、clock/reset 等）仍使用 SmartRelationshipBuilder 内原有正则逻辑。
- **符号解析数据来源（仅使用 Tree-sitter）**
  - **当前数据来源**：如上，符号数据由 SVTreeSitterParser 独家提供。
  - **Tree-sitter（SVTreeSitterParser）**：项目已集成 tree-sitter 核心库（thirdparty/tree_sitter）与 tree-sitter-systemverilog 语法（thirdparty/tree_sitter_systemverilog）。parseSymbols() 从 AST 根递归遍历，维护 module/interface/program 作用域栈，产出带 fileName、moduleScope 的 SymbolInfo。支持的节点类型包括：module_declaration、program_declaration、package_declaration、interface_declaration、class_declaration、module_instantiation（含实例名与模块类型）、task_declaration、function_declaration、always_construct、ansi_port_declaration、port_declaration（input/output/inout/ref）、data_declaration（reg/logic/wire/enum 变量，类型由 AST 节点 reg/logic/net_type、enum_base_type 判定）、net_declaration（wire）、parameter_declaration（sym_parameter）、local_parameter_declaration（sym_localparam）、type_declaration（sym_typedef；typedef enum 时同时产出 sym_enum_value）；module 名称从 *_header 子节点或 simple_identifier 正确提取。类型与端口方向等均按 AST 节点类型判断，不使用字符串匹配。takeComments() 当前返回空列表，注释区域尚未从 AST 收集。
  - **已产出**：parameter/localparam、typedef（含 dataType 如 "enum"）、枚举类型变量（sym_enum_var）、枚举值（sym_enum_value）。**尚未产出**：struct/union 类型与变量（sym_packed_struct、sym_unpacked_struct、sym_struct_member、sym_*_struct_var），故 s/sp/ns/nsp 相关补全/跳转会缺失或异常。实例化引脚（sym_inst_pin）与 REFERENCES 等由 SmartRelationshipBuilder 负责；模块实例化关系（INSTANTIATES）由 SlangManager + Slang 语义分析产出，见上文「双轨解析架构」。
  - **net_declaration 与 data_declaration 歧义**：语法上「类型名 + 标识符 + 分号」（如 `test_e test;`）可同时匹配 net_declaration（nettype_identifier + list_of_net_decl_assignments）与 data_declaration。grammar 中 net_declaration 为 PREC_DYNAMIC 0、data_declaration 为 1，tree-sitter 优先选 net_declaration，故此类变量声明会被解析为 wire。建议在符号层做语义补救：当 net_declaration 的 nettype_identifier 在已解析的 typedef/enum 类型中时，按变量（如 sym_enum_var）处理。
  - **SVSymbolParser 已移除**：符号解析仅使用 SVTreeSitterParser；语法高亮仍由 `SVLexer`（sv_lexer.h/cpp、sv_token.h）驱动。工具栏「Tree-sitter 验证」按钮（MainWindow::onDebug0）仍通过 SVTreeSitterParser::parse(content) + getSymbols() 做验证输出，不写入符号库。
- 支持解析的 SystemVerilog 符号包括但不限于：
  - `module` / `endmodule`
  - **有效模块判定**：仅当同时满足以下条件时才视为“有效模块”（用于补全、状态栏、getCurrentModuleScope 等）：
    1) 存在 `module` 声明；
    2) 存在与之配对的 `endmodule`（按深度匹配，支持嵌套 module）；
    3) 模块名为合法 SV 标识符：非空且符合 `[a-zA-Z_][a-zA-Z0-9_]*`（sym_list::isValidModuleName）。
    若缺少配对 endmodule 或模块名不合法，该段代码不会被判为“在模块内”。
  - `reg` / `wire` / `logic` 变量
  - `task` / `function`
  - 模块端口（ANSI 风格）：`input` / `output` / `inout` / `ref`，以及 dataType（如 logic[7:0]）等，由 SVTreeSitterParser 从 ansi_port_declaration / port_declaration 节点解析。
  - parameter/localparam/typedef/enum 已由 SVTreeSitterParser 产出；struct/union 尚未产出，依赖 s/sp/ns/nsp 的补全/跳转会缺失或异常。`interface`、实例化引脚（`.pin(sig)`）与 REFERENCES 关系由 SmartRelationshipBuilder 等负责；模块实例化（INSTANTIATES）由 SlangManager 基于 Slang AST 产出。
- 具备注释感知能力
  - 通过符号数据库中的注释范围表，避免解析注释中的符号
- **Struct 与注释**
  - typedef/enum 类型与变量已由 Tree-sitter 产出（sym_typedef、sym_enum_var、sym_enum_value）；struct/union 尚未产出，s/sp/ns/nsp 相关补全与跳转会受影响。注释区域（commentRegions）由 takeComments() 提供，当前返回空列表，注释感知能力暂未接入 AST。注释内容仍由 Lexer 识别为 Comment，高亮路径不受影响。
  - 结构体变量：支持 `type name;` / `type name,` 以及数组形式 `type name [4];`、`type name [3:0];`。
  - **Packed / Unpacked 区分**：struct 类型与变量当前 Tree-sitter 路径不产出，故 s / sp / ns / nsp 补全与跳转会缺失或异常。若后续在 SVTreeSitterParser 中实现，需支持 `packed struct` / `struct packed` 产出 sym_packed_struct / sym_unpacked_struct 及对应变量符号，并维护类型名以便 `type_name var;` 解析为正确的 packed/unpacked 变量。

【作用域树 (Scope Tree) — scope_tree.h】
符号管理采用分层作用域表，替代原先扁平的 QList + 字符串 moduleScope 匹配（O(N) 查找、无法正确表达嵌套与遮蔽）。

- 数据结构
  - ScopeNode：作用域类型（Global / Module / Task / Function / Block）、行范围（startLine, endLine）、
    parent/children 指针、本层符号 QHash<QString, SymbolInfo>（O(1) 查找）。
  - ScopeManager：按文件维护根节点；由 sym_list 在解析时构建并持有（getScopeManager()）。
- 解析方式（栈式）
  - 在 sym_list::extractSymbolsAndContainsOnePassImpl 中先调用 SVTreeSitterParser::parseSymbols() 得到符号列表（符号已含 moduleScope），再按符号顺序维护 QStack<ScopeNode*>：
    - 遇到 module / task / function 时创建对应 ScopeNode 并 push；
    - 遇到 reg / wire / logic / 端口时写入当前 scopeStack.top()->symbols 并照常 addSymbol；
    - 遇到 endmodule / endtask / endfunction（通过符号的 startLine/endLine）时设置 endLine 并 pop。
  - 结构符号与作用域闭合由 SVTreeSitterParser 产出的 SymbolInfo 驱动，不再使用 findNextStructuralMatch 等正则匹配。
- 接口
  - findScopeAt(fileName, line)：返回该行所在的最深层作用域。
  - resolveSymbol(name, startScope)：沿 parent 链向上查找符号，实现词法遮蔽（内层同名遮蔽外层）。
  - 在 clearSymbolsForFile 时会同步清除该文件的作用域树。

分析模式：
- 打开标签分析：`analyzeOpenTabs`
  - 对当前打开的 SV 文件（`TabManager::getOpenSystemVerilogFiles()`），先对该批文件名执行 `clearSymbolsForFile`，再逐文件通过 `getPlainTextFromOpenFile` 取内容后调用 `analyzeFileContent`，不依赖编辑器对象
- 工作区分析：`analyzeWorkspace` / `startAnalyzeWorkspaceAsync`
  - 通过 `WorkspaceManager` 拿到整个目录树中的 `.sv/.v/.vh/.svh/.vp/.svp` 文件
  - 使用 QFile+QTextStream 读入内容，每个文件送入 `sym_list::setContentIncremental`（阶段 B 轻量化，不创建 MyCodeEditor）
- 单文件分析：`analyzeFile`
  - 供文件变化回调 (`fileChanged`) 调用

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
    - 可跳转定义类型包含：module/interface/package/task/function、端口、reg/wire/logic/parameter/localparam，**struct 类型与变量**，以及 **enum 类型、枚举变量、枚举值**；SVTreeSitterParser 已产出 module/task/function/port/reg/wire/logic/parameter/localparam/typedef/enum 等，可跳转；struct 相关符号尚未产出，对应跳转会缺失或异常。
    - 优先跳当前文件中的定义；端口类型优先级高于 reg/wire/logic。
    - 再考虑其他文件中、且仍在当前模块作用域内的定义（若有）。
  - **Struct 相关跳转**：
    - **成员跳转**：在 `var.member` 表达式中 Ctrl+点击成员名（如 member0），根据变量名解析出 struct 类型，跳转到该 struct 内该成员的定义位置；结构体成员的 moduleScope 为结构体类型名，跳转时按类型过滤、不按模块名过滤。
    - **变量跳转**：Ctrl+点击 struct 变量名，跳转到其声明（packed/unpacked struct 变量已纳入 isSymbolDefinition 与 definitionTypePriority）。
    - **类型名跳转**：在声明语句（如 `test_s test_s_var;` 或 `test_sp test_sp_var;`）中 Ctrl+点击类型名，跳转到 `typedef struct [packed] { ... } type_name;` 中别名位置。当前 Tree-sitter 路径不产出 struct 类型符号，该跳转会缺失或异常。definitionTypePriority 中 sym_packed_struct / sym_unpacked_struct 显式优先级 6，与 parameter/localparam 一致。
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
- `SlangManager`（slangmanager.h/cpp）：基于 Slang 的语义分析，当前仅用于提取模块/接口/程序实例化（InstanceSymbol），供 SmartRelationshipBuilder 写入 INSTANTIATES 关系。
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
- Tree-sitter：符号解析依赖 thirdparty/tree_sitter 与 thirdparty/tree_sitter_systemverilog，用于 SVTreeSitterParser；构建时需能编译 C 源（lib.c、parser.c）
- Slang：thirdparty/slang 子项目，用于 SlangManager 的语义分析（模块实例化）；CMake 已配置 slang::slang 链接与 C++20

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
     等接口，直接对 QString 调用 sym_list::setContentIncremental（内部由 SVTreeSitterParser::parseSymbols() 解析），
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
    直接对 QString 调用 sym_list::setContentIncremental。
  - setContentIncremental 内部统一走 extractSymbolsAndContainsOnePass，即 SVTreeSitterParser::parseSymbols()；
    非首次分析也改为全量重算，保证符号唯一来自 SVTreeSitterParser（当前该路径存在功能异常）。
  - analyzeWorkspace / analyzeFile 改为 QFile+QTextStream 读内容后调用
    setContentIncremental，不再创建 MyCodeEditor。
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
  - sym_list：在 extractSymbolsAndContainsOnePassImpl 中先调用 SVTreeSitterParser::parseSymbols() 得到符号列表，
    再按符号顺序栈式构建作用域树；clearSymbolsForFile 时同步 clearFile 作用域树；
    getScopeManager() 惰性创建并返回 ScopeManager。findNextStructuralMatch 已移除，结构符号由 SVTreeSitterParser 产出；typedef/enum 已产出，struct 尚未产出。
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
    解析统一走 analyzeFileContent(fileName, content)，sym_list 仅使用 setContentIncremental。
  - sym_list：已移除无调用者的 setCodeEditor / setCodeEditorIncremental，解析入口仅保留
    setContentIncremental(fileName, content)。持写锁分析路径中，findSymbolIdByName / getSymbolById
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
  - 符号解析：仅使用 SVTreeSitterParser，setContentIncremental 通过 extractSymbolsAndContainsOnePass
    调用 SVTreeSitterParser::parseSymbols()。SVSymbolParser 已从工程中移除。语法高亮由 SVLexer 驱动；hasSignificantChanges 等
    改为简单字符串/词边界判断。

若发现新的冗余，可参考本节原则处理并更新本段说明。

==========================================================================
已知问题 (Known Issues)
==========================================================================

- **UI 卡顿 (UI Lag)**：以下操作时界面可能出现短暂卡顿或响应延迟，属已知现象，后续会通过进一步异步化与分批更新优化：
  - 打开工作区：选择目录并加载工作区时，会触发批量文件扫描与符号分析，主线程可能参与进度更新或数据回写，导致界面短暂不流畅。
  - 从导航页打开文件：在导航面板中点击文件或符号进行跳转/打开时，若需加载大文件或触发符号/关系查询，会有可感知的延迟。
  - 符号分析进行中：打开标签分析、工作区分析或单文件分析执行时，若与 UI 刷新、导航树更新、状态栏更新等叠加，可能出现卡顿。当前分析已采用 QtConcurrent 等异步方式，但结果回主线程写回符号库/关系引擎、刷新导航与补全缓存时仍可能造成主线程短时繁忙。

- **双轨解析**：符号解析仅使用 Tree-sitter（SVTreeSitterParser）；模块实例化关系使用 Slang（SlangManager）。
  extractSymbolsAndContainsOnePassImpl 使用 SVTreeSitterParser::parseSymbols()；SVSymbolParser 已从工程中移除。
  parameter/localparam/typedef/enum 已产出；struct 相关符号未产出，s/sp/ns/nsp 补全与跳转会缺失或异常。commentRegions 由 takeComments() 提供，当前返回空列表。

- **Tree-sitter（SVTreeSitterParser）**：主路径已使用 parseSymbols() 写入符号库；工具栏「Tree-sitter 验证」
  按钮仍通过 parse(content) + getSymbols() 做验证输出。module 名称已从 module_ansi_header / module_nonansi_header
  正确提取；若后续发现其他声明类型在部分 grammar 结构下名称为空，可参考 nameFromDeclarationNode 的 fallback 逻辑扩展。
  **「类型名 变量名;」被解析为 wire**：因 grammar 歧义，`TYPE_NAME id;` 会优先匹配 net_declaration（PREC_DYNAMIC 0），
  故如 `test_e test;` 的变量会显示为 wire。建议在符号层根据 nettype_identifier 是否为本文件已解析的 typedef/enum 类型做语义补救，按变量产出。

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
  的缓存内容做“光标是否在模块内”判定（CompletionManager::findModuleAtPosition），不再仅依赖磁盘读取与 findEndModulePosition 正则。当前符号由 SVTreeSitterParser 产出。若仍显示「无模块」，
  ​可能原因包括：（1）缓存内容与编辑器当前内容不一致，cursorPosition 在“缓存 + position 转行号”
  时产生偏差；（2）需改为传入编辑器当前缓冲区内容做 position-to-line，使行号与光标所在文档一致。
  建议后续：getCurrentModule 或 findModuleAtPosition 支持可选“当前文档内容”参数，优先用其做
  position 转 cursorLine，无再回退到缓存/磁盘。

- **作用域树 (Scope Tree)**：getCompletions(prefix, cursorFile, cursorLine) 基于 findScopeAt
  的按作用域补全与词法遮蔽已实现框架；struct 相关命令（s/sp/ns/nsp）的严格作用域（模块外
  不补全、模块内聚合 internal+include+import、全局仅 moduleScope 为空）已修复。若发现其他
  按作用域补全或跳转行为异常，可优先排查作用域解析与 getCurrentModule 边界。

- **作用域背景 (Scope Background)**：左侧条带与编辑器内 module/logic 的背景由符号分析驱动。
  已采用“持久光标缓存”：编辑时 highlighCurrentLine 仅使用缓存的 m_scopeSelections（Qt 会随
  文档自动更新光标位置），背景随文本移动无回弹；分析完成后 refreshScopeAndCurrentLineHighlight
  调用 updateScopeBackgrounds 从数据库刷新缓存。若分析尚未完成则沿用上一轮缓存。


==========================================================================
备注 (Notes)
==========================================================================

- 本文件是面向“阅读/维护代码的人”的说明文档，侧重介绍目前仓库中已经实现的架构与能力。
- 如果你在阅读代码时发现 README 与实际实现不一致，以**代码实现**为准，再回过头来更新本文件即可。
