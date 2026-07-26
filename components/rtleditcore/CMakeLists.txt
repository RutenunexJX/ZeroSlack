add_library(rtledit-core STATIC
    src/rtledit/edit_plan.cpp
    src/rtledit/expose_signal_to_top.cpp
    src/rtledit/patch_engine.cpp
    src/rtledit/text_edit.cpp
)

add_library(rtledit::core ALIAS rtledit-core)

set_target_properties(rtledit-core PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
    EXPORT_NAME core
)

target_compile_features(rtledit-core PUBLIC cxx_std_17)

target_include_directories(rtledit-core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

if(MSVC)
    target_compile_options(rtledit-core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rtledit-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_library(rtledit-core-test-support STATIC
        tests/support/mock_workspace_document_manager.cpp
    )
    add_library(rtledit::test_support ALIAS rtledit-core-test-support)

    set_target_properties(rtledit-core-test-support PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )

    target_compile_features(rtledit-core-test-support PUBLIC cxx_std_17)
    target_include_directories(rtledit-core-test-support
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/include"
    )
    target_link_libraries(rtledit-core-test-support PUBLIC rtledit::core)

    if(MSVC)
        target_compile_options(
            rtledit-core-test-support PRIVATE /W4 /permissive-)
    else()
        target_compile_options(
            rtledit-core-test-support PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        get_filename_component(
            RTLEDITCORE_TEST_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(RTLEDITCORE_TEST_PATH
            "${RTLEDITCORE_TEST_RUNTIME_DIR};$ENV{PATH}")
        string(REPLACE ";" "\\;" RTLEDITCORE_TEST_PATH
            "${RTLEDITCORE_TEST_PATH}")
    endif()

    function(rtleditcore_add_test test_name target_name)
        add_test(NAME "${test_name}" COMMAND "$<TARGET_FILE:${target_name}>")
        set_tests_properties("${test_name}" PROPERTIES TIMEOUT 120)
        if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set_tests_properties("${test_name}" PROPERTIES
                ENVIRONMENT "PATH=${RTLEDITCORE_TEST_PATH}")
        endif()
    endfunction()

    set(RTLEDITCORE_TEST_TARGETS
        rtleditcore_patch_engine_tests
        rtleditcore_api_smoke_tests
        rtleditcore_edit_plan_tests
        rtleditcore_text_edit_tests
        rtleditcore_expose_signal_to_top_tests
    )

    add_executable(rtleditcore_patch_engine_tests
        tests/patch_engine_tests.cpp)
    add_executable(rtleditcore_api_smoke_tests
        tests/api_smoke_tests.cpp)
    add_executable(rtleditcore_edit_plan_tests
        tests/edit_plan_tests.cpp)
    add_executable(rtleditcore_text_edit_tests
        tests/text_edit_tests.cpp)
    add_executable(rtleditcore_expose_signal_to_top_tests
        tests/expose_signal_to_top_tests.cpp)

    foreach(test_target IN LISTS RTLEDITCORE_TEST_TARGETS)
        set_target_properties("${test_target}" PROPERTIES
            AUTOMOC OFF
            AUTORCC OFF
            AUTOUIC OFF
        )
        target_link_libraries(
            "${test_target}" PRIVATE rtledit::test_support)
        rtleditcore_add_test("${test_target}" "${test_target}")
    endforeach()

    add_executable(rtleditcore_embedded_consumer_smoke_bin
        tests/embedded_consumer_smoke.cpp)
    set_target_properties(rtleditcore_embedded_consumer_smoke_bin PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_compile_features(
        rtleditcore_embedded_consumer_smoke_bin PRIVATE cxx_std_17)
    target_link_libraries(
        rtleditcore_embedded_consumer_smoke_bin PRIVATE rtledit::core)

    add_test(
        NAME rtleditcore_embedded_consumer_smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DRTLEDITCORE_PUBLIC_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include/rtledit"
            "-DRTLEDITCORE_CONSUMER_EXECUTABLE=$<TARGET_FILE:rtleditcore_embedded_consumer_smoke_bin>"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/embedded_consumer_smoke.ctest"
    )
    set_tests_properties(rtleditcore_embedded_consumer_smoke PROPERTIES
        TIMEOUT 120
    )
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_tests_properties(rtleditcore_embedded_consumer_smoke PROPERTIES
            ENVIRONMENT "PATH=${RTLEDITCORE_TEST_PATH}")
    endif()
endif()
