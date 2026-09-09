# 四应用文档清理完成记录

审计日期：2026-09-09；实施完成日期：2026-09-10。原实施清单已执行。四个仓库保持独立，版本不因本次文档清理升级。

## 状态基线

| 应用 | 清理前 main HEAD | 版本 |
|---|---|---|
| ZeroSlack | e1c340ec | 0.22.0 |
| Pinloom | 29c02ef | 0.4.0 |
| WaveWorkbench | 305d3e0 | 0.12.0 |
| RegMapWorkbench | c305649 | 0.3.0 |

Workspace Hub、suite-context、PDF Viewer Adapter、Scenario 生命周期、语义主题和 RegMap 外部变化
逐项决策已作为现有能力记入文档。正式包清单与上述版本一致；本次没有重新打包或联网核验远端。

## 已实施

- 删除 ZeroSlack 四份 2026-08-12 归档记录及空归档索引。
- 删除四应用共八份已完成实施计划，先保存有效架构、交互、数据与 ABI 约束。
- 删除 ZeroSlack 已完成问题清单，保留现行编辑行为说明。
- 将三份计划式规范迁为 [suite protocol](suite-app-protocol.md)、[CLI](cli.md) 和
  [suite workflows](suite-workflows.md)；补充 [suite UI](suite-ui.md) 与 [Wave simulation](wave-simulation.md)。
- 精简四应用 PLAN/GOAL；Wave README 的交互说明拆入其 `docs/interaction.md`，并加入 Portable 安装清单。
- 修正 ZeroSlack README/PLAN/GOAL/包说明版本，补 0.22.0 changelog；用户手册注明源码核对基线和人工验收范围。
- 修正 Pinloom Schema 16、Adapter 和 annotated-copy 高亮说明。
- 删除 Wave 已失效的 Scenario 生命周期、仅深色主题和仿真未接 UI 限制；补 CLI 操作字段及例子。
- 修正 RegMap 发布版本示例和旧 reset-domain 同步描述；区分 disk/CLI 决策与 managed RTL 三方合并。
- 保留 Wave 长列表添加按钮不常驻等仍成立的限制，保留有效接口、许可证及测试说明。

旧文件共移除 17 个路径，其中 3 份规范迁至新路径，其余 14 份为历史计划、归档或已完成问题记录。
历史全文可由各仓库上表的 Git 基线恢复，不再复制到新的归档目录。

## 测试材料清理

用户已明确解除此前保护清单。删除旧截图 `current_app_signal_usage_hotspot_full_after.png` 和
`test_sv/huge_prj/.zs`、`test_sv/new/.zs`；将 `chl_ctrl.sv` 的格式测试改动和 RegMap
`examples/minimal/.regmap.yaml`、`examples/minimal/rtl/minimal_registers.sv` 的手工试验状态恢复到已提交基线。
保留这些已跟踪示例文件，避免破坏现有测试和文档引用；不把临时试验提交为正式产品变更。

## 验证与范围

- ZeroSlack 现有 `version_documentation_guard.ctest` 通过：版本一致为 0.22.0。
- 核对变更 Markdown 的本地链接及行尾空白；修复 Wave 手册迁移后的相对链接。
- 从实际 CMake 源提取 Portable 文档安装块，在独立临时目录配置和安装八份文档。
- 使用已发布的 Wave CLI 创建独立临时工程并执行文档中的 Scenario rename dry-run，结果为 ok、written=false。
- 逐仓检查旧文件引用和 `git diff --check`。源码逻辑未变，未重新运行全部应用测试。
- 文档基线与历史测试记录明确区分；本次未操作用户运行中的应用或正式包。
