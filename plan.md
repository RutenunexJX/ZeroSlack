# ZeroSlack Next Major Plan

## Current Status

当前架构主线已经收束到 Route A：

- Tree-sitter：实时语法、高亮、增量文档、live module scope。
- Slang：符号、语义、补全、跳转、scope tree、关系分析事实来源。
- CTest：已覆盖 `ts_doc_test`、`completion_test`、`jump_test`、`relationship_test`。
- 关系分析：实例化、task/function call、assignment、condition/control read、timing sensitivity 已迁移到 Slang AST / semantic model。
- `smartrelationshipbuilder` 不再使用 `QRegularExpression`；clock/reset 只在 Slang timing-control 符号引用基础上做符号名分类。

## Next Major Minimum

下一个大版本至少要做到“可验证、可交付、可继续扩展”，建议定为 GUI / 工作区可靠性版本，而不是继续扩大架构重构范围。

最低完成范围：

- GUI 自动化烟测：
  - 打开工作区。
  - 打开大 SystemVerilog 文件。
  - 连续输入换行和普通字符。
  - 上下移动光标。
  - 触发补全并确认弹窗可用。
  - Ctrl+Click 跳转本文件和跨文件符号。
  - 点击导航窗格跳转。
- 性能基线：
  - 使用 `test_sv/new` 或等价大工程样例。
  - 空白/换行编辑后不触发不必要的 Slang / 关系重分析。
  - 连续编辑、移动光标、补全触发没有可感知卡顿回归。
- 关系分析真实样例验证：
  - 在一个更接近真实工程的小型多文件 SV fixture 上验证 module instantiation、call、assignment、condition read、clock/reset。
  - clock/reset 符号名分类规则保守可解释；不为了猜测覆盖率引入新的文本正则。
- 构建与回归入口稳定：
  - clean build 能生成 `demo` 和 4 个测试目标。
  - `ctest --output-on-failure` 稳定发现并运行 4 个测试。
  - README 中的构建、运行、测试命令仍可执行。

## Suggested Order

1. 先做 GUI 自动化能力选型和最小 smoke test。
2. 固化大文件编辑性能基线，必要时只加针对性探针，不恢复长期 perflog。
3. 增加多文件关系 fixture，覆盖真实工程里最容易误判的 clock/reset 和跨文件跳转。
4. 修正 smoke/perf/fixture 暴露的问题。
5. 最后更新 README、版本号，并提交一个 release-candidate commit。

## Definition Of Done

- 4 个现有 CTest 全绿。
- 新增 GUI smoke test 可在本机 Qt MinGW 环境重复运行。
- README 清楚说明当前架构、测试入口、人工验证记录和下一步。
- 没有恢复 SVLexer、旧 Tree-sitter parser、Tree-sitter 验证按钮或关系分析正则路径。
- 用户能在新会话中只读 `readme.txt` + `plan.md` 就知道下一步该做什么。

## Useful Commands

```powershell
$env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\CMake_64\bin;E:\QT6\Tools\Ninja;$env:PATH"
& "E:\QT6\Tools\CMake_64\bin\cmake.exe" --build "E:\ZeroSlack\ZeroSlack\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug" --target demo relationship_test
& "E:\QT6\Tools\CMake_64\bin\ctest.exe" --output-on-failure
```
