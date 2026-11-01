# 控件样式、所有权与验证边界

当前仅维护 Ela 界面。历史预览、后端选择及独立控件试点不再是构建或运行入口；
相应历史实施记录可从 Git 历史取得。本文描述当前应用所有权及尚未完成的验证，
不将历史测试统计移作当前构建结果。

## 样式与输入所有权

`ApplicationThemeManager` 初始化 ElaWidgetTools，安装 `RoundedIcons::Style`，并设置
应用字体、调色板和 Ela 样式表。二者均为实际使用的样式层。
`UiControls` 创建 Ela 控件；`InsightControlStyle` 和 `InsightVisualStyle` 保留角色、
尺寸及局部样式。`applicationStyleSheet()` 与 `chromeStyleSheet()` 仍有调用或测试。
编辑器、圆环菜单和 Insight 图视图保留各自渲染器，不再注册子树样式修复过滤器。
模型、选择、输入法、无障碍及业务状态仍由 Qt 和应用对应对象负责。

状态优先级为禁用优先，按下/选中优先于悬停，键盘焦点叠加在可用状态；只读不是禁用。
尺寸使用 `InsightDensityTokens`、`InsightFocusTokens` 与 `UiTypography::Role`。
最低高度须容纳字体、内边距与边框；布局尊重 `sizeHint`/`minimumSizeHint`，不能靠
缩小字体隐藏溢出。滚动内容应可达，菜单和弹出列表应受屏幕可用区域约束。
这些是验收规则，不声称所有字面尺寸都已转换为 token。

| 控件 | 应用边界与验证入口 | 剩余边界 |
| --- | --- | --- |
| 输入框与下拉框 | Ela 控件负责外观；Qt 负责编辑、选区、输入法和提交；`ela_controls_test`、`ui_dialog_contract_test` | 只读/错误/禁用组合、屏幕边缘弹出几何需专项证据 |
| 列表、表格与树 | 应用模型和委托负责内容、选择及编辑；`ui_item_menu_contract_test`、高 DPI 布局检查 | 不能从单一状态截图推定全部组合 |
| 滚动条 | Ela 动效与 Qt range/pageStep；`ela_motion_test` | 原生拖动与跨屏手感不能由离屏结果推出 |
| 文档标签 | ElaTabBar 与 EditorSplitController；`ela_document_tabs_test`、`compact_layout_test` | 物理拖放与混合 DPI 需独立验证 |
| 菜单与提示 | 应用 QAction、菜单策略与 Qt 事件；圆环菜单保留专业交互 | 子菜单边缘、真实提示换行和延时需专项证据 |
| 分隔条与面板 | Qt 布局和应用停靠控制器；面板及布局契约测试 | 原生连续拖动、指定插入线与多显示器仍有缺口 |
| 标签与文字角色 | UiTypography 与应用角色；`ui_label_contract_test`、`ui_text_view_test` | 全部主题的长译文、链接焦点及对比度未完整验收 |

## 动效场景规格（2026-09-20）

以下保留既有场景规则与其证据边界。`--no-ui-animations` 经主题管理器控制已接入的
侧栏、底栏、Context、页面切换和起始页动效；部分 Ela 控件内部动效仍独立运行。

| 场景 | 时长与曲线 | 中断与输入 | 减少动画边界 | 验证范围 |
| --- | --- | --- | --- | --- |
| ZeroSlack 左侧栏 | 255 ms，OutCubic，独立合成 | 从当前进度反向；普通输入先结束过渡；Ela 不逐帧重排编辑器 | `setExpanded(..., false)` 或全局动画偏好关闭时即时；未自动读取系统偏好 | Ela 四档 DPI 开合、输入与像素端点已验证；Windows 原生数据见 `ela-migration.md`，不等同于逐帧呈现测量 |
| ZeroSlack 底部工具抽屉 | 255 ms，OutCubic，内容纵向合成 | 按钮条固定；反向点击沿当前进度继续，普通输入先结束过渡 | 局部开关或全局动画偏好关闭时即时；恢复布局即时；旧 140 ms 高度动画已删除 | `ui_panel_motion_test` 四档 DPI 验证反向、按钮位置、编辑器几何和输入接管；原生证据见 `ela-migration.md` |
| Ela Context 整栏与内部区块（0.29.39） | 255 ms，OutCubic；整栏平移，区块平移并裁切 | 同面板连续反向，不同面板先结束前一过渡；不缩放文字 | 恢复、浮动／标签化停靠、资源删除及全局动画偏好关闭时即时 | `ui_panel_motion_test` 四档 DPI 验证保留视图、快速切换、重排和销毁；不将系统合成刷新率当成应用帧率 |
| Files/Design/Pinloom 条目底纹过渡候选 | 100 ms，OutCubic | 从当前颜色重定向，不重启固定起点，不阻塞选中 | 未来开关关闭后即时；当前底纹仍即时 | 仅规格未实现；不得把现有静态 hover 规则计作过渡通过 |
| 信息浮窗淡入/关闭候选 | 打开 100 ms、关闭 60 ms，OutCubic | 交互区立即可用，重复开关从当前透明度反向；关闭取消挂起任务 | 未来开关关闭后即时；不得淡化文字与内容独立造成透明度叠乘 | 仅规格未实现；原生材质与此淡入规格分开验收 |
| 圆环菜单及子项条候选 | 80 ms，OutCubic | 立即命中，不延迟关闭；从当前透明度反向 | 未来开关关闭后即时；禁止恢复矩形阴影 | 仅规格未实现；当前菜单行为保持原样 |
| Activity 数字提示、复制成功指示候选 | 提亮 120 ms / OutCubic；成功勾号保留 800 ms 后即时恢复 | 新事件重置提示期限，不能排队积累闪烁 | 关闭装饰动画后仍直接显示语义结果；不丢计数与成功反馈 | 仅规格未实现；没有计作已覆盖 |


离屏图像采样不是 DWM 呈现时间戳，不用于宣称原生帧率或流畅度。上表候选仍未实现。
控件与布局测试保留实际交互和断言；样式字符串存在、截图生成或局部像素相等均不能
代替完整交互与桌面验收。原生待验项目见 [手工检查清单](unverified-manual-checks.md)。
