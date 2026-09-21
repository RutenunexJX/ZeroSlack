# ZeroSlack Ela 实际迁移

分支：`codex/zeroslack-ela-migration`，基线 `3164377`。

用户在 2026-09-21 明确要求开始移植。本轮从实际应用接入 Ela，不再以此前
对照实验中的主观流畅度观察作为实施前置条件；这不改变历史报告的测量结论。

## 已接入范围

- `ApplicationThemeManager` 增加 Ela 后端，启动前固定选择。
- `UiControls` 在欢迎页、设置中心、编辑器外观设置、工作区配置、文件导航、全局命令面板、
  Problems、Activity、工作区中心、上下文 Peek/停靠面板和临时编辑器工具栏中创建真实 Ela 控件，
  共接入 60 处创建位置。
- 八类控件：按钮、工具按钮、输入框、下拉框、复选框、整数/小数步进框、滑块。
  现有业务代码仍通过 Qt 基类连接信号和访问值。
- 六套主题映射到 Ela 色彩表；移除这些控件上的通用输入框/按钮 QSS 覆盖。
- 保留窗口管理、编辑器、文件/设计树模型、专业图画布、专用分析逻辑和现有图标。
  本轮不是将全部 UI 替换为 `ElaWindow`，也没有改写专业图。

## 兼容处理

Ela 初始化发生在任何控件创建之前，初始化后恢复应用字体和原生窗口属性。
未接入全局递归事件过滤器；专业视图无需被逐个保护。

适配层释放上游固定宽高，保留响应式布局。按钮补齐 QIcon、助记符、键盘按压、
checked、焦点和默认按钮反馈。主题刷新重设按钮缓存色彩，支持同为浅色/深色的
不同 Catppuccin 主题切换。下拉框使用 Qt 的可立即关闭生命周期和 Ela 绘制，
避免上游动画期间不能关闭的问题。

销毁回归发现工具按钮、下拉框及步进框在 Qt 完成窗口关闭前释放局部样式，
关闭事件可能访问失效指针。补丁在释放前解除自身、下拉视图及内嵌输入框的
样式引用。八类控件均加入可见状态下直接销毁的回归检查。

## 构建与运行

使用 Qt 6.10.2 / MinGW 13.1.0，新建构建目录，并设置：

```powershell
cmake -S . -B build/ela-migration -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DZEROSLACK_ENABLE_ELA=ON `
  -DZEROSLACK_ENABLE_SUITEUI=OFF `
  -DZEROSLACK_ENABLE_QLEMENTINE=OFF
cmake --build build/ela-migration --parallel 4
ctest --test-dir build/ela-migration --output-on-failure
```

Qt、MinGW、Ninja、Python 路径需按本机配置；首次编译所需 fmt 可使用本地缓存。
Ela 使用 Qt 私有头，CMake 强制检查 Qt 6.10.2。更换 Qt 版本需重新验证。

运行 `build/ela-migration/ZeroSlack-Ela.exe`，默认使用 Ela。
`--ui-style=classic` 在相同隔离配置中使用原控件，切换后端需重启。

通过 `scripts/package-ela.ps1` 打包到新的独立目录；普通 `package-release.ps1`
会拒绝 Ela 构建。配置位于 `ZeroSlack/ZeroSlack-Ela` 的 AppConfigLocation，
近期工作区、编辑器配置和会话存储与经典版隔离。用户显式打开同一工程时，
工程本地设置和源文件仍是同一份。

## 源码与许可

Ela 固定提交 `454cac2d57a47d3cc28577dc817793aec1881ca7`，源码、MIT 完整文本、
上游 README、字体 SIL OFL 1.1 以及可重建补丁均保存在 `thirdparty/elawidgettools`。
独立包包含对应许可证和补丁。详见 `THIRD-PARTY-NOTICES.md`。

## 验证

2026-09-21 已完成实际应用、独立 DLL 与测试目标的 Release 构建。

| 检查 | 结果 | 本地证据 |
| --- | --- | --- |
| Ela 实例、键盘、图标、下拉框关闭、主题/编辑器字体隔离及可见控件销毁 | 通过，包含在下述 29 项内 | `build/ela-migration/ctest-final.log` |
| 设置、文件导航、CLI、面板、编辑器分屏、上下文与果核图，以及四档 DPI 布局/交互回归 | 29/29 通过，35.78 秒 | `build/ela-migration/ctest-final.log` |
| 欢迎页改用 Ela 工具按钮后的相关主窗复测 | 8/8 通过，16.93 秒；是上述测试的复测 | `build/ela-migration/welcome-ctest.log` |
| 0.29.25 发布构建后的版本、主窗、CLI 与源码规则检查 | 6/6 通过，13.12 秒 | `build/release-ela-0.29.25/verification.log` |
| Windows 原生设置页、下拉框 Escape 关闭、Light/Dark 切换 | 通过 | `build/ela-migration/native-evidence/settings-light.png` |
| Files 连续打开 `counter.sv`、`top.sv`，检查等宽字体、语义提示和最大化 | 显示正常，未崩溃 | `build/ela-migration/native-fixture/` |
| 包内程序与构建产物 SHA-256、Ela 许可及七份补丁 | 一致、齐全 | 包内 `SHA256SUMS.txt`、`licenses/ElaWidgetTools/` |

四档 DPI 为 100%、125%、150%、200%。高 DPI 审计沿用已接受的滚动视口验收：
普通控件不得重叠、越界或小于 minimumSizeHint；滚动内容允许裁切，但须可滚动到达。
本轮执行的是相关回归集，没有重跑全部历史测试，也没有新增帧率或延迟基准测量。

## 正式包与后续范围

初次原生验证使用独立预览包 `build/packages/ZeroSlack-Ela-0.29.24/ZeroSlack-Ela.exe`，
当时的 `build-info.json` 标记 `dirty: true`；该预览记录不代表正式发布提交。

2026-09-21 用户授权提交推送并打包正式包，版本更新为 `0.29.25`，Ela 发布标签为
`ela-v0.29.25`。本分支使用固定目录名 `ZeroSlack-Ela-win64`，正式发布位置为
`E:/PinloomRoot/AppPackage/AppSuite/Apps/ZeroSlack-Ela-win64/ZeroSlack-Ela.exe`，
归档为同级 `ZeroSlack-Ela-win64.zip`。经典正式包及其快捷方式保留。

`scripts/package-ela.ps1 -Formal` 要求源码已提交、工作区干净且生成版本匹配。
包内 `build-info.json` 记录正式提交、分支、`channel: formal`、`dirty: false` 及发布标签；
`SHA256SUMS.txt` 用于核对分发文件，完整 Ela 许可和七份补丁随包提供。

本批完成基础控件接入；导航树、标签页、通用对话框以及剩余专业面板工具栏仍待
逐项迁移。窗口壳保持现有实现，是否替换另行评估。数据模型、编辑器和专业图画布
继续保留，每次迁移按实际交互验证，不以控件类名替换数量作为完成标准。
