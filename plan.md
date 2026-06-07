# ZeroSlack Next Plan

当前版本：`0.0.16/slang21`
当前分支：`tree_sitter_and_slang`

本计划服务于 `goal.md` 中的产品 / 架构目标。后续实现默认继续向：

`ProjectModel / DocumentModel / AnalysisScheduler / SemanticIndex / Query Services`

收敛。除非遇到无法绕过的实现问题，不再为每个功能重新选择架构方向。

## Current Status

主线仍是 Route A：

- Tree-sitter：实时语法、高亮、增量文档、live module scope。
- Slang：符号、类型、定义、跳转、补全、关系、诊断事实来源。
- SemanticIndex：当前是 sym_list-backed facade，新增语义读取优先通过它。
- Query Services：Definition / Completion / Relationship / Hierarchy / Reference /
  Diagnostic / Search 最小边界已落地。
- Navigation UI：模块层级和符号 outline 已有显式 model，Widget 不再在双击时反查 sym_list。
- Scheduler：workspace opened/rescanned/project config changed 已经通过 ProjectModel
  projectChanged 进入 AnalysisScheduler。

最近验证：

- completion_test：14 checks, 0 failed。
- jump_test：10 checks, 0 failed。
- relationship_test：59 checks, 0 failed。
- gui_smoke_test：46 checks, 0 failed。
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
- RelationshipService / HierarchyService：关系和层级查询最小边界已落地。
- ReferenceService / DiagnosticService / SearchService：最小边界已落地并有测试断言。

### P4 Navigation 消费端迁移

- ModuleHierarchyGroup：模块层级 UI 显式模型。
- SymbolOutlineGroup：符号 outline UI 显式模型。
- NavigationManager 消费 DefinitionService / HierarchyService / SearchService / SemanticIndex。
- NavigationWidget 不再在符号双击时反查 sym_list / service，而是使用 item payload。

## 下一步建议

### 1. 继续迁移 editor / UI 消费端

当前仍有少量直接 sym_list 读取：

- `MyCodeEditor` 内的注释判断和补全触发细节。
- `CompletionManager` 内部大量旧逻辑。
- `MainWindow::setupRelationshipEngine()` 仍需要把 relationship engine 绑定到 sym_list，这是当前过渡期可接受的初始化点。

建议小步推进，不要一次性重写 CompletionManager。

### 2. 关系浏览 UI 迁到 services

RelationshipService / HierarchyService 已有底层边界，后续可以继续把关系浏览 UI 的消费端迁过去。

### 3. Reference / Diagnostic UI

- ReferenceService 当前已能从 RelationshipService 查询 incoming references。
- DiagnosticService 当前委托 SemanticIndex::getDiagnostics，仍为空实现。
- 后续可先补 Slang diagnostics 到 SemanticIndex，再挂 Problems panel。

### 4. SemanticIndexSnapshot 设计

下一块较大的架构工作：

```text
后台分析产出 SemanticIndexSnapshot
主线程原子切换 currentSnapshot
UI / Services 只读 snapshot
旧 snapshot 自动释放
```

短期可以继续保持 sym_list-backed facade，但新增代码要避免绕过 SemanticIndex / services。

## Definition Of Done

每轮较大改动至少保持：

- `ctest --output-on-failure` 6/6 passed。
- `git diff --check` 无错误。
- 不恢复 SVLexer、旧 Tree-sitter parser、Tree-sitter 验证按钮或关系分析正则路径。
- 不引入长期散落 perflog。
- `.claude/settings.local.json` 不删除、不 stage。

## Useful Commands

```powershell
$env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\CMake_64\bin;E:\QT6\Tools\Ninja;$env:PATH"

& "E:\QT6\Tools\CMake_64\bin\cmake.exe" --build "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --target demo gui_smoke_test relationship_test completion_test jump_test

& "E:\QT6\Tools\CMake_64\bin\ctest.exe" --test-dir "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --output-on-failure
```
