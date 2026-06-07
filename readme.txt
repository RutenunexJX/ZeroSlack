==========================================================================
ZeroSlack README / Development Handoff
==========================================================================

当前版本：0.0.16/slang21
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


==========================================================================
当前架构状态
==========================================================================

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
  - 当前仍是 sym_list-backed facade。
  - 提供 symbols / definitions / completions / relationships / diagnostics 查询入口。
  - getSymbols(fileName) 已补路径规范化兜底，避免 Windows 路径格式差异导致本地查询失败。

- Query Services
  - DefinitionService：Ctrl+Click / jump definition 主要路径已迁入 service；集中处理本文件优先、
    module scope、struct member 类型过滤、跨文件目标选择。
  - CompletionService：普通补全、struct member 专用补全、命令模式补全已开始经由 service；
    普通补全的 SymbolInfo 详情也从 editor 迁入 service。
  - RelationshipService：关系查询最小入口已落地。
  - HierarchyService：实例层级查询最小入口已落地。
  - ReferenceService：最小入口已落地，当前包装 RelationshipService 的 incoming reference 查询。
  - DiagnosticService：最小入口已落地，当前委托 SemanticIndex::getDiagnostics，暂为空结果。
  - SearchService：symbol search 最小入口已落地，支持文本、文件、类型、exact、maxResults。

- Navigation
  - 新增 ModuleHierarchyGroup / SymbolOutlineGroup 显式 UI 数据模型。
  - NavigationManager 已开始消费 DefinitionService / HierarchyService / SearchService / SemanticIndex。
  - 模块层级优先通过 HierarchyService / RelationshipService 构造实例化树，缺失时回退文件分组。
  - 符号 outline cache 现在保留完整 SymbolInfo，不再只传 symbol name。
  - NavigationWidget 双击符号时直接从 item payload 取完整 SymbolInfo，不再反查 sym_list 或 service。

- Editor
  - MyCodeEditor 跳转定义 / tooltip / canJump 旧的不可达 sym_list 实现已删除，当前经由 DefinitionService。
  - 普通补全、struct member 补全、命令模式补全已开始经由 CompletionService。
  - 仍有少量 editor 内部路径直接读 sym_list，主要集中在注释判断、补全触发细节和旧 CompletionManager 协作点。


==========================================================================
本轮 0.0.16/slang21 已完成
==========================================================================

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


==========================================================================
验证记录
==========================================================================

最近验证均通过：

- completion_test.exe：14 checks, 0 failed。
- jump_test.exe：10 checks, 0 failed。
- relationship_test.exe：59 checks, 0 failed。
- gui_smoke_test.exe：46 checks, 0 failed。
- 完整 ctest --output-on-failure：6/6 passed。
- git diff --check：无错误。

常见 warning：
- git 会提示 unable to access C:\Users\14971/.config/git/ignore: Permission denied。
- git 会提示 LF will be replaced by CRLF。
- 以上不是 diff --check 错误。


==========================================================================
当前工作区注意事项
==========================================================================

不要 push，用户会自己 push。

未跟踪文件中 `.claude/settings.local.json` 不要删除、不要加入提交。

本轮新增未跟踪源码文件需要后续提交时包含：
- modulehierarchymodel.h
- symboloutlinemodel.h
- referenceservice.cpp / referenceservice.h
- diagnosticservice.cpp / diagnosticservice.h
- searchservice.cpp / searchservice.h

git status 可能列出一些没有内容 diff 的 M 文件，这是前面恢复/编码写回后 index stat 噪音；
判断实际内容改动请优先看：

    git diff --name-only


==========================================================================
后续建议
==========================================================================

优先级建议：

1. 继续迁移 UI / editor 消费端到 Query Services
   - MyCodeEditor 仍有少量 sym_list 直连点。
   - CompletionManager 内部仍大量直接读 sym_list，这是较大块，适合分阶段处理。

2. 关系浏览 UI 继续迁到 RelationshipService / HierarchyService
   - 当前底层 service 已有，UI 消费侧还可继续收束。

3. 补 ReferenceService / DiagnosticService 的真实 UI 消费点
   - ReferenceService 已有最小边界。
   - DiagnosticService 当前返回空，后续可接 Slang diagnostics。

4. 开始设计 SemanticIndexSnapshot
   - 当前 SemanticIndex 仍是 sym_list-backed facade。
   - 下一阶段目标是后台分析产出只读 snapshot，主线程原子切换，UI 只读 snapshot。


==========================================================================
新会话交接文件
==========================================================================

新会话建议先读：

1. readme.txt
2. plan.md
3. goal.md
4. version.h
5. semanticindex.cpp / semanticindex.h
6. definitionservice.cpp / definitionservice.h
7. completionservice.cpp / completionservice.h
8. referenceservice.cpp / referenceservice.h
9. diagnosticservice.cpp / diagnosticservice.h
10. searchservice.cpp / searchservice.h
11. navigationmanager.cpp / navigationmanager.h
12. navigationwidget.cpp / navigationwidget.h
13. mycodeeditor.cpp / mycodeeditor.h
14. analysisscheduler.cpp / analysisscheduler.h
15. projectmodel.cpp / projectmodel.h
