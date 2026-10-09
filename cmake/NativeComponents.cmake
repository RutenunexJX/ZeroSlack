# Optional, explicit native runtime deployment, independent of SuiteApp_FOUND.
# These directories contain the component and its privately named Ela runtime.
set(ZEROSLACK_XIPS_COMPONENT_DIR "" CACHE PATH "xIPs native runtime directory")
set(ZEROSLACK_TICKX_COMPONENT_DIR "" CACHE PATH "Pinned Tickx 0.15.2 waveform runtime directory")
set(ZEROSLACK_XIPS_ADDITIONAL_RUNTIMES "" CACHE STRING "Additional xIPs DLL filenames from its final runtime manifest")

function(zeroslack_deploy_component id directory)
    if(NOT WIN32)
        message(FATAL_ERROR "Native component deployment currently targets Windows")
    endif()
    foreach(file IN LISTS ARGN)
        if(NOT EXISTS "${directory}/${file}")
            message(FATAL_ERROR "Missing ${id} runtime: ${directory}/${file}")
        endif()
    endforeach()
    set(files)
    foreach(file IN LISTS ARGN)
        list(APPEND files "${directory}/${file}")
    endforeach()
    add_custom_target(zeroslack_deploy_${id} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demo>/components/${id}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${files} "$<TARGET_FILE_DIR:demo>/components/${id}"
        VERBATIM)
    add_dependencies(demo zeroslack_deploy_${id})
endfunction()

if(ZEROSLACK_XIPS_COMPONENT_DIR)
    zeroslack_deploy_component(xips "${ZEROSLACK_XIPS_COMPONENT_DIR}"
        xips-browser.dll xips-browser-impl.dll XipsEla.dll ${ZEROSLACK_XIPS_ADDITIONAL_RUNTIMES})
    # xIPs imports QtSql. Use the same Qt build as the host, never a component's
    # unrelated Qt distribution; its SQLite plugin belongs to the host runtime.
    find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS Sql)
    if(NOT TARGET Qt6::QSQLiteDriverPlugin)
        message(FATAL_ERROR "xIPs native deployment requires the matching Qt SQLite plugin")
    endif()
    add_custom_target(zeroslack_deploy_xips_qt ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demo>/sqldrivers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Sql>" "$<TARGET_FILE_DIR:demo>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>" "$<TARGET_FILE_DIR:demo>/sqldrivers"
        VERBATIM)
    add_dependencies(zeroslack_deploy_xips zeroslack_deploy_xips_qt)
endif()
if(ZEROSLACK_TICKX_COMPONENT_DIR)
    foreach(file wavewidgets.dll WaveWorkbenchEla.dll)
        if(NOT EXISTS "${ZEROSLACK_TICKX_COMPONENT_DIR}/${file}")
            message(FATAL_ERROR "Missing SimDock waveform dependency: ${file}")
        endif()
        file(SHA256 "${ZEROSLACK_TICKX_COMPONENT_DIR}/${file}" digest)
        if(file STREQUAL "wavewidgets.dll")
            set(expected "c326b99e68bcbb2f8dd5e3e7585f4def58ba33d77ba384a86c9ede48cd444a70")
        else()
            set(expected "85bce4affee3c45f4b3afef010eeb6e82d9a0bd42f6bfdc033371bb4ee6aa439")
        endif()
        if(NOT digest STREQUAL expected)
            message(FATAL_ERROR "Tickx runtime does not match the frozen 0.15.2 boundary: ${file}")
        endif()
    endforeach()
    zeroslack_deploy_component(wave "${ZEROSLACK_TICKX_COMPONENT_DIR}" wavewidgets.dll WaveWorkbenchEla.dll)
endif()
