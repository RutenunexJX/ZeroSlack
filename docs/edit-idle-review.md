# 编辑空闲 trivia 刷新验收记录

实施版本：0.25.10。对照基线：`23f6e5e05020e21832f0c9b5956c5afac63c7eaf`（0.25.9），2026-09-12，Qt 6.10.2 / MinGW 13.1.0 Shared Release。

新增行为只覆盖当前工程中已有语义基准的文件。停止编辑 250 ms 后，工作线程验证注释/空白变化并重映射快照；文档仍未保存，但可恢复 Current。非 trivia、错误树、不兼容映射被丢弃。保存、action 要求及 Dirty → Stale 映射保持原有规则。

本报告记录实施验收结果。原生桌面手工验证未测；不能把自动化服务测试视为手工操作结果。

## 验收清单

路径均相对于仓库 `E:/ZeroSlack/ZeroSlack`。本地测试日志位于忽略目录 `build/evidence/`。

| # | 检查项 | 要求 / 证据 | 结果 |
| --- | --- | --- | --- |
| 1 | 枚举追加 | `src/semantic/semanticanalysisrequest.h:21`，名称映射 `semanticanalysisrequest.cpp:20` | 末尾追加 EditIdle，原数值未变。 |
| 2 | worker 门禁顺序 | `src/analysis/incrementalsemanticanalysisworker.cpp:331`；后续才进入依赖图处理和 computation 注册 | 分类完成后检查 TriviaOnly、错误树与位置映射兼容性；拒绝不进入依赖图或 Slang。 |
| 3 | 丢弃而非 Failed | `semanticanalysisrequest.h:88`；`symbolanalyzerincremental.cpp:150`；`workspacesymbolanalysiscontroller.cpp:86` | 新增 TriviaGateRejected，经 dropped 信号与现有过期清理结束；失败分支未用于拒绝。 |
| 4 | 保留原编辑钩子 | `src/analysis/analysisschedulerdocuments.cpp:574`；`build/evidence/edit-idle-scheduler.diff` | 原语句全部保留，末尾只追加 scheduleEditIdleSemanticRefresh。 |
| 5 | 防抖及取消 | 常量 `analysisscheduler.h:149`；重置 `analysisschedulerdocuments.cpp:495`；保存 593、文档关闭 508、工程变更 631、工程关闭 660；`analysisscheduler.cpp:42,87,166` | 250 ms PreciseTimer；退出、禁用、模型切换亦取消。timer 由 scheduler 所有，stop + deleteLater。 |
| 6 | 六项前置条件 | `analysisschedulerdocuments.cpp`：工程打开 469、源集合 471–477；策略 468；Dirty 477；文档快照 483–485；基准非 null/文本不同 486–488 | 六项全部实现；另外 481 检查执行器忙碌，理由见下文。 |
| 7 | 上下文服务未修改 | `build/evidence/edit-idle-action-guards.diff`、`edit-idle-static-verification.json` | 差异 0 bytes，Dirty → Stale 未改。 |
| 8 | SemanticCurrent 未降级 | `edit-idle-action-guard-before.txt`、`edit-idle-action-guard-after.txt` | `actionregistry.cpp` 中文本出现次数 13 → 13，文件差异为空；这是出现次数，非 action 数量。 |
| 9 | 原发布路径 | `analysisschedulerdocuments.cpp:743`、766 | dirtyBufferPublished 原分支未改；revision、sourceOverrides、发布文本一致性仍共同决定 Current。 |
| 10 | 九类新增用例 | 下表列出函数；`edit-idle-regression-details.log` | 调度器原 245 条断言，加 63 条新断言，共 308 条，0 失败。 |
| 11 | 位置期望独立计算 | `test_sv/analysis_scheduler_test.cpp:3545`；下方代码 | 从编辑后文本独立扫描实际声明 offset、行、列；没有用 delta 推算。头尾编辑之间的声明同样验证。 |
| 12 | 指定五项回归 | `build/evidence/edit-idle-regression.log` | 5/5 通过，18.05 s；详见下表。 |
| 13 | 全量 CTest 对照 | `edit-idle-baseline-ctest.log`、`edit-idle-final-ctest.log`、`edit-idle-ctest-comparison.json` | 基线 104/107 通过、3 失败，242.26 s；最终 104/107 通过、相同 3 项失败，241.17 s。失败集合比较相同；首轮中间结果保存在 edit-idle-first-ctest.log。 |
| 14 | 既有测试未放宽 | `build/evidence/edit-idle-tests.diff`、`edit-idle-perf-details.log` | analysis_scheduler_test 原断言不变；large_file_perf_test 的三条零请求断言按已记录的行为变更更新，新增 9 条更强约束，67 checks、0 failed。所有性能阈值、超时和零发布/重建断言保留。大文件生成器原样提取。 |
| 15 | 输入延迟 | `edit-idle-baseline-metrics.log`、`edit-idle-regression-details.log`、`edit-idle-final-metrics.log` | 原 editor_incremental_test 阈值全部通过；数值见下表。未单独测量挂接 scheduler 的改动前/后 100 次输入，不能由该结果宣称输入耗时绝无变化。 |
| 16 | 连续输入请求数 | `runEditIdleDebouncesContinuousTyping`，日志 `edit_idle.continuous_typing.requests` | 8 次编辑、间隔 40 ms，期间请求数 0；停顿后恰好 1 请求、1 发布。 |
| 17 | 大文件耗时 | `build/evidence/edit-idle-large.log`；`runEditIdleLargeFileMeasurement` | 2,712,122 字符，端到端 4,501 ms，worker 4,088 ms；未调用 Slang。使用单符号位置基准，局限见下文。 |
| 18 | 无新增未保存文本写盘 | `runEditIdleTriviaAllowsDirtyBuffer`、`runEditIdleGatePreservesDiagnosticsAndState` | 接受/拒绝前后递归比较临时工程全部文件的 SHA-256 及文件集合，完全一致；CLI 源码未改，语义 worker/发布链只更新内存。原有崩溃恢复机制不属于本次新增写入。 |
| 19 | 手工注释后恢复与跳转 | 见下方手工步骤；自动化补充为 `runEditIdleActionAvailabilityFollowsPublishedState` | **未测**原生桌面操作，无截图。自动化已验证 Current、F12 原 action 可执行和声明行列，但未实际按 F12 跳转。 |
| 20 | 手工重命名后置灰 | 同上自动化函数 | **未测**原生桌面操作，无截图。服务测试确认 Stale、F12 不可执行，右键语义项提示仍为原文。 |
| 21 | 文档版本同步 | VERSION / CHANGELOG / README / 用户手册 / plan / goal / VERSIONING / package README | 均同步到本次行为或 0.25.10；VERSION 使用仓库规定的纯数字格式。 |

## 新增用例与回归

完整构建通过。最终全量测试与基线一致的 3 项失败如下；未将其算作通过，也未改动这些用例。

| 已有失败 | 基线与最终观察到的问题 |
| --- | --- |
| temporary_editor_context_provider_test | Back navigation 未同时更新 view 和共用 Peek 标题，1/20 失败。 |
| pinloom_context_provider_test | Pinloom rail 打开资源和 host bridge 统一搜索断言失败，随后 access violation。 |
| workspace_persistence_test | 真实 new / huge_prj legacy fixture 导入及字节/时间戳不变断言失败，27/29 通过。 |

新增用例均位于 `test_sv/analysis_scheduler_test.cpp`，测试辅助访问沿用 AnalysisSchedulerTestAccess。

| 性质 | 测试函数 |
| --- | --- |
| 未保存 trivia 恢复 Current | runEditIdleTriviaAllowsDirtyBuffer |
| 声明位置正确 | runEditIdleDeclarationLineMatchesEditedText |
| 分散 trivia 与中间声明 | runEditIdleSeparatedTriviaMapsMiddleSymbol |
| 重命名、位宽、增加端口拒绝 | runEditIdleNonTriviaRemainsDirty |
| 错误新树拒绝 | runEditIdleSyntaxErrorRemainsDirty |
| 无基准零请求 | runEditIdleNoBaselineDoesNotRequest |
| 连续输入防抖 | runEditIdleDebouncesContinuousTyping |
| 拒绝保留诊断、状态与磁盘 | runEditIdleGatePreservesDiagnosticsAndState |
| 实际 TabManager 保存及干净 Ctrl+S | runEditIdleSaveUsesExistingTabSavePath |
| FullWorkspace 策略、另一文件 pending clean、取消 | runEditIdlePolicyAndCancellation |
| 在途请求被编辑失效、只发布最新版本 | runEditIdleInFlightRequestIsInvalidated |
| 错误旧树、缺基准、映射不兼容 | runEditIdleWorkerGateBoundaries |
| action 状态与原置灰文案 | runEditIdleActionAvailabilityFollowsPublishedState |
| 大文件独立测量参数 `--edit-idle-large` | runEditIdleLargeFileMeasurement |

位置断言的关键计算如下，直接以编辑后的文本为依据：

```cpp
const QString text = editor.toPlainText();
const int position = text.indexOf(name);
const int line = text.left(position).count(QLatin1Char('\n')) + 1;
const int column = position - text.lastIndexOf(QLatin1Char('\n'), position);
return record.location.startLine == line
    && record.location.startColumn == column
    && record.location.position == position;
```

| 指定回归 | 结果 | 耗时 |
| --- | --- | --- |
| analysis_scheduler_test | 308 checks, 0 failed | 11.26 s |
| action_registry_test | 73 checks, 0 failed | 0.07 s |
| editor_incremental_test | 169 checks, 0 failed，原阈值不变 | 5.63 s |
| ts_large_file_semantics_test | 26 checks, 0 failed | 0.77 s |
| rtl_action_coordinator_test | 25 checks, 0 failed | 0.27 s |
| large_file_perf_test（补充复测） | 67 checks, 0 failed | 7.37 s |

输入测试采用已有 editor_incremental_test 的按键与事件处理时延统计，单位 μs。以下是单次基线/指定回归运行，不是受控调度器增量开销实验。

| fixture / 指标 | 0.25.9 | 0.25.10 指定回归 | 0.25.10 最终全量 |
| --- | ---: | ---: | ---: |
| rtl_top 整体 p50 / p95 | 654 / 752 | 668 / 942 | 645 / 744 |
| huge 整体 p50 / p95 | 724 / 832 | 756 / 1165 | 719 / 845 |
| visible Wave 整体 p50 / p95 | 1818 / 2186 | 1789 / 2154 | 1775 / 2312 |
| rtl_top 同步按键 p95 | 333 | 371 | 336 |
| huge 同步按键 p95 | 314 | 446 | 334 |
| visible Wave 同步按键 p95 | 567 | 601 | 567 |

## 与实施文件描述不同的边界

0. 第一轮修改后全量 CTest 为 103/107，除原 3 项失败外，large_file_perf_test 的三条“编辑后启动的请求/工作区任务数为 0”断言失败，实际计数为 1/2/2。这些旧断言禁止了本次明确要求的 EditIdle 分类请求，已不适用于新行为。先记录这一例外，再更新该测试：要求停顿后恰好一个单文件 EditIdle 请求、明确拒绝且保持 Dirty；既有零 worker 发布、零快照发布、零 Design 重建断言保留，另验证 Slang 和诊断/快照不变。全部原性能阈值与 2200 ms 等待不变。该旧用例的“trivia burst”末尾没有换行，可能把原文件首行并入注释，不应用来断言 trivia 接受；真正的接受及独立位置性质由新增调度器用例验证。

1. `incrementalanalysisplanservice.cpp:170` 原来优先执行 FullWorkspace 策略，会把已经证明等价的请求升级为 Slang 编译。本次在规划层为门禁已通过的请求保留单文件 TriviaOnly，普通保存和工作区请求仍走原策略。
2. `semanticsourceremap.cpp:194` 原来未检查 isCompatible，可能使用 fallback delta。本次 worker 在任何计算注册前拒绝不兼容映射，remapSnapshot 本身也返回空指针作防御。
3. 执行器原本会让新请求替代当前任务。`analysisschedulerdocuments.cpp:481` 新增忙碌检查，避免空闲编辑抢占保存或工作区分析，并保证没有 EditIdle 积压。若 250 ms 到时执行器仍忙，本轮静默让位，不自动重试；后续编辑或保存重新触发。这是六项条件之外的保守限制。
4. 既有 `ts_large_file_semantics_test::makeFixture()` 含用于补全的 `always_comb beg` 错误片段，原样用于接受测试必然被门禁拒绝。生成器已原样移到 `large_file_semantic_fixture.h`，原测试不变；本次性能用例仅把该片段补全为 `begin end`。
5. 首轮性能准备尝试为 24,001 个模块构建完整 Slang 基准，运行超过数分钟后停止，未获得可用的完整基准测量。补充测量以相同大文件文本和一个位置记录作为受控基准，通过真实 editor → scheduler → worker → publication 链测量；不能外推到完整工程关系快照。
6. 大文件补充测量超过 1 秒：worker 的 4,088 ms 占端到端 4,501 ms 的约 91%。该路径包含分类、兼容性检查、依赖事实和重映射中的多轮全文件 Tree-sitter 处理；各子阶段未分别计时，不能给出更细的归因。此阶段未优化它们。
7. 现有右键环形菜单已排除 Go to Definition；该功能仍由 F12/action 提供（`actionregistry.cpp:874,925`）。手工验证应分别检查 F12 与右键内的其它语义项。
8. 当前工具不提供原生桌面交互，验收 19/20 未测，未生成冒充手工验证的截图。

## 手工复核步骤（未执行）

截图预留目录：`build/evidence/edit-idle-review/`。

1. 打开已完成分析的 SystemVerilog 工程。在一个信号声明上方插入一行注释和空行，不保存。暂停后检查 RTL Insights 右键项恢复可用；在引用处按 F12，确认跳到编辑后实际声明行。记录 `trivia-current-and-definition.png`。
2. 在同一文件重命名信号但不保存。暂停后检查语义项仍不可执行，悬停文案仍为 `Semantic snapshot is stale; analyze the workspace.`。记录 `identifier-stale.png`。
3. 若另一分析任务尚在运行，等任务结束后再追加一个空格触发新的 idle 周期；本阶段不重试被忙碌检查跳过的周期。

## 改动文件

| 文件 | 摘要 |
| --- | --- |
| src/semantic/semanticanalysisrequest.h、.cpp | 追加 EditIdle、TriviaGateRejected 和门禁字段/名称。 |
| src/semantic/symbolanalyzer.h、symbolanalyzerincremental.cpp | worker 返回明确 disposition，经 dropped 信号转交执行器。 |
| src/analysis/workspacesymbolanalysiscontroller.cpp | 接收丢弃结果并复用原过期清理，避免 Failed。 |
| src/analysis/incrementalsemanticanalysisworker.cpp | 分类后、依赖图前检查门禁及映射兼容性。 |
| src/analysis/incrementalanalysisplanservice.cpp | 门禁已通过的请求不被运行策略扩大为编译。 |
| src/semantic/semanticsourceremap.cpp | 不兼容映射不再产生可发布快照。 |
| src/analysis/analysisscheduler.h、.cpp、analysisschedulerdocuments.cpp | 防抖、生命周期取消、单文件请求与 Dirty 状态保持。 |
| test_sv/analysis_scheduler_test.cpp | 新增 9 类行为测试及策略、取消、action、大文件补充测试。 |
| test_sv/large_file_perf_test.cpp | 更新禁止 idle 请求的三条旧断言，增加单文件/原因/拒绝/状态/Slang/快照约束，保留原性能阈值。 |
| test_sv/large_file_semantic_fixture.h、ts_large_file_semantics_test.cpp | 共享原大文件生成器，原断言不变。 |
| CMakeLists.txt | 两个测试目标显式列出共享 fixture 头。 |
| VERSION、CHANGELOG.md、README.md、用户手册.md、plan.md、goal.md、VERSIONING.md、packaging/ZeroSlack-PACKAGE-README.txt | 同步版本与语义可用性第 1 阶段说明。 |
| docs/edit-idle-review.md | 本验收报告与已知限制。 |
