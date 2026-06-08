# ZeroSlack Product / Architecture Goal

当前版本：`0.0.20/slang25`

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
- qmake 路径已淘汰；不要恢复 `demo.pro`、任何 `*.pro` 或 `*.pri` 文件。

## 当前完成度

已经落地：

- ProjectModel 最小入口。
- DocumentModel 最小入口。
- AnalysisScheduler 最小入口。
- SemanticIndex facade。
- SemanticIndexSnapshot 生产 / 切换闭环的第一版。
- SemanticIndex / SemanticIndexSnapshot 已提供 base snapshot capture 和 relationship merge helper。
- SemanticIndex / SemanticIndexSnapshot 已提供 diagnostics replacement helper，用于 single-file/open-tab 分析保留其它文件 diagnostics。
- SemanticIndex 已提供 module end line、module name validity 和 contentAffectsSymbols 过渡 helper，
  ScopeBandWidget / AnalysisScheduler / MainWindow / CompletionManager 已不再直接硬编码 sym_list::getInstance。
- TSDocument 已提供 isCommentAt，MyCodeEditor 注释区补全抑制改走 Tree-sitter live syntax。
- DefinitionService。
- CompletionService。
- RelationshipService。
- HierarchyService。
- ReferenceService。
- DiagnosticService + Slang diagnostics。
- SearchService。
- ModuleHierarchyGroup。
- SymbolOutlineGroup。
- Problems / References / Relationships 面板第一版 UI，并已开始改为消费 service report。
- Problems 面板已支持 Current File / Workspace Files / All Files scope、service fileGroups、diagnostic replacement 生命周期和 workspace close/reopen 清空。
- Relationships Tree 已开始消费 HierarchyReport；底部树刷新会保留展开/折叠状态。
- 0.0.20/slang25 交接文档已同步；本轮提交和上传前按用户要求不再重复编译 / 测试。
- `demo.pro` / qmake 文件已删除；后续只维护 Qt 6 + CMake + Ninja 构建路径。
- GUI smoke / large file perf / relationship fixture 等 CTest 回归。

仍未完成或仍是过渡形态：

- SemanticIndexSnapshot 已接入后台生产和主线程世代切换，但仍需要继续减少 live sym_list 消费。
- base snapshot 生产和 relationship merge 已收束到 SemanticIndex / SemanticIndexSnapshot helper。
- DiagnosticService 已有真实 Slang diagnostics 数据，Problems 面板已支持基础筛选、workspace scope、
  行/列跳转、DiagnosticReport 驱动的排序/计数/fileGroups、diagnostic replacement 生命周期和 workspace close/reopen 清空。
- ReferenceService 已有 References dock 作为真实 UI 消费点，并提供 ReferenceReport 驱动 scope/type
  过滤、排序和计数。
- RelationshipService 已有 Relationships dock 作为真实 UI 消费点；relationship / hierarchy 浏览已支持双向树、
  递归去重、根节点、展开状态保持和分组计数，Direct 视图已通过 RelationshipReport 下沉 incoming/outgoing 合并、
  排序、direction/type 计数；Tree 视图已通过 HierarchyReport 下沉节点和 depth/direction/root/type 计数；
  后续仍可继续打磨按层刷新策略和更细的类型策略。
- CompletionManager 大批只读符号查询、关系读取、scope names、cached file content
  已收束到 SemanticIndex / RelationshipService；仍有状态性过渡依赖。
- MyCodeEditor 注释判断、跳转定义和主要补全入口已开始经由 Tree-sitter live syntax / services；
  仍有少量补全触发细节和状态性过渡依赖。

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
DiagnosticService / ReferenceService / RelationshipService 已开始提供 report API，承接底部面板的排序、
过滤结果和摘要计数；HierarchyService 已开始提供 HierarchyReport，承接 Relationships Tree 的节点和计数。
后续重点是继续消费端迁移，并让服务内部从 sym_list-backed 迁到 snapshot-backed。

### 阶段 6：SemanticIndexSnapshot

snapshot 类型和第一版生产 / 切换闭环已落地：

- `SemanticIndexSnapshot` 保存 symbols / relationships / diagnostics / cached file content / minimal scope names。
- `SemanticIndex` 支持 `setSnapshot` / `clearSnapshot`。
- snapshot 存在时，symbols / definitions / relationships / diagnostics / cached file content /
  scope symbol names 优先读 snapshot。
- SymbolAnalyzer 在符号写回后发布 symbols-only snapshot。
- Workspace / single-file 关系后台基于 snapshot 计算关系，并按 base snapshot 世代发布 enriched snapshot。
- base snapshot capture 由 SemanticIndex 统一保留上一代 diagnostics；enriched snapshot relationship 合并由
  SemanticIndexSnapshot 统一去重。
- single-file/open-tab diagnostics replacement 由 SemanticIndex / SemanticIndexSnapshot helper 统一处理。
- module end line、module name validity 和 contentAffectsSymbols 过渡查询已进入 SemanticIndex；
  snapshot 路径可通过 cached file content 查询 module end line。

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
- Problems 支持 Workspace Files scope，过滤逻辑由 DiagnosticService 提供。
- Problems 结果按 severity / file / line / column 稳定排序。
- Problems All Files 分组显示数量，并提供 No problems 空状态。
- Problems 面板已改为消费 DiagnosticReport，排序、总数、file count、severity count 和 fileGroups 由 DiagnosticService 提供。
- Problems 在 single-file/open-tab 分析时只替换目标文件 diagnostics，保留其它文件 diagnostics。
- Problems 在 workspace close 和 workspace symbol analysis start 时刷新，避免旧 diagnostics 视觉残留。

后续重点：

- 更多真实 fixture。
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
- References 面板已改为消费 ReferenceReport，scope/type 过滤结果、排序、file/type 计数由 ReferenceService 提供。
- 编辑器支持 Shift+F12 触发 Find References。
- MainWindow 提供底部 Relationships dock，编辑器右键 Show Relationships 触发。
- Relationships dock 通过 RelationshipService 查询 incoming / outgoing relationships。
- Relationships dock 支持 direction / type 筛选。
- Relationships Direct 视图按 direction -> relationship type -> result 分组。
- Relationships Direct 视图按类型和位置稳定排序，direction / type 分组显示数量。
- Relationships Direct 视图已改为消费 RelationshipReport，incoming/outgoing 合并、排序、方向/type 计数由
  RelationshipService 提供。
- Relationships Tree 视图接入 HierarchyService，支持 Depth 1..4 层级浏览。
- HierarchyService 支持 Children / Parents / Both 方向查询，并通过路径去重防止递归循环。
- Relationships Tree 支持 Root 节点、Incoming / Outgoing 分支、方向筛选和全部关系类型策略。
- Relationships Tree 消费 HierarchyReport，节点、depth/direction/root/type 计数和 All Types 策略由 HierarchyService 提供。
- Problems / References / Relationships 树刷新保留用户展开/折叠状态。
- 编辑器支持 Ctrl+Shift+R 触发 Show Relationships。
- References / Relationships 条目双击可跳转文件、行和列。

后续重点：

- References 结果摘要、更多 workspace 维度和跨文件上下文。
- Relationships 按层刷新和更细的类型策略。
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

## Session 2026-06-08 architecture note

- SemanticIndex now hides relationship engine attachment and SmartRelationshipBuilder creation from MainWindow and CompletionManager.
- This keeps live sym_list database wiring inside the facade boundary while preserving the current snapshot migration path.
- relationship_test covers the new facade boundary.
- Verification passed for relationship_test, completion_test, and gui_smoke_test via CTest; git diff --check and the source/test/UI/CMake non-ASCII scan also passed.
- Continue toward the same goal: UI/editor/services should read through SemanticIndex and Query Services, with live sym_list access contained inside transition facades until snapshot-backed reads replace it.

## Session 2026-06-08 continuation note

- CompletionManager now centralizes more semantic reads through helper methods backed by SemanticIndex.
- Module-context completion prefers SemanticIndex cached content before direct file fallback, improving snapshot-backed read coverage without changing completion behavior.
- Focused verification passed for completion_test and gui_smoke_test.

## Session 2026-06-08 continuation 2 note

- CompletionManager now uses SemanticIndex typed queries for several type-specific completion helpers instead of scanning all symbols.
- This is another small step toward UI/editor reads going through narrower snapshot-backed query shapes.
- Focused verification passed for completion_test and gui_smoke_test.

## Session 2026-06-08 continuation 3 note

- CompletionManager now uses narrower typed SemanticIndex reads for additional command/type-specific paths instead of local full-symbol scans where the requested type is already known.
- ReferenceService and RelationshipService now provide structured report groups for the bottom docks.
- MainWindow References and Relationships Direct refresh paths now render service-provided groups instead of owning file/type or direction/type grouping policy.
- This advances the goal that MainWindow coordinates windows while services own query results, grouping, counts, and stable report shape.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test, completion_test, and gui_smoke_test: 3/3 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no *.pro/*.pri files and .claude is absent.
- Next goal step: continue moving refresh/analysis policy out of MainWindow, add real Problems/References/Relationships fixtures, and keep UI/service reads behind SemanticIndex, snapshots, and Query Services.

## Session 2026-06-08 continuation 4 note

- ReferenceService and RelationshipService reports now expose the resolved subject symbol for their result sets.
- MainWindow consumes that report subject for References / Relationships Direct titles and status messages, keeping more result context in services.
- Workspace relationship batch analysis moved from MainWindow into AnalysisScheduler:
  - scheduler owns ProjectSnapshot traversal, base snapshot reads, relationship computation, enriched snapshot merge, and builder cancellation.
  - MainWindow only wires the builder and remains responsible for UI/progress coordination.
- relationship_test covers the scheduler workspace relationship path with the real multi-file fixture and verifies the enriched snapshot contains the cross-file instantiation.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test and gui_smoke_test: 2/2 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue thinning MainWindow progress/refresh policy and keep UI/service reads behind SemanticIndex, snapshots, and Query Services.

## Session 2026-06-08 continuation 5 note

- AnalysisScheduler now owns both workspace and single-file relationship background analysis boundaries.
- The single-file path moved out of MainWindow:
  - scheduler owns the watcher, cancellation of older single-file work, base snapshot capture, relationship computation, and enriched snapshot merge.
  - MainWindow consumes AnalysisScheduler::relationshipAnalysisFinished and keeps only engine/UI completion handling.
- This advances the goal that MainWindow coordinates windows while AnalysisScheduler owns analysis task policy.
- relationship_test covers the scheduler single-file path with the real multi-file fixture and verifies the enriched snapshot contains the cross-file instantiation.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test, gui_smoke_test, and large_file_perf_test: 3/3 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue thinning relationship progress/refresh policy in MainWindow and keep UI/service reads behind SemanticIndex, snapshots, Query Services, and scheduler-owned task boundaries.

## Session 2026-06-08 continuation 6 note

- AnalysisScheduler now owns the relationship progress/error/cancel signal boundary for relationship tasks.
- Scheduler emits result-based progress before single-file and workspace relationship finished signals, giving UI a stable scheduler-owned progress event.
- MainWindow no longer connects directly to SmartRelationshipBuilder analysisCompleted / analysisError / analysisCancelled.
- This advances the goal that MainWindow remains a window coordinator while AnalysisScheduler owns analysis task policy and progress boundaries.
- relationship_test covers scheduler single-file progress forwarding on the real multi-file fixture.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test, gui_smoke_test, and large_file_perf_test: 3/3 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue thinning MainWindow refresh scheduling and keep UI/service reads behind SemanticIndex, snapshots, Query Services, and scheduler-owned task boundaries.

## Session 2026-06-08 continuation 7 note

- AnalysisScheduler now owns Problems diagnostics refresh request timing through diagnosticsRefreshRequested(fileName).
- MainWindow no longer decides Problems refresh timing for workspace close, workspace analysis start, or SymbolAnalyzer batch completion.
- MainWindow keeps only UI debounce and tree rendering for Problems.
- This advances the goal that analysis lifecycle policy belongs to AnalysisScheduler while MainWindow remains a window coordinator.
- relationship_test covers scheduler project-close diagnostics refresh requests.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test, gui_smoke_test, and large_file_perf_test: 3/3 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue thinning References/Relationships refresh scheduling and keep UI/service reads behind SemanticIndex, snapshots, Query Services, and scheduler-owned task boundaries.

## Session 2026-06-08 continuation 8 note

- AnalysisScheduler now owns the SymbolRelationshipEngine relationship data invalidation and refresh-request boundary.
- Scheduler emits relationshipDataInvalidated immediately for relationship changes, coalesces relationshipDataRefreshRequested for additions, and requests immediate refresh on clear.
- MainWindow no longer connects directly to relationshipAdded / relationshipsCleared and no longer owns relationshipRefreshDeferTimer.
- This advances the goal that MainWindow coordinates windows while AnalysisScheduler owns analysis lifecycle and refresh timing boundaries.
- relationship_test covers scheduler relationship invalidation, coalesced refresh on additions, and immediate refresh on clear.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test, gui_smoke_test, and large_file_perf_test: 3/3 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue shrinking remaining MainWindow progress UI policy if a clean scheduler signal boundary exists, or add another real Problems / References / Relationships fixture.

## Session 2026-06-08 continuation 9 note

- ReferenceService real fixture coverage was tightened for current-file filtering.
- relationship_test now checks that currentFileOnly keeps the matching top file for the rel_stage instantiation reference, not only that it hides the non-matching stage file.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test: passed.
- Next goal step: continue adding focused real Problems / References / Relationships fixture coverage, or move another clean MainWindow progress UI boundary into AnalysisScheduler.

## Session 2026-06-08 continuation 10 note

- Final handoff point requested because context is compressing.
- Problems / Relationships real fixture coverage was tightened again:
  - DiagnosticReport direct fileName filtering now checks topPath keeps two diagnostics and one file group.
  - RelationshipReport incoming-only browsing now checks rel_stage sees the top -> stage instantiation and top peer symbol.
- Verification passed:
  - Full ctest --output-on-failure: 6/6 passed.
  - Focused ctest for relationship_test: passed.
- Next goal step: in a fresh session, continue with another focused Problems / Relationships real fixture or extract one remaining clean MainWindow progress UI boundary into AnalysisScheduler.

## Session 2026-06-08 post-push handoff note

- User requested commit and push after the continuation 10 handoff.
- The accumulated scheduler refresh and real fixture changes were committed and pushed.
- readme.md's opener now expects a clean tree and tells the next session to use git log for the latest commit.
- Validation immediately before commit:
  - Full ctest --output-on-failure: 6/6 passed.
  - git diff --check passed.
  - source/test/UI/CMake non-ASCII scan was empty, excluding readme.md / plan.md / goal.md.
  - forbidden-file guard found no demo.pro, no *.pro, no *.pri, and .claude is absent.
- Next goal step: continue from a clean tree with another focused Problems / Relationships fixture or a clean MainWindow progress UI boundary extraction.
