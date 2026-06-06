==========================================================================
ZeroSlack README / Development Handoff
==========================================================================

ZeroSlack 是一个面向 SystemVerilog 的轻量级代码编辑器 / 浏览器，基于 Qt 6 + CMake
开发。当前重点不是做完整 IDE，而是提供工程浏览、符号理解、补全、跳转和语法高亮等
“静态 IDE”能力。

当前版本：0.0.12/slang17
当前分支：tree_sitter_and_slang
版本显示：运行时显示在窗口标题和状态栏右下角，定义在 version.h。


==========================================================================
当前架构
==========================================================================

ZeroSlack 现在采用 Route A 架构：

    Tree-sitter 实时语法层 + Slang 语义层

分工如下。

Tree-sitter 负责实时、容错、低延迟的信息：
- 语法高亮。
- 注释 / 字符串 / 关键字 / 数字 / 操作符分类。
- 每个编辑器文档内的增量语法树。
- 光标当前位置所属 module scope 的即时判断。

Slang 负责真实语义信息：
- module/interface/package/function/task/typedef/enum/struct/logic/wire/reg/port/instance 等符号提取。
- 类型、结构体成员、枚举值、实例化关系等语义信息。
- 补全候选、跳转目标、符号导航窗格、scope tree 和关系分析的数据来源。
- 工作区级符号分析。

关键原则：
- UI 即时反馈尽量走 Tree-sitter。
- 需要“这到底是什么类型/定义/关系”的问题走 Slang。
- sym_list 的符号数据以 Slang 为准。
- 旧 SVLexer 已删除，不应恢复。
- demo.pro 是废弃 qmake 工程，不维护；构建以 CMakeLists.txt 为准。


==========================================================================
核心文件
==========================================================================

main.cpp
- Qt 应用入口。

mainwindow.cpp / mainwindow.h
- 主窗口、菜单、工具栏、状态栏、工作区入口。
- 符号分析、关系分析、导航和版本号显示的协调层。

analysisscheduler.cpp / analysisscheduler.h
- 分析触发调度入口。
- 当前订阅 DocumentModel opened/edited/saved，集中打开文件分析、保存分析、打开文件编辑去抖、外部文件变更去抖和单文件关系分析显著变更判断；后台 watcher 和工作区批量关系进度 UI 暂仍在 MainWindow。

mycodeeditor.cpp / mycodeeditor.h
- 代码编辑器控件。
- 持有每个文档自己的 TSDocument。
- 负责按键、补全触发、当前行高亮、Ctrl+Click 跳转等编辑器交互。

tsdocument.cpp / tsdocument.h
- 每文档 live tree-sitter 语法树。
- 支持 UTF-16 文本、增量 edit、highlightSpans、enclosingModuleName。

documentmodel.cpp / documentmodel.h
- 打开文档状态模型。
- 当前由 TabManager 持有，跟踪 fileName、dirty/saved、textVersion、cursor 和 live module 名，并发出 opened/edited/saved/closed/cursorChanged 事件；TSDocument 所有权暂仍保留在 MyCodeEditor。

myhighlighter.cpp / myhighlighter.h
- QSyntaxHighlighter 封装。
- 从 TSDocument 读取当前 block 的语法 span 并 setFormat。

slangmanager.cpp / slangmanager.h
- Slang 语义提取入口。
- 负责从 Slang AST / symbol model 中产出 sym_list::SymbolInfo。

symbolanalyzer.cpp / symbolanalyzer.h
- SlangManager 的调度层。
- 单文件分析走后台线程，结果回主线程写入 sym_list。
- 工作区分析在后台提取 Slang 符号，并把每个文件的内容一起回主线程写入 sym_list，用于初始化增量状态。
- hasSignificantChanges 用于判断是否需要重跑语义/关系分析。

syminfo.cpp / syminfo.h
- 全局符号数据库 sym_list。
- 保存符号、scope tree、关系分析所需的部分缓存。

semanticindex.cpp / semanticindex.h
- 语义查询 facade。
- 当前内部暂包 sym_list 和现有服务，提供 symbols、definitions、completions、relationships、diagnostics 查询入口；后续新功能优先经由它读取语义事实。

completionmanager.cpp / completionmanager.h
- 补全逻辑。
- 使用 Slang 符号库 + live Tree-sitter 当前 module 信息。

navigationmanager.cpp / navigationwidget.cpp
- 导航窗格数据组织和 UI 展示。

smartrelationshipbuilder.cpp / symbolrelationshipengine.cpp
- 关系分析和关系存储。
- module instantiation、task/function call、assignment、condition/control read、timing sensitivity 由 Slang AST / semantic model 提供；clock/reset 在 Slang timing control 基础上按符号名分类。


==========================================================================
已完成的架构迁移
==========================================================================

A1: TSDocument 增量文档模型
- 每个编辑器文档持有自己的 TSTree。
- contentsChange 时调用 applyEditChars 做增量更新。
- 使用 UTF-16 解析，中文注释等非 ASCII 偏移正确。

A2: Tree-sitter 高亮
- 高亮由 Tree-sitter span 驱动。
- 支持关键字、注释、字符串、数字、操作符等分类。
- 旧 SVLexer 已删除。

A3: live scope
- 当前 module 判断改用 TSDocument::enclosingModuleName。
- 补全和跳转不再依赖滞后的 Slang scope 判断。
- 光标在 module 内时，能即时得到当前 module。

A4: 架构收敛
- Slang 作为符号与语义唯一来源。
- Tree-sitter 作为实时 UI 语法层。
- readme 与代码路径按该架构整理。


==========================================================================
slang16 性能修复记录
==========================================================================

用户复现问题：
- 打开工作区 test_sv/new。
- 在大 SystemVerilog 文件里连续输入换行。
- 随后上下移动光标会卡顿。

定位结论：
- 纯换行/空格这类空白编辑本不改变语义，却会触发后续语义/关系分析路径。
- 旧的行数变化触发逻辑会把“插入空行”误判成需要重分析。
- 旧的作用域阴影背景是调试残留，会造成大范围 ExtraSelection 和不必要重绘。

已修复：
- 纯空白输入不再启动关系分析 debounce。
- 行数变化不再直接调度 Slang 单文件符号分析。
- hasSignificantChanges 改为比较“结构关键字行集合”，插入/删除空行不再误判为显著结构变化。
- 删除作用域阴影背景，仅保留当前行高亮。
- 删除临时性能日志 perflog.h、notify 探针和 Perf 压测按钮。

保留能力：
- 高亮仍由 Tree-sitter 实时更新。
- 补全、跳转、导航、符号分析和关系分析没有被整体禁用。
- 真正修改 module/function/task/logic/wire/reg 等结构性代码时，仍可触发必要分析。


==========================================================================
主要功能
==========================================================================

编辑器基础
- 多标签文本编辑。
- 新建、打开、保存、另存为。
- 行号栏。
- 当前行高亮。
- SystemVerilog 语法高亮。

模式
- Normal Mode：普通编辑。
- Command Mode：通过前缀触发符号补全。
- Alternate Mode：命令式操作，如 save/open/copy/paste/comment 等。

补全
- 支持模糊匹配和前缀匹配。
- 支持 module/reg/wire/logic/task/function/interface/parameter 等符号。
- 支持 enum/struct 相关命令。
- 支持 struct member 和 enum value 补全。
- 当前 module 由 Tree-sitter live scope 提供，候选符号来自 Slang。

跳转
- Ctrl+Click 跳转到定义。
- 支持本文件和跨文件跳转。
- module、typedef、enum、struct、function/task 等符号跳转由 Slang 符号库支撑。

导航窗格
- 显示 module、interface、parameter、port、reg/wire/logic、task/function、typedef、enum、struct、instance 等符号分类。
- function/task 内部形参和局部变量不混入 module 级逻辑分组。

关系分析
- 支持实例化关系等工程浏览能力。
- 单文件关系分析带显著变更判断，避免空白编辑触发重活。
- 多 module 文件中的实例化、调用、条件读取、时钟/复位关系按所在行归属到对应 module。
- task/function call、assignment、condition/control read、timing sensitivity 关系由 Slang AST 解析结果驱动，不再使用正则猜测。
- clock/reset 关系基于 Slang timing control 的符号引用和符号名分类。


==========================================================================
构建
==========================================================================

推荐构建方式：CMake + Ninja + Qt 6 MinGW。

当前本机常用命令：

    $env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;$env:PATH"
    Set-Location "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug"
    & "E:\QT6\Tools\Ninja\ninja.exe" demo

运行：

    $bd = "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug"
    $env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;$env:PATH"
    $env:QT_PLUGIN_PATH = "E:\QT6\6.10.2\mingw_64\plugins"
    Start-Process -FilePath "$bd\demo.exe" -WorkingDirectory $bd

注意：
- 不要使用 demo.pro / qmake 作为正式构建入口。
- 如果 g++/Qt DLL 找不到，优先检查 PATH 和 QT_PLUGIN_PATH。


==========================================================================
测试和验证
==========================================================================

当前已接入 CTest 回归测试：

    ctest --output-on-failure

应发现并运行 6 个测试。

已注册的无头/近无头测试：
- test_sv/ts_doc_test.cpp：Tree-sitter 文档与高亮相关验证。
- test_sv/completion_test.cpp：补全逻辑验证。
- test_sv/jump_test.cpp：跳转逻辑验证。
- test_sv/relationship_test.cpp：多 module 文件中的关系归属回归验证。
- test_sv/gui_smoke_test.cpp：Qt offscreen GUI 烟测，覆盖打开工作区、打开大文件、编辑换行/字符、移动光标、补全弹窗、Ctrl+Click 和导航窗格双击跳转。
- test_sv/large_file_perf_test.cpp：大文件性能基线，验证空白/换行编辑不启动不必要的符号分析或关系分析 debounce，同时确认普通字符编辑仍保留必要分析入口。

这些测试通过 CMakeLists.txt 注册为 ts_doc_test、completion_test、jump_test、relationship_test、gui_smoke_test、large_file_perf_test。
completion_test、jump_test 和 relationship_test 由 CTest 注入 Qt offscreen 运行环境。
gui_smoke_test 和 large_file_perf_test 也由 CTest 注入 Qt offscreen 运行环境；large_file_perf_test 使用 test_sv/new。

GUI 相关能力仍需人工或 GUI 自动化验证：
- 已有最小 GUI 自动化烟测覆盖 Ctrl+Click、补全弹窗、导航窗格双击跳转、打开工作区后的大文件基础编辑。
- 已有大文件性能基线覆盖空白/换行编辑不触发不必要的 Slang / 关系重分析路径。
- 仍建议继续扩展 GUI 自动化覆盖更多真实工作区操作和失败诊断。

当前人工验证记录：
- 已在 test_sv/new 工作区验证大文件编辑流畅度。
- 覆盖连续输入换行、上下移动光标、补全触发、Ctrl+Click 基本交互。
- 未观察到 slang16 记录中的空白编辑后光标移动卡顿回归。


==========================================================================
后续建议
==========================================================================

目标骨架见 goal.md；详细执行计划见 plan.md；README 只保留当前状态和交接信息。
后续功能和重构默认以 goal.md 为现阶段最终目标，除非遇到无法越过的实现问题，不再为每个新功能重新选择架构方向。

已完成 P0
- completion_test.cpp、jump_test.cpp、ts_doc_test.cpp 已正式接入 CMake/CTest。
- 补全、跳转、Tree-sitter 文档/高亮/live scope 已有自动回归测试入口。
- relationship_test.cpp 已覆盖多 module 文件中的关系归属回归。

已完成 P1
- 已删除 sv_treesitter_parser / Tree-sitter 验证按钮的遗留用途。
- 已删除旧 Tree-sitter 符号提取路径和 sym_list 中对应的未调用入口。
- Tree-sitter 仍保留为 TSDocument 实时语法、高亮和 live scope 来源。

已完成 P2
- 工作区批量分析写回每个文件的 symbols + content，和单文件分析共用 sym_list 的内容哈希/符号相关哈希状态。
- 工作区重新扫描时会覆盖空符号文件的旧结果，并让后续 contentAffectsSymbols 判断有稳定基线。
- 关系写回仍可继续观察和细化。

slang17 已完成
- GUI / 工作区可靠性版本最小底座已落地。
- 已有最小 smoke test 覆盖：打开工作区、打开文件、输入换行/字符、上下移动、触发补全、Ctrl+Click 跳转、导航窗格双击跳转。
- 已有大文件性能基线覆盖：空白/换行编辑不启动不必要的符号分析或关系分析 debounce。
- 已有多文件关系 fixture 覆盖 package/import、跨文件实例化、调用、赋值、条件读取、clock/reset。
- SemanticIndex facade 已补最小入口，新增语义查询代码应优先通过 facade。
- DocumentModel 已补最小入口，由 TabManager 持有并跟踪打开文档状态。
- AnalysisScheduler 已补最小入口，打开/保存/编辑去抖/外部文件变更/单文件关系分析显著变更判断已开始集中到 scheduler。
- 后续可继续补更细的失败诊断、更多真实工作区路径和跨文件跳转 GUI 验证。

已完成 P3
- 已修正多 module 文件中部分关系误归属到首个 module 的问题。
- 关系分析中的实例化、task/function call、assignment、condition/control read、timing sensitivity 已迁移到 Slang AST / semantic model。
- smartrelationshipbuilder 不再使用 QRegularExpression；注释和字符串中的伪调用/伪赋值不再参与这些关系来源。
- clock/reset 关系基于 Slang timing control 的符号引用和符号名分类，后续仍可按真实工程样例细化分类规则。

slang17 交付状态
- 目标是“GUI / 工作区可靠性版本”，而不是继续扩大架构重构范围。
- GUI 自动化烟测已补最小入口：打开工作区、打开大文件、编辑换行/字符、移动光标、触发补全、Ctrl+Click、导航窗格跳转。
- 大文件性能基线已补最小入口：空白/换行编辑后不触发不必要的 Slang / 关系重分析；连续编辑和移动光标仍可继续扩展更细性能断言。
- 已增加一个更接近真实工程的多文件关系 fixture，验证 package/import、跨文件实例化、调用、赋值、条件读取、clock/reset；跨文件 Ctrl+Click GUI 验证仍可继续补。
- SemanticIndex facade 已补最小入口；新增语义查询代码应优先通过 facade。
- DocumentModel 已补最小入口；分析触发已开始由 AnalysisScheduler 接管。
- AnalysisScheduler 已补最小入口；下一步优先补 ProjectModel 最小版，或继续把 workspace 批量关系分析触发搬入 scheduler。
- 版本号已更新到 0.0.12/slang17；完成标准见 goal.md 和 plan.md；新会话优先读取 readme.txt、plan.md 和 goal.md。


==========================================================================
给下一个会话的快速交接
==========================================================================

如果新开 Codex 会话，建议先让它读这些文件：

1. readme.txt
2. plan.md
3. goal.md
4. version.h
5. analysisscheduler.cpp / analysisscheduler.h
6. semanticindex.cpp / semanticindex.h
7. documentmodel.cpp / documentmodel.h
8. mycodeeditor.cpp / mycodeeditor.h
9. tsdocument.cpp / tsdocument.h
10. slangmanager.cpp / slangmanager.h
11. symbolanalyzer.cpp / symbolanalyzer.h
12. completionmanager.cpp / completionmanager.h

当前不要误会的点：
- goal.md 是现阶段产品 / 架构最终骨架；后续实现默认按 ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services 方向收敛。
- 不是“禁用分析”解决卡顿，而是让空白编辑不再误触发语义/关系分析。
- Slang 是符号语义来源；Tree-sitter 是实时语法和当前 scope 来源。
- SVLexer 已经不是当前架构的一部分。
- readme 中不再保留旧性能插桩方案；后续性能调试应按问题重新加针对性探针。
- test_sv/new 是用户用来复现大文件卡顿的本地工作区数据，不要随便删除或纳入提交。
