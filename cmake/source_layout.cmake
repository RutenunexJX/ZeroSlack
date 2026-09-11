# First-party modules share stable, unique quoted header names. Keep the
# repository root off the normal include path: VERSION shadows <version> on Windows.
set(ZEROSLACK_SOURCE_MODULES
    app
    analysis
    cli
    commands
    completion
    documents
    editor
    insights
    integrations/pinloom
    integrations/suite
    integrations/wave
    navigation
    semantic
    settings
    ui
    workspace)
set(ZEROSLACK_MODULE_INCLUDE_DIRS)
foreach(module IN LISTS ZEROSLACK_SOURCE_MODULES)
    list(APPEND ZEROSLACK_MODULE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/${module}")
endforeach()

# Also cover the small standalone tests that intentionally do not link the core.
include_directories(${ZEROSLACK_MODULE_INCLUDE_DIRS})
set(CMAKE_AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor")
