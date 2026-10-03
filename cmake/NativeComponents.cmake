# Optional, explicit native runtime deployment, independent of SuiteApp_FOUND.
# These directories contain the component and its privately named Ela runtime.
set(ZEROSLACK_XIPS_COMPONENT_DIR "" CACHE PATH "xIPs native runtime directory")
set(ZEROSLACK_SIMDOCK_COMPONENT_DIR "" CACHE PATH "SimDock native runtime directory")
set(ZEROSLACK_SIMDOCK_WAVE_COMPONENT_DIR "" CACHE PATH "Optional Tickx DLL directory for SimDock's embedded waveform editor")
set(ZEROSLACK_XIPS_ADDITIONAL_RUNTIMES "" CACHE STRING "Additional xIPs DLL filenames from its final runtime manifest")
set(ZEROSLACK_SIMDOCK_ADDITIONAL_RUNTIMES "" CACHE STRING "Additional SimDock DLL filenames from its final runtime manifest")

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
if(ZEROSLACK_SIMDOCK_COMPONENT_DIR)
    zeroslack_deploy_component(simdock "${ZEROSLACK_SIMDOCK_COMPONENT_DIR}" simdock-workbench.dll SimDockEla.dll simdock-workbench.json ${ZEROSLACK_SIMDOCK_ADDITIONAL_RUNTIMES})
endif()
if(ZEROSLACK_SIMDOCK_WAVE_COMPONENT_DIR)
    if(NOT ZEROSLACK_SIMDOCK_COMPONENT_DIR)
        message(FATAL_ERROR "The Tickx dependency requires SimDock native deployment")
    endif()
    foreach(file wavewidgets.dll WaveWorkbenchEla.dll)
        if(NOT EXISTS "${ZEROSLACK_SIMDOCK_WAVE_COMPONENT_DIR}/${file}")
            message(FATAL_ERROR "Missing SimDock waveform dependency: ${file}")
        endif()
    endforeach()
    add_custom_target(zeroslack_deploy_simdock_wave ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZEROSLACK_SIMDOCK_WAVE_COMPONENT_DIR}/wavewidgets.dll"
            "${ZEROSLACK_SIMDOCK_WAVE_COMPONENT_DIR}/WaveWorkbenchEla.dll"
            "$<TARGET_FILE_DIR:demo>/components/simdock"
        VERBATIM)
    add_dependencies(zeroslack_deploy_simdock_wave zeroslack_deploy_simdock)
    add_dependencies(demo zeroslack_deploy_simdock_wave)
endif()
