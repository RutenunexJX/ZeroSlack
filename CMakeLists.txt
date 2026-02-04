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

# =========================================================
# 2. Qt 6 配置
# =========================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt6 组件 (对应原 .pro 的 QT += core gui widgets concurrent)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# =========================================================
# 3. 源文件与资源
# =========================================================
set(PROJECT_SOURCES
    # --- C++ 源文件 ---
    main.cpp
    mainwindow.cpp
    completionmanager.cpp
    completionmodel.cpp
    sv_lexer.cpp
    sv_treesitter_parser.cpp
    modemanager.cpp
    mycodeeditor.cpp
    myhighlighter.cpp
    navigationmanager.cpp
    navigationwidget.cpp
    relationshipprogressdialog.cpp
    smartrelationshipbuilder.cpp
    symbolanalyzer.cpp
    symbolrelationshipengine.cpp
    syminfo.cpp
    tabmanager.cpp
    workspacemanager.cpp

    # --- 头文件 (CMake不强求列出，但列出对IDE友好) ---
    mainwindow.h
    completionmanager.h
    scope_tree.h
    completionmodel.h
    sv_lexer.h
    sv_treesitter_parser.h
    sv_token.h
    modemanager.h
    mycodeeditor.h
    myhighlighter.h
    navigationmanager.h
    navigationwidget.h
    relationshipprogressdialog.h
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
# 4. 定义可执行目标
# =========================================================
# 对应 .pro 的 TARGET
add_executable(demo ${PROJECT_SOURCES})

# 设置 Windows 应用程序属性 (防止出现黑色控制台窗口)
if(WIN32)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

# =========================================================
# 5. 包含路径与链接库
# =========================================================
# 对应 .pro 的 INCLUDEPATH
target_include_directories(demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    thirdparty/tree_sitter/lib/include
    thirdparty/tree_sitter/lib/src
)

# 链接 Qt 库
target_link_libraries(demo PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# =========================================================
# 6. 编译器标志与宏定义
# =========================================================

# 针对 tree-sitter 的 C 文件忽略特定警告 (对应 QMAKE_CFLAGS += -Wno-unused-parameter)
set_source_files_properties(
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
)

# MinGW 特定定义 (对应 win32-g++: DEFINES += ...)
if(MINGW)
    target_compile_definitions(demo PRIVATE __USE_MINGW_ANSI_STDIO=1)
endif()

# =========================================================
# 7. (可选) 未来集成 Slang 的预留位置
# =========================================================
# target_include_directories(demo PRIVATE /path/to/slang/include)
# target_link_libraries(demo PRIVATE slang::slang)