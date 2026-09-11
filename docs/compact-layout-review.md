# P0 窄空间布局验收 — v0.25.9

日期：2026-09-12。基线：`05a366ef` / v0.25.8。环境：Qt 6.10.2、MinGW 13.1、Release、Windows；几何测试使用 offscreen 平台。

## 1. 根因与修复

### P0-1：标签持续压缩

`EditorSplitController::configureGroup()` 原先只设置中间省略；此外，MainWindow 在创建分组和刷新主题时会重新应用 `InsightVisualStyle::tabBarStyleSheet()`，因此仅给 controller 添加局部样式会被覆盖。现在两条路径都保留 108px 标签内容最小宽度，关闭 expanding 并明确启用滚动。Qt 私有滚动按钮也按现有按钮样式的 minimumSizeHint 分配宽度，保留原有图标。

- `src/editor/editorsplitcontroller.cpp:472`：分组配置。
- `src/ui/insightvisualstyle.cpp:1190`：实际主窗口使用的标签宽度规则。
- `src/ui/roundedicons.h:100`：滚动按钮宽度与零重叠度量；继续使用原有 RoundedIcons 样式。

### P0-2：必须区分工具条硬裁与整窗重叠

**实际工具条缺陷**：单行 QHBoxLayout 在受限宽度内压缩子控件，`Open Full View` 曾只有 87px，而其 minimumSizeHint 为 109px；文字是在按钮自己的裁剪区域内被截断。原完整图工具条只给第一行加横向滚动，允许按钮部分露出，也没有处理第二行筛选项。换行布局现在以完整控件为单位分配空间，并按实际换行高度更新外层工具条，防止下一行覆盖上一行。Context 的完整视图、Unpin、关闭当前标签操作移到独立头部，避免 corner widget 与窄标签栏争抢空间。

**参考图中跨父级覆盖的真正原因**：`gui_smoke_test.cpp:9775` 的 F24 用例只发送 MouseButtonPress，没有配对的 MouseButtonRelease。事件传播至 MainWindow 的 `(204,633)` 左侧停靠分隔线后，Qt 一直保留分隔线调整状态，后续主窗口布局不再正常更新。异常时窗口为 1900×1040，但布局矩形为 0×0，中央区域仍为 `(210,40,898,520)`，与从 x=828 开始的 Context 重叠；并非子控件突破了 Qt 的祖先裁剪。补齐该用例的释放事件后，布局恢复为 `(4,40,1892,996)`，中央区域为 `(210,40,1348,948)`，与 Context 不再重叠。原有断言均保留，截图前另加主布局有效及中央区不重叠的断言。

机制依据：[Qt 6.10.2 QMainWindowLayout 实现](https://raw.githubusercontent.com/qt/qtbase/v6.10.2/src/widgets/widgets/qmainwindowlayout.cpp) 中的分隔线调整状态，以及 `setGeometry()` 对 savedState 的保护。事件跟踪留在 `build/p0-mouse-test.log`，最终几何留在 `build/p0-compact-review/geometry.txt`。

修复位置：

- `src/ui/compactlayout.h:10`、`:27`、`:111`：工具条按宽度换行、正确申请高度、长标题尾部省略并保留全文 tooltip；宽度充足时保留原 stretch 对齐。
- `src/insights/liveinsightscontextview.cpp:520`：Context 概览工具条。
- `src/insights/rtlinsightworkbench.cpp:276`、`:321`：工作台工具条。
- `src/insights/insightviewsurface.h:28`：专用图面板工具条，保留 Track/Matrix 和各自渲染器。
- `src/ui/contextdockhost.cpp:32`：头部操作与滚动标签分离；关闭操作作用于当前标签。
- `src/editor/editorgutter.cpp:152`：行号区高度跟随正文 viewport，避免窄临时编辑器的横向滚动条与行号区重叠。
- `test_sv/gui_smoke_test.cpp:9786`：完成原模拟鼠标手势；`:332` 新增整窗几何断言。

### P0-3：Context 显示内部身份

实际调用路径先由 `EditorLocation::displayText()` 将无路径文档的 documentId 写入非空 resource.title，所以只修 QUrl::fileName 回退并不充分。provider 现在优先读取同一文档的活动编辑器标签文字，并在视图数或修改标记变化时同步；Context host 仅显示人类可读标题。资源身份、持久化边界、原 URI tooltip 均保留。

- `src/ui/temporaryeditorcontextprovider.cpp:24`、`:90`、`:124`、`:192`、`:223`：标题来源及同步。
- `src/ui/contextdockhost.cpp:13`、`:162`、`:180`：标题显示回退及更新。

### 宽度与范围约束

- 未新增生产代码 `resize()`，未增加窗口、面板或 dock 的显式最小宽度或默认宽度。
- 唯一新增的生产 `setMinimumWidth(0)` 用于解除搜索输入框原有 180px 限制；Context 概览原有 240px 最小宽度被移除。
- 标签项的 CSS 最小宽度为 108px，不是窗口或 dock 的最小宽度；滚动箭头只恢复按钮自身应有尺寸。
- 测试中的 fixed width 和 resize 仅用于强制 220/270/340/480px、615px 单组及 900px 分屏场景；没有放宽既定不变量。
- 未改配色、字号、间距规范、语义所有权、只读 CLI 或专用图语义；未增加动画或永久隐藏功能。

## 2. 不变量与量化结果

新增测试在 `CMakeLists.txt:1389` 构建，`:1910` 独立注册。

| 不变量 | 测试函数 | 验证方式 / 结果 |
|---|---|---|
| I1 | editorTabs | 单组 8 标签、分屏每组 8 标签，逐个激活检查 tabRect ≥108px |
| I2 | editorTabs | 自定义 QPaintEngine 捕获实际 drawTextItem 文本，移除省略号后最短为 10 字符，要求 ≥3 |
| I3 | contextWidths / fullViewWidths / untitledNames | 遍历当前可见 QWidget 子树，兄弟全局矩形交集均为空 |
| I4 | 同上 | 子控件完全包含于父控件矩形，0px 容差 |
| I5 | 同上 | 所有可见按钮满足 minimumSizeHint，按钮文本完整；不得硬裁 |
| I6 | untitledNames | Context 标签与独立读取的实际编辑器 tabText 相等；覆盖初始 untitled、复制视图后缀、修改标记；UUID 正则不匹配，URI tooltip 不变 |
| I7 | 上述三个 Context 用例 | 每个用例分别扫描 220、270、340、480px |
| 额外集成 | mainWindowGeometry / gui_smoke_test | 真实主窗口包含 Context 和专用完整图时，调整窗口后中央区与 dock 不重叠 |

8 标签实际宽度（px）：

| 场景 | 每个标签宽度 | usesScrollButtons | 滚动按钮可见 |
|---|---|---|---|
| 单组，可用 615px | [138,138,138,138,138,138,138,138] | true | true |
| 分屏左组，可用 448px | [138,138,138,138,138,138,138,138] | true | true |
| 分屏右组，可用 448px | [138,138,138,138,138,138,138,138] | true | true |

每格依次为 I3 / I4 / I5 违例数量：

| Context 宽度 | Insight 概览 | 完整热点图 | 临时编辑器（含长行） |
|---|---|---|---|
| 220px | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| 270px | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| 340px | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| 480px | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |

详细日志：`build/p0-compact-detail.txt`。覆盖范围为表中实际视图与场景，不将这些结果推定为所有其他 provider 的全量几何审计。

## 3. 独立测试、反向验证与回归

独立 ctest 输出（`build/p0-compact-ctest.log`）：

```text
1/1 Test #57: compact_layout_test .............. Passed 2.77 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 2.81 sec
```

反向验证：通过 apply_patch 临时把实际主窗口标签样式的 min-width 从 108px 改为 0px，重新构建并运行 editorTabs。结果如下，退出码为 1；随后已通过 apply_patch 恢复 108px，并重新构建、运行最终测试。

```text
FAIL! : CompactLayoutTest::editorTabs()
  'bar->tabRect(i).width()>=108' returned FALSE.
test_sv/compact_layout_test.cpp(133) : failure location
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 76ms
Reverse test exit: 1
```

原始失败证据：`build/p0-reverse-test.txt`。行号对应反向验证时的测试源码。

最终完整 ctest 汇总（`build/p0-lifetime-tests.log`；截图验收轮次另存于 `build/p0-final-tests.log`）：

```text
1/9 Test #17: settings_center_panel_test ........ Passed 0.20 sec
2/9 Test #54: panel_layout_controller_test ...... Passed 0.39 sec
3/9 Test #55: editor_split_controller_test ...... Passed 0.12 sec
4/9 Test #57: compact_layout_test ............... Passed 2.82 sec
5/9 Test #58: context_workspace_test ............ Passed 0.11 sec
6/9 Test #78: gui_smoke_test .................... Passed 18.94 sec
7/9 Test #83: insight_visual_style_test ......... Passed 0.70 sec
8/9 Test #84: signal_usage_hotspot_panel_test .... Passed 0.41 sec
9/9 Test #99: version_documentation_guard ....... Passed 0.05 sec
100% tests passed, 0 tests failed out of 9
Total Test time (real) = 23.79 sec
```

Release GUI、CLI 与上述测试目标均已构建；现有测试断言没有删除、放宽或跳过。

## 4. 截图逐张复核

统一目录：`build/p0-compact-review/`。以下九张均重新生成并逐张查看：

| 截图 | 复核结论 |
|---|---|
| live_insights_signal_usage_hotspot_full_after.png | 标签可辨识并有滚动箭头；Detach 与图钉分属正常布局区域，零覆盖；Open Full View 完整；Center Current 完整；原代码轨道图保留 |
| split_preview_left.png | 分屏标签可辨识，落点预览保留 |
| split_preview_right.png | 分屏标签可辨识，落点预览保留 |
| split_preview_above.png | 分屏标签可辨识，上方落点预览保留 |
| split_preview_below.png | 分屏标签可辨识，下方落点预览保留 |
| theme_light_context_pinned.png | Context 显示 untitled，与源标签相同；无 UUID |
| theme_dark_context_pinned.png | Context 显示 untitled，与源标签相同；原主题及图标保留 |
| context_width_220.png | 标题尾部省略，按钮与筛选项换行，无越界或硬裁；窄图可使用现有缩放功能 |
| context_width_480.png | 原行内对齐保留，工具条按需换行，无越界或硬裁 |

## 5. 修改规模

口径为 Git 相对基线的新增/删除行，加上新文件实际行数，不包含文档：生产代码 **+215 / -48**，测试 **+257 / -0**，CMake **+10 / -0**，合计 **+482 / -48**。详细逐文件数据在 `build/p0-code-metrics.json`。净增加 434 行；修改行按 Git 的删除旧行、增加新行分别计数。

## 6. 清理结果

删除前逐项确认：根目录仅为 ZeroSlack-win64 部署副本、README 版本低于当时 VERSION 0.25.8、没有独占日志或截图；路径均在 build 内且无重解析点。

| 实际删除目录 | 字节 | MB（十进制） |
|---|---:|---:|
| build/release-0.25.2 | 135907724 | 135.908 |
| build/release-0.25.4 | 135910047 | 135.910 |
| build/release-0.25.5 | 135912033 | 135.912 |
| build/release-0.25.6 | 135968174 | 135.968 |
| build/release-0.25.7 | 135968174 | 135.968 |

清理前 build：**1446.901 MB**；删除完成后：**767.235 MB**；释放 **679.666 MB（648.180 MiB）**。这是删除动作前后的测量值，不包含随后新生成的构建、日志和截图增量。

保留活动构建目录、release-0.25.8、evidence、全部历史 review 目录、日志、文本及 deploy 脚本。没有因条件不满足而跳过的候选目录。核对及回执：`build/p0-cleanup-inventory.json`、`build/p0-cleanup-result.json`。

## 7. 文档与交付范围

VERSION、README、CHANGELOG、VERSIONING、goal、plan 和包装 README 已同步为 0.25.9，用户手册补充新交互，版本文档守卫通过。文档不宣称正式包已更新。本次按任务文件进行一次本地 release 提交；未执行推送或正式包替换。
