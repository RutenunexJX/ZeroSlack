# ZeroSlack Wave Simulation Preview Plan

## 1. 目标

Wave Preview 的产品定位是：

> 面向正在编写中的、可以完成语法解析与 elaboration 的模块或实例，通过图形化激励自动生成仿真环境，在不手写 testbench 的前提下快速完成短周期功能验证。

该功能必须形成稳定的日常闭环：

```text
选择模块或实例
    -> 配置或复用图形化激励
    -> 运行真实仿真
    -> 查看波形和失败信息
    -> 修改代码或激励
    -> 快速重跑
```

Wave Preview 不是无激励的代码猜测器，不代替 Vivado/XSim，不扩展成完整 UVM 平台，也不在每次键入后自动运行。

## 2. 已确认的技术方向

### 2.1 职责划分

- **ZeroSlack / Slang**：提供 DUT、实例上下文、参数、端口、类型、文件依赖、include、define 和诊断。
- **WaveWorkbench**：提供图形化激励编辑、波形显示、时间轴、Marker、场景持久化及 Expected/Actual 能力。
- **Verilator**：编译并执行 SystemVerilog，产生真实仿真结果。
- **Wellen（后续可选）**：提供大型 VCD/FST/GHW 文件的高效读取和按需访问信号。

### 2.2 当前 Wave Preview 的处理原则

ZeroSlack 当前 Wave Preview 自行实现了词法切分、`assign`/`always` 识别、表达式求值、赋值执行和固定周期推进。这部分只能作为有限的符号代码预览，不能继续扩展成 SystemVerilog 仿真器。

新的真实仿真链路成熟后：

- 停止扩展手写符号执行逻辑。
- 删除或退出 `TraceExpressionParser`、手写 statement execution 和固定周期 trace 生成。
- 如果保留结构化代码视图，将其改名为 `Assignment Flow` 或 `Code Flow`，不得继续称为真实波形。

### 2.3 初期集成方式

早期使用版本化 JSON 和进程边界，暂不直接把 WaveWorkbench 链接进 ZeroSlack：

```text
ZeroSlack
  -> module-manifest.json
  -> WaveWorkbench stimulus.json
  -> Verilator runner
  -> result.vcd
  -> WaveWorkbench TraceCanvas
```

原因：

- 两个仓库可以独立开发、测试和暂停。
- 不产生必须同时修改两个仓库的半完成接口。
- JSON Schema 可以独立回归和版本化。
- 技术链路稳定后，再将 WaveWorkbench 控件提取为共享库。

第一版使用 VCD。Wave Preview 主要面向选中模块和短时间仿真，现有 WaveWorkbench VCD 解析器足以验证产品闭环。FST/Wellen 在出现明确规模需求后单独接入，不自行实现 FST 解析器。

## 3. 开发组织原则

整个功能不建立一个连续执行到结束的长期 Goal。长期状态只记录在本文档中；每个执行侧 Goal 只负责一个切片。

每个切片必须满足：

1. 只引入一个核心能力或一个可观察闭环。
2. 尽量只修改一个仓库。
3. 有独立测试和明确的人工验收行为。
4. 完成后仓库可构建、现有功能可运行。
5. 完成后提交，不留下依赖下一阶段才能编译的代码。
6. 未达到正式开放门槛前，入口保持隐藏或实验状态。
7. 不建立跨阶段长期分支或额外工作树。
8. 不把生成的源码镜像、仿真二进制和波形缓存提交到 Git。

用户可以在任一切片结束后暂停 Wave Preview，转而开发 ZeroSlack 的其他小功能。

## 4. 功能数据契约

### 4.1 Module Manifest

由 ZeroSlack 从 Slang 的当前有效语义快照生成，至少包含：

```json
{
  "schemaVersion": 1,
  "workspaceId": "...",
  "target": {
    "module": "uart_rx",
    "instancePath": "top.uart_rx",
    "sourceFile": "rtl/uart_rx.sv"
  },
  "sources": [],
  "includeDirs": [],
  "defines": {},
  "parameters": {},
  "ports": [],
  "clockCandidates": [],
  "resetCandidates": []
}
```

要求：

- 所有工程路径相对 workspace root。
- 模块定义模式和实例上下文模式必须可区分。
- 端口包含方向、完整类型、位宽、数组维度及 enum/struct 元数据。
- 参数必须使用当前实例的实际生效值。
- Manifest 自身不包含用户激励。

### 4.2 Stimulus Scenario

由 WaveWorkbench 保存，至少包含：

- Manifest identity 和 Schema 版本。
- clock 周期、相位、占空比和初值。
- reset 有效电平、起止时间和同步属性。
- 普通输入的常量、区间、序列或生成器。
- 仿真起止时间或周期数。
- watch list、顺序、分组和 radix。
- 可选的轻量检查。

场景建议保存在：

```text
.zs/simulation/<module-or-instance>/<scenario>.json
```

场景不得依赖绝对路径。仿真结果默认只进入缓存，不随场景提交。

### 4.3 Result Trace

第一阶段使用标准 VCD。WaveWorkbench 内部结果模型必须按真实时间戳保存 transition，不能继续使用“每周期一个字符串”的固定数组。

结果状态至少区分：

- `current`
- `stale-source`
- `stale-stimulus`
- `compiling`
- `running`
- `cancelled`
- `failed`

## 5. 可暂停开发切片

### S0：基线收口

状态：`completed`（2026-08-18）

范围：

- 分别检查 ZeroSlack 和 WaveWorkbench 的 Git 状态。
- 先独立收口 WaveWorkbench 现有未提交改动，不与仿真功能混合。
- 两个仓库完成全量构建和测试基线。
- 建立隐藏开关 `ExperimentalWaveSimulation=false`。
- 确定源码镜像、构建缓存和结果缓存目录及 `.gitignore`。

验收：两个仓库均可独立构建，正式 UI 无行为变化。

S0 实际结果：

- ZeroSlack 基线 `c968f1f8`：既有测试 86/86 通过；新增
  `wave_simulation_configuration_test` 通过。
- WaveWorkbench 基线 `9f0a7ad`：工作树干净，测试 76/76 通过。
- 隐藏开关存储键为
  `experimental/ExperimentalWaveSimulation`，未配置时严格为 `false`；
  本阶段未增加 UI 入口。
- 生成物默认位于应用级
  `QStandardPaths::CacheLocation/wave-simulation/v1/`，分别使用
  `source-mirrors/`、`build-cache/` 和 `results/`。读取配置不创建目录。
- 工作区不保存源码镜像、编译产物或结果波形；仓库 `.gitignore` 另行防御
  显式本地覆盖使用的 `wave-simulation-cache/` 与
  `.wave-simulation-cache/`。
- ZeroSlack 既有用户 RTL 修改和未跟踪产物均未纳入或改写；当前手写
  Wave Preview 保持冻结但未在 S0 删除。

### S1：Module Manifest 契约

状态：`completed`（2026-08-18）

范围：

- 在 ZeroSlack 定义纯数据 Module Manifest。
- 增加 JSON Schema 和版本字段。
- 从 Slang 导出选中模块及实例的真实配置。
- 不接 UI，不调用 Verilator。

验收：真实 fixture 的参数、端口、类型、依赖、include 和 define 与语义快照一致。

S1 实际结果：

- 新增纯 QtCore 数据契约 `WaveSimulationModuleManifest`，Schema 版本固定为
  `1`；对应 JSON Schema 位于
  `schemas/wave-simulation-module-manifest-v1.schema.json`。
- 新增 `WaveSimulationManifestService`，输入仅为不可变
  `SemanticIndexSnapshot`、`ProjectSnapshot`、模块稳定键和可选实例路径；
  不直接访问 UI，也不重新解析 SystemVerilog 文本。
- 模块定义模式使用 Slang 的 `defaultInfo`；实例模式只接受完全匹配的
  `instanceInfoByPath` / `declaredTypeFactsByPath`。实例路径不存在时明确失败，
  不回退到模块默认值。
- Manifest 覆盖编译源及 source role、include dirs、defines、参数实际值、
  端口方向、位宽、packed/unpacked 维度、interface/modport、typedef chain、
  enum value 和 struct member 元数据；enum/struct 成员保持语义记录的源码顺序。
- `workspaceId` 由相对源文件、include、define、扩展名与 top module 的规范化
  内容计算 SHA-256，工作区移动不会因绝对根目录变化而改变；序列化结果不包含
  workspace 绝对路径。
- v1 严格执行“所有工程路径相对 workspace root”：遇到根目录外的 source 或
  include dir 时返回 `project-path-outside-workspace`，不把绝对路径写入契约。
  外部依赖镜像映射留给后续独立 Schema 版本处理。
- 新增真实 Slang fixture，验证 define 驱动的默认参数、两个实例覆盖、参数化端口
  位宽、enum、packed struct、依赖、include 与 JSON 序列化；未知实例和外部路径
  也有失败路径测试。
- ZeroSlack Shared Debug 完整构建通过，全量测试 `88/88` 通过；本阶段未增加 UI，
  未调用 Verilator，旧手写 Wave Preview 仍保持冻结且未修改。

### S2：WaveWorkbench 导入目标

状态：`completed`（2026-08-18）

范围：

- WaveWorkbench 读取 Module Manifest。
- 自动创建 clock、reset、input stimulus 和 output/watch lanes。
- 对唯一 clock/reset 自动建议；有歧义时明确标记。
- 默认输入值可见，不允许隐藏生成激励。

验收：导入后可以直接编辑模块输入波形，不需要手工重建全部信号。

S2 实际结果：

- WaveWorkbench 新增严格的 Module Manifest v1 解析器，逐字段对齐 ZeroSlack
  的版本化 Schema；拒绝未知字段、非法版本、绝对路径、重复身份和无效候选引用。
- 新增实验性命令
  `wave-bridge import-module <module-manifest.json> <output.wave.json>`，将模块或
  实例目标转换为可直接打开、保存和继续编辑的 WaveWorkbench 工程。
- 唯一 clock candidate 创建可见 Clock lane 和显式 ClockDomain；唯一 reset
  candidate 标记对应输入 lane。候选为空或有歧义时不猜测，并输出明确状态与诊断。
- `input`、`inout` 和 `ref` 创建可见 stimulus lane；除 clock 外均以覆盖完整
  duration 的显式零值 Segment 初始化。`output` 创建无驱动 watch lane；`inout`
  和 `ref` 同时保留 stimulus/watch 角色。
- bit、bus 和 enum 映射到原生 Lane；enum 名称与 Slang 生效值进入 enum map。
  声明文本、方向、源码位置、类型 identity、typedef chain、workspace identity 和
  manifest identity 保存在版本化扩展字段中。
- interface、unpacked array 以及无可靠固定 integral 位宽的端口不做隐式扁平化，
  而是跳过并产生明确诊断，留给 S12 的结构化输入切片。
- 新增真实契约 fixture、解析/分类/歧义/非法输入/普通编辑命令测试，以及 CLI
  生成后由 `wave-cli inspect` 重载的冒烟测试。WaveWorkbench 全量测试
  `78/78` 通过；生成工程经 `wave-cli validate` 验证为有效，未驱动 output 的
  undefined warning 属于预期 watch 状态。

本轮切片：S2 WaveWorkbench 导入目标
完成内容：Manifest v1 严格读取、确定性 Project/Scenario/Lane 映射、clock/reset
建议、显式默认输入、watch lane、CLI 导入、保存重载与测试。
明确未做：Stimulus Scenario 独立契约、Verilator、正式 GUI 入口、interface/unpacked
array 编辑和仿真运行。
用户可见行为：仅新增实验性 `wave-bridge import-module`；ZeroSlack 正式 UI 无变化。
自动测试：WaveWorkbench `wave-tests` 55/55；CTest 78/78。
人工验证：fixture 成功生成 `.wave.json`，并由 `wave-cli validate`/`inspect` 重新读取；
得到 6 条 stimulus lane、3 条 watch lane、唯一 clock/reset 和显式 interface warning。
Schema/接口版本：ZeroSlack Module Manifest `v1`，生产者契约未修改。
修改仓库与提交：WaveWorkbench `a454f6b`；ZeroSlack 本文档提交。
已知限制：output 在真实仿真结果导入前保持 undefined；interface、unpacked array 和
非固定 integral 类型暂不创建 lane。
下一最小切片：S3 Stimulus Scenario 契约。
恢复开发所需上下文：从 WaveWorkbench 的 `wave/module_manifest.h` 与
`wave-bridge import-module` 继续；以工程扩展中的 manifest identity 为 S3 场景绑定依据。

### S3：Stimulus Scenario 契约

状态：`completed`（2026-08-18）

范围：

- 定义并保存 `stimulus.json`。
- 覆盖 bit、bus、enum、clock、reset 和 duration。
- 增加 Manifest identity 和端口迁移信息。
- 不接 Verilator。

验收：场景保存、重开和 workspace 移动后保持一致；无绝对路径泄漏。

S3 实际结果：

- WaveWorkbench 新增纯逻辑模块 `wave/stimulus_scenario.h` 与
  `stimulus_scenario.cpp`；模块只依赖 QtCore 和既有 model/Manifest API，
  不执行文件 I/O，不依赖 QtWidgets，也不重新解释 SystemVerilog。
- 定义严格的 Stimulus Scenario `v1`，对应 JSON Schema 位于
  `schemas/stimulus/v1/stimulus-scenario.schema.json`。所有 tick 以十进制字符串
  保存，覆盖 timebase、duration、bit/bus/enum stimulus、clock
  period/phase/duty/edge/initial value、reset 元数据与显式区间，以及 watch
  role、顺序、分组、可见性和 radix。
- 契约绑定 Manifest schema/identity、workspace identity、module/instance target
  及每个端口的方向、宽度、signed、canonical type、declaration shape 和源码顺序；
  内容另有 SHA-256 identity，解析时拒绝未知字段、非法版本、重复身份、显示顺序冲突、
  值/类型冲突、stimulus 区间缺口和内容哈希不一致。
- 新增实验性命令
  `wave-bridge export-stimulus <project.wave.json> <stimulus.json>` 和
  `wave-bridge import-stimulus <module-manifest.json> <stimulus.json>
  <output.wave.json>`；统一 `wave-cli bridge` 转发入口也已实际验证。
- 恢复时先由当前 Module Manifest 生成基础工程，不建立第二套模块事实源。Manifest
  identity 未变化时要求端口契约精确一致；identity 变化时只迁移同 workspace、同 target、
  同名且方向/类型兼容的端口。删除、类型变化、新增端口分别输出 missing、incompatible、
  new 计数，不进行相似名称或位置猜测；enum 成员变化视为类型不兼容。
- 场景文件不保存源码、工程或缓存路径。自动测试将同一个 `stimulus.json` 移动到另一临时
  目录后重开，并验证 identity 不变；恢复工程还经过正常 `.wave.json` 保存/重开以及
  `wave-cli inspect`/`validate`。
- 新增 Schema、安装包内容、核心迁移及四级 CLI 链测试。WaveWorkbench 核心测试
  `56/56`、CTest `82/82` 通过；恢复工程仅有未驱动 output watch lane 的预期
  undefined warning。

本轮切片：S3 Stimulus Scenario 契约
完成内容：严格版本化场景契约、Manifest 绑定、便携保存、确定性端口迁移、CLI
导入导出、安装 Schema、目录移动和保存重开测试。
明确未做：Verilator 探测与运行、正式 GUI 入口、interface/unpacked array、结果波形、
轻量检查规则和多命名场景管理。
用户可见行为：仅新增实验性 CLI；ZeroSlack 和 WaveWorkbench 正式 GUI 均无新增入口。
自动测试：WaveWorkbench `wave-tests` 56/56；CTest 82/82；portable install 包含
Stimulus Scenario v1 Schema。
人工验证：经 `wave-cli bridge` 导出 8 个端口的场景，使用同一 Manifest 恢复后由
`wave-cli validate` 接受；2 个 output watch 的 undefined warning 符合当前阶段预期。
Schema/接口版本：Module Manifest `v1`；Stimulus Scenario `v1`。
修改仓库与提交：WaveWorkbench `f77a367`；ZeroSlack 仅更新本文档。
已知限制：reset active level/synchronization 在当前 Manifest 只有候选信息时保存为
`unspecified`；interface/unpacked array 仍沿用 S2 的显式 deferred 诊断；本阶段不产生
真实仿真结果。
下一最小切片：S4 Verilator 环境探测。
恢复开发所需上下文：从 WaveWorkbench 的 `wave/stimulus_scenario.h`、
`wave-bridge import-stimulus` 和本文档 S4 继续；S4 只建立独立 runner 的工具链探测、
取消、超时和结构化失败，不生成 DUT 模型。

### S4：Verilator 环境探测

状态：`completed`（2026-08-18）

范围：

- 建立独立 runner。
- 探测 Verilator 与 C++ 工具链版本。
- 支持启动、取消、超时、stdout/stderr 和结构化失败。
- 不生成 DUT 模型。

验收：未安装、版本不兼容、运行失败和取消均有确定状态，不阻塞 GUI。

S4 实际结果：

- WaveWorkbench 新增独立 `wavesim` 静态库。`ProcessRunner` 只依赖 QtCore 事件循环，
  异步启动进程并分别捕获 stdout/stderr，支持输出容量上限、硬超时和取消；实现不调用
  `waitForStarted`、`waitForFinished` 或 `processEvents`。
- `ToolchainProbeRunner` 并行探测 Verilator 与 C++ 编译器。发现顺序为显式程序、
  `VERILATOR`/`VERILATOR_ROOT`/`CXX`、请求 PATH、宿主 PATH 兜底；请求环境优先，
  `inheritCurrentProcessPath=false` 可关闭宿主继承。
- 当前兼容基线为 Verilator 5.0、GCC 8、Clang 7、MSVC 19.20。版本无法识别时拒绝继续，
  不按“可能兼容”处理。
- 工具与总体结果分别使用确定状态，区分 `ready`、`not-found`、
  `incompatible-version`、`version-unrecognized`、`execution-failed`、`timed-out` 和
  `cancelled`；保留解析程序路径、参数、退出码、耗时、stdout/stderr 和截断标记。
- 新增实验性 `wave-sim-runner probe`，输出
  `wave-workbench.toolchain-probe/v1` JSON。命令可指定工具路径、超时和自动取消；本阶段
  不读取 Manifest/Stimulus、不生成 harness、不编译或 elaboration DUT。
- 测试通过独立子进程 fixture 覆盖成功、缺失、旧版本、非零退出、超时和取消，并验证
  Qt 事件循环在探测期间继续推进、输出截断、PATH 自动发现和严格环境隔离。
- `wave-tests` 为 `58/58`，全量 CTest 为 `83/83`；便携安装契约确认 runner 与
  `docs/simulation-runner.md` 均被安装。
- 当前开发机实测耗时约 29 ms：G++ 13.1.0 为 `ready`，Verilator 未安装为
  `not-found`，总体为 `unavailable`。该结果符合 S4 对未安装环境的验收要求。

本轮切片：S4 Verilator 环境探测
完成内容：异步通用进程层、双工具链探测、版本兼容判定、结构化报告、实验性 CLI、
六类终态测试及便携安装交付。
明确未做：DUT/harness 生成、Verilator 编译和运行、VCD 产生与读取、正式 GUI 入口。
用户可见行为：新增实验性 `wave-sim-runner probe`；ZeroSlack 与 WaveWorkbench 正式 GUI
均无入口变化。
自动测试：WaveWorkbench `wave-tests` 58/58；CTest 83/83。
人工验证：当前机器探测到 G++ 13.1.0，确定报告 Verilator 缺失，总耗时约 29 ms。
Schema/接口版本：Toolchain Probe Report `v1`。
修改仓库与提交：WaveWorkbench `9bf1e8b`；ZeroSlack 仅更新本文档。
已知限制：当前机器没有真实 Verilator，因此 S4 验证的是缺失路径和确定性进程 fixture；
真实 Verilator 编译链将在 S5 固定 fixture 中形成端到端证据。
下一最小切片：S5 固定 Fixture 端到端。
恢复开发所需上下文：从 WaveWorkbench 的 `wave/simulation_runner.h`、
`wave-sim-runner probe` 和本文档 S5 继续；必须复用现有异步进程状态模型，不在 CLI 或 GUI
另建同步 QProcess 路径。

### S5：固定 Fixture 端到端

状态：`completed`（2026-08-19）

范围：

- 选择一个仓库内的小型固定 DUT。
- 从固定 Manifest 和 Stimulus 生成 harness。
- 调用 Verilator 产生 VCD。
- 用 WaveWorkbench TraceIndex 读取并由 TraceCanvas 显示。

验收：自动测试覆盖完整链路，不依赖用户工程，不暴露正式入口。

S5 实际结果：

- WaveWorkbench 新增独立 `wave/simulation_pipeline.h`，以
  `VerilatorSimulationRunner` 串联契约校验、异步工具链探测、harness 生成、模型构建、
  仿真执行和 VCD 导入。外部进程全部复用 S4 的 `ProcessRunner`，未在 CLI 或 GUI 中增加
  同步等待路径。
- 固定 DUT `wave_fixed_counter`、Module Manifest 和 Stimulus Scenario 均纳入仓库。
  runner 要求两份契约精确恢复一致，并校验 source、include、define、parameter 与端口能力；
  不支持的 interface、inout/ref、unpacked array、未知位宽或 X/Z 输入会明确失败，不静默降级。
- 生成的 C++ harness 按 Stimulus 驱动输入与时钟，使用 Manifest 的顶层、源文件、include、
  define 和 parameter 调用 Verilator，运行后将 VCD 解析为现有 `TraceIndex`。
- 实验性 `wave-sim-runner run-fixture` 输出
  `wave-workbench.simulation-run/v1` JSON，保留终止阶段、诊断、工具链、构建与运行证据、
  harness/model/VCD 路径及导入后的 signal/transition 摘要；支持硬超时和取消。
- 自动测试覆盖成功、构建失败、运行超时、取消、CLI 契约和离屏 `TraceCanvas` 非空渲染；
  CMake 若发现真实 Verilator，会额外注册 `wave-fixed-fixture-real-verilator`。
- 修复 `ProcessRunner` 在 Windows 下解析程序与子进程实际收到的 `Path/PATH` 不一致问题，
  并补齐既有 module-manifest load 测试遗漏的 Qt/MinGW 运行环境。
- `wave-tests` 为 `59/59`，全量 CTest 为 `85/85`；手工确定性链路报告为
  `succeeded`，导入 4 个信号、16 次 transition，TraceCanvas 截图测试通过。

本轮切片：S5 固定 Fixture 端到端
完成内容：固定 DUT 契约、异步仿真流水线、harness、Verilator 构建/运行编排、VCD 导入、
结构化报告、实验性 CLI、TraceCanvas 冒烟及失败终态测试。
明确未做：ZeroSlack 当前选中模块、未保存源码镜像、正式 GUI 入口、复杂端口类型与缓存复用。
用户可见行为：仅新增实验性 `wave-sim-runner run-fixture`；两个正式 GUI 均无入口变化。
自动测试：WaveWorkbench `wave-tests` 59/59；CTest 85/85。
人工验证：确定性独立进程 fixture 完成 build -> run -> VCD -> TraceIndex 全链，得到
4 个信号和 16 次 transition；离屏 TraceCanvas 输出非空波形图。
Schema/接口版本：Simulation Run Report `v1`；Toolchain Probe Report 继续为 `v1`。
修改仓库与提交：WaveWorkbench `9a65170`；ZeroSlack 仅更新本文档。
已知限制：当前机器仍未安装真实 Verilator，因此本机未执行真实编译 CTest；真实测试已按
工具可用性条件注册，确定性 fixture 验证的是同一参数、artifact 和异步终态编排，不冒充
真实 Verilator 编译证据。
下一最小切片：S6 选中模块运行。
恢复开发所需上下文：从 WaveWorkbench 的 `wave/simulation_pipeline.h`、
`wave-sim-runner run-fixture`、ZeroSlack 的 Module Manifest 导出和本文档 S6 继续；正式接入前
必须先建立未保存文档的临时源码镜像，不能直接仿真磁盘旧内容。

### S6：选中模块运行

状态：`completed`（2026-08-19）

范围：

- 使用 ZeroSlack 当前选中的模块或实例替代固定 fixture。
- 使用当前未保存文档的临时源码镜像。
- 使用真实文件依赖、include、define 和参数。
- 初期只支持能够完整 elaboration 的模块。
- 结果先在独立 WaveWorkbench 窗口显示。

验收：选中真实模块后无需手写 TB 即可产生真实波形。

S6 实际结果：

- ZeroSlack 新增 `WaveSimulationPreparationService`。用户主动运行时，它从当前
  `ProjectSnapshot` 和全部已打开 `SharedDocument` 构造一次不可变源码快照；未保存
  缓冲区覆盖磁盘旧内容，其余工程文件从磁盘读取。随后复用 Slang overlay、
  `SemanticIndexSnapshot`、`SmartRelationshipBuilder` 和既有 Manifest Service，未建立
  第二套 SystemVerilog 事实源。
- 目标从当前编辑位置的 `EditorSemanticContext` 获取模块；存在同工作区绑定实例时同时
  传递精确 instance path。重复声明、实例不匹配、语义不完整、非 input/output、
  interface、unpacked array、未知/动态位宽及超过 64 bit 的端口均明确拒绝，不回退到
  猜测或磁盘旧快照。
- 源码镜像保持工作区相对目录结构，并携带真实 source、include、define、parameter 和
  port 契约。每次运行使用唯一 run id，Manifest、Stimulus、构建产物、VCD 与结果工程均
  位于应用缓存，不写入用户工程。
- 新增 `WaveSimulationCoordinator`，通过 QtConcurrent 准备快照，并按顺序异步执行
  `wave-bridge import-module`、`wave-bridge export-stimulus`、
  `wave-sim-runner run-module`；失败、取消和成功均为唯一终态，子进程输出有容量上限。
  成功后以独立进程启动
  `wave-workbench --load-first-trace result.wave.json`。
- WaveWorkbench 的 runner 在 VCD 导入后物化包含相对 trace 引用和自动 signal mapping
  的结果工程。`--load-first-trace` 使用独立结果模式和中央 `TraceCanvas`，不再依赖已退役
  Trace Dock；结果窗口只显示实际可用的 Zoom/Fit 工具，解析完成后才进入 ready。
- ZeroSlack 工具发现支持显式目录、设置项、`WAVEWORKBENCH_HOME`、应用相邻目录和 PATH。
  正式入口继续受 `experimental/ExperimentalWaveSimulation=false` 隐藏开关保护；开启后
  仅在有工作区、活动编辑器且无运行任务时显示可执行状态。
- 自动测试覆盖未保存源码优先、真实 `test_sv/new/elec_phy_import/top/rst_gen.v` 模块
  解析与镜像、异步三进程编排、独立结果进程参数和 runner 失败终态。WaveWorkbench
  全量 CTest `86/86`；ZeroSlack S6 高相关测试 `7/7`。ZeroSlack 全量 CTest 为 `88/89`，
  唯一失败是本切片未修改且不进入 Wave Simulation 调用链的
  `scoped_search_panel_test` 启动即访问冲突，单独复跑结果一致，未通过放宽或跳过处理。

本轮切片：S6 选中模块运行
完成内容：实时源码快照、真实模块/实例 Manifest、异步宿主编排、通用 `run-module`、
结果工程物化及独立 TraceCanvas 结果窗口。
明确未做：WaveWorkbench 内 Run/Stop/Rerun、激励编辑后重跑、构建缓存、正式入口、
interface/unpacked array 和大于 64 bit 端口。
用户可见行为：默认无变化；显式开启实验设置后，Tools 菜单可从当前模块发起运行并在独立
WaveWorkbench 窗口显示结果。
Schema/接口版本：Module Manifest `v1`、Stimulus Scenario `v1`、Simulation Run Report
`v1`，未引入不兼容版本。
修改仓库与提交：WaveWorkbench `006e108`；ZeroSlack 提交见本切片提交记录。
已知限制：当前开发机仍未安装 Verilator，因此真实 `test_sv` 模块已完成 Slang 解析、
契约和源码镜像验证，完整运行链由确定性独立进程 fixture 验证；本机未冒充已经完成真实
Verilator 编译。具备 Verilator 的环境会走同一 `run-module` 流水线。
下一最小切片：S7 图形激励直接运行。
恢复开发所需上下文：从 ZeroSlack 的 `WaveSimulationCoordinator`、WaveWorkbench 的
`wave-sim-runner run-module` 和 `--load-first-trace` 结果模式继续；S7 只增加
Run/Stop/Rerun 与可见运行状态，不提前实现 S8 构建缓存。

### S7：图形激励直接运行

状态：`pending`

范围：

- WaveWorkbench 增加 `Run`、`Stop` 和 `Rerun`。
- 显示 `Ready`、`Compiling`、`Running`、`Current`、`Stale` 和 `Failed`。
- 修改图形激励后直接重新运行。

验收：用户不需要手工操作 JSON、命令行或 VCD 文件。

### S8：构建缓存

状态：`pending`

范围：

- 建立可验证的 build fingerprint。
- 仅激励变化时复用 Verilator 模型。
- RTL、端口、参数、define 或依赖变化时重新编译。
- 加入 generation、取消和旧结果隔离。

验收：只修改激励不会重新编译；旧异步结果不能覆盖新场景。

### S9：场景持久化

状态：`pending`

范围：

- 每个模块至少支持默认场景和命名场景。
- 恢复信号顺序、radix、缩放、Marker 和激励。
- 处理端口新增、删除、重命名和位宽变化。
- 结果缓存与场景配置分离。

验收：修改代码并重新打开工程后，场景仍可继续使用。

### S10：正式接入 ZeroSlack

状态：`pending`

范围：

- 从当前 module、选中 `always` 和 Design instance 打开。
- 选中 `always` 只缩小默认观察范围，底层仍编译完整模块。
- 从编辑器和层级树添加观察信号。
- 编译错误和运行错误可以跳转源码。
- 达到正式开放门槛后启用正式入口。

验收：日常操作不需要先打开 WaveWorkbench 或处理文件。

### S11：共享控件嵌入

状态：`pending`

范围：

- 从 WaveWorkbench 提取 `wavecore`、`wavetrace`、`wavewidgets`。
- 提取 StimulusCanvas、TraceCanvas 和公共时间轴。
- ZeroSlack 以编辑区 Wave Tab 承载完整界面。
- 保留 WaveWorkbench 独立应用。

验收：嵌入前后使用相同场景和结果契约，仿真语义不变化。

### S12：独立增强项

状态：`pending`

以下项目必须继续拆成独立切片，不得合并为一个“大完善阶段”：

- 内部信号层级浏览。
- 多时钟和异步事件。
- struct、array 和 interface 输入编辑。
- Expected/Actual 比较。
- 时刻值、稳定性、边沿响应等轻量检查。
- FST/Wellen 按需读取。
- unresolved module 的显式 stub。
- 模块全部场景批量运行。
- 结果与 driver/source 的双向导航。

## 6. 正式开放门槛

以下条件全部满足前，入口保持实验状态：

1. 选中模块后能够自动获得真实端口、参数和依赖。
2. clock/reset 可以确认和配置。
3. 普通输入可以图形化编辑。
4. 一键运行能够产生并显示真实 VCD。
5. 场景可以保存、恢复并随 workspace 移动。
6. 仅修改激励时不重新编译。
7. 编译或运行失败能够显示原因并跳转源码。
8. 旧结果具有明确的 stale 标记。
9. 未保存缓冲区不会被忽略或与磁盘版本混用。
10. 取消和新 generation 不会被旧结果覆盖。

## 7. 日常交互目标

### 7.1 第一次运行

```text
光标位于模块或实例
  -> 打开 Wave Preview
  -> 确认自动识别的 clock/reset
  -> 默认输入明确显示为 0 或 X
  -> 编辑必要激励
  -> Run
```

无歧义的简单模块应只需要一次确认和一次运行。

### 7.2 重复运行

- 修改激励：复用模型并重新运行。
- 修改 RTL：保留旧波形但灰显为 stale，用户主动重新编译运行。
- 重新打开工程：恢复上次场景。
- 端口变化：尽可能迁移已有激励，无法迁移时列出具体冲突。

### 7.3 编辑器联动

- 从代码双击或上下文动作将信号加入当前场景。
- 从层级树拖入内部信号。
- 双击波形信号跳到定义。
- 编译诊断、unsupported construct 和运行错误跳回源码。

## 8. 测试层次

每个切片根据所涉及层次补齐测试：

1. **Schema 测试**：版本、必填字段、前后兼容和非法输入。
2. **Slang fixture 测试**：端口、参数、实例上下文、include 和 define。
3. **WaveWorkbench model 测试**：激励编辑、保存、迁移和 Undo/Redo。
4. **Runner 测试**：启动、取消、超时、日志、路径和 generation。
5. **端到端测试**：固定 DUT 的预期 transition 和 VCD 读取。
6. **缓存测试**：输入变化复用模型，源码变化强制重建。
7. **GUI smoke 测试**：首次运行、重跑、失败、stale 和恢复。
8. **真实工程抽查**：`test_sv/new` 和 `test_sv/huge_prj` 中的代表性模块。

不得以降低测试阈值、静默 fallback、延时刷新或只测试单一 fixture 的方式通过验收。

## 9. 每个切片的交接记录

每次执行侧完成当前切片后，必须在本文档相应章节更新状态，并附加以下记录：

```text
本轮切片：
完成内容：
明确未做：
用户可见行为：
自动测试：
人工验证：
Schema/接口版本：
修改仓库与提交：
已知限制：
下一最小切片：
恢复开发所需上下文：
```

控制侧独立验收后才能提交或 push。ZeroSlack GUI 行为发生变化时同步更新应用版本；纯内部契约和隐藏实验能力不单独打正式安装包。

## 10. 暂停纪律

任一切片结束时必须满足：

- 工作树不存在属于该切片的未提交代码。
- 全量或明确约定的测试集通过。
- 生成文件和缓存均可删除并重新生成。
- Schema 和接口版本已记录。
- 正式 UI 不依赖未完成的下一切片。
- 本文档明确指出下一步唯一的最小工作项。

因此 Wave Simulation 可以在任意切片边界暂停，不阻碍 ZeroSlack 的 formatter、编辑器交互、导航或其他日常小功能继续迭代。
