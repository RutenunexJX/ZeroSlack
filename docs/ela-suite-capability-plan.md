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
| ZeroSlack | E:/ZeroSlack/ZeroSlack | 当前任务 | 1c7194c／0.31.11，已推送并安装 |
| Pinloom | E:/Pinloom/Pinloom | Pinloom执行侧 / 01a04d23-c1b6-7b50-b7dd-b1b23fc3be53 | 02b29c4／0.4.8，已推送并安装 |
| RegMapWorkbench | E:/RegMapWorkbench/RegMapWorkbench | RegMapWorkbench执行侧 / 01a03836-e5e3-77e2-bec8-0498d5526ef3 | 6420730／0.3.5，已推送并安装 |
| WaveWorkbench | E:/WaveWorkbench/WaveWorkbench | WaveWorkbench执行侧 / 01a038a2-4113-7e10-ab72-e4584cec0190 | 73a0162／0.12.0，已推送并安装 |
| SimDock | E:/SimDock/SimDock | 开发 Questasim 集成套件 / 01a0ce40-2180-75e1-b773-b174a4dcf387 | 2b90166／0.2.0，已推送并安装 |
| xIPs | E:/xIPs/xIPs | 查看本地 xIPs 应用 / 01a0ce6b-4f79-7151-a9bd-64ea81f6c3bc | e0100ff／2.2.1，已推送并安装 |

初始 ZeroSlack HEAD：c482aab。未跟踪的 docs/questa-suite-plan.md 属于其他任务，保留。
用户明确指定表内五个对应执行任务，后续直接续接，不另建任务。
实施期间不 amend 共享分支上的其他任务提交。跨应用发布由本任务最后串行进行，避免同时改写清单。

## 验证与发布台账

- [x] ZeroSlack 能力矩阵与剩余适配
- [x] ZeroSlack 性能前后对照
- [x] 五应用正式实施分派与结果审计
- [x] 六应用功能回归、版本与许可检查
- [x] 六应用提交与远端一致性
- [x] 六个正式包与 AppSuite 清单替换及校验

## ZeroSlack 实际组件与职责

| 界面 | 实际组件／行为 | 应用保留内容 |
| --- | --- | --- |
| 通用输入、按钮、数字、选择 | ElaLineEdit、ElaPushButton、ElaToolButton、ElaSpinBox、ElaDoubleSpinBox、ElaCheckBox、ElaRadioButton、ElaSlider | 字体、最小尺寸、英文菜单、校验和命令 |
| 下拉选择 | ElaComboBox 的高度／位置及指示器动画；输入立即中断 | 选项模型、业务提交；关闭不等待动画 |
| 普通树、表、列表 | ElaTreeView、ElaTableView、ElaListView；依赖 item API 的 Qt 容器使用 Ela 官方 style/delegate | role、复选状态、模型、排序与导航 |
| 滚动和展开 | ElaScrollBar 平滑滚轮，160 ms；Qt/Ela 树展开 | 普通视图按像素路由；文本保持 Qt 行单位；程序定位和新输入即时生效 |
| 通用只读文字 | ElaPlainTextEdit | Qt 标准操作与英语标签、复制、只读、业务日志 |
| 页面和标题 | ElaCentralStackedWidget、ElaAppBar、ElaDockWidget、ElaToolBar | 页面生命周期、紧凑图标、窗口及资源状态 |
| 菜单、对话框、提示 | ElaMenu 的可中断 160 ms 展开、ElaContentDialog、ElaToolTip，以及现有反馈适配器 | 确认／取消含义、校验、焦点恢复、提示生命周期 |
| 专业内容和系统能力 | 专用编辑器／图画布、Qt QSplitter、系统文件选择 | 编辑精度、撤销、坐标、工作区、数据和文件格式 |

不添加无业务用途的日历、轮播、Ribbon、状态栏等入口。径向菜单和可交互符号卡片不改成
普通悬浮提示。Ela 没有 QSplitter 对应类，不能把 Qt 分隔布局列为 Ela 原生组件。
适配扩展记录在补丁 26–30；组件存在不等于应用所有权也应由它接管。
补丁 27 恢复此前 native-item 路径绕过的菜单展开，同时保留 Qt 菜单语义。
菜单动作、主题、尺寸变化和输入会终止过渡；含实时编辑控件的 QWidgetAction 菜单不使用快照。

## ZeroSlack 验证和性能

补丁 26 的 25 组 CTest 全部通过（79.70 秒）；加入补丁 27 后再次 25/25 通过（84.60 秒），
覆盖 100%／200% 控件、树、菜单、对话框、
侧栏、底栏、动效、主窗口、浮窗、编辑背景，以及托管 Tab、设置与 Context 工作区。
Context 工作区保留 238 项检查。隐藏 Windows HWND 的无边框命中与 Acrylic 检查在
100%／200% 均为 4/4，通过过程中未显示窗口、抢焦点或操作桌面鼠标。
补丁 26 对 c482aab 的 13 个供应商源文件重放一致；补丁 27 对 89eea24 的 3 个文件逐字节一致。
许可文本保持不变。共享 xIPs ABI 要求 `ela=454cac2d-p27`，实际正式 DLL 加载验证在发布前执行。

xIPs 新增聚焦列表销毁用例发现 ElaListView 提前释放 style。补丁 28 合入其已验证修复，
主任务增加裸列表／树／表聚焦销毁回归；100%／200% 控件、导航和弹出 6/6 通过（22.55 秒）。
因此 0.31.10 暂存候选不部署；既有提交与 tag 保持不变，最终正式包递增至 0.31.11。

WaveWorkbench 颜色选择器回归发现 overlay 滚动条仍引用被替换的源滚动条。补丁 29
使用 QPointer 和销毁清理，三个文件重放逐字节一致；源条替换、延迟销毁、缩放与抓图
回归通过。Pinloom 联测进一步发现 QPlainTextEdit 使用行单位，不能直接套用像素增量；
普通只读文本的精确滚动交由 Qt 处理，角度滚轮继续使用 Ela 动画。两档 DPI 下均逐事件
对比原生 Qt 的横纵位置和最终文本。专业代码编辑器保持独立滚动语义。
合入补丁 29 和文本滚动修复后，25 组完整相关回归再次全部通过（82.25 秒）。

RegMap 的弹层终态截图进一步发现，Qt 算出的弹层高度未包含 Ela 的 6 px 底部留白，
ZeroSlack、xIPs 和 SimDock 均复现首末选项裁切。补丁 30 按本次 Qt 高度补齐留白并
约束屏幕边界，已可见时只结束过渡，不重复增加高度。原失败用例保留；1／3／5 个
选项、首末选择、反复关闭重开及可见时再次 show 均验证完整可见且尺寸稳定。
ZeroSlack 两档 DPI 下控件／弹出／对话框 6/6 通过（24.99 秒）；各应用增量更新候选包。

实际 xIPs 插件已使用 ZeroSlack 干净 Release 暂存中的核心及 Ela DLL 验证，100%／200%
均为 4/4，覆盖原生视图创建、使用、导出哈希、引用及工作区关闭，并断言创建和销毁
插件不改变宿主应用字体和调色板。最终 1c7194c／e0100ff 的正式 DLL 又完成相同两档
4/4 联测，ABI 保持 p27，双方实现补丁均为 30。

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
`ela-suite-final-regression.txt`、`ela-suite-p27-regression.txt`、`ela-suite-hidden-native-{1,2}.txt`、
`ela-suite-patch26-provenance.json`、`ela-suite-patch27-provenance.json`。
后台事件间隔不代表显示器帧率。真实 OLE 拖放、跨屏混合 DPI、合成器观感和结束闪动仍没有
桌面鼠标工具验收，不能据此声明全部视觉验收通过。

## 六应用最终交付（2026-09-24）

正式根目录为 `E:/PinloomRoot/AppPackage/AppSuite/Apps`，直接更新既有应用目录；
无 ZIP、无旧正式包备份。共享 `suite-manifest.json` 和 `SHA256SUMS.txt` 已更新。
Runtime 保持 1.0.1，Toolchain 及其他校验项保留。475 个套件校验项全部通过，六应用
版本、源码提交和发布元数据一致。ZeroSlack 两处桌面快捷方式仍指向正式目录，
`build/packages/ZeroSlack-win64` 也已更新为同一 0.31.11 包。

| 应用 | 正式子目录 | 源码提交 | 回归与交付证据 |
| --- | --- | --- | --- |
| ZeroSlack 0.31.11 | ZeroSlack-win64 | 1c7194c803a85b71327a8ef8d734c68e7250efbb | 25 组相关回归；补丁 30 定向 6/6；正式 GUI／CLI 启动、xIPs 两档宿主各 4/4 |
| Pinloom 0.4.8 | Pinloom | 02b29c49a56db66c5bc3709efe2ca538ec993d0d | 全量 28/28；增量 13/13；正式 UI／SQLite／PDF helper 自检；2 个可选真实 PDF probe 未执行 |
| RegMapWorkbench 0.3.5 | RegMapWorkbench | 6420730d0470b8a60ac2d6910ae663aad619816f | 全量 10/10、GUI 168/168；增量 3/3；16 组主窗和 120 状态图；隔离 CLI 生成／校验 |
| WaveWorkbench 0.12.0 | WaveWorkbench | 73a0162a404005ec62676bb959aab8e9168c78df | 全量 122/122；增量 10/10；暂存运行时 10 组、正式目录启动通过 |
| SimDock 0.2.0 | SimDock | 2b90166d6c7a2d747fcdf91b59208abd9d45b410 | 核心 15 通过、2 个真实 Questa 用例跳过；两档 UI 各 21 通过；正式目录渲染／Suite descriptor 验证 |
| xIPs 2.2.1 | xIPs | e0100ffd923e64d119ffcb3cb6a6d6ac59b23379 | 两档全量各 8/8；增量各 5/5；正式 GUI／CLI 启动和实际宿主联测通过 |

本任务逐仓库核对了本地和远端提交。ZeroSlack 发布标签为 v0.31.11，SimDock 为
v0.2.0，xIPs 为 v2.2.1；既有 v0.31.10／v2.2.0 标签未改写。其他三应用以提交号
标识此包，未新建标签。所有包保留 MIT／OFL、其他运行时许可及可重放的供应商补丁。
Pinloom 的发布元数据原在候选目录外，统一安装时原样放入应用目录并生成对应相对路径
清单，应用文件与其已验证候选逐字节一致。

发布过程中云同步目录从 CLOUD_6 变成 CLOUD_2，初次目录替换在 ZeroSlack 完成后
中断。扩大标记范围的重试被自动审批拒绝；最终采用经过预检的逐文件受管更新，
仅覆盖与旧清单哈希一致的包文件并移除 10 个旧 Pinloom 受管文件，没有递归删除
其余正式目录，也未覆盖用户修改。清理了本次留下的空 incoming 目录。正式目录
最终 8 组启动检查通过；Wave 的 offscreen 插件仅用于包外测试夹具。

性能对照使用固定数据和窗口，以下为 CPU 分派／查询中位耗时，不是桌面 FPS：

| 应用／场景 | 修改前 → 后 |
| --- | --- |
| ZeroSlack 4K 右栏／底栏 | 28.28 → 3.58 ms／27.97 → 2.60 ms |
| Pinloom 2000 条 Clip，1000×700 | 12049.79 → 152.94 ms |
| RegMap 1000 寄存器搜索，960 宽 | 1.7619 → 0.6051 ms |
| SimDock 1500 文件工程切换，100% | 28.3584 → 0.8323 ms |
| xIPs 2000 资产／64 文件过滤，100% | 18.2163 → 0.9411 ms |

并非所有路径提速：Wave 连续动画较即时基线增加重绘和 CPU；SimDock 勾选没有改善，
日志合批降低提交耗时但将完整显示延迟从约 13 ms 增至 27–33 ms；RegMap Results
部分窗口的耗时略升。这些原始数据及限制均保留在各应用交付文档。

总回执位于 `build/ela-migration/ela-suite-staging-plan.installed.json`、
`ela-suite-installed-verification.json`、`ela-suite-installed-smoke/receipt.json` 和
`ela-suite-final-xips-host.json`。各应用明细见各自 Ela 能力文档及交付目录的验证回执。
真实桌面拖拽、混合显示器 DPI、全局热键／托盘、live PDF 和 Questa 授权环境未在
本轮后台验证中执行；专业画布、模型、工作区和文档生命周期保留应用自身职责。
