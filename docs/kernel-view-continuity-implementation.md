# Kernel 视图状态连续性实施记录

日期：2026-09-20。源码基线：`main` / `3cd3532` / `v0.29.18`。
对应方案：`E:/ZeroSlack/AppSuite-UI-现代化方案.md` 第 0、1 阶段。
状态：第 0、1 阶段代码实施和后台回归已完成；全量回归及环境差异见下文。原生桌面独立验收尚未执行。

## 第 0 阶段基线

- Qt 6.10.2、MinGW 13.1、Ninja、Release，构建目录 `build/Desktop_Qt_6_10_2_MinGW_64_bit-Release`。
- 本机 Windows 10.0.26200，Intel64 Family 6 Model 183 Stepping 1；测试使用 `offscreen` 平台和 `QT_SCALE_FACTOR`，窗口 1060×760 逻辑像素。
- 修改生产代码前，构建退出码 0，日志 `FAILED:` 数量 0；全量 CTest **104/104 通过**，耗时 115.63 秒，允许失败集合为空。
- `test_sv/new`、`test_sv/huge_prj` 及本地名称头文件存在。私有工程和名称不纳入版本管理。
- 历史 `106/107` 及两条真实工程持久化断言不适用于当前版本；该两条断言在 `f94c273` / `v0.29.14` 中已移除。本轮未删除或放宽既有测试。
- 证据保存在忽略目录 `build/evidence/kernel-continuity-20260920/`：`baseline-build.log`、`baseline-ctest.log`、`baseline-LastTest.log`。

## 修复前定向复现

新增 `kernel_view_continuity_test`，通过实际搜索框、复选框和图内分组点击验证。合成场景包含中心节点、6 个输入和 12 个输出，输出组成高扇出分组；窗口为 1060×760 逻辑像素。

修改生产代码前，在 100%、125%、150%、200% 下测量：

| 指标 | 修复前结果 |
| --- | --- |
| 输入搜索词前后缩放 | 1.7 → 0.58447，四档一致 |
| 搜索后原中心偏移 | 约 281.59 逻辑像素，四档一致 |
| 搜索后选择 | 丢失，四档一致 |
| 主题刷新累计偏移 | 约 16.97 逻辑像素，四档一致 |
| 搜索重建均值 | 0.172–0.182 ms，每组 30 次 |
| 离屏视口绘制均值 | 0.73–1.52 ms，每组 30 次 |

主题往返覆盖 3 档画布缩放、3 个观察位置，每个组合连续刷新 12 次，共 108 次。累计偏移属于缺陷，不能直接作为容差上限。修复前新回归用例为 **11/22 通过**，失败涉及搜索、选择、筛选、分组、报告刷新和重复显示。

## 本轮验收口径

- 缩放矩阵保持不变；在边界允许时，连续操作中心偏差不超过 2 个逻辑像素，包含整数滚动坐标的舍入余量。累计漂移不得随重复次数增长。边界限制另行验证，不纳入普通保持用例的误差统计。
- 状态恢复与性能分别验收。在相同环境和操作序列下，定向重建与绘制的平均耗时不得高于基线的 1.25 倍加 1 ms；该绝对余量用于低于 1 ms 的小图测量噪声。复杂场景另记规模和重复次数，不能把小图数据推广为任意规模保证。
- 保留现有主题、专业图与导出测试；新增用例必须覆盖稳定身份、失效选择、主动定位、空报告与过期 Fit。
- 全部自动化运行在后台，离屏测量不代表原生 Windows、DWM、Snap 或跨屏验收。

## 实施结果

### 行为与代码范围

- 输入或清空搜索词只更新匹配高亮及计数。搜索框 Enter 定位首个可见匹配节点或折叠分组，保持缩放和选择；无匹配时不移动。
- 筛选、分组、同目标报告更新、主题刷新及重新显示沿用同一套视口状态恢复。场景边界收缩时按滚动范围限制中心；暂时无报告时保留上一有效视口，清除失效选择。
- 选择以稳定符号身份、节点角色和精确关系证据匹配。数字 ID 改变不丢失仍有效的选择；ID 复用或身份歧义不误选；已被筛掉的选择不因取消筛选而复活。
- Kernel 面板负责首次有效显示的 Fit；外层 Workbench 不再重复安排 Kernel Fit。切换目标、显式缩放、滚动条及画布操作取消过期任务，避免随后覆盖用户视口。
- 修正整数视口中心恢复造成的累计偏移，处理反复改变尺寸时旧视口尺寸的捕获，并保证面板父对象先销毁时挂起回调失效。图形项触发重建前复制回调与参数，避免场景清空后访问已销毁项。

生产代码仅涉及 `src/insights/signalkernelgraphpanelcoordinator.{h,cpp}` 和 `src/insights/rtlinsightworkbench.cpp`。新增 `test_sv/kernel_view_continuity_test.cpp`，在 CMake 注册四档缩放测试。未修改报告生成、专业布局、编辑器、窗口框架或公共 SDK；未引入新依赖。

### 后台验证

- 四档缩放各 **36/36** 项检查通过，合计 144 项。包括实际搜索框 Enter、复选框、图内分组点击、滚动条操作，以及报告身份变动、空报告、隐藏重显、快速切换目标、窗口尺寸往返和销毁顺序。
- 各档 108 次主题往返后，缩放不变，最大累计中心偏移 **0 逻辑像素**；搜索缩放 **1.7 → 1.7**，中心偏移 **0**，有效选择保留。
- 已检查四档合成图和 100%、200% 真实图的离屏截图，控件、文字与图形正常显示。截图与原始日志保留在证据目录，未将私有工程内容纳入仓库。
- 全量 Release 构建退出码 0，`FAILED:` 数量 0。沙箱内全量 CTest 首次 **107/108**：`gui_smoke_test` 的两条文件监听断言失败，其余通过；沙箱内单独复跑同样失败，沙箱外同一离屏可执行文件 **998/998** 检查通过。相关生产代码与断言均未改动，结果表明失败与 Windows 文件通知运行环境相关，不能把它记为删减断言或允许失败。
- 沙箱外完整 CTest **108/108 通过**，耗时 **119.23 秒**，允许失败集合为空。沙箱外仍使用 `offscreen`，不等同于原生窗口验收。

小图测量（每个均值 30 次，四档独立串行运行）：

| 显示缩放 | 搜索重建，修复前 → 后（ms） | 修复后视口绘制（ms） | 搜索中心偏移 | 主题累计偏移 |
| --- | --- | --- | --- | --- |
| 100% | 0.175 → 0.255 | 0.564 | 0 px | 0 px |
| 125% | 0.173 → 0.245 | 0.809 | 0 px | 0 px |
| 150% | 0.182 → 0.245 | 1.066 | 0 px | 0 px |
| 200% | 0.172 → 0.240 | 1.604 | 0 px | 0 px |

状态恢复增加少量计算，测量在约定阈值内。小图修复前因错误 Fit 而处于不同缩放，其绘制耗时不用于推断性能收益。

### 真实工程对照

本地 `test_sv/huge_prj`：**428 个 HDL 文件、308 个节点、2 个分组**，将高扇出组全部展开。测量器按关系度排序前 32 个候选，选择节点最多的报告；目标名称随私有截图保存在本地。报告生成重复 10 次，搜索及绘制各重复 30 次。绘制对照统一设置画布缩放 1.7；这组性能采样未选择图形项，选择保持另由合成场景验证。

| 指标 | 修复前，100% | 修复后，100% | 修复后，200% |
| --- | --- | --- | --- |
| 报告生成均值 | 1499.99 ms | 1488.02 ms | 1833.36 ms |
| 搜索场景重建均值 | 35.40 ms | 34.31 ms | 39.92 ms |
| 视口绘制均值 | 4.10 ms | 4.06 ms | 8.82 ms |
| 搜索中心偏移 | 728.22 px | 0 px | 0 px |
| 搜索缩放保持 | 否 | 是 | 是 |

真实工程的修复前对照采用 `3cd3532` 原始面板源码，在隔离测量可执行文件中编译，链接本轮未改动的语义与图服务；不是完整旧版安装包。该测量器的小图控制实验复现了原始基线的缩放、281.59 px 搜索偏移及 16.97 px 主题偏移。原始源码副本与构建配置留在忽略目录 `baseline-probe-source/`。

100% 同场景重建和绘制均未出现超阈值退化；200% 数据用于记录较高像素密度下的结果，没有对应的旧版真实工程测量，不作横向收益比较。上述均为本机抽样均值，未测端到端输入延迟或分位数。报告生成约 1.5 秒和搜索时整场景重建仍是现有成本，本轮不宣称已经消除卡顿。

### 复核入口与剩余边界

后台复核前确认 Qt/MinGW 路径，并检查构建真实退出码。不得使用 `--measure-only` 或设置 `ZEROSLACK_KERNEL_WORKSPACE` 运行正式断言；这两者是独立的性能采样模式。

```powershell
$env:PATH = 'E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
& 'E:\QT6\Tools\CMake_64\bin\cmake.exe' --build 'build/Desktop_Qt_6_10_2_MinGW_64_bit-Release' --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Remove-Item Env:ZEROSLACK_KERNEL_WORKSPACE -ErrorAction SilentlyContinue
& 'E:\QT6\Tools\CMake_64\bin\ctest.exe' --test-dir 'build/Desktop_Qt_6_10_2_MinGW_64_bit-Release' --output-on-failure --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

定向检查可加 `-R '^kernel_view_continuity_'`；真实工程采样另行运行测试程序，设置 `QT_QPA_PLATFORM=offscreen`、`QT_PLUGIN_PATH`、`QT_SCALE_FACTOR` 和 `ZEROSLACK_KERNEL_WORKSPACE`，每轮输出 `WORKSPACE_METRICS`。测试源保留完整采样步骤。

主要证据：`final-continuity-tests.log`、`final-full-build.log`、`final-full-ctest.log`、`final-LastTest.log`、`gui-smoke-recheck.log`、`gui-smoke-native-notifications-LastTest.log`、`final-system-ctest.log`、`final-system-LastTest.log`、`real-workspace-before-1.log`、`real-workspace-final-{1,2}.log`、`verified-source-hashes.json` 及 `review/`。路径均相对于 `build/evidence/kernel-continuity-20260920/`。

尚未执行原生桌面、DWM、Snap、多屏 DPI 或跨屏独立验收。后续按方案在实际使用工程中复核手感；第 2–5 阶段的第二应用验证和 SuiteUi 提取尚未启动。

实现已纳入 `v0.29.19` 发布提交 `5bccf7b`。正式包保持固定路径 `E:/PinloomRoot/AppPackage/AppSuite/Apps/ZeroSlack-win64`；发布构建、验证与部署记录保存在本地 `build/release-0.29.19/`。
