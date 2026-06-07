# ZeroSlack Next Plan

当前版本：`0.0.20/slang25`
当前分支：`tree_sitter_and_slang`

本计划服务于 `goal.md` 中的产品 / 架构目标。后续实现默认继续向：

`ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services`

收敛。除非遇到无法绕过的实现问题，不再为每个功能重新选择架构方向。

## Current Status

主线仍是 Route A：

- Tree-sitter：实时语法、高亮、增量文档、live module scope。
- Slang：符号、类型、定义、跳转、补全、关系、诊断事实来源。
- SemanticIndex：当前默认是 sym_list-backed facade，新增语义读取优先通过它。
- SemanticIndexSnapshot：已接入 workspace / single-file 后台分析生产和主线程世代切换。
  Snapshot 当前保存 symbols / relationships / diagnostics / cached file content / minimal scope names。
  base snapshot capture 和 enriched relationship merge 已有 SemanticIndex / SemanticIndexSnapshot helper。
- Query Services：Definition / Completion / Relationship / Hierarchy / Reference /
  Diagnostic / Search 最小边界已落地；DiagnosticService 已能消费 Slang diagnostics。
  DiagnosticService / ReferenceService / RelationshipService / HierarchyService 已提供 report API，
  底部面板排序、筛选结果集、二级分组计数和 tree 计数继续从 MainWindow 下沉到 service。
- Navigation UI：模块层级和符号 outline 已有显式 model，Widget 不再在双击时反查 sym_list。
- Scheduler：workspace opened/rescanned/project config changed 已经通过 ProjectModel
  projectChanged 进入 AnalysisScheduler。
- 本轮收口：Problems / References / Relationships 底部面板已补稳定排序、分组计数、Relationships Tree
  双向浏览，并继续下沉为 service report 驱动的薄 UI。
- 本轮 0.0.20/slang25 收口：提交和上传前按用户要求不再重复编译 / 测试，沿用本轮最近一次全量验证结果。

最近验证：

- completion_test：14 checks, 0 failed。
- jump_test：10 checks, 0 failed。
- relationship_test：117 checks, 0 failed。
- gui_smoke_test：91 checks, 0 failed。
- ctest：6/6 passed。
- git diff --check：无错误。

## 已完成

### P0 回归底座

- `ts_doc_test`
- `completion_test`
- `jump_test`
- `relationship_test`
- `gui_smoke_test`
- `large_file_perf_test`

全部接入 CTest。

### P1 Tree-sitter / Slang 分工

- Tree-sitter 保留为实时语法、高亮、live scope。
- Slang 作为真实符号和语义来源。
- 旧 SVLexer、旧 Tree-sitter symbol parser、Tree-sitter 验证按钮不恢复。

### P2 Project / Document / Scheduler

- ProjectModel 最小入口已落地。
- DocumentModel 最小入口已落地。
- AnalysisScheduler 已接管主要分析触发点。
- workspace 符号分析完成后由 scheduler 接续启动 workspace 关系分析。

### P3 Query Services

- DefinitionService：MyCodeEditor 跳转定义主要路径已迁入。
- CompletionService：普通补全、struct member 补全、命令模式补全已开始迁入。
- RelationshipService / HierarchyService：关系和层级查询边界已落地。
- RelationshipService 增加 findRelatedSymbolIds / hasRelationship。
- HierarchyService / ReferenceService 名称解析和 ID 反查改走 SemanticIndex。
- ReferenceService / DiagnosticService / SearchService：最小边界已落地并有测试断言。
- DiagnosticService / ReferenceService / RelationshipService 增加 report API，承接底部面板的排序、
  scope/type/severity 过滤结果和摘要计数。
- HierarchyService 增加 HierarchyReport 和 allRelationshipTypes，承接 Relationships Tree 的节点、
  depth/direction/root/type 计数和 All Types 策略。

### P4 Navigation 消费端迁移

- ModuleHierarchyGroup：模块层级 UI 显式模型。
- SymbolOutlineGroup：符号 outline UI 显式模型。
- NavigationManager 消费 DefinitionService / HierarchyService / SearchService / SemanticIndex。
- NavigationWidget 不再在符号双击时反查 sym_list / service，而是使用 item payload。

### P5 SemanticIndexSnapshot 最小边界

- 新增 `semanticindexsnapshot.cpp / semanticindexsnapshot.h`。
- Snapshot 保存 symbols / relationships / diagnostics 的只读副本。
- Snapshot 支持 symbols / definitions / symbol id / relationships / diagnostics 查询。
- SemanticIndex 支持 setSnapshot / clearSnapshot；snapshot 存在时优先读 snapshot。
- relationship_test 覆盖 snapshot symbols / symbol id / relationships / clearSnapshot。
- 后台分析已产出 snapshot，主线程按 base snapshot 世代切换 current snapshot。
- Snapshot 已扩展 cached file content / minimal scope names 查询。
- Workspace / single-file relationship 后台已改为 snapshot-backed，避免读取过期 current snapshot。
- SemanticIndex 新增 captureSnapshotPreservingDiagnostics，统一 base snapshot 生产并保留 diagnostics。
- SemanticIndexSnapshot 新增 withAdditionalRelationships，统一 enriched snapshot relationship 合并和去重。

### P6 Slang diagnostics / Problems UI

- SlangManager 新增 `extractDiagnostics` / `extractWorkspaceDiagnostics`。
- SemanticDiagnostic 已从 Slang diagnostics 进入 SemanticIndexSnapshot。
- DiagnosticService 已能查询真实 diagnostics。
- MainWindow 新增底部 Problems dock，显示 Severity / File / Line / Column / Message。
- Problems 面板支持 current file / all files 和 severity 筛选。
- Problems 条目双击可跳转到对应文件、行和列。
- Problems 面板刷新已加 100ms 合并；All Files 模式按 file 分组，Current File 保持紧凑列表。
- Problems 面板结果按 severity / file / line / column 稳定排序；All Files 分组显示数量。
- Problems 空状态显示 No problems；非诊断节点双击不会触发空路径跳转。
- Problems 面板改为消费 DiagnosticReport，排序、总数、file count 和 severity count 由 DiagnosticService 提供。
- relationship_test 覆盖 Slang diagnostics -> snapshot -> DiagnosticService。
- relationship_test 覆盖 DiagnosticReport 排序、severity 过滤、file/severity 计数。
- gui_smoke_test 覆盖 Problems 面板显示真实 Slang diagnostic 和 all-files 分组。

### P7 References / Relationships UI

- MainWindow 新增底部 References dock，编辑器右键 Find References 触发。
- References dock 通过 ReferenceService 查询 incoming references，并支持 all files / workspace files / current file 筛选。
- References dock 支持 Reference type 筛选，并按 file -> relationship type -> result 分组。
- 编辑器新增 Shift+F12 快捷键入口触发 Find References。
- MainWindow 新增底部 Relationships dock，编辑器右键 Show Relationships 触发。
- Relationships dock 通过 RelationshipService 查询 incoming / outgoing relationships，并支持方向 / 类型筛选。
- Relationships dock Direct 视图按 direction -> relationship type -> result 分组。
- Relationships dock Direct 视图结果按类型和位置稳定排序，direction / type 分组显示数量。
- Relationships dock 新增 Tree 视图，接入 HierarchyService，支持 Depth 1..4 层级浏览。
- HierarchyService 支持 Children / Parents / Both 方向查询，并通过路径去重防止递归循环。
- Relationships Tree 支持 Root 节点、Incoming / Outgoing 分支、方向筛选和全部关系类型策略。
- References dock 结果按类型和位置稳定排序，file / relationship type 分组显示数量。
- References dock 改为消费 ReferenceReport，scope/type 过滤结果、排序、file/type 计数由 ReferenceService 提供。
- Relationships Direct 视图改为消费 RelationshipReport，incoming/outgoing 合并、排序、方向/type 计数由
  RelationshipService 提供。
- RelationshipReport 新增 directionCounts / directionTypeCounts，Direct 视图二级分组计数不再由 MainWindow 计算。
- 编辑器新增 Ctrl+Shift+R 快捷键入口触发 Show Relationships。
- References / Relationships 条目双击可跳转到对应文件、行和列。
- relationship_test 覆盖 ReferenceReport / RelationshipReport 的排序、过滤和计数。
- relationship_test 覆盖 HierarchyReport、ReferenceReport file/type、RelationshipReport direction/type 计数。
- gui_smoke_test 覆盖 References / Relationships dock 的 service-backed UI 消费。
- gui_smoke_test 覆盖 Relationships Tree 刷新后保留折叠 root。
- Problems / References / Relationships 树刷新会保留已有展开/折叠状态，首次渲染仍默认展开。

## 下一步建议

### 1. 继续迁移 editor / UI 消费端

当前仍有少量直接 sym_list 读取：

- `MyCodeEditor` 内的注释判断和补全触发细节。
- `CompletionManager` 大批只读符号查询和关系读取已迁到 SemanticIndex / RelationshipService；
  剩余主要是状态性过渡依赖和旧补全策略。
- `MainWindow::setupRelationshipEngine()` 仍需要把 relationship engine 绑定到 sym_list，这是当前过渡期可接受的初始化点。

建议小步推进，不要一次性重写 CompletionManager。

### 2. 继续打磨关系浏览 UI

Relationships dock 已有 Direct 分组、双向 Tree 层级浏览、递归去重、根节点 UX、展开状态保持、
HierarchyReport 计数和服务侧 All Types 策略。
后续重点是按层刷新和更细的类型策略，并继续把相关读取固定到 RelationshipService / HierarchyService。

Snapshot 生产 / 切换闭环已继续收束：AnalysisScheduler / MainWindow 的关系后台路径通过 SemanticIndex /
SemanticIndexSnapshot helper 生产 base/enriched snapshot，后续可继续减少 SymbolAnalyzer / CompletionManager 等
过渡直连点。

### 3. Reference / Problems UI 打磨

- ReferenceService 已有 References dock 作为真实 UI 消费点，当前查询 incoming references。
- DiagnosticService 已消费 Slang diagnostics，Problems panel 已支持基础筛选和行/列跳转。
- References 已有结果排序、结果分组、workspace files 筛选、type 筛选、快捷键入口和 service report；
  后续可补结果摘要、更多 workspace 维度和跨文件上下文。
- Problems panel 已有刷新合并、稳定排序、空状态、all-files 分组和 service report；后续可补清空策略、
  诊断生命周期和更多真实 fixture。

### 4. SemanticIndexSnapshot 生产 / 切换闭环

下一块较大的架构工作：

```text
后台分析产出 SemanticIndexSnapshot
主线程按 base snapshot 世代切换 currentSnapshot
UI / Services 只读 snapshot
旧 snapshot 自动释放
```

基础闭环已落地；下一步重点是继续减少 live sym_list 消费，并把更多 UI / services
读路径固定到 snapshot-backed 数据。

## Definition Of Done

每轮较大改动至少保持：

- `ctest --output-on-failure` 6/6 passed。
- `git diff --check` 无错误。
- 不恢复 SVLexer、旧 Tree-sitter parser、Tree-sitter 验证按钮或关系分析正则路径。
- 不引入长期散落 perflog。
- 不恢复 `.claude/` 或任何 Claude 本地配置。

## Useful Commands

```powershell
$env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\CMake_64\bin;E:\QT6\Tools\Ninja;$env:PATH"

& "E:\QT6\Tools\CMake_64\bin\cmake.exe" --build "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --target demo gui_smoke_test relationship_test completion_test jump_test

& "E:\QT6\Tools\CMake_64\bin\ctest.exe" --test-dir "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --output-on-failure
```
