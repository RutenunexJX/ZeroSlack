add_library(insightgraph-core STATIC
    src/graph.cpp src/projection.cpp src/fsm_layout.cpp src/heatmap.cpp src/trace.cpp)
add_library(insightgraph::core ALIAS insightgraph-core)
set_target_properties(insightgraph-core PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(insightgraph-core PUBLIC cxx_std_17)
target_include_directories(insightgraph-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(insightgraph-core PUBLIC Qt6::Core)

if(TARGET Qt6::Svg)
    add_library(insightgraph-render STATIC src/render.cpp)
    add_library(insightgraph::render ALIAS insightgraph-render)
    set_target_properties(insightgraph-render PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF
        POSITION_INDEPENDENT_CODE ON)
    target_compile_features(insightgraph-render PUBLIC cxx_std_17)
    target_include_directories(insightgraph-render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(insightgraph-render PUBLIC Qt6::Core Qt6::Gui Qt6::Svg)
endif()

if(BUILD_TESTING)
    function(insightgraph_test name source target)
        add_executable(${name} ${source})
        set_target_properties(${name} PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
        target_link_libraries(${name} PRIVATE ${target})
        add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
        set_tests_properties(${name} PROPERTIES TIMEOUT 120
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_HASH_SEED=0")
        get_target_property(qt_core_runtime Qt6::Core LOCATION)
        get_filename_component(qt_runtime_dir "${qt_core_runtime}" DIRECTORY)
        get_filename_component(compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${qt_runtime_dir};PATH=path_list_prepend:${compiler_runtime_dir}")
    endfunction()
    insightgraph_test(insightgraph_core_test tests/core_test.cpp insightgraph::core)
    insightgraph_test(insightgraph_consumer_test tests/consumer_test.cpp insightgraph::core)
    if(TARGET insightgraph-render)
        insightgraph_test(insightgraph_render_test tests/render_test.cpp insightgraph::render)
    endif()
endif()
