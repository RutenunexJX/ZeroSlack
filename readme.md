# ZeroSlack README / Development Handoff

当前版本：0.0.20/slang25
当前分支：tree_sitter_and_slang
构建系统：Qt 6 + CMake + Ninja，demo.pro / qmake 已删除，不要恢复。

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
  - SemanticIndex 已新增 captureSnapshotPreservingDiagnostics，后台关系分析的 base snapshot 生产统一经由 facade。
  - SemanticIndex 已新增 findEndModuleLine / contentAffectsSymbols 过渡入口，ScopeBandWidget 和
    AnalysisScheduler 不再直接读取 live sym_list 单例。
  - 提供 symbols / definitions / completions / relationships / diagnostics 查询入口。
  - getSymbols(fileName) 已补路径规范化兜底，避免 Windows 路径格式差异导致本地查询失败。
  - getSymbolById / findSymbolId / cached file content / scope symbol names / struct refresh
    已作为过渡 facade 暴露，减少消费端直接读 sym_list。

- Query Services
  - DefinitionService：Ctrl+Click / jump definition 主要路径已迁入 service；集中处理本文件优先、
    module scope、struct member 类型过滤、跨文件目标选择。
  - CompletionService：普通补全、struct member 专用补全、命令模式补全已开始经由 service；
    普通补全的 SymbolInfo 详情也从 editor 迁入 service。
  - RelationshipService：关系查询入口已扩展，支持 related symbol id 查询和精确 hasRelationship；
    Direct 关系浏览的 incoming/outgoing 合并、稳定排序、方向计数和类型计数已通过
    RelationshipReport / RelationshipBrowseQuery 下沉到 service；direction/type 嵌套计数也已由 report 提供。
  - HierarchyService：实例层级查询入口已落地，并改用 SemanticIndex 做 ID 反查。
    HierarchyReport 已统一 Relationships Tree 的节点、depth count、direction count、
    root direction count 和 type count；All Types 策略也下沉到 service。
  - ReferenceService：最小入口已落地，当前包装 RelationshipService 的 incoming reference 查询，
    名称解析改走 SemanticIndex::findSymbolId；MainWindow 已有 References dock 作为真实 UI 消费点；
    current/workspace scope 过滤、稳定排序、file/type 计数和 file/type 嵌套计数已通过 ReferenceReport 下沉到 service。
  - DiagnosticService：当前委托 SemanticIndex::getDiagnostics，已消费 Slang diagnostics 并服务 Problems 面板；
    severity/file 排序、file/severity 计数已通过 DiagnosticReport 下沉到 service。
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
  - Problems / References / Relationships 的结果排序、筛选结果集和摘要计数已主要下沉到
    DiagnosticService / ReferenceService / RelationshipService report API；MainWindow 只负责树渲染和窗口协调。
  - Problems / References / Relationships 树刷新会保留已有展开/折叠状态；首次渲染仍默认展开。

- Editor
  - MyCodeEditor 跳转定义 / tooltip / canJump 旧的不可达 sym_list 实现已删除，当前经由 DefinitionService。
  - MyCodeEditor 注释区补全抑制改为读取 TSDocument::isCommentAt 的 Tree-sitter live syntax，
    不再依赖 sym_list comment region。
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
qmake 相关文件已删除；不要恢复 `demo.pro`、任何 `*.pro` 或 `*.pri` 文件。

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

4. 不要恢复 qmake 文件
   - `demo.pro` 已删除。
   - 当前维护构建系统只有 Qt 6 + CMake + Ninja。
   - 后续不要重新创建、恢复或提交 `demo.pro`、任何 `*.pro` 或 `*.pri` 文件。

5. 源码注释和 UI 文案必须保持英文 / ASCII
   - 代码注释不要再写中文。
   - UI 可见文字、状态栏、进度对话框、tooltip、日志文字全部保持英文。
   - 源码、测试、.ui、CMake 文件应避免非 ASCII 字符，三份中文文档除外：
     readme.md / plan.md / goal.md。
   - 清理后复扫建议：

        Get-ChildItem -Path . -Recurse -File -Include *.cpp,*.h,*.hpp,*.c,*.cc,*.ui,*.qss,*.cmake,CMakeLists.txt -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -notmatch '\\(build|thirdparty|\.git|\.qtcreator|\.agents|\.codex)\\' -and $_.Name -notin @('readme.md','plan.md','goal.md') } |
          Select-String -Pattern '[^\x00-\x7F]'

6. 注意编码显示误导
   - PowerShell 默认显示可能把 UTF-8 中文内容显示成乱码。
   - 读中文文档或旧中文源码时优先使用：

        Get-Content -Encoding UTF8 -Path <file>

   - 但源码侧的长期目标不是“正确显示中文”，而是避免中文/非 ASCII 再进入源码和 UI。

7. 不要恢复已淘汰路径
   - 不要恢复 SVLexer。
   - 不要恢复旧 Tree-sitter symbol parser。
   - 不要恢复 Tree-sitter 验证按钮。
   - 不要恢复关系分析正则路径。
   - 不要重新引入长期散落的 perflog 式性能探针。

8. Query Services 收束要小步验证
   - UI/editor 迁移到 DefinitionService / CompletionService / SearchService /
     RelationshipService / HierarchyService 时，每次只迁一小段路径。
   - 高风险点包括：
     completion 命令模式作用域过滤、struct member 补全、jump 本文件优先、
     Windows 路径规范化、NavigationWidget item payload。
   - 每轮至少跑 git diff --check；涉及行为变更时跑对应 exe 或完整 ctest。

9. 测试素材也要避免中文
   - 过去 ts_doc_test 使用中文注释测试 UTF-16 offset，后续已改为 ASCII。
   - 如需测试多字节字符，请优先用受控、明确的测试说明，并确认不会破坏“源码非 ASCII 复扫”规则；
     当前默认是不在源码测试中使用非 ASCII。

10. 文档可以中文，但不要用文档规则反推源码规则
   - readme.md / plan.md / goal.md 是中文交接文档，可以保留中文。
   - 源码注释、UI 文案、测试字符串、CMake 注释仍按英文 / ASCII 执行。

11. GUI smoke 访问私有 Qt widget 指针时要包含完整 Qt 类型
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


## 本轮 0.0.19/slang24 已完成

- HierarchyService 新增 HierarchyReport，统一 Relationships Tree 所需的节点列表、总数、depth count、
  direction count、root direction count 和 type count。
- HierarchyService 新增 allRelationshipTypes，Relationships Tree 的 All Types 策略不再散落在 MainWindow。
- ReferenceReport 新增 fileTypeCounts，References dock 的 file -> relationship type 分组计数由 ReferenceService 提供。
- RelationshipReport 新增 directionCounts / directionTypeCounts，Relationships Direct 视图的 direction -> type
  分组计数由 RelationshipService 提供。
- MainWindow 的 References / Relationships Direct / Relationships Tree 改为消费更完整的 report count，
  删除对应的本地二级分组计数逻辑。
- Problems / References / Relationships 树刷新新增展开状态保持：首次渲染默认展开，后续刷新保留用户折叠/展开状态。
- relationship_test 新增 HierarchyReport、ReferenceReport file/type、RelationshipReport direction/type 回归。
- gui_smoke_test 新增 Relationships Tree 刷新后保留折叠 root 的 GUI 回归。

最近验证：
- cmake build 通过：relationship_test / gui_smoke_test。
- 完整 ctest --output-on-failure：6/6 passed。
- relationship_test.exe：114 checks, 0 failed。
- gui_smoke_test（ctest -V）：103 checks, 0 failed。
- git diff --check 通过。
- 源码/测试/UI/CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。
- 本轮 0.0.19/slang24 修改已包含在 0.0.20/slang25 交接提交中。


## 本轮 0.0.20/slang25 已完成

- SemanticIndex 新增 captureSnapshotPreservingDiagnostics，统一从当前语义 facade 生产 base snapshot 并保留上一代 diagnostics。
- SemanticIndexSnapshot 新增 withAdditionalRelationships，统一 enriched snapshot 的 relationship 合并和去重。
- SemanticIndexSnapshot 新增 withReplacedDiagnostics，支持按文件替换 diagnostics 并保留其它文件诊断。
- SemanticIndex 新增 captureSnapshotReplacingDiagnostics，统一单文件分析后的 snapshot capture 和 diagnostics 生命周期更新。
- AnalysisScheduler 的 workspace relationship base snapshot 生产改走 SemanticIndex facade，不再直接从 live sym_list 组装 diagnostics。
- MainWindow 的 single-file / workspace relationship 后台路径删除本地 relationship 去重合并逻辑，改为消费 snapshot helper。
- SymbolAnalyzer 的 single-file / open-tab 分析改为只替换目标文件 diagnostics，避免干净文件分析误清空其它文件 Problems。
- Problems 面板新增 Workspace Files scope；Current File / Workspace Files / All Files 均通过 DiagnosticService 查询。
- DiagnosticService / DiagnosticReport 新增 workspaceFilesOnly 过滤和 DiagnosticFileGroup，Problems 文件分组结果由 service report 提供。
- MainWindow 的 Problems All Files / Workspace Files 分组改为渲染 DiagnosticReport::fileGroups，不再本地组装 file key / label / count。
- Problems 面板在 workspace close 和 workspace symbol analysis start 时刷新，避免旧 diagnostics 视觉残留。
- gui_smoke_test 增加 Problems diagnostic preservation、Workspace Files scope、workspace close/reopen 清空回归，并让 expectBool 即时 flush stdout。
- relationship_test 新增 snapshot relationship merge 去重、新关系保留、capture snapshot 保留/替换 diagnostics、DiagnosticReport fileGroups / workspace 过滤回归。

最近验证：
- cmake build 通过：relationship_test / gui_smoke_test。
- 完整 ctest --output-on-failure：6/6 passed。
- relationship_test.exe：125 checks, 0 failed。
- gui_smoke_test（ctest -V）：103 checks, 0 failed。
- git diff --check 通过。
- 源码/测试/UI/CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。
- 本轮提交和上传前按用户要求未重复编译或测试；以上为代码改动后的最近一次验证记录。

## 本轮 0.0.20/slang25 追加完成

- SemanticIndex 新增 isValidModuleName / findEndModuleLine / contentAffectsSymbols 过渡 facade。
- ScopeBandWidget 改为通过 SemanticIndex 读取文件 symbols 和 module end line；snapshot 存在时可读
  snapshot cached file content，不再直接调用 sym_list 单例。
- AnalysisScheduler 的 skip-unchanged 判断改为通过 SemanticIndex::contentAffectsSymbols，不再直接读 sym_list 单例。
- MainWindow::setupRelationshipEngine 和 CompletionManager::setRelationshipEngine 改为通过 SemanticIndex 获取
  当前 symbol database，消费端不再硬编码 sym_list::getInstance。
- TSDocument 新增 isCommentAt，基于 Tree-sitter one_line_comment / block_comment 节点判断注释区。
- MyCodeEditor 的注释区自动补全抑制改为读取 TSDocument live syntax，不再依赖 sym_list comment region。
- relationship_test 新增 SemanticIndex facade / snapshot module end line 回归。
- ts_doc_test 新增 Tree-sitter comment query 回归。

最近验证：
- cmake build 通过：ts_doc_test / relationship_test / gui_smoke_test。
- ctest -R "ts_doc_test|relationship_test|gui_smoke_test" --output-on-failure：3/3 passed。
- 完整 ctest --output-on-failure：6/6 passed。
- git diff --check 通过。
- 源码/测试/UI/CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。
- 本轮提交和上传前按用户要求未重复编译或测试；以上为用户要求前的最近一次验证记录。


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
当前版本：0.0.20/slang25。
最新提交为本轮 0.0.20/slang25 交接提交，已 push 到 origin/tree_sitter_and_slang；具体哈希以 `git log -1 --oneline` 为准。

重要规则：
- Claude 相关文件已删除，不要恢复 .claude/ 或任何 Claude 本地配置。
- qmake 相关文件已删除，不要恢复 demo.pro、任何 *.pro 或 *.pri 文件。
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
- References dock 已增强为 file -> relationship type -> result 两级分组，新增 Workspace Files 和 Reference type 筛选。
- Relationships dock 已增强为 Direct / Tree 视图：Direct 按 direction -> relationship type -> result 分组；
  Tree 通过 HierarchyService 浏览层级并支持 Depth 1..4。
- Problems dock 在 All Files 模式下按 file 分组，Current File 保持紧凑列表，刷新加 100ms 合并。
- 编辑器新增快捷键入口：Shift+F12 Find References，Ctrl+Shift+R Show Relationships。
- large_file_perf_test 已跟随 single-file relationship watcher 的新 result 类型修正。
- 本轮继续打磨底部面板浏览体验：
  - HierarchyService 支持 Children / Parents / Both 方向的 hierarchy 查询。
  - Relationships Tree 支持双向树、递归路径去重、Root 节点、Incoming / Outgoing 分支和方向筛选。
  - Relationships Tree 的 All Types 覆盖全部关系类型，不再隐式收窄到 Instantiates。
  - Relationships Direct / Tree、References、Problems 的结果排序和分组计数已增强。
  - Problems 空状态显示 No problems，组节点双击不会触发空路径跳转。
- 本轮继续下沉底部面板结果职责：
  - DiagnosticService 新增 DiagnosticReport，统一排序、总数、file count 和 severity count。
  - ReferenceService 新增 ReferenceReport，统一 current/workspace scope 过滤、排序、file count 和 type count。
  - RelationshipService 新增 RelationshipBrowseQuery / RelationshipReport，统一 Direct 视图 incoming/outgoing 合并、
    排序、方向计数和 type count。
  - MainWindow 删除 Problems / References / Relationships 本地排序逻辑，改为消费 service report 并只做树渲染。
- 本轮继续下沉 Relationships Tree 和二级分组计数职责：
  - HierarchyService 新增 HierarchyReport 和 allRelationshipTypes。
  - ReferenceReport 新增 fileTypeCounts。
  - RelationshipReport 新增 directionCounts / directionTypeCounts。
  - MainWindow 删除 References / Relationships 的本地二级分组计数，并保留底部树刷新前的展开/折叠状态。
- 本轮继续收束 snapshot 生产和 relationship 合并职责：
  - SemanticIndex 新增 captureSnapshotPreservingDiagnostics。
  - SemanticIndexSnapshot 新增 withAdditionalRelationships。
  - AnalysisScheduler / MainWindow 的关系后台路径改为通过 snapshot/facade helper 生产 base/enriched snapshot。
- 本轮继续打磨 Problems diagnostics 生命周期和 service report：
  - SemanticIndexSnapshot 新增 withReplacedDiagnostics。
  - SemanticIndex 新增 captureSnapshotReplacingDiagnostics。
  - SymbolAnalyzer 的 single-file/open-tab 分析只替换目标文件 diagnostics，保留其它文件 diagnostics。
  - DiagnosticService / DiagnosticReport 新增 workspaceFilesOnly 过滤和 DiagnosticFileGroup。
  - Problems 面板新增 Workspace Files scope，并改为渲染 DiagnosticReport::fileGroups。
  - Problems 面板在 workspace close 和 workspace symbol analysis start 时刷新清空旧状态。
  - gui_smoke_test 覆盖外部 diagnostic 保留、Workspace Files 隐藏外部 diagnostic、All Files 恢复显示、workspace close/reopen 清空。
- 本轮追加继续减少 live sym_list 消费：
  - SemanticIndex 增加 module end line、module name validity 和 contentAffectsSymbols facade。
  - ScopeBandWidget / AnalysisScheduler / MainWindow / CompletionManager 不再直接调用 sym_list::getInstance。
  - TSDocument 增加 isCommentAt；MyCodeEditor 注释区补全抑制改走 Tree-sitter live syntax。
  - relationship_test / ts_doc_test 增加对应回归。

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
- 本轮追加改动后，ts_doc_test / relationship_test / gui_smoke_test 对应 CTest 均通过。
- relationship_test.exe：125 checks, 0 failed。
- gui_smoke_test（ctest -V）：103 checks, 0 failed。
- git diff --check 通过。
- 源码/测试/UI/CMake 非 ASCII 复扫为空，排除 readme.md / plan.md / goal.md。
- 本轮提交和上传前按用户要求未重复编译或测试；以上为代码改动后的最近一次验证记录。

下一步建议：
1. 继续让 services / UI 只读 snapshot，减少 live sym_list 消费。
2. 继续把 MainWindow 中的分析/刷新策略下沉到 scheduler/services/model。
3. 继续打磨 Relationships Tree：按层刷新和更细的类型策略。
4. 继续改进 Problems 面板诊断生命周期、清空策略和更多真实 fixture。
5. 继续打磨 References dock：结果摘要、更多 workspace 维度和跨文件上下文。
6. 保持 6 项 CTest 全绿。

## Session 2026-06-08 follow-up

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 3a799b7 Reduce live symbol database coupling.
- Focused change: SemanticIndex now owns relationship engine attachment and SmartRelationshipBuilder creation helpers.
- MainWindow::setupRelationshipEngine and CompletionManager::setRelationshipEngine no longer fetch the live sym_list database directly for builder wiring.
- relationship_test adds facade-boundary coverage for relationship engine attachment and builder creation.
- Verification passed:
  - cmake build targets: relationship_test, completion_test, gui_smoke_test.
  - ctest -R "relationship_test|completion_test|gui_smoke_test" --output-on-failure: 3/3 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty.
  - forbidden-file guard: no *.pro/*.pri files in working tree, .claude absent.
- Existing staged deletion of demo.pro was present at session start and was not restored.
- Next best step: continue reducing live sym_list exposure from service/editor transition points, especially remaining mutable completion state paths, without expanding the refactor scope.

## Session 2026-06-08 continuation

- Additional focused change: CompletionManager now routes file-specific symbols, cached file content, scope names, symbol id lookup, and module-name validation through private SemanticIndex helper methods.
- CompletionManager module-context completion now prefers SemanticIndex cached file content before falling back to reading the file from disk.
- This keeps more completion read paths snapshot-friendly while preserving the old fallback behavior.
- Verification passed:
  - cmake build targets: completion_test, gui_smoke_test.
  - ctest -R "completion_test|gui_smoke_test" --output-on-failure: 2/2 passed.
- Next best step: continue with another small CompletionManager slice only if it can move old strategy logic behind services/facades without rewriting completion behavior.

## Session 2026-06-08 continuation 2

- Additional focused change: CompletionManager type-specific completion helpers now use SemanticIndex type queries instead of full-symbol scans where the desired type is known.
- Updated enum variable lookup, module port module lookup, enum value completions, and struct member completions to use getSemanticSymbolsByType.
- This narrows snapshot-backed reads and keeps matching behavior unchanged.
- Verification passed:
  - cmake build targets: completion_test, gui_smoke_test.
  - ctest -R "completion_test|gui_smoke_test" --output-on-failure: 2/2 passed.
- Next best step: stop expanding CompletionManager unless the next slice can be covered by existing completion_test/gui_smoke_test or a very small targeted assertion.

## Session 2026-06-08 continuation 3

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 8998b8a Tighten SemanticIndex completion boundaries.
- Completed three focused increments:
  - CompletionManager now uses a private typed semantic query helper for command/type-specific completion paths, including typedef enum compatibility, module-internal variables, struct variables, module-context symbols, and global type completions.
  - ReferenceService now provides structured ReferenceReport fileGroups -> typeGroups -> references, and MainWindow renders References from that report instead of rebuilding file/type groups locally.
  - RelationshipService now provides structured RelationshipReport directionGroups -> typeGroups -> relationships, and MainWindow renders Relationships Direct from that report instead of rebuilding direction/type groups locally.
- This keeps completion reads narrower and pushes References / Relationships tree grouping further into services while preserving existing UI behavior.
- Verification passed:
  - cmake build targets: relationship_test, completion_test, gui_smoke_test.
  - ctest -R "relationship_test|completion_test|gui_smoke_test" --output-on-failure: 3/3 passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no *.pro/*.pri files in the working tree, .claude absent.
- git diff --name-only before handoff:
  - completionmanager.cpp
  - completionmanager.h
  - mainwindow.cpp
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/relationship_test.cpp
  - readme.md
  - plan.md
  - goal.md
- Next best step: continue moving remaining MainWindow refresh policy into scheduler/services, or add the next real Problems/References/Relationships fixture if it can be covered by relationship_test/gui_smoke_test without broad UI churn.

## Session 2026-06-08 continuation 4

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- Existing readme.md handoff-opener edit was present at session start and was preserved.
- Completed three focused increments:
  - ReferenceReport and RelationshipReport now expose the resolved subject symbol id and subject symbol.
  - MainWindow References / Relationships Direct titles and status messages consume the service report subject, and the unused local file-group helper left from earlier grouping migration was removed.
  - Workspace relationship batch analysis moved from a long MainWindow callback into AnalysisScheduler; MainWindow now only injects the SmartRelationshipBuilder, while scheduler owns project-file traversal, base snapshot use, relationship merge, and cancellation.
- relationship_test now covers the scheduler workspace relationship path against the real multi-file relationship fixture and checks the enriched snapshot contains the cross-file instantiation relationship.
- Verification passed:
  - cmake build targets: relationship_test, gui_smoke_test.
  - ctest -R "relationship_test|gui_smoke_test" --output-on-failure: 2/2 passed.
  - ctest -R "relationship_test" --output-on-failure after adding scheduler fixture: passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no demo.pro, no *.pro, no *.pri, .claude absent.
- git diff --name-only before handoff:
  - analysisscheduler.cpp
  - analysisscheduler.h
  - mainwindow.cpp
  - readme.md
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/relationship_test.cpp
- No commit or push was requested; leave these changes uncommitted for the next session.
- Next best step: continue extracting remaining MainWindow refresh/progress policy into scheduler/services, or add the next Problems/References/Relationships real fixture if it can stay inside relationship_test/gui_smoke_test coverage.

## Session 2026-06-08 continuation 5

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- Continued thinning MainWindow analysis ownership:
  - AnalysisScheduler now owns single-file relationship background analysis.
  - Scheduler manages the single-file watcher, previous-task cancellation, base snapshot capture, relationship computation, and enriched snapshot merge.
  - MainWindow now subscribes to AnalysisScheduler::relationshipAnalysisFinished and only applies finished relationships to the engine and UI status flow.
  - AnalysisScheduler exposes a shared setRelationshipBuilder entry for single-file and workspace relationship analysis.
  - MainWindow no longer includes QtConcurrent or owns relationshipSingleFileWatcher / pendingRelationshipFileName.
- gui_smoke_test and large_file_perf_test drain helpers now cancel relationship work through AnalysisScheduler instead of reaching into MainWindow's old watcher.
- relationship_test now covers AnalysisScheduler::requestRelationshipAnalysis with the real multi-file fixture and checks the enriched snapshot contains the top -> stage instantiation.
- Verification passed:
  - cmake build targets: relationship_test, gui_smoke_test, large_file_perf_test.
  - ctest -R "relationship_test|gui_smoke_test|large_file_perf_test" --output-on-failure: 3/3 passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no demo.pro, no *.pro, no *.pri, .claude absent.
- git diff --name-only before handoff:
  - analysisscheduler.cpp
  - analysisscheduler.h
  - goal.md
  - mainwindow.cpp
  - mainwindow.h
  - plan.md
  - readme.md
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/gui_smoke_test.cpp
  - test_sv/large_file_perf_test.cpp
  - test_sv/relationship_test.cpp
- No commit or push was requested; keep these changes uncommitted.
- Next best step: continue moving remaining progress/refresh policy out of MainWindow, especially relationship progress completion and bottom-panel refresh timing, or add the next Problems/References/Relationships real fixture.

## Session 2026-06-08 continuation 6

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- Continued thinning MainWindow relationship progress ownership:
  - AnalysisScheduler now exposes relationshipAnalysisProgress, relationshipAnalysisError, and relationshipAnalysisCancelled signals.
  - Scheduler owns the SmartRelationshipBuilder progress/error/cancel boundary and emits result-based progress before single-file/workspace finished signals.
  - MainWindow no longer connects directly to SmartRelationshipBuilder analysisCompleted / analysisError / analysisCancelled.
  - setupManagerConnections no longer duplicates relationship engine signal wiring that already belongs to setupRelationshipEngine.
  - Workspace finished handling now applies relationships and snapshots; progress counting is driven by scheduler progress events.
- relationship_test now checks that AnalysisScheduler forwards single-file relationship progress for the real multi-file fixture.
- Verification passed:
  - cmake build targets: relationship_test, gui_smoke_test, large_file_perf_test.
  - ctest -R "relationship_test|gui_smoke_test|large_file_perf_test" --output-on-failure: 3/3 passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no demo.pro, no *.pro, no *.pri, .claude absent.
- git diff --name-only before handoff:
  - analysisscheduler.cpp
  - analysisscheduler.h
  - goal.md
  - mainwindow.cpp
  - mainwindow.h
  - plan.md
  - readme.md
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/gui_smoke_test.cpp
  - test_sv/large_file_perf_test.cpp
  - test_sv/relationship_test.cpp
- No commit or push was requested; keep these changes uncommitted.
- Next best step: continue shrinking MainWindow refresh timing policy, especially bottom dock refresh scheduling, or add the next focused Problems/References/Relationships real fixture.

## Session 2026-06-08 continuation 7

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- Continued moving Problems refresh timing policy toward AnalysisScheduler:
  - AnalysisScheduler now emits diagnosticsRefreshRequested(fileName).
  - Scheduler emits diagnostics refresh requests for single-file analysis completion, workspace batch completion, workspace analysis start, project close, and closed project snapshots.
  - MainWindow now subscribes to AnalysisScheduler::diagnosticsRefreshRequested and keeps only the 100ms UI debounce/tree rendering.
  - MainWindow no longer directly schedules Problems refresh from WorkspaceManager::workspaceClosed, workspaceSymbolAnalysisStarted, or SymbolAnalyzer::batchAnalysisCompleted.
  - MainWindow still listens to SymbolAnalyzer::analysisCompleted only for editor scope/highlight refresh.
- relationship_test now checks that scheduler requests diagnostics refresh on project close.
- Verification passed:
  - cmake build targets: relationship_test, gui_smoke_test, large_file_perf_test.
  - ctest -R "relationship_test|gui_smoke_test|large_file_perf_test" --output-on-failure: 3/3 passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no demo.pro, no *.pro, no *.pri, .claude absent.
- git diff --name-only before handoff:
  - analysisscheduler.cpp
  - analysisscheduler.h
  - goal.md
  - mainwindow.cpp
  - mainwindow.h
  - plan.md
  - readme.md
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/gui_smoke_test.cpp
  - test_sv/large_file_perf_test.cpp
  - test_sv/relationship_test.cpp
- No commit or push was requested; keep these changes uncommitted.
- Next best step: continue shrinking remaining MainWindow bottom-panel refresh coordination, especially References/Relationships refresh timing, or add the next Problems/References/Relationships real fixture.

## 新会话开场白

```text
/goal 请使用 zeroslack-handoff-dev skill 接手 ZeroSlack，并进入持续迭代开发模式。

请先阅读 readme.md、plan.md、goal.md、version.h，然后检查：
- git log -1 --oneline
- git branch --show-current
- git diff --name-only
- git status -sb

当前已知状态：
- 工作目录：E:\ZeroSlack\ZeroSlack
- 当前分支：tree_sitter_and_slang
- 当前版本：0.0.20/slang25
- 最新提交：6b16c3b Push report grouping into services
- 当前工作区保留上一轮未提交变更；以 git diff --name-only 为准
- 当前 diff 应包含：analysisscheduler.cpp、analysisscheduler.h、goal.md、mainwindow.cpp、mainwindow.h、plan.md、readme.md、referenceservice.cpp、referenceservice.h、relationshipservice.cpp、relationshipservice.h、test_sv/gui_smoke_test.cpp、test_sv/large_file_perf_test.cpp、test_sv/relationship_test.cpp
- demo.pro / *.pro / *.pri 已删除，不要恢复
- .claude 不存在，不要恢复任何 Claude 本地配置

重要规则：
- 只使用 Qt 6 + CMake + Ninja，不使用 qmake。
- 不要恢复 demo.pro、任何 *.pro、任何 *.pri。
- 不要恢复 .claude/ 或任何 Claude 本地配置。
- 不要恢复 SVLexer、旧 Tree-sitter symbol parser、Tree-sitter verify button、regex relationship analysis、长期 perflog。
- 源码注释、测试字符串、CMake 注释、UI 可见文案必须是英文 / ASCII。
- readme.md / plan.md / goal.md 是中文交接文档，可以保留中文。
- git status 可能有 Windows index stat / CRLF 噪声，真实内容变更优先看 git diff --name-only。

上一轮最新完成：
- AnalysisScheduler 新增 diagnosticsRefreshRequested(fileName)，承接 single-file/batch diagnostics refresh、workspace analysis start 和 project close 的 Problems 刷新请求。
- MainWindow 改为订阅 AnalysisScheduler::diagnosticsRefreshRequested；窗口只保留 Problems 100ms UI debounce 和树渲染。
- MainWindow 不再从 WorkspaceManager::workspaceClosed、workspaceSymbolAnalysisStarted 或 SymbolAnalyzer::batchAnalysisCompleted 直接调度 Problems 刷新。
- relationship_test 增加 scheduler project close diagnostics refresh 请求断言。
- 验证通过：relationship_test/gui_smoke_test/large_file_perf_test 目标构建，focused CTest 3/3，完整 ctest 6/6，git diff --check，非 ASCII 扫描，禁用文件 guard。
- AnalysisScheduler 新增 relationshipAnalysisProgress / relationshipAnalysisError / relationshipAnalysisCancelled，统一承接 SmartRelationshipBuilder progress/error/cancel 边界。
- MainWindow 不再直接连接 SmartRelationshipBuilder analysisCompleted / analysisError / analysisCancelled；relationship progress 由 scheduler result-based progress 事件驱动。
- 删除 setupManagerConnections 中与 setupRelationshipEngine 重复的 relationship engine 信号连接。
- relationship_test 增加真实 fixture 覆盖 AnalysisScheduler single-file relationship progress 转发。
- 验证通过：relationship_test/gui_smoke_test/large_file_perf_test 目标构建，focused CTest 3/3，完整 ctest 6/6，git diff --check，非 ASCII 扫描，禁用文件 guard。
- AnalysisScheduler 接管 single-file relationship 后台分析、取消旧任务、base snapshot capture、relationship compute 和 enriched snapshot merge。
- MainWindow 删除 single-file relationship watcher / QtConcurrent 路径，只消费 AnalysisScheduler::relationshipAnalysisFinished 并负责 UI/engine 收尾。
- gui_smoke_test / large_file_perf_test 改为通过 AnalysisScheduler drain relationship work。
- relationship_test 增加真实 fixture 覆盖 AnalysisScheduler::requestRelationshipAnalysis 产生 enriched snapshot。
- 验证通过：relationship_test/gui_smoke_test/large_file_perf_test 目标构建，focused CTest 3/3，完整 ctest 6/6，git diff --check，非 ASCII 扫描，禁用文件 guard。
- ReferenceReport / RelationshipReport 新增已解析 subject symbol id 和 subject symbol，MainWindow References / Relationships Direct 标题和状态消息改为消费 service report subject。
- 删除 MainWindow 中 References 分组下沉后遗留的未使用 file-group helper。
- Workspace relationship 批量分析从 MainWindow 长回调下沉到 AnalysisScheduler；MainWindow 只注入 SmartRelationshipBuilder。
- relationship_test 增加真实 fixture 覆盖 AnalysisScheduler::requestWorkspaceRelationshipAnalysis 产生 enriched snapshot。
- 验证通过：relationship_test/gui_smoke_test 目标构建，focused CTest 2/2，完整 ctest 6/6，git diff --check，非 ASCII 扫描，禁用文件 guard。

本次任务：
请继续向 goal.md 的最终目标推进，优先选择高价值、小范围、可验证的迭代：
- 减少 live sym_list 消费，让 UI / services 更多只读 SemanticIndex / snapshot / Query Services。
- 继续薄化 MainWindow，把分析/刷新策略下沉到 scheduler/services/model。
- 继续打磨 Problems / References / Relationships，尤其真实 fixture、结果摘要、scheduler/service 边界。
- 保持现有 CTest 回归绿色。

请不要只完成一次开发-测试循环就停止。请在同一会话中持续迭代，直到你判断已经到达适合切换新会话的交接点，例如：
- 已完成多个连贯小步并通过合适验证；
- 上下文接近变长，需要交接；
- 下一步需要用户决策；
- 验证被环境问题阻塞；
- 或已经形成清晰的 commit/handoff 边界。

到达交接点时：
1. 更新 readme.md、plan.md、goal.md，写清楚本轮完成、验证结果、禁用文件检查、当前分支/版本、下一步建议。
2. 运行或记录本轮合适的验证；如果用户明确要求提交前不再编译/测试，则遵守并写入交接。
3. 检查 git diff --name-only，确认没有恢复 demo.pro / *.pro / *.pri / .claude。
4. 如果用户明确要求提交并 push，则提交并 push 到 origin/tree_sitter_and_slang；否则保留未提交变更并说明。
5. 最后给出下一轮新会话开场白，保持本段 `/goal` 形式。
```

## Session 2026-06-08 continuation 9

- Added one more real fixture assertion for ReferenceService current-file filtering.
- relationship_test now checks that currentFileOnly both hides non-matching files and keeps the matching real fixture file for the rel_stage instantiation reference.
- Verification passed:
  - cmake build target: relationship_test.
  - ctest -R "relationship_test" --output-on-failure: passed.
  - Full ctest --output-on-failure: 6/6 passed.
- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- No commit or push was requested; keep these changes uncommitted.
- Next best step: continue with another small real fixture for Problems / Relationships, or move another clean MainWindow progress UI boundary into AnalysisScheduler.

## Session 2026-06-08 continuation 10

- Final handoff point requested because context is compressing.
- Added two focused real fixture assertions in relationship_test:
  - DiagnosticReport now verifies direct fileName filtering keeps the two diagnostics for topPath and produces one file group.
  - RelationshipReport now verifies incoming-only browsing for rel_stage finds the top -> stage instantiation and keeps top as the peer symbol.
- Verification passed:
  - cmake build target: relationship_test.
  - ctest -R "relationship_test" --output-on-failure: passed.
  - Full ctest --output-on-failure: 6/6 passed.
- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- No commit or push was requested; keep these changes uncommitted.
- Next best step: start a fresh session from the updated opener below, then either continue adding small real Problems / Relationships fixtures or move one remaining MainWindow progress UI boundary into AnalysisScheduler.

## Session 2026-06-08 continuation 8

- Current branch: tree_sitter_and_slang.
- Current version: 0.0.20/slang25 in version.h.
- Latest commit at session start: 6b16c3b Push report grouping into services.
- Continued moving References / Relationships refresh policy toward AnalysisScheduler:
  - AnalysisScheduler now binds SymbolRelationshipEngine through setRelationshipEngine.
  - Scheduler emits relationshipDataInvalidated immediately when relationship data changes.
  - Scheduler owns the 400ms coalesced relationshipDataRefreshRequested timer for relationship additions.
  - Scheduler requests an immediate relationship data refresh when relationships are cleared.
  - MainWindow no longer connects directly to SymbolRelationshipEngine::relationshipAdded or relationshipsCleared.
  - MainWindow no longer owns relationshipRefreshDeferTimer; it only invalidates completion caches and refreshes Navigation when scheduler asks.
- relationship_test now checks scheduler relationship data invalidation, coalesced refresh on additions, and immediate refresh on clear.
- Verification passed:
  - cmake build targets: relationship_test, gui_smoke_test, large_file_perf_test.
  - ctest -R "relationship_test|gui_smoke_test|large_file_perf_test" --output-on-failure: 3/3 passed.
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check: passed.
  - source/test/UI/CMake non-ASCII scan: empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard: no demo.pro, no *.pro, no *.pri, .claude absent.
- git diff --name-only before handoff:
  - analysisscheduler.cpp
  - analysisscheduler.h
  - goal.md
  - mainwindow.cpp
  - mainwindow.h
  - plan.md
  - readme.md
  - referenceservice.cpp
  - referenceservice.h
  - relationshipservice.cpp
  - relationshipservice.h
  - test_sv/gui_smoke_test.cpp
  - test_sv/large_file_perf_test.cpp
  - test_sv/relationship_test.cpp
- No commit or push was requested; keep these changes uncommitted.
- Next best step: continue extracting remaining MainWindow progress UI policy if a clean scheduler signal exists, or add another real Problems / References / Relationships fixture.

## Updated new-session opener

```text
/goal Please use the zeroslack-handoff-dev skill to take over ZeroSlack and continue iterative development.
First read readme.md, plan.md, goal.md, and version.h, then inspect:
- git log -1 --oneline
- git branch --show-current
- git diff --name-only
- git status -sb

Known current state:
- Workspace: E:\ZeroSlack\ZeroSlack
- Branch: tree_sitter_and_slang
- Version: 0.0.20/slang25
- Latest commit: 6b16c3b Push report grouping into services
- The working tree intentionally keeps uncommitted changes from the previous rounds; use git diff --name-only as truth.
- Expected diff includes: analysisscheduler.cpp, analysisscheduler.h, goal.md, mainwindow.cpp, mainwindow.h, plan.md, readme.md, referenceservice.cpp, referenceservice.h, relationshipservice.cpp, relationshipservice.h, test_sv/gui_smoke_test.cpp, test_sv/large_file_perf_test.cpp, test_sv/relationship_test.cpp.

Rules:
- Use Qt 6 + CMake + Ninja only.
- Do not restore demo.pro, *.pro, *.pri, qmake, .claude, SVLexer, the old Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship analysis, or long-lived perflog.
- Keep source/test/UI/CMake text English / ASCII; readme.md / plan.md / goal.md may remain Chinese.
- Prefer git diff --name-only over git status noise.

Latest completed continuation:
- AnalysisScheduler now owns relationship data invalidation and refresh request timing from SymbolRelationshipEngine.
- MainWindow no longer connects directly to relationshipAdded / relationshipsCleared and no longer owns relationshipRefreshDeferTimer.
- relationship_test covers immediate invalidation, coalesced refresh on additions, and immediate refresh on clear.
- relationship_test also checks that ReferenceService currentFileOnly keeps the matching real fixture file for the rel_stage instantiation reference.
- relationship_test now also checks DiagnosticReport direct file filtering and RelationshipReport incoming-only browsing against the real fixture.
- Validation passed: target builds, focused CTest, full CTest 6/6, git diff --check, non-ASCII scan, forbidden-file guard.

Continue toward goal.md with another small, verifiable step: keep thinning MainWindow progress/refresh policy, or add the next real Problems / Relationships fixture.
```
