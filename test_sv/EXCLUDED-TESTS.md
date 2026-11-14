# 受管测试执行策略

本清单排除的是默认运行，不排除编译；硬性访问禁令独立生效。

由 scripts/test_policy.py 从 test-execution-policy.json 生成，不手工维护规则。
裸 ctest 与手动直接运行程序不等于受管默认集合；注册包装层仅能约束通过该注册启动的命令。
受管默认入口：python scripts/release_gate.py test --config <构建目录>/managed-test-config.json --output <新证据目录>。
在当前任务另有授权时，--names <准确名称数组 JSON> 可显式选择默认排除项；硬性限制仍拒绝。发布准备不接受此选择参数。
当前授权范围：20261029-stage-d-release-gates。准确受限变体仅在该任务及复核期限内有效，不能作为后续任务的永久授权。

| CTest 注册 | 执行 | 责任 | 原因 | 复核期限 |
|---|---|---|---|---|
| action_registry_test | run | action_registry_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| analysis_scheduler_test | run | analysis_scheduler_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| annotation_layer_test | run | annotation_layer_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| automation_service_test | run | automation_service_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| bottom_panels_ui_test | run | bottom_panels_ui_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| build_inputs_contract_test | run | build_inputs_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| compact_layout_test | required | compact_layout_test | Complete compact layout contract, all modes/states retained; actual DPR evidence required. | 2026-11-06 |
| compact_layout_test_1.5 | required | compact_layout_test | Complete compact layout contract, all modes/states retained; actual DPR evidence required. | 2026-11-06 |
| compact_layout_test_2 | required | compact_layout_test | Complete compact layout contract, all modes/states retained; actual DPR evidence required. | 2026-11-06 |
| completion_self_contained_test | run | completion_test | Exact self-contained variant authorized only for phase D. | 2026-11-06 |
| completion_test | restricted | completion_test | Full command is not authorized; only the exact phase D self-contained variant is allowed. | 2026-11-06 |
| context_workspace_125pct_dpi_test | run | context_workspace_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| context_workspace_150pct_dpi_test | run | context_workspace_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| context_workspace_200pct_dpi_test | run | context_workspace_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| control_style_interaction_1.25_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| control_style_interaction_1.5_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| control_style_interaction_1_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| control_style_interaction_2_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| controller_boundary_test | run | controller_boundary_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| crash_recovery_service_test | run | crash_recovery_service_test | Registered test retained in the default safe set; runtime failures are not exclusions. 恢复数据由测试显式临时目录管理；默认根路径仅用于只读断言，不注入恢复根目录覆盖。 | 2026-11-06 |
| declaration_identity_contract_test | run | declaration_identity_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| declare_signal_fact_collector_test | run | declare_signal_fact_collector_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| declare_signal_proposal_test | run | declare_signal_proposal_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| delayed_consumer_generation_test | run | delayed_consumer_generation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| design_inline_selection_test | run | design_inline_selection_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| diagnostic_panel_contract_test | run | diagnostic_panel_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| document_persistence_generation_test | run | document_persistence_generation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| domain_contract_test | run | domain_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editing_time_contract_test | run | editing_time_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_annotation_settings_test | run | editor_annotation_settings_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_background_1_test | run | editor_background_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_background_2_test | run | editor_background_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_context_menu_test | run | editor_context_menu_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_file_identity_test | run | editor_file_identity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_incremental_self_contained_test | run | editor_incremental_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_incremental_test | restricted | editor_incremental_test | Original variant accesses prohibited resources and may wait after a failed prerequisite. | 2026-11-06 |
| editor_input_boundary_contract_test | run | editor_input_boundary_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_insight_target_pick_test | run | editor_insight_target_pick_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_line_operation_test | run | editor_line_operation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_mode_controller_test | run | editor_mode_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_multicursor_controller_test | run | editor_multicursor_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_paste_transaction_test | run | editor_paste_transaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_radial_menu_test | run | editor_radial_menu_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_source_navigation_test | run | editor_source_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_split_controller_test | run | editor_split_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_structural_input_test | run | editor_structural_input_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_template_slot_controller_test | run | editor_template_slot_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_view_local_folding_test | run | editor_view_local_folding_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| editor_visible_region_perf_test | run | editor_visible_region_perf_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| effective_value_test | run | effective_value_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_context_floating_background_1_test | run | context_floating_background_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_context_floating_background_2_test | run | context_floating_background_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_context_floating_window_1_test | run | context_floating_window_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_context_floating_window_2_test | run | context_floating_window_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_context_workspace_test | run | context_workspace_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_document_tabs_test | run | ela_document_tabs_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_controls_1.25_test | run | ela_controls_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_controls_1.5_test | run | ela_controls_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_controls_1_test | run | ela_controls_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_controls_2_test | run | ela_controls_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_motion_1.25_test | run | ela_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_motion_1.5_test | run | ela_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_motion_1_test | run | ela_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_motion_2_test | run | ela_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_navigation_1.25_test | run | ela_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_navigation_1.5_test | run | ela_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_navigation_1_test | run | ela_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ela_navigation_2_test | run | ela_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_graph_export_panel_integration_test | run | graph_export_panel_integration_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_high_dpi_layout_1.25_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_high_dpi_layout_1.5_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_high_dpi_layout_1_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_high_dpi_layout_2_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_instance_pair_connection_panel_test | run | instance_pair_connection_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_instance_pair_connection_workflow_test | run | instance_pair_connection_workflow_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_kernel_view_continuity_test | run | kernel_view_continuity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_main_window_1.25_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_main_window_1.5_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_main_window_1_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_main_window_2_test | run | control_style_interaction_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_motion_classic_test | run | ela_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_multi_signal_propagation_panel_test | run | multi_signal_propagation_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_navigation_switch_test | run | navigation_switch_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_panel_layout_controller_1.25_test | run | panel_layout_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_panel_layout_controller_1.5_test | run | panel_layout_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_panel_layout_controller_1_test | run | panel_layout_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_panel_layout_controller_2_test | run | panel_layout_controller_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_pinloom_context_provider_test | run | pinloom_context_provider_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_rtl_high_risk_edit_panel_test | run | rtl_high_risk_edit_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_rtl_insight_workbench_test | run | rtl_insight_workbench_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_scoped_replace_workflow_test | run | scoped_replace_workflow_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_scoped_search_panel_test | run | scoped_search_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_settings_center_panel_test | run | settings_center_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_signal_usage_hotspot_panel_test | run | signal_usage_hotspot_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_dialog_contract_1.25_test | run | ui_dialog_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_dialog_contract_1.5_test | run | ui_dialog_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_dialog_contract_1_test | run | ui_dialog_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_dialog_contract_2_test | run | ui_dialog_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_item_menu_contract_1.25_test | run | ui_item_menu_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_item_menu_contract_1.5_test | run | ui_item_menu_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_item_menu_contract_1_test | run | ui_item_menu_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_item_menu_contract_2_test | run | ui_item_menu_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_label_contract_1.25_test | run | ui_label_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_label_contract_1.5_test | run | ui_label_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_label_contract_1_test | run | ui_label_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_label_contract_2_test | run | ui_label_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_navigation_rail_1.25_test | run | ui_navigation_rail_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_navigation_rail_1.5_test | run | ui_navigation_rail_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_navigation_rail_1_test | run | ui_navigation_rail_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_navigation_rail_2_test | run | ui_navigation_rail_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_panel_motion_1.25_test | run | ui_panel_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_panel_motion_1.5_test | run | ui_panel_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_panel_motion_1_test | run | ui_panel_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_panel_motion_2_test | run | ui_panel_motion_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_popup_contract_1.25_test | run | ui_popup_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_popup_contract_1.5_test | run | ui_popup_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_popup_contract_1_test | run | ui_popup_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_popup_contract_2_test | run | ui_popup_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_sidebar_contract_1.25_test | run | ui_sidebar_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_sidebar_contract_1.5_test | run | ui_sidebar_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_sidebar_contract_1_test | run | ui_sidebar_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_sidebar_contract_2_test | run | ui_sidebar_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_text_view_1.25_test | run | ui_text_view_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_text_view_1.5_test | run | ui_text_view_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_text_view_1_test | run | ui_text_view_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_text_view_2_test | run | ui_text_view_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_window_chrome_1.25_test | run | ui_window_chrome_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_window_chrome_1.5_test | run | ui_window_chrome_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_window_chrome_1_test | run | ui_window_chrome_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_ui_window_chrome_2_test | run | ui_window_chrome_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ela_unified_peek_container_test | run | unified_peek_container_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| expose_signal_to_top_fixture_test | restricted | expose_signal_to_top_fixture_test | Original command references both prohibited local projects. | 2026-11-06 |
| expose_signal_to_top_gui_test | run | expose_signal_to_top_gui_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| expose_signal_to_top_test | run | expose_signal_to_top_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| external_document_sync_test | run | external_document_sync_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| formatter_cursor_anchor_test | run | formatter_cursor_anchor_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| formatter_tree_indentation_test | run | formatter_tree_indentation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| fsm_ui_snapshot_test | restricted | fsm_ui_snapshot_test | main probes test_sv/new and test_sv/huge_prj before loading the model (fsm_ui_snapshot_test.cpp:467-499). | 2026-11-06 |
| global_control_ow_self_contained_test | run | global_control_ow_test | Exact self-contained variant authorized only for phase D. | 2026-11-06 |
| global_control_ow_test | restricted | global_control_ow_test | Original command references prohibited local projects. | 2026-11-06 |
| graph_export_service_test | run | graph_export_service_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| gui_smoke_test | restricted | gui_smoke_test | Full GUI smoke is explicitly prohibited. | 2026-11-06 |
| high_dpi_layout_1.25_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| high_dpi_layout_1.5_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| high_dpi_layout_1_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| high_dpi_layout_2_test | run | high_dpi_layout_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insert_palette_catalog_test | run | insert_palette_catalog_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insertion_creation_contract_test | run | insertion_creation_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insight_visual_style_test | run | insight_visual_style_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insightgraph_consumer_test | run | insightgraph_consumer_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insightgraph_core_test | run | insightgraph_core_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insightgraph_migration_test | run | insightgraph_migration_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| insightgraph_render_test | run | insightgraph_render_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| instance_pair_connection_facade_test | run | instance_pair_connection_facade_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| jump_test | restricted | jump_test | runRealWorkspaceEnumDecorationRegression probes test_sv/new (jump_test.cpp:235-246). | 2026-11-06 |
| kernel_view_continuity_1.25_test | run | kernel_view_continuity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| kernel_view_continuity_1.5_test | run | kernel_view_continuity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| kernel_view_continuity_1_test | run | kernel_view_continuity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| kernel_view_continuity_2_test | run | kernel_view_continuity_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| large_file_perf_test | restricted | large_file_perf_test | Original command references prohibited local project and may wait after a failed prerequisite. | 2026-11-06 |
| legacy_field_policy_guard | run | cmake/legacy_field_policy_guard.ctest | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| live_insight_session_test | run | live_insight_session_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| live_insights_context_provider_test | run | live_insights_context_provider_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| live_insights_ui_snapshot_test | run | live_insights_ui_snapshot_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| module_diagram_1.25_test | run | rtl_insight_workbench_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| module_diagram_1.5_test | run | rtl_insight_workbench_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| module_diagram_2_test | run | rtl_insight_workbench_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| multi_signal_propagation_planner_test | run | multi_signal_propagation_planner_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| narrow_layout_test_1 | required | narrow_layout_test | Complete B/C narrow layout matrix: modes, states, themes, widths and resize/restore; actual DPR required. | 2026-11-06 |
| narrow_layout_test_1.5 | required | narrow_layout_test | Complete B/C narrow layout matrix: modes, states, themes, widths and resize/restore; actual DPR required. | 2026-11-06 |
| narrow_layout_test_2 | required | narrow_layout_test | Complete B/C narrow layout matrix: modes, states, themes, widths and resize/restore; actual DPR required. | 2026-11-06 |
| native_component_host_test | run | native_component_host_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| native_component_integration_test | excluded | native_component_integration_test | Loads the real external xIPs DLL runtime; its source explicitly forbids substituting the controllable fixture. Built in ALL, run separately with the reviewed native deployment; fixture-based host tests remain in the default set. | 2026-11-06 |
| navigation_history_contract_test | run | navigation_history_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| navigation_virtual_source_group_test | run | navigation_virtual_source_group_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| notification_center_test | run | notification_center_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| package_header_command_test | run | package_header_command_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| pinloom_code_link_store_test | run | pinloom_code_link_store_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| pinloom_host_transport_test | run | pinloom_host_transport_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| pinloom_hover_test | run | pinloom_hover_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| relationship_perf_test | restricted | relationship_perf_test | Original command references prohibited private project. | 2026-11-06 |
| relationship_test | restricted | relationship_test | main always calls runRealWorkspaceIncludeFixture, which probes and recursively reads test_sv/new; ZEROSLACK_SKIP_LOCAL_FIXTURES still probes before skipping (relationship_test.cpp:9920-9950,12171). | 2026-11-06 |
| relationship_value_access_contract_test | run | relationship_value_access_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rounded_icons_content_box_test | run | rounded_icons_content_box_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_action_coordinator_test | run | rtl_action_coordinator_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_connection_transform_planner_test | run | rtl_connection_transform_planner_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_insight_linkage_test | run | rtl_insight_linkage_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_insight_workbench_test | run | rtl_insight_workbench_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_rename_planner_test | run | rtl_rename_planner_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_source_identity_contract_test | run | rtl_source_identity_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtl_transform_workflow_test | run | rtl_transform_workflow_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_api_smoke_tests | run | rtleditcore_api_smoke_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_edit_plan_tests | run | rtleditcore_edit_plan_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_embedded_consumer_smoke | run | rtleditcore_embedded_consumer_smoke_bin | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_expose_signal_to_top_tests | run | rtleditcore_expose_signal_to_top_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_patch_engine_tests | run | rtleditcore_patch_engine_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_text_edit_tests | run | rtleditcore_text_edit_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| rtleditcore_workspace_edit_transaction_tests | run | rtleditcore_workspace_edit_transaction_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| scoped_replace_scope_contract_test | run | scoped_replace_scope_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| search_scope_service_test | run | search_scope_service_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| semantic_decoration_scheduler_test | run | semantic_decoration_scheduler_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| semantic_hot_path_test | run | semantic_hot_path_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| semantic_pipeline_contract_test | run | semantic_pipeline_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| semantic_source_generation_test | run | semantic_source_generation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| settings_center_test | run | settings_center_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| settings_dialog_test | run | settings_dialog_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| shared_core_policy_guard | run | cmake/shared_core_policy_guard.ctest | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| shared_core_runtime_test | run | shared_core_runtime_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| shared_document_test | run | shared_document_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| sidebar_mainwindow_test | run | sidebar_mainwindow_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| signal_trace_semantic_test | run | signal_trace_semantic_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| signal_trace_ui_test | run | signal_trace_ui_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| signal_usage_hotspot_panel_test | run | signal_usage_hotspot_panel_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_core | run | simdock_core_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_core_live | excluded | simdock_core_tests | Requires configured live Questa executable and its external simulation environment; pure SimDock registrations remain in the default set. | 2026-11-06 |
| simdock_legacy_compatibility | run | simdock_legacy_compatibility_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_scan_cache | run | simdock_scan_cache_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_storage | run | simdock_storage_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_ui | run | simdock_ui_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_ui_200 | run | simdock_ui_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_workbench_host | run | simdock_workbench_host_tests | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| simdock_workbench_host_live | excluded | simdock_workbench_host_tests | Requires configured live Questa executable and its external simulation environment; pure SimDock registrations remain in the default set. | 2026-11-06 |
| simulation_workspace_ui | run | simulation_workspace_ui_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| source_preview_contract_test | run | source_preview_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| source_structural_navigation_test | run | source_structural_navigation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| tb_editor_assistance | run | tb_editor_assistance_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| tb_task_templates | run | tb_task_templates_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| temporary_editor_context_provider_test | run | temporary_editor_context_provider_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| test_execution_policy_guard | run | scripts/test_policy.py | Same guard is mandatory in default ALL and default safe CTest execution. | 2026-11-06 |
| tree_sitter_compile_options_guard | run | cmake/tree_sitter_compile_options_guard.ctest | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ts_doc_test | run | ts_doc_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| ts_large_file_semantics_test | run | ts_large_file_semantics_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| unsaved_document_manager_test | run | unsaved_document_manager_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| user_template_range_contract_test | run | user_template_range_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| version_documentation_guard | run | cmake/version_documentation_guard.ctest | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| welcome_mainwindow_test | run | welcome_mainwindow_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| welcome_resource_test | run | welcome_resource_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_document_contract_test | run | workspace_document_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_file_operation_test | run | workspace_file_operation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_junction_contract_test | run | workspace_junction_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_path_confirmation_test | run | workspace_path_confirmation_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_persistence_test | run | workspace_persistence_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_session_coordinator_test | run | workspace_session_coordinator_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_switcher_test | run | workspace_switcher_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_virtual_source_group_ui_test | run | workspace_virtual_source_group_ui_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| workspace_watch_handoff_contract_test | run | workspace_watch_handoff_contract_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| xips_context_provider_test | run | xips_context_provider_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| zeroslack_cli_test | run | zeroslack_cli_test | Registered test retained in the default safe set; runtime failures are not exclusions. Git 文件名回归依赖配置阶段固定的 Git，受管入口在启动前核对其路径与哈希。 | 2026-11-06 |
| zeroslack_suiteapp_integration_test | run | zeroslack_suiteapp_integration_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |
| zeroslack_suiteapp_ipc_test | run | zeroslack_suiteapp_ipc_test | Registered test retained in the default safe set; runtime failures are not exclusions. | 2026-11-06 |

## 资源约束

| 路径 | 操作约束 | 复核期限 |
|---|---|---|
| test_sv/new | deny_access | 2026-11-06 |
| test_sv/huge_prj | deny_access | 2026-11-06 |
| test_sv/local_fixture_names.h | deny_access | 2026-11-06 |
| docs/questa-suite-plan.md | hash_only | 2026-11-06 |
| resources/templates/__pycache__ | preserve | 2026-11-06 |
| src/completion/codetemplateservice.cpp | preserve | 2026-11-06 |
