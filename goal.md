# ZeroSlack Product / Architecture Goal

当前版本：`0.0.18/slang23`

本文记录 ZeroSlack 当前阶段的最终产品目标和架构骨架。后续功能和重构默认以此为准；
除非遇到无法绕过的实现问题，否则不再为每个新功能重新选择架构方向。

## 产品目标

ZeroSlack 当前不是完整 IDE，而是可靠的 SystemVerilog 工程浏览器 / 轻量编辑器：

- 稳定打开真实 SV workspace。
- 理解 module、package、include、typedef、enum、struct、interface、instance 等工程结构。
- 提供可信的补全、跳转、导航、关系分析和诊断入口。
- 在大文件和多文件工程中保持可验证的响应性。

产品优先级：

1. 读工程。
2. 理解工程。
3. 定位定义和关系。
4. 再逐步扩展完整编辑体验。

## 核心架构目标

目标架构分为五层：

```text
ProjectModel
  管工程输入：root、filelist、include dirs、defines、top、ignored paths

DocumentModel
  管打开文档：文本版本、dirty state、cursor、live module、Tree-sitter live tree

AnalysisScheduler
  管分析任务：何时跑 Slang、何时跑关系、取消、去抖、优先级、缓存命中

SemanticIndex
  管语义事实：symbols、definitions、references、relationships、diagnostics

Feature / UI Layer
  只消费 model、index、services；不直接扫文件，不直接调 Slang
```

Tree-sitter + Slang 分工保持不变：

- Tree-sitter：即时、容错、低延迟编辑体验，高亮、增量语法树、当前 module scope。
- Slang：真实语义，符号、类型、定义、跳转、关系、诊断、工程级事实。

## 实现原则

- UI 不直接触发 Slang。
- 新功能不直接依赖 sym_list，短期通过 SemanticIndex 包住 sym_list，长期迁到 snapshot。
- 所有分析触发集中到 AnalysisScheduler。
- Ctrl+Click、导航窗格、补全面板、关系浏览、右键菜单复用 Query Services。
- Tree-sitter 只用于即时语法体验；定义、类型、关系、诊断以 Slang / SemanticIndex 为准。
- 性能探针只做针对性、可移除或测试内探针，不恢复长期散落 perflog。

## 当前完成度

已经落地：

- ProjectModel 最小入口。
- DocumentModel 最小入口。
- AnalysisScheduler 最小入口。
- SemanticIndex facade。
- SemanticIndexSnapshot 生产 / 切换闭环的第一版。
- DefinitionService。
- CompletionService。
- RelationshipService。
- HierarchyService。
- ReferenceService。
- DiagnosticService + Slang diagnostics。
- SearchService。
- ModuleHierarchyGroup。
- SymbolOutlineGroup。
- Problems / References / Relationships 面板第一版 UI。
- GUI smoke / large file perf / relationship fixture 等 CTest 回归。

仍未完成或仍是过渡形态：

- SemanticIndexSnapshot 已接入后台生产和主线程世代切换，但仍需要继续减少 live sym_list 消费。
- DiagnosticService 已有真实 Slang diagnostics 数据，Problems 面板已支持基础筛选和行/列跳转。
- ReferenceService 已有 References dock 作为真实 UI 消费点。
- RelationshipService 已有 Relationships dock 作为真实 UI 消费点；relationship / hierarchy 浏览已支持双向树、递归去重、根节点和分组计数，后续仍可继续打磨刷新策略和类型策略。
- CompletionManager 大批只读符号查询、关系读取、scope names、cached file content
  已收束到 SemanticIndex / RelationshipService；仍有状态性过渡依赖。
- MyCodeEditor 仍有少量旧直连点，但跳转定义和主要补全入口已开始经由 services。

## 阶段路线

### 阶段 1：SemanticIndex facade

已完成最小入口。后续新增语义读取必须优先经过 facade / services。

### 阶段 2：DocumentModel

已完成最小入口。后续可继续把 TSDocument 所有权和更多文档状态迁出 MyCodeEditor。

### 阶段 3：AnalysisScheduler

已完成主要触发点收束。后续可继续把进度 UI 和更多策略从 MainWindow 迁出。

### 阶段 4：ProjectModel

已完成最小入口。后续 Slang 工程配置应继续从 ProjectSnapshot 获取。

### 阶段 5：Query Services

最小边界已补齐。RelationshipService 已扩展 related id / exact relationship 查询；
HierarchyService / ReferenceService 的名称解析和 ID 反查已收束到 SemanticIndex。
后续重点是消费端迁移和服务内部从 sym_list-backed 迁到 snapshot-backed。

### 阶段 6：SemanticIndexSnapshot

snapshot 类型和第一版生产 / 切换闭环已落地：

- `SemanticIndexSnapshot` 保存 symbols / relationships / diagnostics / cached file content / minimal scope names。
- `SemanticIndex` 支持 `setSnapshot` / `clearSnapshot`。
- snapshot 存在时，symbols / definitions / relationships / diagnostics / cached file content /
  scope symbol names 优先读 snapshot。
- SymbolAnalyzer 在符号写回后发布 symbols-only snapshot。
- Workspace / single-file 关系后台基于 snapshot 计算关系，并按 base snapshot 世代发布 enriched snapshot。

最终目标仍是：

```text
后台分析产出 SemanticIndexSnapshot
主线程按 base snapshot 世代切换 currentSnapshot
UI / Services 只读 snapshot
旧 snapshot 自动释放
```

下一阶段重点是继续减少 live sym_list 消费，并扩大 snapshot-backed UI / service 覆盖。

### 阶段 7：Diagnostics / Problems UI

第一版和基础打磨已落地：

- SlangManager 提供 single-file / workspace diagnostics 提取。
- Slang diagnostics 转为 SemanticDiagnostic，并随 SemanticIndexSnapshot 发布。
- DiagnosticService 能查询真实 diagnostics。
- MainWindow 提供底部 Problems 面板，显示 Severity / File / Line / Column / Message。
- Problems 面板支持 current file / all files 和 severity 筛选。
- Problems 条目双击可跳转文件、行和列。
- Problems 刷新已加 100ms 合并。
- Problems 在 All Files 模式下按 file 分组，Current File 保持紧凑列表。
- Problems 结果按 severity / file / line / column 稳定排序。
- Problems All Files 分组显示数量，并提供 No problems 空状态。

后续重点：

- Problems 面板清空策略、诊断生命周期和更多真实 fixture。
- 将诊断刷新策略继续下沉到更清晰的 service / scheduler 边界。
- 补更多真实 fixture 覆盖 include/import/宏展开相关 diagnostics。

### 阶段 8：References / Relationships UI

第一版和基础浏览打磨已落地：

- MainWindow 提供底部 References dock，编辑器右键 Find References 触发。
- References dock 通过 ReferenceService 查询 incoming references。
- References dock 支持 all files / workspace files / current file 筛选。
- References dock 支持 Reference type 筛选。
- References 结果按 file -> relationship type -> result 分组。
- References 结果按类型和位置稳定排序，file / relationship type 分组显示数量。
- 编辑器支持 Shift+F12 触发 Find References。
- MainWindow 提供底部 Relationships dock，编辑器右键 Show Relationships 触发。
- Relationships dock 通过 RelationshipService 查询 incoming / outgoing relationships。
- Relationships dock 支持 direction / type 筛选。
- Relationships Direct 视图按 direction -> relationship type -> result 分组。
- Relationships Direct 视图按类型和位置稳定排序，direction / type 分组显示数量。
- Relationships Tree 视图接入 HierarchyService，支持 Depth 1..4 层级浏览。
- HierarchyService 支持 Children / Parents / Both 方向查询，并通过路径去重防止递归循环。
- Relationships Tree 支持 Root 节点、Incoming / Outgoing 分支、方向筛选和全部关系类型策略。
- 编辑器支持 Ctrl+Shift+R 触发 Show Relationships。
- References / Relationships 条目双击可跳转文件、行和列。

后续重点：

- References 结果摘要、更多 workspace 维度和跨文件上下文。
- Relationships 按层刷新、展开状态保持和更清晰的类型策略。
- 继续让 UI 只读 snapshot-backed services，减少 live sym_list 消费。

## 完成标准

底座完成时应满足：

- 新增功能主要接入 ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services。
- MainWindow 只做窗口协调，不做分析策略中心。
- MyCodeEditor 只做编辑器 UI 和即时 Tree-sitter 体验，不承担工程语义判断中心。
- 现有补全、跳转、导航、关系分析、GUI smoke、大文件性能测试全部通过。
- 至少一个真实多文件 SV fixture 覆盖 package/import、跨文件跳转、实例化、调用、赋值、
  条件读取、clock/reset。
- 新会话只读 readme.md、plan.md、goal.md、version.h 就能理解当前状态、下一步和最终骨架。
