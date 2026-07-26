cmake_minimum_required(VERSION 3.16)
project(TreeSitterCompileOptionsProbe LANGUAGES C)

if(NOT ZEROSLACK_SOURCE_DIR)
    message(FATAL_ERROR "ZEROSLACK_SOURCE_DIR is required")
endif()

set(CMAKE_C_STANDARD 99)
set(ZEROSLACK_TREE_SITTER_SOURCES
    "${ZEROSLACK_SOURCE_DIR}/thirdparty/tree_sitter/lib/src/lib.c"
    "${ZEROSLACK_SOURCE_DIR}/thirdparty/tree_sitter_systemverilog/src/parser.c")
set(ZEROSLACK_TREE_SITTER_ROOT "${ZEROSLACK_SOURCE_DIR}")
add_subdirectory(
    "${ZEROSLACK_SOURCE_DIR}/cmake/tree_sitter_runtime"
    "${CMAKE_CURRENT_BINARY_DIR}/tree_sitter_runtime")

# A same-language control translation unit proves that source isolation is
# real, rather than an accidental configuration-wide flag mutation. The
# generated manifests also cover Visual Studio generators, which do not emit
# compile_commands.json.
add_library(tree_sitter_compile_options_control OBJECT control.c)

get_target_property(_tree_debug_flags zeroslack_tree_sitter
    ZEROSLACK_SCOPED_C_FLAGS_DEBUG)
get_target_property(_tree_release_flags zeroslack_tree_sitter
    ZEROSLACK_SCOPED_C_FLAGS_RELEASE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tree-debug-flags.txt"
    "${_tree_debug_flags}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/control-debug-flags.txt"
    "${CMAKE_C_FLAGS_DEBUG}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tree-release-flags.txt"
    "${_tree_release_flags}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/control-release-flags.txt"
    "${CMAKE_C_FLAGS_RELEASE}")
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tree-options-$<CONFIG>.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:zeroslack_tree_sitter,COMPILE_OPTIONS>, >")
