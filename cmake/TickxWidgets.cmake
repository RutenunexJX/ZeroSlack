# Frozen wave-editor/v1 component; exact artifacts are required, with no fallback build.
set(ZEROSLACK_TICKX_WIDGETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/coordination/20261113-block-wave-interaction-redesign/implementation/tickx/frozen-wstyle1-20261114" CACHE PATH "Accepted Tickx wave-editor/v1 component directory")
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/wavewidgets.dll")
    message(FATAL_ERROR "Missing accepted Tickx component: wavewidgets.dll")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/wavewidgets.dll" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "e89d2845a7c132380a1dc9a5aa23c6ef303c8d283ec4c697da613ef096031057")
    message(FATAL_ERROR "Changed accepted Tickx component: wavewidgets.dll")
endif()
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/WaveWorkbenchEla.dll")
    message(FATAL_ERROR "Missing accepted Tickx component: WaveWorkbenchEla.dll")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/WaveWorkbenchEla.dll" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "61e96709023acb4710a1edbcef26b832e1a35c7079bb0d5414b5337d89723207")
    message(FATAL_ERROR "Changed accepted Tickx component: WaveWorkbenchEla.dll")
endif()
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/LICENSE")
    message(FATAL_ERROR "Missing accepted Tickx component: LICENSE")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/LICENSE" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "a310654192acd627fd9ffae6d3692e3ec66782f298b3f9b7f6c61c4c61e1fd7e")
    message(FATAL_ERROR "Changed accepted Tickx component: LICENSE")
endif()
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/NOTICE")
    message(FATAL_ERROR "Missing accepted Tickx component: NOTICE")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/NOTICE" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "36729df452801962a892fd8ad48b83763531a26b81134a684ae0375abd326fe6")
    message(FATAL_ERROR "Changed accepted Tickx component: NOTICE")
endif()
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/THIRD-PARTY-NOTICES.md")
    message(FATAL_ERROR "Missing accepted Tickx component: THIRD-PARTY-NOTICES.md")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/THIRD-PARTY-NOTICES.md" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "618eaf788fca27b5e25a5b5af1b247c50edac5073afa9da418c8f5033e9e95fc")
    message(FATAL_ERROR "Changed accepted Tickx component: THIRD-PARTY-NOTICES.md")
endif()
if(NOT EXISTS "${ZEROSLACK_TICKX_WIDGETS_ROOT}/build-info.json")
    message(FATAL_ERROR "Missing accepted Tickx component: build-info.json")
endif()
file(SHA256 "${ZEROSLACK_TICKX_WIDGETS_ROOT}/build-info.json" tickx_component_hash)
if(NOT tickx_component_hash STREQUAL "54f8a8149666c5ab093c01d085bfd6dcdbc29f4923cb3a99e26a5f0218feaa88")
    message(FATAL_ERROR "Changed accepted Tickx component: build-info.json")
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
