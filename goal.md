# ZeroSlack Product / Architecture Goal

本文档记录 ZeroSlack 现阶段的最终目标和开发骨架。后续功能和重构默认以此为准；
除非遇到无法越过的实现问题，否则不再为每个新功能重新选择架构方向。

## 目标定位

ZeroSlack 当前目标不是一次性做完整 IDE，而是先成为一个可靠的 SystemVerilog
工程浏览器 / 轻量编辑器：

- 能稳定打开真实 SV workspace。
- 能理解 module、package、include、typedef、enum、struct、interface、instance 等工程结构。
- 能提供可信的补全、跳转、导航、关系分析和诊断入口。
- 能在大文件和多文件工程中保持可验证的响应性。

产品形态上，ZeroSlack 应优先服务“读工程、理解工程、定位定义和关系”，再逐步扩展完整编辑体验。

## 核心架构目标

后续目标架构分为五层：

```text
ProjectModel
  管工程：root、filelist、include dirs、defines、top、文件集合

DocumentModel
  管打开文档：文本版本、dirty state、Tree-sitter live tree、光标相关轻量信息

AnalysisScheduler
  管分析任务：何时跑 Slang、何时跑关系、取消、去抖、优先级、缓存命中

SemanticIndex
  管事实快照：symbols、definitions、references、relationships、diagnostics

Feature/UI Layer
  只消费 index 和 document，不直接扫文件、不直接调 Slang
```

当前 `Tree-sitter + Slang` 路线保持不变：

- Tree-sitter 负责即时、容错、低延迟的编辑器体验：高亮、增量语法树、当前 module scope。
- Slang 负责真实语义：符号、类型、定义、跳转、关系、诊断、工程级事实。
- 不恢复 SVLexer、旧 Tree-sitter symbol parser、Tree-sitter 验证按钮或关系分析正则路径。

## 实现原则

后续新增功能默认遵守这些规则：

- UI 不直接触发 Slang；编辑器、导航栏、补全面板只能请求服务。
- Slang 结果不散落写入 UI 或临时全局状态；先进入 `SemanticIndex` 或其 facade。
- 新功能不直接依赖 `sym_list`；短期可由 `SemanticIndex` 包住 `sym_list`，长期迁到 snapshot。
- 所有分析触发集中到 `AnalysisScheduler`，避免在 `MainWindow`、`MyCodeEditor`、各 UI 控件中继续添加零散 debounce / timer / reanalysis 判断。
- Tree-sitter 只用于即时语法体验；定义、类型、关系、诊断以 Slang / SemanticIndex 为准。
- 功能先落服务层，再接 UI。Ctrl+Click、导航窗格、命令面板、右键菜单应复用同一套 query service。
- 每个工程级功能都必须有 fixture 或测试入口，尤其覆盖 package、include、macro、file order、interface、modport、多文件关系。
- 性能调试只加针对性、可移除或测试内探针；不恢复长期散落的 `perflog`。

## 分阶段路线

### 阶段 0：冻结当前回归基线

目的：迁移底座前先钉住现有能力。

- 保持现有 CTest 全绿。
- 补真实多文件关系 fixture，覆盖 package/import、跨文件 instance、call、assignment、condition read、clock/reset。
- 明确不可回退行为：补全、跳转、导航、GUI smoke、大文件空白编辑性能。

### 阶段 1：SemanticIndex Facade

目的：阻止新功能继续直接依赖 `sym_list`。

第一版可以内部调用现有 `sym_list`，但外部新增代码只通过正式接口读语义事实：

- `getSymbols(file)`
- `findDefinitions(name, context)`
- `findCompletions(context)`
- `getRelationships(scope)`
- `getDiagnostics(file)`

实现路线分两条，不要混成同一件事，否则会被存量规模拖死：

- **新代码合规（硬约束，facade 落地即生效）**：从 facade 可用起，所有新增功能只走 facade，禁止再直接读写 `sym_list`。这是一道门，不是迁移工作量，可以立即达成。
- **存量迁移（持续进行，可长期并行）**：现有补全、跳转、导航、关系查询对 `sym_list` 的直接引用分散在 completion / editor / navigation 等多个文件，量很大。按文件逐步迁移，不要求在本阶段内一次清空；facade 与 `sym_list` 在迁移期并存是预期状态，不视为阶段未完成。

阶段“完成”的判定以新代码合规为准；存量迁移作为跨阶段的持续任务，贯穿后续阶段推进。

### 阶段 2：DocumentModel

目的：把“打开文档的状态”从 `MyCodeEditor` 里抽出来，成为独立、可被调度层订阅的一层。

现状是 `MyCodeEditor` 直接持有 `TSDocument` 并自己管理文档状态，导致分析触发逻辑也被迫挂在编辑器上。最小版本包含：

- 每个打开文档的文本版本号。
- dirty / saved 状态。
- 该文档的 `TSDocument`（live tree）持有权。
- 光标位置、当前 enclosing module 等轻量信息。
- 对外发布文档事件（opened / edited / saved），供 `AnalysisScheduler` 订阅。

完成后，`MyCodeEditor` 只负责编辑器 UI 与交互，文档状态和事件由 `DocumentModel` 统一持有。

### 阶段 3：AnalysisScheduler

目的：收束分析触发路径。

统一处理：

- workspace opened / rescanned
- document opened
- document edited
- file saved
- project config changed

统一决定：

- 是否跑 Slang。
- 是否跑关系分析。
- 是否取消旧任务。
- 是否 debounce。
- 是否只更新 Tree-sitter 即时状态。
- 什么时候发布新 index。

`MainWindow` 和 `MyCodeEditor` 不再继续承担分析调度中心职责。

实现注意：

- 本阶段依赖阶段 2 的 `DocumentModel` 提供统一文档事件，否则 scheduler 只能在 `MainWindow` / `MyCodeEditor` 上转发信号，退化成壳子。
- scheduler 抽取与阶段 1 的存量 `sym_list` 读取迁移会相互牵动，不必强求严格串行：可以先让 scheduler 接管“何时触发、是否取消、是否 debounce”的决策，数据读取迁移继续并行推进。

### 阶段 4：ProjectModel

目的：让 ZeroSlack 真正理解 SV 工程输入，而不是只扫描文件。

最小版本包含：

- workspace root
- SystemVerilog 文件集合
- include dirs
- defines
- optional filelist
- optional top module
- ignored paths

后续 SlangManager / AnalysisScheduler 使用 `ProjectModel` 提供的工程输入，而不是各自临时拼文件列表。

### 阶段 5：Query Services

目的：让产品功能复用统一查询能力。

逐步建立：

- `DefinitionService`
- `CompletionService`
- `ReferenceService`
- `RelationshipService`
- `HierarchyService`
- `DiagnosticService`
- `SearchService`

现有 Ctrl+Click、导航跳转、补全、关系浏览应迁移到这些服务。后续 UI 功能只调用服务，不复制符号过滤和跳转逻辑。

### 阶段 6：SemanticIndex Snapshot

目的：稳定线程边界和 UI 读取一致性。

最终形态：

```text
后台分析产出 SemanticIndexSnapshot
主线程原子切换 currentSnapshot
UI 只读 snapshot
旧 snapshot 自动释放
```

短期可以先 facade 包 `sym_list`，长期应减少 UI 对可变全局数据库的直接读写。

## 产品能力路线

底座稳定后，功能优先级建议如下：

1. 真实工程配置：filelist、include dirs、defines、top module。
2. Problems / diagnostics 面板：展示 Slang 错误、warning、include 找不到、宏缺失等原因。
3. 工程浏览增强：module hierarchy、instance tree、who instantiates this、signal read/write、package/import 浏览。
4. 查询能力：find references、symbol search、quick open、jump history。
5. 编辑器体验：搜索/替换、session restore、split view、快捷键体系、行内诊断标记。
6. 可视化：文件依赖图、关系图、时钟/复位视图。

## 当前避免扩大的点

后续开发应避免继续扩大这些模式：

- 不继续让 `MainWindow` 堆分析调度逻辑。
- 不继续让 `MyCodeEditor` 直接理解复杂语义。
- 不让新功能直接读写 `sym_list`。
- 不让每个功能各自决定何时重跑 Slang。
- 不为 UI 功能临时添加 SystemVerilog 正则语义猜测。
- 不把性能探针长期散在业务代码里。

## 推荐排期

现实估计：

- 2-3 周：搭出可用底座的 70%，主要完成 facade、scheduler 最小版、project model 最小版。
- 4-6 周：底座比较扎实，Query Services 和 snapshot 边界开始稳定。
- 8 周以上：连同诊断面板、工程配置 UI、产品体验一起打磨。

推荐顺序：

```text
多文件 fixture
→ SemanticIndex facade
→ DocumentModel
→ AnalysisScheduler
→ ProjectModel
→ Query Services
→ SemanticIndex snapshot
```

## 完成标准

底座完成时，应满足：

- 后续新增功能主要接入 `ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services`。
- `MainWindow` 只做窗口协调，不做分析策略中心。
- `MyCodeEditor` 只做编辑器 UI 和即时 Tree-sitter 体验，不承担工程语义判断中心。
- 现有补全、跳转、导航、关系分析、GUI smoke、大文件性能测试全部保持通过。
- 至少一个真实多文件 SV fixture 覆盖 package/import、跨文件跳转、实例化、调用、赋值、条件读取、clock/reset。
- 新会话只读 `readme.txt`、`plan.md`、`goal.md` 就能理解当前状态、下一步和最终骨架。
