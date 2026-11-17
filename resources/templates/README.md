# 用户模板资源

## `/` 普通模板与 Block 预设

`editor.json` 是内置编辑资源。`Tools > Templates > Manage Templates…` 复用同一管理入口，可创建、编辑、删除、导入和导出 `/name` 资源，并选择全局或工作区保存。导入先作为草稿显示，保存前校验全部依赖；不应使用样例覆盖已有资源文件。

普通记录使用 `editor` 对象，示例：

```json
{
  "id": "example:label",
  "command": "/label",
  "label": "Named signal",
  "body": "logic ${name};${cursor}",
  "editor": {
    "kind": "snippet",
    "context": "codeLine",
    "extensions": ["sv", "svh"],
    "aliases": ["declaration"],
    "inputs": {"name": {"kind": "Text", "default": "ready"}}
  }
}
```

`context` 支持 `any`、`code`、`codeLine`、`module`；位置限制只在确认模板时检查，不限制 `/` 搜索。`Text` 接受普通文字；数值约束使用 `Value`。`Module`、`Instance`、`Port`、`Internal`、`Signal`、`Parameter` 复用现有语义服务，事实不可用时显示原因。

`${include:id}` 或 `${include:/command}` 在插入前展开依赖，`${cursor}` 指定最终光标。同名输入必须具有一致约束并在填参时联动。组合限制为 16 层、128 个节点、1 MiB；缺失依赖、循环或无效输入拒绝保存与插入。资源声明 `acceptsBody: true` 后，`${body}` 接收选区。完整有效资源才会替换同一作用域的缓存；切换工作区不会复用上一工作区目录，正在填写的实例保留原定义。

Block 预设使用 `editor: {"kind":"block","blockType":"text","text":"Note"}`；类型为 `text`、`image` 或 `reference`，可配置 `caption`、`width`、`height`。图片确认导入后才创建对象，引用接受现有对象 URI。增加这些资源无需编译；新增渲染器或语义来源仍需代码。

普通模板生成的正文写入源码，辅助输入在 Esc、最后一次 Tab 或离开输入范围后结束。关联 Block 不写源码字符，其数据位于 `.zeroslack/editor-blocks/<源码相对路径>.json`，图片位于同目录的 `assets/`。共享工程时应一并保留此目录。只改 Block 时保存不改写源码字节或修改时间。

## 已有仿真资源

`tb-dut.json` 包含 `@clk_init`、`@reset`；`examples/` 中每个文件只包含一份可编辑定义。两份样例由现有 UserTemplateService、共享模板规划器和模板元数据处理，不按指令名称分派 C++ 处理器。

## 随包模板

`catalog.json` 是唯一随包自动加载目录，格式为 `{"schemaVersion": 1, "templates": [...]}`。正式包携带 `templates/catalog.json`，GUI 与 CLI 都从各自可执行文件所在的安装目录读取，不依赖当前工作目录，也不需要导入。目录启用 `/timescale`、`/signals` 和 `/parameters`，稳定 ID 分别为 `user:timescale`、`user:signals`、`user:parameters`。Timescale 正文精确为 `` `timescale 1ns / 1ps ``。其他资源文件仍是手动导入的样例，不会被自动扫描或恢复为内置模板。

`/signals`、`/parameters` 在模块内空行使用，共用模块搜索、参数配置和预览浮层。候选同时显示来源文件及行号；选择单一搜索结果也不会自动插入。可编辑非 localparam 参数值或类型，并设置生成名称的前后缀。`/signals` 保留端口数据类型、符号宽度和数组维度，去除方向，并默认生成依赖的 localparam；inout 保留网络连接语义。取消勾选 Include required parameters 时使用当前位置已有的参数，不修改其值。`/parameters` 可选择参数、选择 localparam 或 parameter 输出，依赖项按顺序补入，原 localparam 保持派生且只读。

预览使用 Slang 源语法生成，名字冲突、参数循环、无效表达式和目标作用域中不可用的类型会阻止插入。接口或组合表达式端口不能直接转换为普通内部信号；仍可使用该模块的 `/parameters`。Insert 一次写入，支持一次撤销与重做。Esc、Cancel、源文件／光标／模板变化不会写入预览。Ctrl+Space 行为保持。

这两项的资源配置为 `editor: {"kind":"moduleDeclarations","mode":"signals"或"parameters","context":"module"}`，无固定 body；模板管理器可修改命令、可见性及覆盖，生成规则共用同一核心服务，不能作为 `${include:...}` 片段嵌套。

CLI 的 `document.insert` 使用相同生成器，指定 `templateId`、`module`，重名时补 `moduleFile` 和 `moduleLine`。`declarations` 接收 `prefix`、`suffix`、`keyword`（localparam／parameter）、`includeParameters`、`selectedParameters` 数组及 `values`（参数名到 SV 表达式字符串）。省略 selectedParameters 时选择可覆盖参数。原 expectedRevision、dryRun 和显式 sources 契约保持，生成结果是普通源码，不创建模板实例元数据。

优先级为工作区 > 用户全局 > 随包；已有的文件限定命令仍优先作用于对应文件。稳定 ID 或同一作用域的命令冲突会在有效目录中去重，原用户数据不删除。编辑随包条目时，选择保存作用域后 Save 生成覆盖；包内 JSON 保持只读，升级不覆盖个人正文、名称或输入配置。

`Tools > Templates > Manage Templates…` 标注 Bundled 及覆盖来源。Hide／Show 在所选作用域保存可见状态；Reset override 显式删除该作用域的定义及可见性覆盖，恢复继承的内容。用户自建条目仍使用 Delete。纯可见状态以带 `bundledId` 和 `bundledStateOnly: true` 的轻量记录存入原有用户／工作区 JSON；不会复制默认正文。条目从包中暂时移除时状态仍保留，重新加入后继续生效。文件作用域仅影响对应文件，工作区状态不带到其他工作区。

目录缺失、JSON 损坏、版本不支持或重复身份／命令均报告错误；每层完整校验后才发布，失败保留该层最近一次有效数据，已有效的其他配置仍可使用。重启后无法从磁盘重建损坏层的旧内容，错误仍明确报告；不会把失败结果写回个人配置。显式 Reload 可重新读取修正后的目录，旧编辑会拒绝覆盖外部更新。

## 合并与重新加载

开发验证仅使用隔离 profile，实际用户配置安装由统筹验收后进行。不要用整份样例覆盖现有 `user_templates.json`。

```powershell
python merge_tb_templates.py --existing <现有user_templates.json> --templates tb-dut.json --output <新文件.json>
```

输出必须是新路径，同时生成 `.merge-report.json`。已有 ID、指令或别名冲突时保留原条目并报告，不静默覆盖；其他配置及手动角色保持。省略 `--existing` 可准备独立目录。`--templates examples/clk_init.json` 可只合并一项。审核结果后备份原配置，再用合并结果更新对应用户或工作区目录。UserTemplateService 检测文件版本变化，下一次打开管理窗口即加载；已打开管理窗口需关闭重开，旧编辑会拒绝覆盖外部更新。

已有同名用户 `@reset` 不会自动替换。新插入样例的语义是时间保持，旧保存实例仍采用其保存定义，内置按周期 reset 也不被改写。

## 编辑定义

通过 `Tools > Templates > Manage Templates…` 可修改指令、正文及输入项；Code 页的 `${name}` 表示输入槽，保存时自动计算位置。高级表达式及约束保存在 JSON 的 `simulation` 对象内，可直接编辑文件后重新加载；不需要重新编译或打包。直接编辑原始 `body` 时须同步 `slots[].start/length`（UTF-16 位置），不要用字节偏移。

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

Ctrl+Space 已移除模板分类、候选及插入入口。已有模板实例的输入提示与未完成原因仍显示在标题栏，悬停可查看全文。文档保持可编辑；修改模板固定文本会使受影响的模板身份失效，源代码保留。

普通 Code 容器 `@definitions`、`@process`、`@repeat` 保留手写代码、嵌套模板和输入槽。Tab/Shift+Tab 切换输入，Ctrl+Enter 结束当前输入；候选的上下选择、撤销重做和保存重开保持有效。

`slots[].input` 沿用现有输入模型。Value 支持十进制小数、`finite`、`minimum`、`exclusiveMinimum`、`maximum`；`displayUnit` 显示单位。Signal 使用可见/可驱动范围及类型过滤；Clock/Reset 能力输入必须同时为封闭、可写、单比特模块变量，↑/↓ 选候选，Tab/Shift+Tab 切换输入。键入、粘贴、IME 不能绕过。没有可靠候选时给出原因。

时钟默认 100 MHz，初值 0、50% 占空比，半周期 `500/frequency` ns。复位默认保持 200 ns，极性 0 为低有效、1 为高有效。频率/时长可输入 156.25、12.5。数值输入不启用方向键步进。时钟在实际量化延时为零时于循环前报错；复位零时长先在 t=0 赋有效电平，随后在同一仿真时刻的 inactive 区释放，不保证所有监视器观察到初始 delta 事件。

自动角色来自 Slang 展开的过程、端口连接、参数和 generate 分支。支持明确的双边沿异步复位和完整初始化至少两个状态对象的简单同步复位；单对象清零、复杂控制、门控表达式、角色冲突或缺失依赖保持未确定。候选提示包含边沿/极性、源实例/位置、映射、参数和分析修订。手动标注单列来源，不能掩盖自动冲突。

