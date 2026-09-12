# Context 真浮层第 2 期验收记录

实施版本：0.25.12。基线为 `5fc9016a`（0.25.11）。

几何夹具修复后，四个 DPI 变体各 104 项通过，指定回归 10/10。
最终全量 106/107，242.47 秒，107 项结果与基线逐项相同，仅保留两条已接受的旧样本断言失败。
本期自动化验收达到规定门槛，源码与文档随下述 0.25.12 提交保存；未推送或更新正式包。
原生桌面手工项 22–25 及 show 前后边框时序仍未测；不将自动化通过视为真机验证完成。

## 已核实差异与测试调整依据

- 实施文件称 temporaryEditor 没有覆写 capabilities，实际 `temporaryeditorcontextprovider.cpp:198` 已有覆写，且尺寸建议不同于默认值。本期只增加 detachable=false，保留所有原值，优先满足源码编辑器行为不变的硬约束。
- v3 的 providerStates 读取门槛原来借用 kVersion。升到 v4 时需要单独保留 v3 门槛，不能把它一并升到 4。
- 既有 Context 测试大量验证 overlay 锚点、resize 手柄与尺寸记忆。为继续原样执行这些断言，旧 mock 明确声明 detachable=false；新增可分离 mock 场景独立验证原生窗口，不删除或放宽原 overlay 断言。
- 既有 Pinloom unpin 测试从 peekHost 读取 view；产品范围要求可分离内容改用原生窗口。将这两处读取调整到当前 floatingSurface，并增加顶层 Qt::Tool、overlay 为空及 QWidget 指针不变断言，其余三次 rail 点击断言原样保留。
- 既有状态测试断言 kVersion=3，将升级为 4 并扩展浮层字段比较；另以真实 0.25.11 构建导出的 v3 INI 及构造的 v3 JSON 验证所有旧字段，不能用当前写端冒充旧版本。

## 设计边界

独立窗口使用 Qt::Tool 和原生标题栏，保留内部 pin/fullView 动作，关闭使用原生关闭按钮。temporaryEditor 继续使用原 ContextPeekHost 函数体。

Qt 区分包含边框的 frameGeometry/move 与不含边框的 geometry/size；位置恢复需明确处理原生边框，不能混用两类尺寸。[Qt 窗口几何文档](https://doc.qt.io/qt-6/application-windows.html#window-geometry)。

v3 → v4 为单向升级：0.25.11 的旧读端会整块忽略 v4 Context 状态。这是产品接受的后果，不宣称双向兼容。

## 验收清单

源码路径相对仓库根目录，日志均在 `build/evidence/`。所有通过结论只适用于已执行的自动化范围。

| # | 检查项 | 当前结果与证据 |
| --- | --- | --- |
| 1 | 基线 | 106/107，253.40 秒；`floating-context-baseline-ctest.log`。唯一失败 workspace_persistence_test，恰好指定两条旧样本断言，27/29 checks。 |
| 2 | detachable 默认 true | `src/ui/contextresource.h:22`；默认能力断言通过。 |
| 3 | 临时编辑器 opt-out | `src/ui/temporaryeditorcontextprovider.cpp:202`，真实 provider 能力断言通过；控制器 detachable 仅 1 次出现，无新增 provider 名单。 |
| 4 | overlay 函数体 | `contextpeekhost.cpp` SHA256 与基线完全相同；头文件全部改动逐行见下文。 |
| 5 | 原生 Tool | `contextfloatingwindow.cpp:31`，父对象主窗口；Qt::Tool/isWindow/非 Frameless 断言通过。 |
| 6 | 唯一选择 | `contextworkspacecontroller.cpp:219–227` 的 floatingSurfaceFor；无其他 detachable 判据。 |
| 7 | 分支预算 | 4 → 5，2 个 placement if/switch + 2 个 placement 三元 + 1 个 detachable 三元。 |
| 8 | 窗口透明实现 | `contextfloatingwindow.cpp:89` 只使用 setWindowOpacity；窗口源码搜索 WA_TranslucentBackground、FramelessWindowHint、providerId 均为 0。 |
| 9 | 全局透明度设置 | `settingscenterkeys.h:7` 的 settingsCenter/appearance/floatingContextOpacity；`settingscenterschema.cpp:83`，60–100，默认 90，workspaceAllowed=false。 |
| 10 | 焦点透明度 | `contextfloatingwindow.cpp:87/240`；verifyFloatingOpacityAndSettings 直接调用交互策略函数，通过。未用 offscreen 激活结果冒充桌面焦点验证。 |
| 11 | 状态 v4 | `contextworkspacestate.h:14`；旧字段保留，新增六字段；读端 :567 单独保留 kProviderStateVersion=3，:574 按 v4 放行几何。 |
| 12 | 纯函数 | `contextworkspacestate.h:29` 的 resolvedFloatingGeometry(saved, savedScreenName, availableScreenGeometries, primaryScreenGeometry, availableScreenNames)，只依赖入参及常量。 |
| 13 | 五类屏幕回落 | verifyFloatingScreenFallback：原屏可见、屏幕缺失、交叠不足、尺寸上下限、空屏幕列表均通过；另测标题栏完整可见、140px 横向阈值和小屏幕。 |
| 14 | 真实 v3 输入 | 保留 0.25.11 测试程序及核心 DLL，导出 v3 INI，再由新构建导入并重新保存；104 checks passed，`floating-context-cross-build.log`。旧字段逐字段相等，新增几何为默认无效值。 |
| 15 | 原指针搬运 | verifyDetachableRoutingAndMovement 通过；各阶段 `view() == original`，`fixture.counters.created == created`，overlay→dock→native→dock→overlay。 |
| 16 | 单实例与范围 | verifySingleFloatingInstance 通过：四次 overlay/native 交替后旧 QPointer 为空，两个 host 的 hasResource 之和恒为 1；diff 无多实例、绑定、每文档布局或竖栈实现。 |
| 17 | 八项新性质 | 分流、真实 temporaryEditor、搬运由 verifyDetachableRoutingAndMovement 覆盖；几何、屏幕、v3、透明度、单实例分别由对应 verify 函数覆盖。四个 DPI 变体均通过，未跳过。 |
| 18 | 十项指定回归 | **10/10**，28.27 秒；下表逐项列出，`floating-context-fix-regression.log`。 |
| 19 | 最终全量 | **106/107**，242.47 秒；floating-context-final-ctest.log 对照 floating-context-baseline-ctest.log，107 项结果完全相同；floating-context-final-comparison.json。 |
| 20 | 旧断言 | 静态核对 68 条 Context、21 条临时编辑器、55 条 Pinloom check；只允许预先说明的版本升级和 Pinloom 浮层取值变更，其他表达式完整保留。 |
| 21 | 受保护测试 | workspace_persistence_test.cpp、temporary_editor_context_provider_test.cpp 与快照逐字节相同；两个 .zs 仍不存在；CMake 仅显式列入三个新文件。 |
| 22 | 手工跨屏拖动/重启 | **未测，无截图**。步骤：浮起 RTL Insight，拖到副屏并调整，重启后重新打开；自动化只覆盖几何捕获与纯函数，未覆盖 Windows 原生拖动及混合 DPI。 |
| 23 | 手工焦点与滑块 | **未测，无截图**。步骤：调滑块、切走、悬停、点击返回；自动化覆盖全局存储、滑块即时信号、100%/设置值策略，未覆盖系统合成效果。 |
| 24 | 手工临时编辑器 | **未测，无截图**。步骤：对照 0.25.11 打开/缩放/固定/取消固定及编辑；自动化覆盖原 overlay 测试、共享视图搬运、真实 provider 不可分离，未覆盖实际输入法/焦点观感。 |
| 25 | 手工断开显示器 | **未测，无截图**。步骤：副屏保存几何，断开副屏，重启再打开；纯函数覆盖缺屏完全回落，未覆盖系统显示器移除事件的实际时序。 |
| 26 | 文档版本 | VERSION、CHANGELOG、plan、goal、VERSIONING、package README、README、用户手册同步 0.25.12；正式包未更新。提交状态见收尾表。 |

## 指定回归与先前失败

| 测试 | 结果 |
| --- | --- |
| context_workspace_test | 通过 |
| context_workspace_125pct_dpi_test | 通过 |
| context_workspace_150pct_dpi_test | 通过 |
| context_workspace_200pct_dpi_test | 通过，104/104 checks |
| temporary_editor_context_provider_test | 通过 |
| temporary_editor_context_search_test | 通过 |
| pinloom_context_provider_test | 通过 |
| live_insights_context_provider_test | 通过 |
| rtl_insight_workbench_test | 通过 |
| gui_smoke_test | 通过 |

修复前失败原文（保留历史证据，非当前结果）：

```
FAIL: floating_geometry: all fields survive capture and restore independently of overlay size
FAIL: floating_geometry: reopening uses saved geometry rather than provider preferred size
```

失败位于新增 verifyFloatingGeometryRoundtrip，测试设定 move(60,70)、resize(420,310)，
在 400×400 的 offscreen 逻辑屏幕上超宽，空屏幕名触发正确的主屏夹取，导致往返不等。
收尾文件已澄清：本期新测试失败应自行定位修复，停止规则针对本期之外的基线或最终全量失败。
修复只改变合法几何夹具，原有断言、产品几何逻辑及 DPI 配置不变。
完整构建成功：`floating-context-fix-build.log`；跨构建验证使用同一仓库根目录作为工作目录，
输入 `floating-context-v3-state.ini`，输出 `floating-context-v4-state.ini`。

## overlay 头文件全部改动

- 第 5 行：增加 `#include "contextfloatingsurface.h"`。
- 第 20 行：继承列表增加 `public ContextFloatingSurface`。
- 第 28、29、30 行：hasResource、resource、view 声明增加 override。
- 第 39、41 行：setActionsAvailable、setView 声明末尾增加 override。
- 第 42、43、44 行：updateResource、takeView、clearView 声明增加 override。
- 无函数体、尺寸接口、信号或事件处理变化；cpp SHA256 为 `19AE0B17E4DB4BDC1E8684A1C1A7B1FD24E3E2323CAF73DB01D78688F17016AE`。

## 静态统计与边界

脚本 `floating-context-static-check.ps1` 与结果 `floating-context-static-verification.json`。
每个 if、switch、三元算一个决策点，复合 if 的短路项不重复统计。
控制器 placement 决策在 311、340、360、487 行，detachable 在 225 行，总计 5。
既有 providerId 比较用于资源归属和 rail 复用，不用于选择原生窗口。
几何捕获独立读取原生 host，以在 overlay 活动期间保留上一次原生窗口位置；这不是第二处窗口类型决策。
隐藏的两个 host 可以同时存在，但同时承载资源的浮层最多一个，测试核对资源和旧 view 销毁。

## 改动文件清单

| 文件 | 摘要 |
| --- | --- |
| src/ui/contextfloatingsurface.h | 新增两类浮层的纯虚公共接口。 |
| src/ui/contextfloatingwindow.h | 声明原生浮层、几何及透明度接口。 |
| src/ui/contextfloatingwindow.cpp | 实现原生边框、同一 view 搬运、屏幕回落和焦点透明度。 |
| src/ui/contextpeekhost.h | 仅接入公共接口和 override。 |
| src/ui/contextresource.h | 新增默认可分离能力。 |
| src/ui/temporaryeditorcontextprovider.cpp | 在既有能力覆写中关闭可分离。 |
| src/ui/contextworkspacecontroller.h | 声明当前浮层及原生 host 的访问与选择。 |
| src/ui/contextworkspacecontroller.cpp | 统一浮层操作、互斥选择、几何保存恢复。 |
| src/ui/contextworkspacestate.h | v4 六字段、保留 v3 阈值、纯几何回落函数。 |
| src/workspace/workspacesessionstateservice.cpp | 写入 v4 几何并保留所有 v3 读取。 |
| src/settings/settingscenterkeys.h | 新增应用级透明度键。 |
| src/settings/settingscenterschema.h | 为整数字段增加滑块表示选项。 |
| src/settings/settingscenterschema.cpp | 声明全局 60–100% 透明度。 |
| src/settings/settingscenterpanel.cpp | 生成、读取和刷新滑块。 |
| src/app/mainwindow.cpp | 初始化并实时应用透明度设置。 |
| test_sv/context_workspace_test.cpp | 八项独立性质及几何、透明度边界测试。 |
| test_sv/pinloom_context_provider_test.cpp | 浮层断言接入统一接口，增加原生窗口和指针检查。 |
| CMakeLists.txt | 显式列入三个新源码/头文件。 |
| VERSION | 版本草稿升至 0.25.12。 |
| CHANGELOG.md | 添加本期实现说明。 |
| README.md | 更新版本和独立浮层能力。 |
| 用户手册.md | 补充原生窗口及透明度设置操作。 |
| plan.md | 记录本期实现、验收停止点及后续期边界。 |
| goal.md | 同步实现方向与未发布状态。 |
| VERSIONING.md | 同步版本草稿。 |
| packaging/ZeroSlack-PACKAGE-README.txt | 同步版本草稿。 |
| docs/floating-context-review.md | 记录证据、差异、失败和未测范围。 |

## 已知限制

原生桌面交互接口不可用，22–25 项均未测，也未评价原生标题栏与应用主题的实际观感。
透明度只通过系统 windowOpacity，不自绘圆角或透明背景；是否符合最终视觉要求仍需原生桌面验收。
offscreen 四个 DPI 变体的 QScreen::name() 均为空，因此集成路径始终进入主屏夹取分支。
“原屏存在且标题栏可见 → 保留位置”的分支仅由 verifyFloatingScreenFallback 的显式屏幕名纯函数测试覆盖，
未通过原生窗口集成路径验证；未改变空名语义，因为首次放置故意使用空名强制完全可见。

**原生边框时序未测，属于遗留风险**：未取得 Windows show 前后的 frameMargins 上/下/左/右实测数字。
构造时 winId 创建句柄不等于已证实边框尺寸有效；若 show 前为 0、show 后才非零，
applyGeometry 可能把外框尺寸当成客户区，rememberGeometry 再保存偏大外框，重启可能逐次漂移。
offscreen 不能排除此风险，本次没有依据修改原生窗口实现。后续真机验证应记录 show 前后四边数值，
保存外框 x/y/width/height，连续重启并重新打开两次，逐字段比较每次外框；若确认漂移再调整换算时机。

多实例、绑定、每文档布局、批量收起、rail 右键菜单和侧栏竖栈均未实现。

## 收尾：夹具修复及 DPI 证据

`test_sv/context_workspace_test.cpp:525` 从当前 screen()->availableGeometry() 取可用矩形 A。
外框宽高为 boundedPeekWidth(A.width()/2)、boundedPeekHeight(A.height()/2)，
外框左上角为 A.topLeft() + ((A.width()-w)/2, (A.height()-h)/2)。
只在 A 比既有最小窗口 280×220 更小时显式说明并跳过几何往返，原生关闭断言仍执行。
客户区 resize 减去当前 frameMargins，新增断言要求 frameGeometry 精确等于目标且目标完全位于 A 内，
未增加容差、缩放条件或放宽原断言。屏幕名原已在复合捕获断言中检查，本次再添加独立断言便于定位。

| 缩放 | 实测 availableGeometry | 屏幕名 | 推导外框 x,y,w,h | 检查结果 | 跳过 |
| --- | --- | --- | --- | --- | --- |
| 默认 | 0,0 800×800 | 空 | 200,200,400,400 | 104/104 | 无 |
| 125% | 0,0 640×640 | 空 | 160,160,320,320 | 104/104 | 无 |
| 150% | 0,0 533×533 | 空 | 126,133,280,266 | 104/104 | 无 |
| 200% | 0,0 400×400 | 空 | 60,90,280,220 | 104/104 | 无 |

证据：`floating-context-fix-dpi.log`，使用原有四个 CTest 注册并开启详细日志。
与收尾文件第 1 节根因一致；补充了其表中未列出的 125% 几何。
与第 2.2 节需补屏幕名断言的描述相比，原源码 :555（修复前 :531）已在复合断言里校验该字段，
本次独立增加 :549 的检查，没有删除原检查。

## 收尾验收清单

| # | 检查项 | 结果与证据 |
| --- | --- | --- |
| 1 | 从 availableGeometry 推导夹具 | :525 起，公式见上；完全可见及精确外框断言通过。 |
| 2 | 四个 DPI 变体 | 各 104/104，无跳过，floating-context-fix-dpi.log。 |
| 3 | resolvedFloatingGeometry 不变 | 收尾前后 contextworkspacestate.h SHA256 相等，floating-context-fix-static-verification.json。 |
| 4 | 无容差/缩放条件化 | diff 只替换夹具并增加检查，97 条修复前静态 check 表达式完整保留。 |
| 5 | 屏幕名独立断言 | context_workspace_test.cpp:549，精确比较 saved.floatingScreenName 与 window->screen()->name()。 |
| 6 | 覆盖盲区 | 已知限制明确 offscreen 集成不覆盖有名屏幕分支，纯函数覆盖。 |
| 7 | 原生边框时序 | **未测**，没有真机四边数字；已知限制列出潜在逐次漂移及两次重启验证步骤。 |
| 8 | overlay cpp 零改动 | 基线 SHA256 与当前相等，floating-context-static-verification.json；git diff 为空。 |
| 9 | 指定回归 | 10/10，28.27 秒，floating-context-fix-regression.log；逐项见原验收表。 |
| 10 | 最终全量 | 106/107，242.47 秒。floating-context-final-ctest.log 与 floating-context-baseline-ctest.log 逐项相同，floating-context-final-comparison.json 含全部 107 项及两条精确失败断言。 |
| 11 | 既有断言 | 修复前 97 条静态 check 表达式逐条保留；原阶段基线 68/21/55 条按已说明变更核对通过。 |
| 12 | 受保护测试/配置 | workspace_persistence_test.cpp 字节相同，样本仍缺失；收尾 CMake SHA256 相同，整个阶段仅新增三个源文件声明。 |
| 13 | 未提前实现后续期 | 收尾源码 diff 仅修改测试，无产品源码改动；原阶段 diff 无多实例、绑定、每文档或竖栈实现。 |
| 14 | 手工 22–25 | 四项均未测，无截图；自动化替代和缺口逐项保留在原表。 |
| 15 | 文档版本与提交 | 0.25.12，源码与本文随下述标题的单次提交保存；提交哈希见会话最终回复与 git log，避免在提交自身内记录循环变化的哈希。 |

本次收尾文件仅为 `test_sv/context_workspace_test.cpp`（合法夹具与独立断言）、
`docs/floating-context-review.md`（证据和风险）、`plan.md`/`goal.md`（最终状态）。
忽略目录中的静态脚本和日志仅作本地证据，不作为产品文件提交。
提交标题：`Release 0.25.12: detach context views into real floating windows`。
