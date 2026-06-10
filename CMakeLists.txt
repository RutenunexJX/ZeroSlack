cmake_minimum_required(VERSION 3.16)

# Project target. C is enabled because tree-sitter includes .c sources.
project(demo LANGUAGES C CXX)

# =========================================================
# 1. Language Standards
# =========================================================
set(CMAKE_CXX_STANDARD 20)          # Required by Slang
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)            # Used by tree-sitter

include(CTest)

# =========================================================
# 2. Slang Integration
# =========================================================
# Disable optional Slang components to keep builds smaller and faster.
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
# Disable mimalloc to avoid allocator conflicts with Qt/CRT on Windows.
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

# Add Slang submodule.
add_subdirectory(thirdparty/slang)
# MinGW/GCC: use large COFF objects for large Slang translation units.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(slang_slang PRIVATE -Wa,-mbig-obj)
endif()

# =========================================================
# 3. Qt 6 Configuration
# =========================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Match the old .pro modules: core gui widgets concurrent.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# =========================================================
# 4. Sources and Resources
# =========================================================
set(ZEROSLACK_CORE_SOURCES
    # --- C++ sources ---
    analysiscommandcoordinator.cpp
    analysiscoordinator.cpp
    analysisprogresscoordinator.cpp
    analysisscheduler.cpp
    mainwindow.cpp
    completionmanager.cpp
    completionmodel.cpp
    completionservice.cpp
    definitionnavigationservice.cpp
    definitionservice.cpp
    diagnosticservice.cpp
    documentmodel.cpp
    editorcoordinator.cpp
    filecommandcoordinator.cpp
    hierarchyservice.cpp
    tsdocument.cpp
    modecommandcoordinator.cpp
    modemanager.cpp
    mycodeeditor.cpp
    myhighlighter.cpp
    navigationcommandcoordinator.cpp
    navigationmanager.cpp
    navigationpanecoordinator.cpp
    navigationservice.cpp
    navigationwidget.cpp
    problemspanelcoordinator.cpp
    projectmodel.cpp
    referencespanelcoordinator.cpp
    referenceservice.cpp
    relationshipspanelcoordinator.cpp
    relationshipservice.cpp
    relationshipprogressdialog.cpp
    searchservice.cpp
    semanticpanelutils.cpp
    semanticpanelrefreshcoordinator.cpp
    semanticindex.cpp
    semanticindexsnapshot.cpp
    semanticruntimecoordinator.cpp
    slangmanager.cpp
    smartrelationshipbuilder.cpp
    symbolanalyzer.cpp
    symbolrelationshipengine.cpp
    syminfo.cpp
    tabmanager.cpp
    workspacemanager.cpp

    # --- Headers (not required by CMake, listed for IDEs) ---
    analysiscommandcoordinator.h
    analysiscoordinator.h
    analysisprogresscoordinator.h
    analysisscheduler.h
    mainwindow.h
    completionmanager.h
    scope_tree.h
    version.h
    completionmodel.h
    completionservice.h
    definitionnavigationservice.h
    definitionservice.h
    diagnosticservice.h
    documentmodel.h
    editorcoordinator.h
    filecommandcoordinator.h
    hierarchyservice.h
    tsdocument.h
    modecommandcoordinator.h
    modemanager.h
    mycodeeditor.h
    myhighlighter.h
    modulehierarchymodel.h
    navigationcommandcoordinator.h
    navigationmanager.h
    navigationpanecoordinator.h
    navigationservice.h
    navigationwidget.h
    problemspanelcoordinator.h
    projectmodel.h
    referencespanelcoordinator.h
    referenceservice.h
    relationshipspanelcoordinator.h
    relationshipservice.h
    relationshipprogressdialog.h
    searchservice.h
    semanticpanelutils.h
    semanticpanelrefreshcoordinator.h
    semanticindex.h
    semanticindexsnapshot.h
    semanticruntimecoordinator.h
    slangmanager.h
    smartrelationshipbuilder.h
    symbolanalyzer.h
    symbolrelationshipengine.h
    symboloutlinemodel.h
    syminfo.h
    tabmanager.h
    workspacemanager.h

    # --- UI files ---
    mainwindow.ui

    # --- Resource files ---
    code.qrc
    images.qrc

    # --- Third-party C sources (tree-sitter) ---
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
)

# =========================================================
# 5. Core Library and Executable
# =========================================================
add_library(zeroslack_core STATIC ${ZEROSLACK_CORE_SOURCES})

# Match the old .pro TARGET.
add_executable(demo main.cpp)

# Build a Windows GUI app without a console window.
if(WIN32)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

# =========================================================
# 6. Include Paths and Libraries
# =========================================================
# Match the old .pro INCLUDEPATH.
target_include_directories(zeroslack_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    thirdparty/tree_sitter/lib/include
    thirdparty/tree_sitter/lib/src
)

# Link libraries: Qt6 and Slang.
target_link_libraries(zeroslack_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    slang::slang
)

target_link_libraries(demo PRIVATE zeroslack_core)

# =========================================================
# 7. Compiler Flags and Definitions
# =========================================================

# Suppress selected warnings for tree-sitter C sources.
set_source_files_properties(
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
)

# MinGW-specific definitions.
if(MINGW)
    target_compile_definitions(zeroslack_core PUBLIC __USE_MINGW_ANSI_STDIO=1)
endif()

# =========================================================
# 8. Regression Tests
# =========================================================
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(ZEROSLACK_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(ZEROSLACK_TEST_PATH
        "${QT_INSTALL_PREFIX}/bin;${ZEROSLACK_COMPILER_BIN_DIR};$ENV{PATH}"
    )
    string(REPLACE ";" "\\;" ZEROSLACK_TEST_PATH "${ZEROSLACK_TEST_PATH}")

    add_executable(ts_doc_test test_sv/ts_doc_test.cpp)
    target_link_libraries(ts_doc_test PRIVATE zeroslack_core)

    add_executable(completion_test test_sv/completion_test.cpp)
    target_link_libraries(completion_test PRIVATE zeroslack_core)

    add_executable(jump_test test_sv/jump_test.cpp)
    target_link_libraries(jump_test PRIVATE zeroslack_core)

    add_executable(relationship_test test_sv/relationship_test.cpp)
    target_link_libraries(relationship_test PRIVATE zeroslack_core)

    add_executable(gui_smoke_test test_sv/gui_smoke_test.cpp)
    target_link_libraries(gui_smoke_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(large_file_perf_test test_sv/large_file_perf_test.cpp)
    target_link_libraries(large_file_perf_test PRIVATE zeroslack_core Qt6::Test)

    add_test(NAME ts_doc_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:ts_doc_test>
    )
    add_test(NAME completion_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:completion_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/test_symbols.sv
    )
    add_test(NAME jump_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:jump_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/test_symbols.sv
    )
    add_test(NAME relationship_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:relationship_test>
    )
    add_test(NAME gui_smoke_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:gui_smoke_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/test_symbols.sv
    )
    add_test(NAME large_file_perf_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:large_file_perf_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
    )
endif()
