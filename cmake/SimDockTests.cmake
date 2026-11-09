# Original SimDock cases compile against the owned implementation. The legacy
# window is test-only chrome; the real ZeroSlack lifecycle is tested separately.
find_package(Qt6 REQUIRED COMPONENTS Test)
# Preserve legacy source-generation regressions without shipping the retired xIPs command builder.
add_executable(tb_task_templates_test test_sv/tb_task_templates_test.cpp
    src/integrations/xips/xipshdlcallbuilder.cpp)
target_link_libraries(tb_task_templates_test PRIVATE zeroslack_core Qt6::Test)
target_include_directories(tb_task_templates_test PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
add_test(NAME tb_task_templates COMMAND tb_task_templates_test
    -o "${CMAKE_BINARY_DIR}/tb-task-templates.xml,junitxml" -o "${CMAKE_BINARY_DIR}/tb-task-templates.log,txt")
set_tests_properties(tb_task_templates PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
add_executable(tb_editor_assistance_test test_sv/tb_editor_assistance_test.cpp)
target_link_libraries(tb_editor_assistance_test PRIVATE zeroslack_core Qt6::Test)
target_include_directories(tb_editor_assistance_test PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
add_test(NAME tb_editor_assistance COMMAND tb_editor_assistance_test numericRules semanticInitializers semanticEnumScopes sharedEnumScopes enumScopeWindow existingTbEditingWorkflow
    -o "${CMAKE_BINARY_DIR}/tb-editor-assistance.xml,junitxml" -o "${CMAKE_BINARY_DIR}/tb-editor-assistance.log,txt")
set_tests_properties(tb_editor_assistance PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
add_executable(workspace_simulation_services_test test_sv/workspace_simulation_services_test.cpp)
add_executable(simulation_build_planner_test test_sv/simulation_build_planner_test.cpp)
target_link_libraries(simulation_build_planner_test PRIVATE zeroslack_core Qt6::Test)
target_include_directories(simulation_build_planner_test PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
zeroslack_qt_test_command(simulation_build_planner_command simulation_build_planner simulation_build_planner_test)
add_test(NAME simulation_build_planner COMMAND ${simulation_build_planner_command})
set_tests_properties(simulation_build_planner PROPERTIES TIMEOUT 120
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR}")
target_link_libraries(workspace_simulation_services_test PRIVATE zeroslack_core Qt6::Test)
target_include_directories(workspace_simulation_services_test PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
zeroslack_qt_test_command(workspace_simulation_services_command workspace_simulation_services workspace_simulation_services_test)
add_test(NAME workspace_simulation_services COMMAND ${workspace_simulation_services_command})
set_tests_properties(workspace_simulation_services PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
add_executable(simulation_workspace_ui_test test_sv/simulation_workspace_ui_test.cpp)
target_link_libraries(simulation_workspace_ui_test PRIVATE zeroslack_core Qt6::Test)
target_include_directories(simulation_workspace_ui_test PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
add_test(NAME simulation_workspace_ui COMMAND simulation_workspace_ui_test
    sourceFirstSingleOwnershipAndSessionRestore navigationSearchAndTransferredModalGuard
    existingTestbenchAndRetiredInputs logSelectionScrollAndHiddenDelivery hostVisualsAndMenus retiredCreationEntrypoints
    logLocationsNavigateWithoutOpeningOutsideWorkspace runWaitsForDiscoveryAndSupportsCancellation
    participatingDocumentSavesAndRevisionChecks participatingSaveConflictBlocksRun automationSharesTbPlanningContract
    -o "${CMAKE_BINARY_DIR}/simulation-workspace-ui.xml,junitxml" -o "${CMAKE_BINARY_DIR}/simulation-workspace-ui.log,txt")
set_tests_properties(simulation_workspace_ui PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
foreach(pair IN ITEMS "scan_cache;scan_cache_test" "storage;storage_test" "core;core_test" "workbench_host;workbench_host_test")
    list(GET pair 0 name)
    list(GET pair 1 source)
    add_executable(simdock_${name}_tests "test_sv/simdock/${source}.cpp")
    target_link_libraries(simdock_${name}_tests PRIVATE zeroslack_core Qt6::Test Qt6::Concurrent)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/test_sv/simdock/${source}.cpp" methods REGEX "^    void [A-Za-z0-9_]+\\(\\)")
    set(pureMethods)
    foreach(method IN LISTS methods)
        string(REGEX REPLACE "^    void ([A-Za-z0-9_]+).*" "\\1" method "${method}")
        if(NOT method MATCHES "(_data$|^init|^cleanup|^live|^simulationLifecycle$)")
            list(APPEND pureMethods "${method}")
        endif()
    endforeach()
    if(name STREQUAL "workbench_host")
        zeroslack_qt_test_command(simdock_host_qt_command simdock_${name} simdock_${name}_tests ${pureMethods})
        add_test(NAME simdock_${name} COMMAND ${simdock_host_qt_command})
    else()
        add_test(NAME simdock_${name} COMMAND simdock_${name}_tests ${pureMethods} -o "${CMAKE_BINARY_DIR}/simdock-${name}.xml,junitxml" -o -,txt)
    endif()
endforeach()
add_executable(simdock_ui_tests test_sv/simdock/ui_test.cpp test_sv/simdock/support/mainwindow.cpp)
target_link_libraries(simdock_ui_tests PRIVATE zeroslack_core Qt6::Test Qt6::Concurrent)
target_compile_definitions(simdock_ui_tests PRIVATE SIMDOCK_VERSION="0.6.1")
if(SuiteApp_FOUND)
    target_sources(simdock_ui_tests PRIVATE src/integrations/simdock/simdocksuitecompatibility.cpp)
    target_link_libraries(simdock_ui_tests PRIVATE SuiteApp::suiteapp)
    target_compile_definitions(simdock_ui_tests PRIVATE SIMDOCK_WITH_APPSUITE=1)
endif()
zeroslack_qt_test_command(simdock_ui_qt_command simdock_ui simdock_ui_tests)
add_test(NAME simdock_ui COMMAND ${simdock_ui_qt_command})
zeroslack_qt_test_command(simdock_ui_200_qt_command simdock_ui_200 simdock_ui_tests)
add_test(NAME simdock_ui_200 COMMAND ${simdock_ui_200_qt_command})
add_executable(simdock_scan_benchmark test_sv/simdock/scan_benchmark.cpp)
target_link_libraries(simdock_scan_benchmark PRIVATE zeroslack_core)
add_executable(simdock_legacy_compatibility_test test_sv/simdock/legacy_compatibility_test.cpp)
target_link_libraries(simdock_legacy_compatibility_test PRIVATE zeroslack_core Qt6::Test)
target_compile_definitions(simdock_legacy_compatibility_test PRIVATE SIMDOCK_LEGACY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test_sv/fixtures/simdock")
zeroslack_qt_test_command(simdock_legacy_compatibility_qt_command simdock_legacy_compatibility simdock_legacy_compatibility_test)
add_test(NAME simdock_legacy_compatibility COMMAND ${simdock_legacy_compatibility_qt_command})
add_executable(simdock_performance test_sv/simdock/performance.cpp)
target_link_libraries(simdock_performance PRIVATE zeroslack_core Qt6::Test)
foreach(name IN ITEMS scan_cache storage core workbench_host ui ui_200 legacy_compatibility)
    set_tests_properties(simdock_${name} PROPERTIES TIMEOUT 600
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1;SIMDOCK_TEST_QUESTA="
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
endforeach()
set_tests_properties(simdock_ui_200 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=2;SIMDOCK_TEST_QUESTA=")
if(ZEROSLACK_TEST_QUESTA_EXECUTABLE)
    add_test(NAME simdock_core_live COMMAND simdock_core_tests liveNamedTypeCompilation liveDependencyCompilation liveStateReaderReleasesHandle liveStatePublicationContention liveLoadFailureAndRecovery liveWaveThemes liveCompilationReuse liveReadableProjectPaths liveQuesta liveTbControlledTermination liveTbDependencyPlan liveNoRuntimeDeadline -o "${CMAKE_BINARY_DIR}/simdock-core-live.xml,junitxml" -o -,txt)
    add_test(NAME simdock_workbench_host_live COMMAND simdock_workbench_host_tests simulationLifecycle -o "${CMAKE_BINARY_DIR}/simdock-host-live.xml,junitxml" -o -,txt)
    foreach(name IN ITEMS core workbench_host)
        set_tests_properties(simdock_${name}_live PROPERTIES TIMEOUT 1200 LABELS "simdock-questa-required"
            ENVIRONMENT "SIMDOCK_TEST_QUESTA=${ZEROSLACK_TEST_QUESTA_EXECUTABLE};QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>;PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR};QT_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins")
    endforeach()
endif()
foreach(target IN ITEMS simdock_scan_cache_tests simdock_storage_tests
        simdock_core_tests simdock_workbench_host_tests simdock_ui_tests simdock_scan_benchmark
        simdock_performance simdock_legacy_compatibility_test)
    target_include_directories(${target} PRIVATE ${ZEROSLACK_MODULE_INCLUDE_DIRS} ${ZEROSLACK_GENERATED_INCLUDE_DIR})
endforeach()
