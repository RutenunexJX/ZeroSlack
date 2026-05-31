# CTest 回归测试接入计划

## Summary

优先完成 README 里的 P0：把 `completion_test.cpp`、`jump_test.cpp`、`ts_doc_test.cpp` 接入 CMake/CTest，形成补全、跳转、Tree-sitter 增量文档的自动回归网。

README 继续做“当前状态 + 架构交接”，本文件记录下一步执行计划，避免 README 越写越像任务系统。

## Key Changes

- 在 `CMakeLists.txt` 中开启 CTest。
- 把应用公共源码拆成内部库目标 `zeroslack_core`，`demo` 只保留 `main.cpp` 并链接该库。
- 新增三个测试目标：`ts_doc_test`、`completion_test`、`jump_test`。
- `completion_test` 和 `jump_test` 使用 Qt offscreen 环境运行。
- 保持架构边界不变：Tree-sitter 负责实时语法层和 live scope，Slang 负责符号、语义、补全和跳转数据。

## Test Plan

- 使用现有 CMake + Ninja + Qt 6 MinGW Debug 构建目录。
- 构建目标包括 `demo` 和三个测试目标。
- `ctest --output-on-failure` 应发现并运行 3 个测试。
- `ts_doc_test` 验证 UTF-16 偏移、高亮 span、增量 edit 与 full parse 一致、live module scope。
- `completion_test` 验证 Slang 符号提取后的 struct member、inline struct、模糊匹配、function-local 不泄漏。
- `jump_test` 验证 module scope 下的可跳转判断、本文件跳转落点、跨文件跳转信号。

## Assumptions

- 不引入新的测试框架，沿用现有测试文件里的 `printf` + 返回码断言风格。
- 不做 GUI 自动化；本轮只接入现有无头/近无头测试。
- 不删除 `sv_treesitter_parser`、Tree-sitter 验证按钮或 `sym_list` 遗留 API，这些留到 P1。
