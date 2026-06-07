# ZeroSlack README / Development Handoff

当前版本：0.0.18/slang23
当前分支：tree_sitter_and_slang
构建系统：Qt 6 + CMake + Ninja，demo.pro / qmake 不再维护。

ZeroSlack 当前目标不是一次性做完整 IDE，而是先成为可靠的
SystemVerilog 工程浏览器 / 轻量编辑器：能打开真实 workspace，能理解符号、
补全、跳转、导航和关系，并且在大文件里保持可验证的响应性。

核心路线保持不变：

    Tree-sitter 实时语法层 + Slang 真实语义层

Tree-sitter 负责编辑器即时体验：
- 语法高亮。
- 每个打开文档的增量语法树。
- 当前光标位置 enclosing module / live scope。

Slang 负责真实语义事实：
- module/interface/package/function/task/typedef/enum/struct/logic/wire/reg/port/instance 等符号。
- struct member、enum value、实例化、调用、赋值、读引用、clock/reset 等关系来源。
- 补全、跳转、导航、关系浏览、后续诊断的事实基础。

不要恢复：
- SVLexer。
- 旧 Tree-sitter symbol parser。
- Tree-sitter 验证按钮。
- 关系分析正则路径。
- 长期散落的 perflog 式性能探针。


## 当前架构状态

已落地的底座：

- ProjectModel
  - 由 WorkspaceManager 持有。
  - 发布 ProjectSnapshot，包含 workspace root、SV 文件集合、include dirs、defines、
    可选 filelist/top module/ignored paths。
  - workspace opened/rescanned/project config changed 通过 projectChanged 进入 scheduler。

- DocumentModel
  - 由 TabManager 持有。
  - 跟踪打开文档的 fileName、dirty/saved、textVersion、cursor、live module。
  - GUI smoke 已覆盖打开和编辑事件。

- AnalysisScheduler
  - 订阅 DocumentModel opened/edited/saved 和 ProjectModel projectChanged。
  - 集中处理打开文件分析、保存分析、编辑去抖、外部文件变更去抖。
  - workspace 符号分析通过 ProjectSnapshot 触发。
  - 符号分析完成后由 scheduler 接续启动 workspace 关系分析。
  - workspace 关系分析 watcher / cancel / finished 信号已收束到 scheduler。
  - 进度 UI 暂仍在 MainWindow。

- SemanticIndex
  - 当前仍以 sym_list-backed facade 为默认 live 数据源。
  - 已新增最小只读 SemanticIndexSnapshot，可保存 symbols / relationships / diagnostics。
  - SemanticIndex 已支持 setSnapshot / clearSnapshot；有 snapshot 时 symbols / definitions /
    relationships / diagnostics 优先读 snapshot。
  - 提供 symbols / definitions / completions / relationships / diagnostics 查询入口。
  - getSymbols(fileName) 已补路径规范化兜底，避免 Windows 路径格式差异导致本地查询失败。
  - getSymbolById / findSymbolId / cached file content / scope symbol names / struct refresh
    已作为过渡 facade 暴露，减少消费端直接读 sym_list。

- Query Services
  - DefinitionService：Ctrl+Click / jump definition 主要路径已迁入 service；集中处理本文件优先、
    module scope、struct member 类型过滤、跨文件目标选择。
  - CompletionService：普通补全、struct member 专用补全、命令模式补全已开始经由 service；
    普通补全的 SymbolInfo 详情也从 editor 迁入 service。
  - RelationshipService：关系查询入口已扩展，支持 related symbol id 查询和精确 hasRelationship。
  - HierarchyService：实例层级查询入口已落地，并改用 SemanticIndex 做 ID 反查。
  - ReferenceService：最小入口已落地，当前包装 RelationshipService 的 incoming reference 查询，
    名称解析改走 SemanticIndex::findSymbolId；MainWindow 已有 References dock 作为真实 UI 消费点。
  - DiagnosticService：当前委托 SemanticIndex::getDiagnostics，已消费 Slang diagnostics 并服务 Problems 面板。
  - SearchService：symbol search 最小入口已落地，支持文本、文件、类型、exact、maxResults。

- Navigation
  - 新增 ModuleHierarchyGroup / SymbolOutlineGroup 显式 UI 数据模型。
  - NavigationManager 已开始消费 DefinitionService / HierarchyService / SearchService / SemanticIndex。
  - 模块层级优先通过 HierarchyService / RelationshipService 构造实例化树，缺失时回退文件分组。
  - 符号 outline cache 现在保留完整 SymbolInfo，不再只传 symbol name。
  - NavigationWidget 双击符号时直接从 item payload 取完整 SymbolInfo，不再反查 sym_list 或 service。

- Problems / References / Relationships UI
  - Problems dock 经 DiagnosticService 读取 snapshot diagnostics，支持 current file / all files 和 severity 筛选。
  - Problems / References / Relationships 条目双击均可跳转到文件、行、列。
  - References dock 通过 ReferenceService 查询 incoming references，并支持 all files / current file 筛选。
  - Relationships dock 通过 RelationshipService 查询 incoming / outgoing relationships，并支持方向 / 类型筛选。

- Editor
  - MyCodeEditor 跳转定义 / tooltip / canJump 旧的不可达 sym_list 实现已删除，当前经由 DefinitionService。
  - 普通补全、struct member 补全、命令模式补全已开始经由 CompletionService。
  - CompletionManager 的只读符号查询、关系读取、scope symbol names、cached file content
    已大幅收束到 SemanticIndex / RelationshipService。
  - 仍有少量旧直连点，主要集中在 relationship builder 初始化、Scope/文档状态过渡适配、
    MainWindow 分析协调和编辑器触发细节。


## 上一轮 0.0.17/slang22 已完成

- 新增 semanticindexsnapshot.cpp / semanticindexsnapshot.h。
- 新增最小只读 SemanticIndexSnapshot：
  - 保存 symbols / relationships / diagnostics 的只读副本。
  - 支持 getSymbols / getSymbolsByType / getSymbolById / findDefinitions / findSymbolId /
    getRelationships / getDiagnostics。
  - 支持从当前 sym_list + relationship engine 构造快照。
- SemanticIndex 增加 setSnapshot / clearSnapshot / snapshot。
- SemanticIndex 在 snapshot 存在时优先从 snapshot 读取 symbols / definitions / relationships / diagnostics。
- SemanticIndex 增加 getSymbolById / findSymbolId / getCachedFileContent /
  getScopeSymbolNames / refreshStructTypedefEnumForFile 过渡入口。
- CompletionManager 的大批只读符号查询迁到 SemanticIndex：
  - all symbols cache / type cache / symbol id lookup。
  - struct / enum / global / module internal completion 查询。
  - cached file content 和 scope symbol names。
- CompletionManager 的关系读取迁到 RelationshipService：
  - module children。
  - related symbols / references。
  - clock/reset relationship completion。
  - relationship score。
- RelationshipService 增加 findRelatedSymbolIds 和精确 hasRelationship。
- HierarchyService / ReferenceService 的名称解析和 ID 反查改走 SemanticIndex。
- SymbolRelationshipEngine 修复查询缓存方向性问题：
  - cache key 现在区分 outgoing / incoming。
  - 单条/单符号关系变更时清空关系查询缓存，避免方向复用旧结果。
- relationship_test 增加：
  - SemanticIndex getSymbolById / findSymbolId / cached content / scope symbols 断言。
  - RelationshipService related ids / exact relationship 断言。
  - SymbolRelationshipEngine outgoing/incoming cache direction 断言。
  - SemanticIndexSnapshot symbols / symbol id / relationships / clearSnapshot 断言。


## 上一轮 0.0.16/slang21 已完成

- CMakeLists.txt 注释清理为 ASCII，避免终端/patch 乱码影响。
- 新增 modulehierarchymodel.h。
- 新增 symboloutlinemodel.h。
- 新增 referenceservice.cpp / referenceservice.h。
- 新增 diagnosticservice.cpp / diagnosticservice.h。
- 新增 searchservice.cpp / searchservice.h。
- NavigationManager / NavigationWidget 模块层级迁到显式 ModuleHierarchyGroup。
- NavigationManager / NavigationWidget 符号 outline 迁到显式 SymbolOutlineGroup，保留完整 SymbolInfo payload。
- NavigationWidget 不再在符号双击时反查 sym_list / SearchService。
- MainWindow 单文件 / workspace 关系分析取文件符号改走 SearchService。
- MyCodeEditor 跳转定义路径清掉不可达旧 sym_list 代码。
- SemanticIndex / DefinitionService 补路径规范化，修复 Windows 路径格式差异下的本地跳转。
- CompletionService 普通补全 SymbolInfo 兜底迁入 service。
- gui_smoke_test 增加：
  - module hierarchy model regression。
  - symbol outline payload regression。
- relationship_test 增加：
  - ReferenceService 断言。
  - DiagnosticService 断言。
  - SearchService 断言。


## 验证记录

最近验证均通过：

- completion_test.exe：14 checks, 0 failed。
- jump_test.exe：10 checks, 0 failed。
- relationship_test.exe：74 checks, 0 failed。
- gui_smoke_test.exe：46 checks, 0 failed。
- 完整 ctest --output-on-failure：6/6 passed。
- git diff --check：无错误。
- 源码 / 测试 / UI / CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。

常见 warning：
- git 会提示 unable to access C:\Users\14971/.config/git/ignore: Permission denied。
- git 会提示 LF will be replaced by CRLF。
- 以上不是 diff --check 错误。


## 当前工作区注意事项

Claude 相关文件已删除；不要恢复 `.claude/` 或任何 Claude 本地配置文件。

提交或 push 只在用户明确要求时执行；提交前先确认真实内容 diff。

git status 可能列出一些没有内容 diff 的 M 文件，这是 Windows index stat / CRLF 噪音；
判断实际内容改动请优先看：

    git diff --name-only


## 已踩坑问题清单 / 防复发规则

以下问题已经在本项目会话中反复出现或造成误判，新会话接手时请优先检查：

1. 构建环境 PATH 不完整会造成假性编译失败
   - 直接调用 E:\QT6\Tools\CMake_64\bin\cmake.exe 或 g++.exe 时，如果没有把 Qt/MinGW bin
     加入 PATH，g++ 可能在启动 cc1plus.exe 或链接阶段静默失败，看起来像源码编译失败。
   - 正确构建前先设置：

        $env:PATH='E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\CMake_64\bin;' + $env:PATH

   - 推荐构建命令：

        E:\QT6\Tools\CMake_64\bin\cmake.exe --build build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug -- -j4

   - 推荐测试命令：

        E:\QT6\Tools\CMake_64\bin\ctest.exe --test-dir build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug --output-on-failure

2. 不要把 git status 当成真实内容 diff
   - 当前仓库在 Windows 上容易出现 index stat / CRLF 噪音，git status 会列出很多 M。
   - 判断真实内容变化优先用：

        git diff --name-only
        git diff --stat
        git diff --check

   - 常见 warning：
     unable to access C:\Users\14971/.config/git/ignore: Permission denied
     LF will be replaced by CRLF
     这些不是 diff --check 错误。

3. 不要恢复 Claude 本地配置
   - `.claude/` 相关文件已删除。
   - 后续不要重新创建、恢复或提交任何 Claude 本地配置文件。

4. 源码注释和 UI 文案必须保持英文 / ASCII
   - 代码注释不要再写中文。
   - UI 可见文字、状态栏、进度对话框、tooltip、日志文字全部保持英文。
   - 源码、测试、.ui、CMake 文件应避免非 ASCII 字符，三份中文文档除外：
     readme.md / plan.md / goal.md。
   - 清理后复扫建议：

        Get-ChildItem -Path . -Recurse -File -Include *.cpp,*.h,*.hpp,*.c,*.cc,*.ui,*.qss,*.cmake,CMakeLists.txt -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -notmatch '\\(build|thirdparty|\.git|\.qtcreator|\.agents|\.codex)\\' -and $_.Name -notin @('readme.md','plan.md','goal.md') } |
          Select-String -Pattern '[^\x00-\x7F]'

5. 注意编码显示误导
   - PowerShell 默认显示可能把 UTF-8 中文内容显示成乱码。
   - 读中文文档或旧中文源码时优先使用：

        Get-Content -Encoding UTF8 -Path <file>

   - 但源码侧的长期目标不是“正确显示中文”，而是避免中文/非 ASCII 再进入源码和 UI。

6. 不要恢复已淘汰路径
   - 不要恢复 SVLexer。
   - 不要恢复旧 Tree-sitter symbol parser。
   - 不要恢复 Tree-sitter 验证按钮。
   - 不要恢复关系分析正则路径。
   - 不要重新引入长期散落的 perflog 式性能探针。

7. Query Services 收束要小步验证
   - UI/editor 迁移到 DefinitionService / CompletionService / SearchService /
     RelationshipService / HierarchyService 时，每次只迁一小段路径。
   - 高风险点包括：
     completion 命令模式作用域过滤、struct member 补全、jump 本文件优先、
     Windows 路径规范化、NavigationWidget item payload。
   - 每轮至少跑 git diff --check；涉及行为变更时跑对应 exe 或完整 ctest。

8. 测试素材也要避免中文
   - 过去 ts_doc_test 使用中文注释测试 UTF-16 offset，后续已改为 ASCII。
   - 如需测试多字节字符，请优先用受控、明确的测试说明，并确认不会破坏“源码非 ASCII 复扫”规则；
     当前默认是不在源码测试中使用非 ASCII。

9. 文档可以中文，但不要用文档规则反推源码规则
   - readme.md / plan.md / goal.md 是中文交接文档，可以保留中文。
   - 源码注释、UI 文案、测试字符串、CMake 注释仍按英文 / ASCII 执行。

10. GUI smoke 访问私有 Qt widget 指针时要包含完整 Qt 类型
   - gui_smoke_test 使用 `#define private public` 直接访问 MainWindow 私有成员。
   - 如果测试要调用某个私有 Qt widget 指针的方法，不能只依赖 mainwindow.h 里的前置声明；
     测试文件本身需要 include 对应 Qt 头，例如 `#include <QComboBox>`。
   - 否则会出现 invalid use of incomplete type，而不是业务代码错误。


## 后续建议

优先级建议：

1. 继续迁移 UI / editor 消费端到 Query Services
   - MyCodeEditor 仍有少量旧触发细节。
   - CompletionManager 大块只读查询已迁到 services / SemanticIndex；后续继续清理状态性依赖。

2. 继续打磨 RelationshipService / HierarchyService 浏览 UI
   - Relationships dock 已有第一版 service-backed UI。
   - 后续可补分组、刷新策略和更完整的层级浏览。

3. 继续打磨 ReferenceService 的真实 UI 消费点
   - References dock 已有第一版 service-backed UI。
   - 后续可补引用结果分组、workspace 维度筛选和编辑器快捷键入口。

4. 继续加固 SemanticIndexSnapshot 生产 / 切换闭环
   - 后台符号分析和关系分析已能产出 snapshot，主线程按 base snapshot 世代切换 current snapshot。
   - Snapshot 当前保存 symbols / relationships / diagnostics / cached file content / minimal scope names。
   - 后续重点是继续减少 live sym_list 消费，并把更多 UI/service 读路径固定到 snapshot。

## 本轮 0.0.18/slang23 已完成

- Workspace / single-file 后台分析已真实产出 SemanticIndexSnapshot。
- MainWindow / AnalysisScheduler 在关系分析完成时按 base snapshot 世代发布 enriched snapshot，
  避免迟到后台任务覆盖新活动文件的语义状态。
- SymbolAnalyzer 在符号写回后立即发布 symbols-only snapshot，使 UI / services 始终能读到最新符号副本。
- SemanticIndexSnapshot 扩展为保存：
  - symbols。
  - relationships。
  - diagnostics。
  - cached file content。
  - minimal scope symbol names 查询所需信息。
- SemanticIndex 在 snapshot 存在时优先读取：
  - symbols / definitions / symbol id。
  - relationships。
  - diagnostics。
  - cached file content。
  - scope symbol names。
- SmartRelationshipBuilder 支持 snapshot-backed 解析；workspace / single-file 关系后台不再通过 SearchService
  读取可能过期的 current snapshot。
- Slang diagnostics 已接入：
  - SlangManager::extractDiagnostics。
  - SlangManager::extractWorkspaceDiagnostics。
  - SemanticDiagnostic。
  - SemanticIndexSnapshot。
  - DiagnosticService。
- MainWindow 新增底部 Problems dock：
  - 显示 Severity / File / Line / Column / Message。
  - 支持 current file / all files 和 severity 筛选。
  - 单文件分析完成、workspace batch 完成和活动 tab 切换后刷新。
  - 双击诊断可跳转到对应文件、行和列。
- MainWindow 新增底部 References dock：
  - 编辑器右键 Find References 触发。
  - 通过 ReferenceService 查询 incoming references。
  - 支持 all files / current file 筛选，筛选变化会复用当前 symbol 查询并刷新结果。
  - 双击引用结果可跳转到对应文件、行和列。
- MainWindow 新增底部 Relationships dock：
  - 编辑器右键 Show Relationships 触发。
  - 通过 RelationshipService 查询 incoming / outgoing relationships。
  - 支持 direction / type 筛选，筛选变化会复用当前 symbol 查询并刷新结果。
  - 双击关系结果可跳转到关系另一端符号。
- relationship_test 新增：
  - snapshot-only relationship builder 跨文件解析断言。
  - snapshot cached file content / scope names 断言。
  - Slang diagnostics -> snapshot -> DiagnosticService 断言。
- gui_smoke_test 新增：
  - single-file watcher result 类型更新。
  - Problems 面板显示真实 Slang diagnostic 的 GUI 回归。
  - References / Relationships dock 消费 snapshot-backed services 的 GUI 回归。

最近验证：
- Build 通过：demo / gui_smoke_test / relationship_test / completion_test / jump_test。
- 完整 ctest --output-on-failure：6/6 passed。
- relationship_test.exe：82 checks, 0 failed。
- gui_smoke_test.exe：71 checks, 0 failed。
- git diff --check 通过。
- 源码 / 测试 / UI / CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。


## 新会话交接文件

新会话建议先读：

1. readme.md
2. plan.md
3. goal.md
4. version.h
5. semanticindex.cpp / semanticindex.h
6. semanticindexsnapshot.cpp / semanticindexsnapshot.h
7. relationshipservice.cpp / relationshipservice.h
8. completionmanager.cpp / completionmanager.h
9. completionservice.cpp / completionservice.h
10. definitionservice.cpp / definitionservice.h
11. referenceservice.cpp / referenceservice.h
12. diagnosticservice.cpp / diagnosticservice.h
13. searchservice.cpp / searchservice.h
14. navigationmanager.cpp / navigationmanager.h
15. navigationwidget.cpp / navigationwidget.h
16. mycodeeditor.cpp / mycodeeditor.h
17. analysisscheduler.cpp / analysisscheduler.h
18. projectmodel.cpp / projectmodel.h


## 新对话开场白

请先阅读 readme.md、plan.md、goal.md、version.h，接手 ZeroSlack 当前状态。

当前分支：tree_sitter_and_slang。
当前版本：0.0.18/slang23。
最新提交已 push 到 origin/tree_sitter_and_slang。

重要规则：
- Claude 相关文件已删除，不要恢复 .claude/ 或任何 Claude 本地配置。
- 源码注释、测试字符串、CMake 注释、UI 可见文案要求英文 / ASCII。
- readme.md / plan.md / goal.md 是中文交接文档，可以保留中文。
- git status 可能有 stat/CRLF 噪声，实际内容变更优先看 git diff --name-only。
- 常见 warning：unable to access C:\Users\14971/.config/git/ignore: Permission denied；
  LF will be replaced by CRLF。这些不是 diff --check 错误。

最近完成：
- SemanticIndexSnapshot 已接入 workspace / single-file 后台分析生产和主线程世代切换。
- Snapshot 保存 symbols / relationships / diagnostics / cached file content / minimal scope names。
- Slang diagnostics 已接入 SemanticDiagnostic / SemanticIndexSnapshot / DiagnosticService。
- MainWindow 新增 Problems dock，显示 diagnostics，支持筛选，双击可跳转到行/列。
- MainWindow 新增 References dock，编辑器右键 Find References 经 ReferenceService 查询。
- MainWindow 新增 Relationships dock，编辑器右键 Show Relationships 经 RelationshipService 查询 incoming/outgoing。
- Workspace / single-file relationship 后台改为 snapshot-backed，避免旧 snapshot / live sym_list 读路径污染。
- relationship_test 增加 snapshot-only builder、snapshot cached content / scope names、Slang diagnostics 断言。
- gui_smoke_test 增加 Problems / References / Relationships dock 的 GUI 回归。

最近验证：
- cmake build 通过；新增 CMake 文件后重配置导致链接较慢，必要时先 -j4 后 -j1 续跑。
- 新会话验证不要默认把 demo 和所有测试目标一起重链。Debug/MinGW 下 demo、
  completion_test、gui_smoke_test 等 exe 可超过 450MB，单个链接可能超过 5 分钟。
  如果只是改 UI/service 小步，优先：
    1. 先跑 git diff --check 和非 ASCII 复扫；
    2. 只构建受影响的最小测试目标；
    3. 需要完整回归时直接给 build/ctest 足够长 timeout；
    4. 不要在 120s/300s 超时之间反复重启同一个链接任务。
- 完整 ctest --output-on-failure：6/6 passed。
- relationship_test.exe：82 checks, 0 failed。
- gui_smoke_test.exe：71 checks, 0 failed。
- git diff --check 通过。
- 源码/测试/UI/CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。

下一步建议：
1. 继续让 services / UI 只读 snapshot，减少 live sym_list 消费。
2. 继续打磨 Relationships dock：分组、刷新策略、层级浏览入口。
3. 继续打磨 References dock：结果分组、workspace 维度筛选、快捷键入口。
4. 继续改进 Problems 面板刷新策略和诊断跳转体验。
5. 保持 6 项 CTest 全绿。
