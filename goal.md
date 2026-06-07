# ZeroSlack Product / Architecture Goal

当前版本：`0.0.17/slang22`

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
- SemanticIndexSnapshot 最小只读边界。
- DefinitionService。
- CompletionService。
- RelationshipService。
- HierarchyService。
- ReferenceService。
- DiagnosticService。
- SearchService。
- ModuleHierarchyGroup。
- SymbolOutlineGroup。
- GUI smoke / large file perf / relationship fixture 等 CTest 回归。

仍未完成或仍是过渡形态：

- SemanticIndexSnapshot 已有最小只读类型和 SemanticIndex 切换 API，但还没有后台生产 /
  主线程原子切换闭环。
- DiagnosticService 还没有真实 Slang diagnostics 数据。
- ReferenceService 还缺 UI 消费点。
- Relationship / hierarchy 浏览 UI 仍可继续迁到 services。
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

最小只读 snapshot 类型已落地：

- `SemanticIndexSnapshot` 保存 symbols / relationships / diagnostics。
- `SemanticIndex` 支持 `setSnapshot` / `clearSnapshot`。
- snapshot 存在时，symbols / definitions / relationships / diagnostics 优先读 snapshot。

最终目标仍是：

```text
后台分析产出 SemanticIndexSnapshot
主线程原子切换 currentSnapshot
UI / Services 只读 snapshot
旧 snapshot 自动释放
```

下一阶段重点是把 AnalysisScheduler / 后台分析接到 snapshot 生产和主线程原子切换。

## 完成标准

底座完成时应满足：

- 新增功能主要接入 ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services。
- MainWindow 只做窗口协调，不做分析策略中心。
- MyCodeEditor 只做编辑器 UI 和即时 Tree-sitter 体验，不承担工程语义判断中心。
- 现有补全、跳转、导航、关系分析、GUI smoke、大文件性能测试全部通过。
- 至少一个真实多文件 SV fixture 覆盖 package/import、跨文件跳转、实例化、调用、赋值、
  条件读取、clock/reset。
- 新会话只读 readme.txt、plan.md、goal.md、version.h 就能理解当前状态、下一步和最终骨架。
