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

状态：`completed (2026-08-19)`

范围：

- WaveWorkbench 增加 `Run`、`Stop` 和 `Rerun`。
- 显示 `Ready`、`Compiling`、`Running`、`Current`、`Stale` 和 `Failed`。
- 修改图形激励后直接重新运行。

验收：用户不需要手工操作 JSON、命令行或 VCD 文件。

实际结果：

- WaveWorkbench 结果工作区改为上下分栏：上方 `Stimulus` 可直接编辑图形激励，下方
  `Actual` 显示最近一次有效结果；工具栏提供 `Run`、`Stop` 和 `Rerun`。
- 新增严格的 `wave-workbench.simulation-session/v1` 私有会话契约，结果工程保存重跑所需
  路径和显式工具参数，但不持久化进程环境；加载时重新获取当前环境。
- 新增独立状态机和 `Ready`、`Compiling`、`Running`、`Current`、`Stale`、`Failed`
  六种可见状态。图形激励修改后立即标记 `Stale`，运行阶段由 runner 实时回调驱动。
- Run/Rerun 将当前内存场景直接写入 Stimulus 契约并在进程内启动既有异步流水线；成功后
  直接消费返回的 trace、刷新 `Actual` 并持久化结果，不要求用户处理 JSON、命令行或 VCD。
- Stop 直接取消 `VerilatorSimulationRunner`。取消和失败均保留上一份有效结果；运行期间
  禁止继续编辑，避免产生未定义的并发场景状态。
- 自动测试覆盖会话 Schema、环境不持久化、状态转换、实时 stage 顺序、修改后重跑、取消、
  失败恢复、结果刷新和持久化。WaveWorkbench 核心测试 `60/60`，全量 CTest `88/88`。

本轮切片：S7 图形激励直接运行。
完成内容：可编辑 Stimulus/Actual 双画布、Run/Stop/Rerun、六态状态机、可恢复会话契约、
实时运行阶段和结果原位刷新。
明确未做：build fingerprint、Verilator 模型复用、命名场景持久化、ZeroSlack 正式入口，
以及 interface、unpacked array 和大于 64 bit 端口支持。
用户可见行为：仅通过 S6 隐藏实验入口打开结果工作区后可见；ZeroSlack 默认正式界面不变。
自动测试：WaveWorkbench 核心测试 `60/60`；全量 CTest `88/88`。
人工验证：检查 `simulation-result-window-smoke.png`，双画布、运行操作和状态提示均无重叠。
Schema/接口版本：新增 Simulation Session `v1`；Module Manifest、Stimulus Scenario 和
Simulation Run Report 继续为 `v1`。
修改仓库与提交：WaveWorkbench `f60febb`；ZeroSlack 为本计划文档提交。
已知限制：当前开发机未安装 Verilator，完整运行闭环由确定性独立进程 fixture 验证，未将
fixture 结果冒充真实 Verilator 编译结果；S7 每次运行仍重新构建模型。
下一最小切片：S8 构建缓存。
恢复开发所需上下文：从 `SimulationSessionStateMachine`、
`VerilatorSimulationRunner::StageChanged` 和结果工作区的进程内 Run/Rerun 继续；S8 只增加
可验证的构建指纹、模型复用、generation/取消隔离，不提前实现 S9 场景持久化。

### S8：构建缓存

状态：`completed (2026-08-19)`

范围：

- 建立可验证的 build fingerprint。
- 仅激励变化时复用 Verilator 模型。
- RTL、端口、参数、define 或依赖变化时重新编译。
- 加入 generation、取消和旧结果隔离。

验收：只修改激励不会重新编译；旧异步结果不能覆盖新场景。

实际结果：

- WaveWorkbench 新增纯数据构建指纹与严格的
  `wave-workbench.simulation-build-cache/v1` 记录。指纹覆盖 Manifest、全部 design/header
  源码、稳定 runtime harness、工具路径/版本/参数和编译环境；Stimulus 不进入指纹。
- harness 改为运行时读取每次运行私有生成的 stimulus plan。仅修改激励时直接复用已验证
  Verilator 模型；端口、parameter、define、RTL、header 依赖、harness 或工具链变化均失效。
- 缓存条目先在唯一 staging 目录构建，写入 evidence 与可执行文件 SHA-256 后再发布；命中时
  重新校验二者，损坏缓存自动重建，取消构建会清理 staging，不发布半成品。
- `SimulationRunReport` 增加 generation 与 build-cache 证据，stage 增加
  `ResolveBuildCache`。GUI 只消费当前 generation 的阶段/完成回调。
- 结果工程旁以 `QLockFile` 和 generation 声明隔离并发运行。较旧任务无论晚完成还是晚启动，
  都以 `superseded` 终止，不能覆盖较新结果；自动 generation 使用跨进程时间基编号。
- ZeroSlack 运行协调器将既有 workspace 级 `cachePaths.buildCache` 显式传给 runner，不再为每次
  结果创建互不复用的模型缓存。
- 自动测试覆盖 fingerprint 确定性、端口/parameter/define/RTL/header/toolchain 失效、仅激励
  复用、损坏缓存修复、构建中取消、CLI 二次命中、旧 generation 晚完成及晚启动隔离。

本轮切片：S8 构建缓存。
完成内容：稳定 runtime harness、内容寻址模型缓存、严格缓存校验、generation/取消隔离和
ZeroSlack 共享缓存接线。
明确未做：命名场景持久化、端口变化迁移、正式 ZeroSlack 入口、共享控件嵌入及更复杂端口类型。
用户可见行为：隐藏实验结果工作区内，仅修改激励后 Run/Rerun 跳过编译；状态栏明确显示
`cached model` 或 `model built`。
自动测试：WaveWorkbench 核心测试 `61/61`；全量 CTest `88/88`；ZeroSlack
`wave_simulation_runtime_test` 及全量 CTest `89/89` 通过。
人工验证：当前开发机未安装真实 Verilator，本切片使用独立确定性进程 fixture 验证构建次数、
缓存损坏、取消和并发结果；未将 fixture 结果冒充真实 Verilator 编译结果。
Schema/接口版本：新增 Simulation Build Cache `v1`；Simulation Run Report、Simulation Session、
Module Manifest 和 Stimulus Scenario 继续为 `v1`，仅向后兼容增加字段。
修改仓库与提交：WaveWorkbench `3145885`；ZeroSlack 本提交。
已知限制：每次运行仍会执行轻量工具链版本探测；缓存不跨不一致 PATH/编译环境复用。
下一最小切片：S10 正式接入 ZeroSlack。
恢复开发所需上下文：从 `.zs/simulation/<target>/default.json`、命名场景目录、
`waveSimulation.session` 和 `SimulationSessionStateMachine` 继续；S10 处理日常入口、
观察范围和源码错误导航，不提前执行 S11 的共享控件嵌入。

### S9：场景持久化

状态：`completed`（2026-08-19）

范围：

- 每个模块至少支持默认场景和命名场景。
- 恢复信号顺序、radix、缩放、Marker 和激励。
- 处理端口新增、删除、重命名和位宽变化。
- 结果缓存与场景配置分离。

验收：修改代码并重新打开工程后，场景仍可继续使用。

S9 实际结果：

- Stimulus Scenario 升级为向后兼容的 v2；v1 文件仍可读取。v2 保存 Marker、当前端口、
  游标 tick 和可见时间跨度，信号顺序、分组、radix、可见性与激励继续使用显式契约。
- 新增原子场景目录：默认场景固定为 `default.json`，命名场景使用稳定 scenario ID
  的 SHA-256 文件名；结果 VCD、result project 和编译模型仍只进入缓存。
- 端口迁移先做精确名称匹配；名称缺失时仅接受唯一结构匹配，必要时以 source order
  消歧。位宽变化保留显示设置并重置不安全激励，删除、新增、重命名和位宽变化均有
  独立计数与诊断。
- WaveWorkbench 结果窗口在打开时恢复默认和命名场景，并提供新建、重命名、删除命名
  场景操作；默认场景不可删除，场景视图不再依赖绝对工程路径的 QSettings 键。
- ZeroSlack 为 module/instance 生成稳定的
  `.zs/simulation/<readable-target>-<digest>/` 目录；存在 `default.json` 时先执行
  `import-stimulus`，否则执行 `import-module`。runner session 显式携带场景目录。
- WaveWorkbench 核心测试 61/61、全量测试 88/88、固定仿真 CLI 与结果窗口控制冒烟
  通过；ZeroSlack runtime 测试 17/17、全量测试 89/89 通过，覆盖首次默认生成、
  已保存默认场景恢复、工作区场景路径和缓存隔离。测试使用确定性工具 fixture，
  本机未安装真实 Verilator。

### S10：正式接入 ZeroSlack

状态：`completed`（2026-08-19）

范围：

- 从当前 module、选中 `always` 和 Design instance 打开。
- 选中 `always` 只缩小默认观察范围，底层仍编译完整模块。
- 从编辑器和层级树添加观察信号。
- 编译错误和运行错误可以跳转源码。
- 达到正式开放门槛后启用正式入口。

验收：日常操作不需要先打开 WaveWorkbench 或处理文件。

S10 实际结果：

- Wave Simulation 已进入统一 Action Registry，Tools 菜单、编辑器上下文和 Design
  实例上下文不再依赖隐藏实验设置；Design 入口保留精确 instance path。
- 当前 `always` 由增量 Tree-sitter 文档定位，只缩小初始观察集；Slang 仍对包含所有
  未保存缓冲区的完整工作区快照 elaboration，并编译完整目标模块。
- Module Manifest 升级为 v2，新增便携 observation scope 和语义 observation；
  WaveWorkbench 兼容读取 v1，并把内部信号作为 watch lane 导入。相同声明的不同成员
  以 access path 保持独立 lane 身份。
- runner 的构建/运行错误输出结构化源码诊断，ZeroSlack Notification Center 提供
  `Go to Source`，并拒绝工作区外路径。
- 场景恢复、仅激励复用模型、stale/generation、取消隔离和未保存缓冲区一致性沿用
  S7-S9 已验收能力，正式开放门槛的产品链路已闭合。
- ZeroSlack Debug CTest `89/89`、WaveWorkbench CTest `88/88` 通过。本机未安装真实
  Verilator，外部编译/运行与诊断路径使用确定性进程 fixture 验证，未将其表述为真实 RTL
  编译结果。

明确未做：S11 的共享控件嵌入、内部信号层级浏览和复杂 structured input 编辑。
下一最小切片：S11 共享控件嵌入。

### S11：共享控件嵌入

状态：`completed`（2026-08-19）

范围：

- 从 WaveWorkbench 提取 `wavecore`、`wavetrace`、`wavewidgets`。
- 提取 StimulusCanvas、TraceCanvas 和公共时间轴。
- ZeroSlack 以编辑区 Wave Tab 承载完整界面。
- 保留 WaveWorkbench 独立应用。

验收：嵌入前后使用相同场景和结果契约，仿真语义不变化。

S11 实际结果：

- WaveWorkbench 新增 `wavewidgets` 共享库和版本化 C ABI 工厂；独立应用改为链接同一
  `MainWindow`、`StimulusCanvas` 与 `TraceCanvas` 实现，没有复制第二套界面。
- trace 实现归入独立 `wavetrace` target，旧 `waveimport` 保留为兼容 target；刺激与
  结果画布的 tick/pixel 换算收敛到无 Widgets 依赖的公共 `TimelineViewport`。
- ZeroSlack 通过运行时 ABI 与 workspace contract 校验加载结果，并由通用非编辑器
  tool tab 承载；正常路径不再启动独立进程，加载失败页才提供显式外部打开操作。
- 便携安装同时包含独立应用、`wavewidgets`、CLI、Schema、文档与示例；独立应用不是
  ZeroSlack 正常嵌入路径的硬依赖。
- ZeroSlack Debug CTest `90/90`、WaveWorkbench CTest `90/90` 通过。真实跨仓运行测试
  动态加载共享库、打开 handshake 工程并确认两个共享画布存在；本机仍未安装真实
  Verilator，RTL 编译/运行链继续由确定性 fixture 验证。

下一最小切片：S12 的内部信号层级浏览，其他增强项继续独立排期。

### S12：独立增强项

状态：`in_progress`（内部信号层级浏览、多时钟和异步事件已完成，2026-08-19）

以下项目必须继续拆成独立切片，不得合并为一个“大完善阶段”：

- 内部信号层级浏览。`completed`
- 多时钟和异步事件。`completed`
- struct、array 和 interface 输入编辑。
- Expected/Actual 比较。
- 时刻值、稳定性、边沿响应等轻量检查。
- FST/Wellen 按需读取。
- unresolved module 的显式 stub。
- 模块全部场景批量运行。
- 结果与 driver/source 的双向导航。

S12.1 实际结果：

- `wavetrace` 新增纯数据层级模型，并在 VCD 解析时保留原始 scope 组件；UI 不再通过
  对 fullName 做字符串拆分来推断实例层级。
- Simulation Result 的 Actual 区域新增搜索框、实例/scope 树、位宽列和复选状态。
  scope 复选控制整棵子树，叶节点复选立即更新 Actual 波形，点击叶节点揭示对应行。
- 初始可见信号优先采用现有场景 trace mapping；其他内部信号保持可发现。用户自定义
  集合在同一会话重跑时按稳定 trace signal ID 保留。
- Verilator harness 的层级深度由 8 提升到 99，构建参数改用 `--trace-vcd`，并启用
  `--trace-structs` 与 `--trace-underscore`。harness 变化继续进入模型缓存指纹。
- `wavewidgets` C ABI 和 simulation workspace contract 继续为 v1，并新增
  `internal-signal-hierarchy/v1` 能力声明。Module Manifest v2、instancePath、observation
  accessPath 和 Stimulus Scenario 契约均未改变。
- WaveWorkbench 与 ZeroSlack 全量 offscreen CTest 均为 `90/90`；真实跨仓测试动态加载
  `wavewidgets`、校验层级能力并完成工具 Tab 关闭生命周期。原生结果窗口截图确认层级树
  和 Actual 波形同时可用。本机未安装真实 Verilator，外部编译/运行继续由确定性 fixture
  验证，不将 fixture 结果表述为真实 RTL 仿真。

本轮切片：S12.1 内部信号层级浏览
完成内容：精确 scope 数据模型、层级搜索/复选 UI、Actual 可见集合联动、深层追踪参数、
能力声明和双仓集成验收。
明确未做：多时钟/异步事件、structured input、Expected/Actual 比较、轻量检查、FST、
unresolved stub、批量场景和结果到源码导航。
用户可见行为：嵌入式 Wave Tab 的 Actual 左侧可按实例层级查找并显示内部信号。
自动测试：WaveWorkbench CTest `90/90`；ZeroSlack CTest `90/90`；真实共享库跨仓加载通过。
人工验证：原生结果窗口截图已检查层级、搜索、位宽、复选和波形占比。
Schema/接口版本：`wavewidgets` ABI v1、workspace contract v1、Module Manifest v2、
Stimulus Scenario v1，均未升级；新增可选能力声明 `internal-signal-hierarchy/v1`。
修改仓库与提交：WaveWorkbench 本提交；ZeroSlack 本提交。
已知限制：真实 Verilator 未安装；当前浏览对象限于 VCD 已追踪信号，源码双向导航属于后续独立切片。
下一最小切片：S12.2 多时钟和异步事件。
恢复开发所需上下文：从 `TraceSignal.scopePath`、`buildTraceHierarchy()`、
`TraceSignalBrowser` 和 Simulation Result 的 `traceVisibleSignalIds_` 继续；不得把后续
structured input 或源码导航并入本切片。

S12.2 实际结果：

- Module Manifest 的全部有效语义时钟候选分别导入为独立 ClockDomain；多候选仍保留明确
  诊断，但不再把所有时钟丢弃。唯一候选的既有建议元数据保持兼容。
- Simulation Result 工具栏新增 `Clocks (N)`，可逐个编辑周期、相位、占空比和有效边沿；
  结果页同时公开既有 `Timing` 动作，可在关联时钟网格与精确 1 tick 异步编辑间切换。
- 运行计划继续逐端口驱动输入。测试覆盖 10 ns 与 17 ns 两个独立时钟、不同相位和占空比，
  以及 1234 tick 的非网格普通输入事件，确认时钟不会合并、事件不会吸附。
- 构建缓存身份不包含运行时刺激；修改时钟或异步事件复用同一编译模型，测试确认 fingerprint
  不变且未再次执行 build process。
- `wavewidgets` C ABI、workspace contract、Module Manifest 和 Stimulus Scenario 版本均未
  升级；新增可选能力声明 `multi-clock-async-events/v1`。

本轮切片：S12.2 多时钟和异步事件
完成内容：多候选时钟独立导入、结果页逐时钟编辑、精确异步事件入口、运行计划与缓存回归测试、
双仓能力门禁。
明确未做：structured input、Expected/Actual 比较、轻量检查、FST、unresolved stub、批量场景和
结果到源码导航。
用户可见行为：嵌入式 Wave Tab 顶部可编辑每个独立时钟，并将刺激编辑切换为精确 1 tick 模式。
自动测试：WaveWorkbench CTest `90/90`；ZeroSlack CTest `90/90`；真实共享库跨仓加载通过。
人工验证：`artifacts/ui/wave/s12-multi-clock-async-events.png` 已检查 Clocks、Timing、Stimulus
和 Actual 的同屏布局。
Schema/接口版本：`wavewidgets` ABI v1、workspace contract v1、Module Manifest v2、
Stimulus Scenario v1，均未升级；新增能力声明 `multi-clock-async-events/v1`。
修改仓库与提交：WaveWorkbench `cda5ef9`；ZeroSlack 本提交。
已知限制：本机未安装真实 Verilator；外部编译/运行由确定性 fixture 验证，不表述为真实 RTL 仿真。
下一最小切片：S12.3 struct、array 和 interface 输入编辑。
恢复开发所需上下文：从 Module Manifest v2 的 port type/shape 元数据、Stimulus Scenario v1
端口表示和 StimulusCanvas 的 lane editor 继续；不得把 Expected/Actual 或检查器并入本切片。

S12.3 实际结果：

- ZeroSlack 将 Module Manifest 升级为 v3，由 Slang elaboration 直接提供 packed struct member、
  固定 unpacked array element 与显式 modport interface member 的 selector、方向、位宽、enum
  和 source/storage index；WaveWorkbench 不重新扫描 SystemVerilog 源码。
- 类型与符号身份在导出边界归一化为不含绝对路径的稳定摘要，工作区移动后仍可恢复。
- WaveWorkbench 将结构化根导入为 group 与 flat leaf lane；Stimulus Scenario v3 持久化结构化
  binding，并支持根端口安全重命名后的 leaf 与自动 group 迁移。
- runner 生成 wrapper 重建 packed struct、固定 unpacked array 和无构造端口的显式 modport
  interface；VCD stable trace name 映射回原 leaf。缺失安全语义事实时明确拒绝，不按总位宽猜测。
- 生产者测试可直接生成消费者 fixture；WaveWorkbench 的 UI 冒烟编辑三类 leaf 并渲染真实
  WaveCanvas，截图为 `artifacts/ui/wave/s12-structured-input-editor.png`。

本轮切片：S12.3 struct、array 和 interface 输入编辑
完成内容：Module Manifest v3、Stimulus Scenario v3、结构化 lane/group、wrapper 重建、trace
映射、迁移与双仓契约测试。
明确未做：Expected/Actual 比较、轻量检查、FST、unresolved stub、批量场景和结果到源码导航。
用户可见行为：结构化输入以可展开 group 显示，成员可直接使用既有 bit/bus/enum 波形编辑器。
自动测试：ZeroSlack CTest `90/90`；WaveWorkbench CTest `91/91`。
人工验证：结构化输入 WaveCanvas 截图已检查三类 group、leaf 名称和值段。
Schema/接口版本：Module Manifest v3、Stimulus Scenario v3；`wavewidgets` ABI 与 workspace
contract 保持 v1。
已知限制：interface 必须无构造端口且显式指定 modport；`inout/ref`、动态数组和超过 64 bit
的 leaf 明确拒绝。本机无真实 Verilator，wrapper/运行链使用确定性 fixture 验证。
下一最小切片：S12.4 Expected/Actual 比较。
恢复开发所需上下文：从 v3 structured binding、stable trace name 与现有 compare 模块继续；
不得重新引入源码字符串扫描，也不得将检查器或 FST 并入比较切片。

S12.4 实际结果：

- Stimulus Scenario 升级为 v4，为 watch lane 增加独立 `expectedSegments`。期望区间允许部分
  覆盖，并与输入 `segments` 分离；运行计划仍只消费输入激励，不会把期望值驱动进 DUT。
- Simulation Result 新增 Compare 动作、X 处理策略、边沿容差、摘要和差异表。比较范围严格
  限于具有期望区间的 watch lane，其他未配置输出不会造成无关映射失败。
- 比较结果同步标注期望 WaveCanvas 和实际 TraceCanvas。选择差异行会定位期望 lane、实际
  trace signal 与对应 tick；场景或结果变化后旧比较结果立即标为 stale。
- `wavewidgets` C ABI 与 workspace contract 保持 v1，新增
  `expected-actual-compare/v1` 能力声明；ZeroSlack 的真实共享库门禁验证能力、动作和结果表。
- Stimulus v1/v2/v3 继续兼容读取；v4 schema 与 portable component 安装闭包均有测试覆盖。

本轮切片：S12.4 Expected/Actual 比较
完成内容：watch-lane 期望契约、嵌入式比较控制、差异双画布标注/导航、能力声明和双仓门禁。
明确未做：轻量检查、FST、unresolved stub、批量场景和结果到源码导航。
用户可见行为：用户可在输出 lane 绘制期望区间，运行后点击 Compare 查看并定位差异。
自动测试：ZeroSlack CTest `90/90`；WaveWorkbench CTest `92/92`；真实共享库跨仓加载通过。
人工验证：`artifacts/ui/wave/s12-expected-actual-compare.png` 已检查期望、实际和差异表同屏。
Schema/接口版本：Stimulus Scenario v4；`wavewidgets` ABI 与 workspace contract 保持 v1；
新增能力声明 `expected-actual-compare/v1`。
已知限制：本机未安装真实 Verilator；端到端运行由确定性 simulator fixture 验证，不表述为
真实 RTL 仿真。当前只比较用户明确绘制了期望区间的 watch lane。
下一最小切片：S12.5 时刻值、稳定性与边沿响应轻量检查。
恢复开发所需上下文：复用 v4 `expectedSegments`、当前 Actual TraceIndex 和比较差异导航，
检查结果必须保持派生数据，不得成为新的场景事实源，也不得把 FST 并入本切片。

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
