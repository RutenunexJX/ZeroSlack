cmake_minimum_required(VERSION 3.16)

# 项目名称 (建议和你的 .pro 目标保持一致，这里暂定为 demo)
# 注意：这里必须开启 C 语言支持，因为包含了 tree-sitter 的 .c 文件
project(demo LANGUAGES C CXX)

# =========================================================
# 1. 标准设置 (为集成 Slang 做准备)
# =========================================================
set(CMAKE_CXX_STANDARD 20)          # Slang 需要 C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)            # Tree-sitter 使用 C99

include(CTest)

# =========================================================
# 2. Slang 集成配置 (Build System Only)
# =========================================================
# 禁用 Slang 的额外组件以加速构建并减少依赖
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
# 关闭 mimalloc，避免与 Qt/CRT 全局分配器冲突导致启动崩溃
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

# 添加 Slang 子项目
# 确保 thirdparty/slang 已经通过 git submodule 下载
add_subdirectory(thirdparty/slang)
# MinGW/GCC: 使用 COFF 大对象格式，避免 Compilation.cpp 等大翻译单元导致 "file too big"
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(slang_slang PRIVATE -Wa,-mbig-obj)
endif()

# =========================================================
# 3. Qt 6 配置
# =========================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt6 组件 (对应原 .pro 的 QT += core gui widgets concurrent)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# =========================================================
# 4. 源文件与资源
# =========================================================
set(ZEROSLACK_CORE_SOURCES
    # --- C++ 源文件 ---
    analysisscheduler.cpp
    mainwindow.cpp
    completionmanager.cpp
    completionmodel.cpp
    completionservice.cpp
    definitionservice.cpp
    documentmodel.cpp
    tsdocument.cpp
    modemanager.cpp
    mycodeeditor.cpp
    myhighlighter.cpp
    navigationmanager.cpp
    navigationwidget.cpp
    projectmodel.cpp
    relationshipprogressdialog.cpp
    semanticindex.cpp
    slangmanager.cpp
    smartrelationshipbuilder.cpp
    symbolanalyzer.cpp
    symbolrelationshipengine.cpp
    syminfo.cpp
    tabmanager.cpp
    workspacemanager.cpp

    # --- 头文件 (CMake不强求列出，但列出对IDE友好) ---
    analysisscheduler.h
    mainwindow.h
    completionmanager.h
    scope_tree.h
    version.h
    completionmodel.h
    completionservice.h
    definitionservice.h
    documentmodel.h
    tsdocument.h
    modemanager.h
    mycodeeditor.h
    myhighlighter.h
    navigationmanager.h
    navigationwidget.h
    projectmodel.h
    relationshipprogressdialog.h
    semanticindex.h
    slangmanager.h
    smartrelationshipbuilder.h
    symbolanalyzer.h
    symbolrelationshipengine.h
    syminfo.h
    tabmanager.h
    workspacemanager.h

    # --- UI 界面文件 ---
    mainwindow.ui

    # --- 资源文件 ---
    code.qrc
    images.qrc

    # --- Thirdparty C 源文件 (Tree-sitter) ---
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
)

# =========================================================
# 5. 定义核心库与可执行目标
# =========================================================
add_library(zeroslack_core STATIC ${ZEROSLACK_CORE_SOURCES})

# 对应 .pro 的 TARGET
add_executable(demo main.cpp)

# 设置 Windows 应用程序属性 (防止出现黑色控制台窗口)
if(WIN32)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

# =========================================================
# 6. 包含路径与链接库
# =========================================================
# 对应 .pro 的 INCLUDEPATH
target_include_directories(zeroslack_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    thirdparty/tree_sitter/lib/include
    thirdparty/tree_sitter/lib/src
)

# 链接库：Qt6 + Slang
target_link_libraries(zeroslack_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    slang::slang
)

target_link_libraries(demo PRIVATE zeroslack_core)

# =========================================================
# 7. 编译器标志与宏定义
# =========================================================

# 针对 tree-sitter 的 C 文件忽略特定警告 (对应 QMAKE_CFLAGS += -Wno-unused-parameter)
set_source_files_properties(
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
)

# MinGW 特定定义 (对应 win32-g++: DEFINES += ...)
if(MINGW)
    target_compile_definitions(zeroslack_core PUBLIC __USE_MINGW_ANSI_STDIO=1)
endif()

# =========================================================
# 8. 回归测试
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
