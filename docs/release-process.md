# 发布准备与证据复用

所有相对入口路径均相对仓库根解析，与调用者或 CTest 的工作目录无关。
以下命令只准备候选，不授权升版、提交、标签、push 或正式安装。

## 首次打包与已有包复用

```powershell
./scripts/prepare-release.ps1 -ReuseEvidence build/accepted/preparation.json `
  -PackageSources build/package-sources.json `
  -VerificationOutput build/reuse-check.json -Formal
./scripts/package-release.ps1 -PreparationEvidence build/reuse-check.json `
  -OutputDirectory build/new-staging -Formal
python -B scripts/release_smoke.py --package build/new-staging --output build/new-smoke --gui
```

首次打包从已验收构建复制产品二进制，复用 `package-release.ps1` 的部署、许可及资源
组装流程。`PackageSources` 是 `zeroslack.package-sources/v1` JSON，必须列出
`native`、`nativeLicenses`、`adapter`、`welcome`、`documents` 五个独立来源；每个来源
包含绝对 `root`、完整 `files` 散列映射和 `provenance`（原始清单的绝对 `file`、
`sha256`、映射键路径 `keys`、可选包内 `prefix`）。来源文件和集合均与原始清单比对，
不接受只列选中文件或新采样散列作为历史证明。`componentBaseline` 引用已批准组件组合
记录的 `file` 和 `sha256`，核对 native/adapter 完整身份及 SDK 防降级；`distribution`
明确为 `standalone` 或 `appsuite`。

Qt/MinGW 运行库、主程序许可和模板分别来自准备记录已核验的工具链依赖及源码清单，
不从同候选整包提取。组件及其许可可以采用各自的独立发布来源。组装后再次逐项校验
组件、许可、资源与所有 Qt/MinGW DLL，绑定到新包清单。此路径不需要先存在
ZeroSlack 的同候选整包。

确实已有同候选整包时，可用 `-AcceptedPackage <目录>` 替代 `-PackageSources`；两者
互斥，前者保留已验证的整包复制路径。只校验候选而尚未选定打包来源时可都不传，
实际暂存前必须补充其中一种来源。

每个输出须不存在。开发中的隔离候选另加 `-AllowDirty`，包中如实记录 `dirty=true`、
`releaseTag=null`；该参数不会放宽输入身份或测试标准。GUI 冒烟打开专用配置与临时文档，
程序报告 `GUI_READY` 后由操作者或 Computer Use 正常关闭该窗口。不得关闭用户窗口。

复用会重新核对原始准备记录、源码快照及当前内容差异、工具链、依赖完整清单、配置、
默认构建图、已执行 Ninja 命令、生成输入、实际二进制、完整日志，以及原必测布局
矩阵的用例、DPR、后端与测量。Ninja 只执行只读 `-t` 查询，不运行构建命令。
命令指纹与原 `.ninja_log` 对照，规则依据
[Ninja 1.12.1 的构建日志实现](https://github.com/ninja-build/ninja/blob/v1.12.1/src/build_log.cc)。
新准备在成功编译后、必测验收前冻结全部生成文件和构建命令，记录与原编译日志绑定的
`buildIdentityOrigin`，测试结束后再核对未变。导入前、导入后均与这个原始指纹比较，
不能以当前采样代替它。`build_identity` 只是采样函数，单独成功不代表旧生成输入有效。
文件指纹不按扩展名筛选，包含对象、链接响应文件、导出定义及无扩展名文件；只排除
测试产物目录和版本控制／字节码缓存，`Testing/ManagedCommands` 测试定义仍单独冻结。
旧的局部采集范围不能声明为完整原始证明。

没有原始 `buildIdentity` 及其来源的旧记录拒绝导入。最小补证是提供当时在验收前捕获、
绑定成功编译日志的完整生成文件/命令指纹与验收后未变证明；这些原始记录不存在时，
须在隔离候选中重新准备以建立真实新证据，不能给旧记录补字段或倒填时间。已封装旧包
的内容验证可以继续使用其原始封包清单，但不能据此绕过新候选的生成输入检查。

提交相同文件内容或新增标签不自动失效；程序若以后嵌入 Git 身份，须同时收紧该判定。
当前 `version.h.in` 只嵌入版本与构建时间。外部发布说明与发布工具变更单列记录，
不宣称旧测试验证了新工具；新工具须有对应定向自测。其他源码、构建输入或配置变化
不在此豁免内。新生成的复用记录被采纳后，相关输入再次变化会使记录失效。

失败、超时、受限、缺日志、缺覆盖、散列变化或不明身份均拒绝直接复用，返回非零并
保留失败记录。程序版本变化返回“增量版本构建及验证”诊断；不得修改旧包版本字段
冒充新程序。应先定版，在可写候选中增量配置/构建受影响目标并补验，再提交与该产物
对应的完整证据。不能可靠限定影响或证据仍不完整时使用原完整准备入口：

```powershell
./scripts/prepare-release.ps1 -PreparationPlan path/to/plan.json `
  -BuildDirectory build/new-preparation -Formal
```

该路径仍执行默认 ALL 和必测矩阵。复用路径不会自动退回构建或扩大测试。
全量回归不因进入发布阶段而默认重跑；原始受限与跳过项仍保持其身份。

## 暂存包与组件

首次打包按已验收构建和分别核准的来源组装；已有包路径则复制后重新封装交付文档、
源码身份和校验和。两者均保留核准的组件、运行库、资源、许可和来源，不从较旧 API
构建依赖覆盖独立部署的运行组件。0.33.5 的有效组合是 xIPs 原生 2.16.0、独立适配器 2.14.2、
Suite SDK 1.0.2，原生组件与适配器不要求版本号相等。

`release_smoke.py` 校验真实 GUI/CLI 版本、格式化、文档分析及原生组件，并限制 DLL、
插件、配置和缓存路径。GUI 模式核对实际加载模块和打开的临时文档。新日志绑定当前
暂存包；旧全量/性能证明仍绑定原产物。损坏包或包/程序版本不一致无法通过闸门。

## 已授权后的安装与恢复

```powershell
python -B scripts/install-release.py --package build/new-staging `
  --smoke build/new-smoke/RESULT.json --suite X:/explicit/AppSuite `
  --backup X:/explicit/Backups/unique-name --work X:/explicit/Staging/unique-name
```

安装目标固定为所选套件的 `Apps/ZeroSlack-win64`。命令本身不构成发布授权。
执行前核对当前包的受管文件、进程占用、组件防降级及专用路径，拒绝联接/符号链接。
旧包未知文件保留，已修改受管文件或新包与未知文件冲突时拒绝覆盖。
旧包及套件两项索引保存在目标外的独立备份；复制验证后交换目录，只更新 ZeroSlack
索引，保留其他应用。安装后执行包内冒烟并比对文件。交换或复验失败则恢复包和索引，
保留旧备份、失败包及日志；恢复不确定时报告 `rollback_failed`，不循环覆盖。

隔离演练使用仓库 `build` 下的模拟套件；正式套件目录、Git 与远端均不参与演练。
