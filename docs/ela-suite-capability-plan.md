# Ela 原生能力与性能实施计划

状态：进行中。用户于 2026-09-24 授权先完成 ZeroSlack，随后协调 Pinloom、RegMapWorkbench、
WaveWorkbench、SimDock、xIPs 全部适用组件与原生交互能力，最后推送并更新六个正式包。
当前 goal 保持 active；只有全部阶段完成才结束。仅维护 Ela，不新建平行产品后端。

## 顺序与交付

1. ZeroSlack：核对实际 UI 与 Ela 接口；补齐折叠、右栏、底栏和并排布局，优化真实工作负载。
2. ZeroSlack：覆盖已有通用控件的键盘、焦点、菜单、滚动、弹出、拖动、状态和主题行为。
   原组件不适用于专业画布、文档模型或被用户删除的功能时，记录原因，不添加无用入口。
3. 验证 ZeroSlack 后，将经过验证的组件补丁与行为契约交给其余五个应用的执行任务。
4. 每个应用独立审计、实施、功能回归、性能检查、许可与补丁记录；主任务复核交付。
5. 所有应用完成后统一提交、推送、构建干净正式包；直接替换对应 AppSuite 目录，
   更新版本清单与校验，不生成 ZIP、不保留旧包备份，不改动用户数据。

## 完成标准

- 每个已有通用 UI 功能列出实际 Ela 类、启用的原生能力、应用保留的业务职责与验证证据。
- 不能把移入第三方目录的自定义动画称为上游原生能力；扩展必须解释原生流程和新增边界。
- 专业编辑器、模块图、热点图、状态图、波形/PDF/寄存器画布继续保留专用实现。
- 重复展开/收起、快速反向、隐藏、缩放、切换工作区、拖动取消、销毁和恢复不能遗失视图或状态。
- 性能使用相同窗口、内容与缩放比较前后结果；报告耗时、重绘/布局次数和快照范围，
  不将后台事件间隔当作桌面呈现帧率，不以删除回归或减少场景换取通过。
- 原生桌面鼠标操作前提醒用户；工具不可用时如实记录，不冒充真机验收。
- 维护 MIT、字体 OFL、上游版本和可重放补丁；正式包与提交一致，独立运行检查通过。

## ZeroSlack 当前缺口

| 项目 | 起点 | 本轮目标 | 状态 |
| --- | --- | --- | --- |
| 左栏 | ElaNavigationBar 显示模式／覆盖流程 | 原生宽度与覆盖位置动画；编辑器背景缓存避免每帧重新合成 | 已验证 |
| 右栏整体 | PanelCompositor | ElaDrawerArea，RightEdge；仅捕获内容区 | 已验证 |
| 底栏抽屉 | PanelCompositor；页面已有 ElaCentralStackedWidget | ElaDrawerArea，BottomEdge；保留页面切换和业务状态 | 已验证 |
| 面板折叠 | 自绘 section 和快照 | ElaDrawerArea 原生展开/收起，支持反向、输入和销毁 | 已验证 |
| 并排布局 | ContextDockHost 手工几何／分隔条 | Qt QSplitter 管理排列与调整，业务层保存顺序与尺寸 | 已验证 |
| Tab／浮窗 | ElaTabBar、ElaTabWidget、ElaDockWidget 已接入 | 所有权、拖放、取消、尺寸恢复与隐藏 HWND 检查 | 已验证；桌面实际拖放待验 |
| 通用控件 | UiControls 及部分兼容包装 | 下拉框恢复可中断原生动画，普通滚动与树展开默认启用 | 已验证 |

工作区隔离、未保存决策、共享文档生命周期及专业模型继续由 ZeroSlack 负责；这不限制
控件、布局与交互使用 Ela/Qt 的原生实现。

## 应用与协作记录

| 应用 | 仓库 | 现有执行任务 | 状态 |
| --- | --- | --- | --- |
| ZeroSlack | E:/ZeroSlack/ZeroSlack | 当前任务 | 本地实施 |
| Pinloom | E:/Pinloom/Pinloom | Pinloom执行侧 / 01a04d23-c1b6-7b50-b7dd-b1b23fc3be53 | 等待 ZeroSlack 验证 |
| RegMapWorkbench | E:/RegMapWorkbench/RegMapWorkbench | RegMapWorkbench执行侧 / 01a03836-e5e3-77e2-bec8-0498d5526ef3 | 等待 ZeroSlack 验证 |
| WaveWorkbench | E:/WaveWorkbench/WaveWorkbench | WaveWorkbench执行侧 / 01a038a2-4113-7e10-ab72-e4584cec0190 | 等待 ZeroSlack 验证 |
| SimDock | E:/SimDock/SimDock | 开发 Questasim 集成套件 / 01a0ce40-2180-75e1-b773-b174a4dcf387 | 等待 ZeroSlack 验证 |
| xIPs | E:/xIPs/xIPs | 查看本地 xIPs 应用 / 01a0ce6b-4f79-7151-a9bd-64ea81f6c3bc | 等待 ZeroSlack 验证 |

初始 ZeroSlack HEAD：c482aab。未跟踪的 docs/questa-suite-plan.md 属于其他任务，保留。
用户明确指定表内五个对应执行任务，后续直接续接，不另建任务。
实施期间不 amend 共享分支上的其他任务提交。跨应用发布由本任务最后串行进行，避免同时改写清单。

## 验证与发布台账

- [x] ZeroSlack 能力矩阵与剩余适配
- [x] ZeroSlack 性能前后对照
- [ ] 五应用正式实施分派与结果审计
- [ ] 六应用功能回归、版本与许可检查
- [ ] 六应用提交与远端一致性
- [ ] 六个正式包与 AppSuite 清单替换及校验

## ZeroSlack 实际组件与职责

| 界面 | 实际组件／行为 | 应用保留内容 |
| --- | --- | --- |
| 通用输入、按钮、数字、选择 | ElaLineEdit、ElaPushButton、ElaToolButton、ElaSpinBox、ElaDoubleSpinBox、ElaCheckBox、ElaRadioButton、ElaSlider | 字体、最小尺寸、英文菜单、校验和命令 |
| 下拉选择 | ElaComboBox 的高度／位置及指示器动画；输入立即中断 | 选项模型、业务提交；关闭不等待动画 |
| 普通树、表、列表 | ElaTreeView、ElaTableView、ElaListView；依赖 item API 的 Qt 容器使用 Ela 官方 style/delegate | role、复选状态、模型、排序与导航 |
| 滚动和展开 | ElaScrollBar 平滑滚轮，160 ms；Qt/Ela 树展开 | 精确触控板按像素路由；程序定位和新输入即时生效 |
| 通用只读文字 | ElaPlainTextEdit | Qt 标准操作与英语标签、复制、只读、业务日志 |
| 页面和标题 | ElaCentralStackedWidget、ElaAppBar、ElaDockWidget、ElaToolBar | 页面生命周期、紧凑图标、窗口及资源状态 |
| 菜单、对话框、提示 | ElaMenu、ElaContentDialog、ElaToolTip，以及现有反馈适配器 | 确认／取消含义、校验、焦点恢复、提示生命周期 |
| 专业内容和系统能力 | 专用编辑器／图画布、Qt QSplitter、系统文件选择 | 编辑精度、撤销、坐标、工作区、数据和文件格式 |

不添加无业务用途的日历、轮播、Ribbon、状态栏等入口。径向菜单和可交互符号卡片不改成
普通悬浮提示。Ela 没有 QSplitter 对应类，不能把 Qt 分隔布局列为 Ela 原生组件。
适配扩展记录在补丁 26；组件存在不等于应用所有权也应由它接管。

## ZeroSlack 验证和性能

最终 25 组 CTest 全部通过（79.70 秒），覆盖 100%／200% 控件、树、菜单、对话框、
侧栏、底栏、动效、主窗口、浮窗、编辑背景，以及托管 Tab、设置与 Context 工作区。
Context 工作区保留 238 项检查。隐藏 Windows HWND 的无边框命中与 Acrylic 检查在
100%／200% 均为 4/4，通过过程中未显示窗口、抢焦点或操作桌面鼠标。
补丁 26 对 c482aab 的 13 个供应商源文件重放一致；许可文本保持不变。

性能采用同一 MainWindow、5000 行 SystemVerilog、模块框图和每场景 12 次切换。
下表为 3840×2160、DPR 1；dispatch 是入口执行中位耗时，P95 是事件间隔，均为 ms。

| 场景 | dispatch 修改前／后 | 事件间隔 P95 修改前／后 |
| --- | --- | --- |
| 左栏 | 2.87 / 2.96 | 34.72 / 23.26 |
| 右栏 | 28.28 / 3.58 | 16.76 / 16.80 |
| 底栏 | 27.97 / 2.60 | 16.80 / 16.94 |
| 面板折叠 | 3.21 / 1.29 | 16.88 / 16.76 |

1000×700 与 2560×1392 也保留完整记录，前后窗口尺寸一致。只抓抽屉正文，单个快照
上限 32 MiB，反向复用，结束释放。编辑背景缓存预合成结果，仍保持原来的比例和右下角定位。
数据来源：`build/ela-migration/ela-suite-panel-before.json`、`ela-suite-panel-final.json`、
`ela-suite-final-regression.txt`、`ela-suite-hidden-native-{1,2}.txt`、`ela-suite-patch26-provenance.json`。
后台事件间隔不代表显示器帧率。真实 OLE 拖放、跨屏混合 DPI、合成器观感和结束闪动仍没有
桌面鼠标工具验收，不能据此声明全部视觉验收通过。
