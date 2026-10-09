# SimDock 0.6.1 source is owned by ZeroSlack. Reuse the host semantic engine,
# tree-sitter runtime (exported by documents), and Ela; no standalone ABI.
set(ZEROSLACK_SIMDOCK_SOURCE_VERSION "0.6.1")
set(ZEROSLACK_SIMDOCK_SOURCE_COMMIT "e1747735735f0e50e06587d729784546efba56eb")
if(NOT ZEROSLACK_ENABLE_ELA)
    message(FATAL_ERROR "The integrated simulation workbench requires the maintained Ela build")
endif()
target_sources(zeroslack_core PRIVATE
    src/simulation/simdock/core/workspace.cpp
    src/simulation/simdock/core/analyzer.cpp
    src/simulation/simdock/core/dependencies.cpp
    src/simulation/simdock/core/testbench.cpp
    src/simulation/simdock/core/stimulus.cpp
    src/simulation/simdock/core/stimulussemantic.cpp
    src/simulation/simdock/core/preparation.cpp
    src/simulation/simdock/core/scoreboard.cpp
    src/simulation/simdock/core/questasession.cpp
    src/simulation/simdock/core/wavepalette.cpp
    src/simulation/simdock/ui/workbench.cpp
    src/simulation/simdock/ui/stimulusdialog.cpp
    src/simulation/simdock/ui/scoreboarddialog.cpp
    src/simulation/simdock/ui/dialogs.cpp
    src/simulation/simdock/ui/uistyle.cpp
    src/simulation/simdock/ui/componentpath.cpp
    src/integrations/simdock/simdockcontextview.cpp
    src/integrations/simdock/simdockstate.cpp
    resources/simdock/simdock.qrc)
target_compile_definitions(zeroslack_core PRIVATE SIMDOCK_VERSION="${ZEROSLACK_SIMDOCK_SOURCE_VERSION}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/simdock-source.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/simdock-source.json" @ONLY)
