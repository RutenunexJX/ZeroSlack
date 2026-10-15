# TB 用户模板

`tb-dut.json` 包含 `@tb`、`@dut`、`@clk_init`、`@reset`；`examples/` 中每个文件只包含一份可编辑定义。四者通过现有 UserTemplateService、编辑页模板搜索和模板元数据运行，不按指令名称分派 C++ 处理器。

## 合并与重新加载

开发验证仅使用隔离 profile，实际用户配置安装由统筹验收后进行。不要用整份样例覆盖现有 `user_templates.json`。

```powershell
python merge_tb_templates.py --existing <现有user_templates.json> --templates tb-dut.json --output <新文件.json>
```

输出必须是新路径，同时生成 `.merge-report.json`。已有 ID、指令或别名冲突时保留原条目并报告，不静默覆盖；其他配置及手动角色保持。省略 `--existing` 可准备独立目录。`--templates examples/clk_init.json` 可只合并一项。审核结果后备份原配置，再用合并结果更新对应用户或工作区目录。UserTemplateService 检测文件版本变化，下一次模板搜索/打开管理窗口即加载；已打开管理窗口需关闭重开，旧编辑会拒绝覆盖外部更新。

已有同名用户 `@reset` 不会自动替换。新插入样例的语义是时间保持，旧保存实例仍采用其保存定义，内置按周期 reset 也不被改写。

## 编辑定义

通过现有 Templates 页可修改指令、正文及输入项；Code 页的 `${name}` 表示输入槽，保存时自动计算位置。高级区域、表达式及结构操作保存在 JSON 的 `simulation` 对象内，可直接编辑文件后重新加载；不需要重新编译或打包。直接编辑原始 `body` 时须同步 `slots[].start/length`（UTF-16 位置），不要用字节偏移。

保留旧实例时，应为新模板使用新 ID/指令；同一模板的新定义只影响之后插入的实例。已保存实例包含固定代码、输入约束和能力快照，不从最新目录重新解释。修改实例的输入仍采用该实例快照，关联的同名物理槽一并更新。

## 能力版本 1

| 字段 | 作用与限制 |
| --- | --- |
| `capabilityVersion` | 数值 `1`；其他版本、字符串和布尔值拒绝 |
| `capability` | `moduleRoot`、`moduleInstance` 或 `moduleProcess` |
| `kind` | 根/过程使用 `snippet`，模块结构操作使用 `operations` |
| `placement` / `content` | 文件根、模块级过程、实例区域等，仍受结构解析和父级区域约束 |
| `regions` / `nameSlot` | 根模板定义声明、实例、行为三类 Code 槽的实际名称，以及模块名槽；每类恰好一个，按声明→实例→行为排列 |
| `activation` | 兼容读取旧根模板的字段，不再触发自动插入；所有入口均须 Enter 确认候选 |
| `semanticInputs` | 模块来自 `compilation`，参数来自 `selectedModule`，端口来自 `elaboratedPorts`；可用 `namePrefix` 缩小模块集合 |
| `operations` | 有序的 `declareSignals`、`instantiateModule`，目标必须对应当前根的声明区/实例区；作为同一个撤销事务提交 |
| `declareSignals` | `target`、`reuse`（`explicit` 或 `never`）、`namePattern`、`label`；命名可用 `${instance}`、`${port}`、`${module}`，冲突用稳定数字后缀 |
| `instantiateModule` | `target` 与 `bindings: named`；共享生成器按 Slang 的实际参数化端口重复生成声明/绑定，不由模板拼接复杂类型 |
| `render` | 受限表达式定义与变量绑定，生成固定 SV 表达式；正文中用 `{{=expression_name}}` 引用 |

`render.bindings` 将逻辑名字映射到正文声明的 SV 变量。`render.expressions` 支持十进制有限数值 `{number:"500.0"}`、`{variable:"frequency"}`、`{expression:"other"}`、`{unit:"ns",value:...}`，以及 `{op:"/",args:[...]}`。运算为 `+ - * / > >= < <= == != && || !` 和三元 `if`；时间单位为 s/ms/us/ns/ps/fs。循环引用、未知变量、未知节点、非法类型、过深表达式以及派生片段与可编辑槽重叠均拒绝。每个展开限制 256 节点、32 层嵌套。

派生表达式生成 SV，随槽所赋变量在仿真时求值；不运行宿主脚本，不访问文件、网络或命令。条件/循环过程由模板正文表达；端口集合的重复生成由受限结构操作执行。当前不提供任意集合查询、自由组合操作图或宿主求值语言。确实新增语义算法或新的结构操作仍需修改核心。

## 输入与时序

F24 按住时显示临时触发提示，F24+Space 切换锁定。输入指令只筛选候选，Enter 才插入，Esc 取消。锁定标识和当前层级显示在编辑区顶部。

根模板的 Code 槽显示为区域按钮与源码边界。单击按钮进入该区域的手写段；空区也保留入口，固定文本及子模板仍受保护。键盘导航时 ↑/↓ 只选择同级对象，Enter 逐层进入模板、区域和手写入口，Ctrl+Enter 逐层返回。具有输入与子项的模板提供 Inputs 项；输入编辑期间保留 Tab/Shift+Tab 切换与已有候选上下选择。Esc 或 F24+Space 退出锁定。

`slots[].input` 沿用现有输入模型。Value 支持十进制小数、`finite`、`minimum`、`exclusiveMinimum`、`maximum`；`displayUnit` 显示单位。Signal 使用可见/可驱动范围及类型过滤；Clock/Reset 能力输入必须同时为封闭、可写、单比特模块变量，↑/↓ 选候选，Tab/Shift+Tab 切换输入。键入、粘贴、IME 不能绕过。没有可靠候选时给出原因。

时钟默认 100 MHz，初值 0、50% 占空比，半周期 `500/frequency` ns。复位默认保持 200 ns，极性 0 为低有效、1 为高有效。频率/时长可输入 156.25、12.5。数值输入不启用方向键步进。时钟在实际量化延时为零时于循环前报错；复位零时长先在 t=0 赋有效电平，随后在同一仿真时刻的 inactive 区释放，不保证所有监视器观察到初始 delta 事件。

自动角色来自 Slang 展开的过程、端口连接、参数和 generate 分支。支持明确的双边沿异步复位和完整初始化至少两个状态对象的简单同步复位；单对象清零、复杂控制、门控表达式、角色冲突或缺失依赖保持未确定。候选提示包含边沿/极性、源实例/位置、映射、参数和分析修订。手动标注单列来源，不能掩盖自动冲突。

模块生成支持已解析的整型常量参数及可靠可表示的端口类型。未确定的类型参数、接口端口及同时编译的重复模块定义拒绝写入。删除或改绑 DUT 保留根拥有的声明，避免误删共享及用户信号；可能留下不再使用的声明，由用户明确处理。
