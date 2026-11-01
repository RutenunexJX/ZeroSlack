# 仿真用户模板

`tb-dut.json` 包含 `@clk_init`、`@reset`；`examples/` 中每个文件只包含一份可编辑定义。两份样例通过现有 UserTemplateService、编辑页模板搜索和模板元数据运行，不按指令名称分派 C++ 处理器。

## 合并与重新加载

开发验证仅使用隔离 profile，实际用户配置安装由统筹验收后进行。不要用整份样例覆盖现有 `user_templates.json`。

```powershell
python merge_tb_templates.py --existing <现有user_templates.json> --templates tb-dut.json --output <新文件.json>
```

输出必须是新路径，同时生成 `.merge-report.json`。已有 ID、指令或别名冲突时保留原条目并报告，不静默覆盖；其他配置及手动角色保持。省略 `--existing` 可准备独立目录。`--templates examples/clk_init.json` 可只合并一项。审核结果后备份原配置，再用合并结果更新对应用户或工作区目录。UserTemplateService 检测文件版本变化，下一次模板搜索/打开管理窗口即加载；已打开管理窗口需关闭重开，旧编辑会拒绝覆盖外部更新。

已有同名用户 `@reset` 不会自动替换。新插入样例的语义是时间保持，旧保存实例仍采用其保存定义，内置按周期 reset 也不被改写。

## 编辑定义

通过现有 Templates 页可修改指令、正文及输入项；Code 页的 `${name}` 表示输入槽，保存时自动计算位置。高级表达式及约束保存在 JSON 的 `simulation` 对象内，可直接编辑文件后重新加载；不需要重新编译或打包。直接编辑原始 `body` 时须同步 `slots[].start/length`（UTF-16 位置），不要用字节偏移。

保留旧实例时，应为新模板使用新 ID/指令；同一模板的新定义只影响之后插入的实例。已保存实例包含固定代码、输入约束和能力快照，不从最新目录重新解释。修改实例的输入仍采用该实例快照，关联的同名物理槽一并更新。

## 能力版本 1

| 字段 | 作用与限制 |
| --- | --- |
| `capabilityVersion` | 数值 `1`；其他版本、字符串和布尔值拒绝 |
| `capability` | `moduleProcess` |
| `kind` | `snippet` |
| `placement` / `content` | 模块级过程及父容器兼容性，按当前语法位置检查 |
| `render` | 受限表达式定义与变量绑定，生成固定 SV 表达式；正文中用 `{{=expression_name}}` 引用 |

`render.bindings` 将逻辑名字映射到正文声明的 SV 变量。`render.expressions` 支持十进制有限数值 `{number:"500.0"}`、`{variable:"frequency"}`、`{expression:"other"}`、`{unit:"ns",value:...}`，以及 `{op:"/",args:[...]}`。运算为 `+ - * / > >= < <= == != && || !` 和三元 `if`；时间单位为 s/ms/us/ns/ps/fs。循环引用、未知变量、未知节点、非法类型、过深表达式以及派生片段与可编辑槽重叠均拒绝。每个展开限制 256 节点、32 层嵌套。

派生表达式生成 SV，随槽所赋变量在仿真时求值；不运行宿主脚本，不访问文件、网络或命令。条件/循环过程由模板正文表达。当前不提供任意集合查询、自由组合操作图或宿主求值语言。新增语义算法仍需修改核心。

## 输入与时序

按 Ctrl+Space 打开统一面板并选择 Templates；输入指令只筛选候选，Enter 确认插入，Esc 取消搜索。普通输入提示与未完成原因显示在标题栏，悬停可查看全文。文档保持可编辑；修改模板固定文本会使受影响的模板身份失效，源代码保留。

普通 Code 容器 `@definitions`、`@process`、`@repeat` 保留手写代码、嵌套模板和输入槽。Tab/Shift+Tab 切换输入，Ctrl+Enter 结束当前输入；候选的上下选择、撤销重做和保存重开保持有效。

`slots[].input` 沿用现有输入模型。Value 支持十进制小数、`finite`、`minimum`、`exclusiveMinimum`、`maximum`；`displayUnit` 显示单位。Signal 使用可见/可驱动范围及类型过滤；Clock/Reset 能力输入必须同时为封闭、可写、单比特模块变量，↑/↓ 选候选，Tab/Shift+Tab 切换输入。键入、粘贴、IME 不能绕过。没有可靠候选时给出原因。

时钟默认 100 MHz，初值 0、50% 占空比，半周期 `500/frequency` ns。复位默认保持 200 ns，极性 0 为低有效、1 为高有效。频率/时长可输入 156.25、12.5。数值输入不启用方向键步进。时钟在实际量化延时为零时于循环前报错；复位零时长先在 t=0 赋有效电平，随后在同一仿真时刻的 inactive 区释放，不保证所有监视器观察到初始 delta 事件。

自动角色来自 Slang 展开的过程、端口连接、参数和 generate 分支。支持明确的双边沿异步复位和完整初始化至少两个状态对象的简单同步复位；单对象清零、复杂控制、门控表达式、角色冲突或缺失依赖保持未确定。候选提示包含边沿/极性、源实例/位置、映射、参数和分析修订。手动标注单列来源，不能掩盖自动冲突。

