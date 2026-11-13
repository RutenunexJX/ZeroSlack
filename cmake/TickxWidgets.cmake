# Frozen, existing memory editor/preview component. No Tickx build or runtime service.
set(ZEROSLACK_TICKX_WIDGETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Tickx/build/releases/tickx-0.15.10-20261110/staging/Tickx-0.15.10-windows-x64" CACHE PATH "Tickx 0.15.10 published component directory")
if(EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/wavewidgets.dll")
    file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/wavewidgets.dll" tickx_widgets_hash)
    file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/WaveWorkbenchEla.dll" tickx_ela_hash)
    if(NOT tickx_widgets_hash STREQUAL "92095197ea2e5512e6a7bb31d6df36c4a2194b84a340ad119fdccd2b4ab75d52" OR
       NOT tickx_ela_hash STREQUAL "61e96709023acb4710a1edbcef26b832e1a35c7079bb0d5414b5337d89723207")
        message(FATAL_ERROR "Tickx component identity differs from the reviewed 0.15.10 runtime")
    endif()
    add_custom_command(TARGET zeroslack_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:zeroslack_core>/components/tickx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/wavewidgets.dll"
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/WaveWorkbenchEla.dll"
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/LICENSE"
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/NOTICE"
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/THIRD-PARTY-NOTICES.md"
            "${ZEROSLACK_TICKX_WIDGETS_ROOT}/build-info.json"
            "$<TARGET_FILE_DIR:zeroslack_core>/components/tickx")
endif()
