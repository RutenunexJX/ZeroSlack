if(NOT ZEROSLACK_TREE_SITTER_SOURCES)
    message(FATAL_ERROR "ZEROSLACK_TREE_SITTER_SOURCES is required")
endif()
if(NOT ZEROSLACK_TREE_SITTER_ROOT)
    message(FATAL_ERROR "ZEROSLACK_TREE_SITTER_ROOT is required")
endif()

# CMake <= 3.31 places /RTC1, /Od and /Ob0 in CMAKE_C_FLAGS_DEBUG.
# Scope their removal to this subdirectory so /O2 is valid for these two C
# translation units without changing Debug checks or optimization elsewhere.
if(MSVC)
    set(_tree_sitter_c_flags_debug "${CMAKE_C_FLAGS_DEBUG}")
    foreach(_flag RTC1 Od Ob0)
        string(REGEX REPLACE
            "(^|[ \t])/${_flag}([ \t]|$)"
            " "
            _tree_sitter_c_flags_debug
            "${_tree_sitter_c_flags_debug}")
    endforeach()
    string(STRIP
        "${_tree_sitter_c_flags_debug}"
        _tree_sitter_c_flags_debug)
    set(CMAKE_C_FLAGS_DEBUG "${_tree_sitter_c_flags_debug}")
    unset(_tree_sitter_c_flags_debug)
endif()

add_library(zeroslack_tree_sitter OBJECT
    ${ZEROSLACK_TREE_SITTER_SOURCES})
set_property(TARGET zeroslack_tree_sitter PROPERTY
    ZEROSLACK_SCOPED_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set_property(TARGET zeroslack_tree_sitter PROPERTY
    ZEROSLACK_SCOPED_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set_target_properties(zeroslack_tree_sitter PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF)
target_include_directories(zeroslack_tree_sitter PRIVATE
    "${ZEROSLACK_TREE_SITTER_ROOT}/thirdparty/tree_sitter/lib/include"
    "${ZEROSLACK_TREE_SITTER_ROOT}/thirdparty/tree_sitter/lib/src")

if(MSVC)
    target_compile_options(zeroslack_tree_sitter PRIVATE
        /wd4100
        "$<$<CONFIG:Debug>:/O2>")
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zeroslack_tree_sitter PRIVATE
        -Wno-unused-parameter
        "$<$<CONFIG:Debug>:-O2>")
endif()
