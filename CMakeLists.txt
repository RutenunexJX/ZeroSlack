cmake_minimum_required(VERSION 3.16)

set(ZEROSLACK_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${ZEROSLACK_VERSION_FILE}")
    message(FATAL_ERROR "Missing VERSION file")
endif()
file(STRINGS "${ZEROSLACK_VERSION_FILE}" ZEROSLACK_VERSION_LINES LIMIT_COUNT 1)
list(GET ZEROSLACK_VERSION_LINES 0 ZEROSLACK_VERSION)
string(STRIP "${ZEROSLACK_VERSION}" ZEROSLACK_VERSION)
if(NOT ZEROSLACK_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must use strict X.Y.Z numeric format")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ZEROSLACK_VERSION_FILE}")
string(REPLACE "." ";" ZEROSLACK_VERSION_PARTS "${ZEROSLACK_VERSION}")
list(GET ZEROSLACK_VERSION_PARTS 0 ZEROSLACK_VERSION_MAJOR)
list(GET ZEROSLACK_VERSION_PARTS 1 ZEROSLACK_VERSION_MINOR)
list(GET ZEROSLACK_VERSION_PARTS 2 ZEROSLACK_VERSION_PATCH)

# Project target. C is enabled because tree-sitter includes .c sources.
project(ZeroSlack VERSION ${ZEROSLACK_VERSION} LANGUAGES C CXX)

string(TIMESTAMP ZEROSLACK_BUILD_TIME "%Y-%m-%dT%H:%M:%SZ" UTC)
set(ZEROSLACK_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${ZEROSLACK_GENERATED_INCLUDE_DIR}/version.h"
    @ONLY
)

# =========================================================
# 1. Language Standards
# =========================================================
set(CMAKE_CXX_STANDARD 20)          # Required by Slang
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)            # Used by tree-sitter

include(CTest)

set(ZEROSLACK_DEBUG_LINKER "AUTO" CACHE STRING
    "Debug linker selection: AUTO, GNU, or LLD")
set_property(CACHE ZEROSLACK_DEBUG_LINKER PROPERTY STRINGS AUTO GNU LLD)
set(ZEROSLACK_LLD_EXECUTABLE "" CACHE FILEPATH
    "Optional path to a MinGW-compatible ld.lld executable")
string(TOUPPER "${ZEROSLACK_DEBUG_LINKER}" ZEROSLACK_DEBUG_LINKER)
if(NOT ZEROSLACK_DEBUG_LINKER MATCHES "^(AUTO|GNU|LLD)$")
    message(FATAL_ERROR
        "ZEROSLACK_DEBUG_LINKER must be AUTO, GNU, or LLD")
endif()

# MinGW helper tools load runtime DLLs from the compiler bin directory. Make
# generated build rules self-contained so `cmake --build` works from a plain
# shell without the caller manually preparing PATH.
if(WIN32 AND CMAKE_CXX_COMPILER)
    get_filename_component(ZEROSLACK_COMPILER_BIN_DIR
        "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
        # Read the inherited PATH when the rule runs. Embedding the complete
        # configure-time PATH makes harmless IDE/session PATH changes alter
        # every Ninja command and forces a full rebuild after reconfigure.
        set(ZEROSLACK_BUILD_TOOL_LAUNCHER
            "${CMAKE_COMMAND} -E env --modify \"PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR}\" --")
    else()
        set(ZEROSLACK_BUILD_TOOL_PATH
            "${ZEROSLACK_COMPILER_BIN_DIR};$ENV{PATH}")
        string(REPLACE ";" "\\;" ZEROSLACK_BUILD_TOOL_PATH
            "${ZEROSLACK_BUILD_TOOL_PATH}")
        set(ZEROSLACK_BUILD_TOOL_LAUNCHER
            "${CMAKE_COMMAND} -E env \"PATH=${ZEROSLACK_BUILD_TOOL_PATH}\"")
    endif()
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
        "${ZEROSLACK_BUILD_TOOL_LAUNCHER}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK
        "${ZEROSLACK_BUILD_TOOL_LAUNCHER}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_CUSTOM
        "${ZEROSLACK_BUILD_TOOL_LAUNCHER}")
endif()

# Use LLD only for Debug links and only after a compiler-driver link probe.
# Release/package links therefore retain the existing MinGW GNU ld path.
set(ZEROSLACK_USE_LLD OFF)
set(ZEROSLACK_LLD_STATUS "not requested")
if(MINGW AND NOT ZEROSLACK_DEBUG_LINKER STREQUAL "GNU")
    set(ZEROSLACK_LLD_CANDIDATE "")
    if(ZEROSLACK_LLD_EXECUTABLE)
        if(EXISTS "${ZEROSLACK_LLD_EXECUTABLE}")
            get_filename_component(ZEROSLACK_LLD_CANDIDATE
                "${ZEROSLACK_LLD_EXECUTABLE}" REALPATH)
        else()
            set(ZEROSLACK_LLD_STATUS
                "configured path does not exist: ${ZEROSLACK_LLD_EXECUTABLE}")
        endif()
    else()
        unset(ZEROSLACK_LLD_CANDIDATE)
        unset(ZEROSLACK_LLD_CANDIDATE CACHE)
        find_program(ZEROSLACK_LLD_CANDIDATE NAMES ld.lld ld.lld.exe)
        if(NOT ZEROSLACK_LLD_CANDIDATE)
            set(ZEROSLACK_LLD_STATUS "ld.lld not found")
        endif()
    endif()

    if(ZEROSLACK_LLD_CANDIDATE)
        execute_process(
            COMMAND "${ZEROSLACK_LLD_CANDIDATE}" --version
            RESULT_VARIABLE ZEROSLACK_LLD_VERSION_RESULT
            OUTPUT_VARIABLE ZEROSLACK_LLD_VERSION_TEXT
            ERROR_VARIABLE ZEROSLACK_LLD_VERSION_ERROR
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "LLD[ \t]+([0-9]+)"
            ZEROSLACK_LLD_VERSION_MATCH "${ZEROSLACK_LLD_VERSION_TEXT}")
        if(NOT ZEROSLACK_LLD_VERSION_RESULT EQUAL 0
           OR NOT ZEROSLACK_LLD_VERSION_MATCH)
            set(ZEROSLACK_LLD_STATUS "unable to identify LLD version")
        else()
            get_filename_component(ZEROSLACK_LLD_BIN_DIR
                "${ZEROSLACK_LLD_CANDIDATE}" DIRECTORY)
            string(MD5 ZEROSLACK_LLD_PROBE_HASH
                "${ZEROSLACK_LLD_CANDIDATE};${ZEROSLACK_LLD_VERSION_TEXT};${CMAKE_CXX_COMPILER};${CMAKE_CXX_COMPILER_VERSION};path-ready-v1")
            set(ZEROSLACK_LLD_PROBE_VAR
                "ZEROSLACK_LLD_LINKS_${ZEROSLACK_LLD_PROBE_HASH}")
            include(CheckCXXSourceCompiles)
            set(ZEROSLACK_SAVED_REQUIRED_LINK_OPTIONS
                "${CMAKE_REQUIRED_LINK_OPTIONS}")
            set(CMAKE_REQUIRED_LINK_OPTIONS
                "-fuse-ld=lld"
                "-B${ZEROSLACK_LLD_BIN_DIR}/")
            # try_compile does not inherit RULE_LAUNCH_COMPILE. Temporarily
            # expose the MinGW runtime DLLs so this checks the linker rather
            # than failing before the compiler front-end can start.
            set(ZEROSLACK_SAVED_CONFIGURE_PATH "$ENV{PATH}")
            set(ENV{PATH}
                "${ZEROSLACK_COMPILER_BIN_DIR};$ENV{PATH}")
            check_cxx_source_compiles([=[
#include <string>
struct B {
    virtual ~B() = default;
    virtual int f() const = 0;
};
struct D final : B {
    int f() const override { return int(std::string("lld").size()); }
};
int main() {
    D d;
    return d.f() == 3 ? 0 : 1;
}
]=] ${ZEROSLACK_LLD_PROBE_VAR})
            set(ENV{PATH} "${ZEROSLACK_SAVED_CONFIGURE_PATH}")
            set(CMAKE_REQUIRED_LINK_OPTIONS
                "${ZEROSLACK_SAVED_REQUIRED_LINK_OPTIONS}")
            if(${ZEROSLACK_LLD_PROBE_VAR})
                set(ZEROSLACK_USE_LLD ON)
                set(ZEROSLACK_LLD_STATUS "${ZEROSLACK_LLD_VERSION_TEXT}")
                add_link_options(
                    "$<$<CONFIG:Debug>:-fuse-ld=lld>"
                    "$<$<CONFIG:Debug>:-B${ZEROSLACK_LLD_BIN_DIR}/>")
            else()
                set(ZEROSLACK_LLD_STATUS
                    "compiler-driver link probe failed for ${ZEROSLACK_LLD_VERSION_TEXT}")
            endif()
        endif()
    endif()
endif()

if(ZEROSLACK_USE_LLD)
    message(STATUS
        "ZeroSlack Debug linker: LLD (${ZEROSLACK_LLD_CANDIDATE})")
else()
    if(MINGW)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-prog-name=ld
            OUTPUT_VARIABLE ZEROSLACK_GNU_LD_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        message(STATUS
            "ZeroSlack Debug linker: GNU ld (${ZEROSLACK_GNU_LD_PATH}); ${ZEROSLACK_LLD_STATUS}")
    else()
        message(STATUS "ZeroSlack linker: compiler default")
    endif()
    if(MINGW AND ZEROSLACK_DEBUG_LINKER STREQUAL "LLD")
        message(WARNING
            "Requested LLD is unavailable or incompatible; using GNU ld safely")
    elseif(ZEROSLACK_DEBUG_LINKER STREQUAL "LLD")
        message(WARNING
            "ZEROSLACK_DEBUG_LINKER=LLD is supported only for MinGW Debug; "
            "using the compiler default")
    endif()
endif()

# =========================================================
# 2. Slang Integration
# =========================================================
# Disable optional Slang components to keep builds smaller and faster.
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
# Disable mimalloc to avoid allocator conflicts with Qt/CRT on Windows.
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

# Add Slang submodule.
add_subdirectory(thirdparty/slang)
# MinGW/GCC: use large COFF objects for large Slang translation units.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(slang_slang PRIVATE -Wa,-mbig-obj)
endif()

# =========================================================
# 3. Qt 6 Configuration
# =========================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Required Qt modules.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# =========================================================
# 4. Sources and Resources
# =========================================================
set(ZEROSLACK_CORE_SOURCES
    # --- C++ sources ---
    activitylogpanelcoordinator.cpp
    activitylogservice.cpp
    analysiscoordinator.cpp
    analysiscoordinatordependencies.cpp
    analysiscoordinatorwiring.cpp
    analysisprogresscoordinator.cpp
    analysisscheduler.cpp
    analysisschedulerdocuments.cpp
    analysisschedulerrelationships.cpp
    analysisschedulerwiring.cpp
    clockresetdomainservice.cpp
    codepreviewservice.cpp
    columnnumbertool.cpp
    commandlayercommandregistry.cpp
    commandlayercoordinator.cpp
    commandlayerservice.cpp
    codetemplateservice.cpp
    customabbreviationservice.cpp
    mainwindow.cpp
    moduleblockdiagramservice.cpp
    modulebriefservice.cpp
    completioncommandmode.cpp
    completioncontexthelper.cpp
    completionmatcher.cpp
    completionmodel.cpp
    completionmodelupdates.cpp
    completionsemanticquery.cpp
    completionservicecommand.cpp
    completionservice.cpp
    completionservicematcher.cpp
    definitionnavigationservice.cpp
    definitionpreviewservice.cpp
    definitionservice.cpp
    diagnosticnavigationservice.cpp
    diagnosticservice.cpp
    diagnosticsrefreshcontroller.cpp
    documentmodel.cpp
    documentmodelconnections.cpp
    documentregistry.cpp
    documentregistrycursor.cpp
    documentregistryidentity.cpp
    documentregistryqueries.cpp
    documentregistryreader.cpp
    documentregistrystate.cpp
    documentsessionqueries.cpp
    documentsessionstate.cpp
    documentsessionversions.cpp
    editorappearance.cpp
    editorappearancepanel.cpp
    editorappearancesettings.cpp
    editorcompletionquery.cpp
    editorcompletionui.cpp
    editorcompletionworkflow.cpp
    effectivevalueservice.cpp
      editorcompletionworkflowkeys.cpp
      editorcoordinator.cpp
      editorcursornavigation.cpp
      editorfileidentity.cpp
      editorfolding.cpp
      editorgeometry.cpp
    editorgutter.cpp
    editorhoverpopup.cpp
    editorruntime.cpp
    editorselection.cpp
    editorsemanticcontextservice.cpp
    editorsemanticruntime.cpp
    editorsourcenavigationquery.cpp
    editorsourcenavigation.cpp
    editorsourcehover.cpp
    editorsyntaxstate.cpp
    editormodestate.cpp
    filecommandcoordinator.cpp
    foldblockshelfmodel.cpp
    foldblockshelfpanel.cpp
    foldshelfpersistenceservice.cpp
    foldshelfrestoreservice.cpp
    fsmgraphlayout.cpp
    fsmgraphservice.cpp
    formattersettings.cpp
    formatterservice.cpp
    ghostannotationservice.cpp
    wavepreviewpanelcoordinator.cpp
    wavepreviewservice.cpp
    workspaceconfigurationdialog.cpp
    workspaceconfigurationservice.cpp
    workspacesessionstateservice.cpp
    globalcontrolcoordinator.cpp
    globalcontrolpanel.cpp
    globalcontrolservice.cpp
    hierarchyservice.cpp
    hierarchyservicetraversal.cpp
    insightgraphview.cpp
    insightvisualstyle.cpp
    inlinecommandmode.cpp
    sourcenavigationservice.cpp
    statetransitiongraphservice.cpp
    statetransitiontriggerservice.cpp
    tsdocument.cpp
    mycodeeditor.cpp
    myhighlighter.cpp
    opendocumentanalysiscontroller.cpp
    opendocumentanalysiscontrollerlifecycle.cpp
    navigationcommandcoordinator.cpp
    navigationmanager.cpp
    navigationmanagerconnections.cpp
    navigationmanagerdata.cpp
    navigationmanagerstate.cpp
    navigationpanecoordinator.cpp
    packagetoolservice.cpp
    navigationservice.cpp
    navigationservicehierarchy.cpp
    navigationwidgetpresentation.cpp
    navigationwidgettrees.cpp
    navigationwidget.cpp
    problemspanelcoordinator.cpp
    projectmodel.cpp
    referencespanelcoordinator.cpp
    referenceservice.cpp
    relationshipspanelcoordinator.cpp
    relationshipspanelrefreshhierarchy.cpp
    relationshipspanelrefresh.cpp
    relationshipservice.cpp
    relationshipserviceordering.cpp
    relationshipanalysisqueue.cpp
    relationshipanalysisqueuelifecycle.cpp
    relationshipanalysiscontroller.cpp
    relationshipanalysiscontrollerasync.cpp
    relationshipanalysisworker.cpp
    relationshipresultpublisher.cpp
    rtlinsightspanelcoordinator.cpp
    rtlbatcheditservice.cpp
    saferenameservice.cpp
    searchservice.cpp
    semanticdiffservice.cpp
    semanticanalysisrequest.cpp
    semanticchangeclassifier.cpp
    semanticdependencygraph.cpp
    incrementalanalysisplanservice.cpp
    incrementalsemanticanalysisworker.cpp
    semanticsourceremap.cpp
    semanticdecorationservice.cpp
    svmacrosemantics.cpp
    semanticpanelutils.cpp
    semanticpanelrefreshpanels.cpp
    semanticpanelrefreshcoordinator.cpp
    semanticdockcoordinator.cpp
    semanticindex.cpp
    semanticindexcompletion.cpp
    semanticindexcompletiontypequeries.cpp
    semanticindexcompletioncontext.cpp
    semanticindexlookup.cpp
    semanticindexlookuphelpers.cpp
    semanticindexmodulecontext.cpp
    semanticindexmodulecontexthelpers.cpp
    semanticindexrelationships.cpp
    semanticindexstore.cpp
    semanticindexsnapshot.cpp
    semanticindexsnapshotqueries.cpp
    semanticindexsnapshotpublication.cpp
    semanticruntimecoordinator.cpp
    semanticruntimewiring.cpp
    signaljourneyservice.cpp
    signalkernelgraphservice.cpp
    signalusagehotspotpanel.cpp
    signalusagehotspotservice.cpp
    signalkernelgraphpanelcoordinator.cpp
    slangmanagerdiagnostics.cpp
    slangrelationshiphelpers.cpp
    smartrelationshipbuilderanalysis.cpp
    smartrelationshipbuilder.cpp
    smartrelationshipbuildercontext.cpp
    smartrelationshipbuildertiming.cpp
    symbolanalyzer.cpp
    symbolanalyzerasync.cpp
    symbolanalyzerincremental.cpp
    symbolanalyzerpublication.cpp
    symbolanalyzerslang.cpp
    symbolanalyzerworkspace.cpp
    symbolhoverservice.cpp
    symbolpresentationservice.cpp
    symbolrelationshipqueries.cpp
    symbolrelationshipengine.cpp
    symboltaxonomy.cpp
    slangsymbolcollector.cpp
    slangsymbolcollectorhelpers.cpp
    slangsymbolpresentation.cpp
    slangmanagerrelationships.cpp
    slangmanagersymbols.cpp
    tabdocumentqueries.cpp
    tabfileio.cpp
    tabmanager.cpp
    tabopencontroller.cpp
    tabsavecontroller.cpp
    tabtitlecontroller.cpp
    workspacemanager.cpp
    workspaceanalysisplanservice.cpp
    workspaceignoreservice.cpp
    usertemplateservice.cpp
    workspaceanalysisrequestqueue.cpp
    workspacesymbolanalysiscontroller.cpp
    workspacesymbolanalysiscontrollerlifecycle.cpp

    # --- Headers (not required by CMake, listed for IDEs) ---
    activitylogpanelcoordinator.h
    activitylogservice.h
    analysiscoordinator.h
    analysisprogresscoordinator.h
    analysisscheduler.h
    clockresetdomainservice.h
    codepreviewservice.h
    columnnumbertool.h
    commandlayercommandregistry.h
    commandlayercoordinator.h
    commandlayerservice.h
    codetemplateservice.h
    customabbreviationservice.h
    mainwindow.h
    moduleblockdiagramservice.h
    modulebriefservice.h
    completioncommandmode.h
    completioncommandkindadapter.h
    version.h.in
    ${ZEROSLACK_GENERATED_INCLUDE_DIR}/version.h
    completionmodel.h
    completionmatcher.h
    completioncontexthelper.h
    completionservice.h
    completionsemanticquery.h
    completiontypes.h
    definitionnavigationservice.h
    definitionpreviewservice.h
    definitionservice.h
    diagnosticnavigationservice.h
    diagnosticservice.h
    diagnosticsrefreshcontroller.h
    documentmodel.h
    documentregistry.h
    documentsnapshot.h
    documentsessionstate.h
    editorappearance.h
    editorappearancepanel.h
    editorappearancesettings.h
    editorcompletionquery.h
    editorcompletionui.h
    editorcompletionworkflow.h
    effectivevalueservice.h
      editorcoordinator.h
      editorcursornavigation.h
      editorfileidentity.h
      editorfolding.h
      editorgeometry.h
    editorgutter.h
    editorhoverpopup.h
    editorruntime.h
    editorselection.h
    editorsemanticcontextservice.h
    editorsemanticruntime.h
    editorsourcenavigationquery.h
    editorsourcenavigation.h
    editorsourcehover.h
    editorsyntaxstate.h
    editormodestate.h
    filecommandcoordinator.h
    foldblockshelfmodel.h
    foldblockshelfpanel.h
    foldshelfpersistenceservice.h
    foldshelfrestoreservice.h
    fsmgraphlayout.h
    fsmgraphservice.h
    formattersettings.h
    formatterservice.h
    ghostannotationservice.h
    wavepreviewpanelcoordinator.h
    wavepreviewservice.h
    workspaceconfigurationdialog.h
    workspaceconfigurationservice.h
    workspacesessionstateservice.h
    globalcontrolcoordinator.h
    globalcontrolpanel.h
    globalcontrolservice.h
    hierarchyservice.h
    insightgraphview.h
    insightvisualstyle.h
    inlinecommandmode.h
    sourcenavigationservice.h
    statetransitiongraphservice.h
    statetransitiontriggerservice.h
    tsdocument.h
    mycodeeditor.h
    myhighlighter.h
    opendocumentanalysiscontroller.h
    modulehierarchymodel.h
    navigationcommandcoordinator.h
    navigationmanager.h
    navigationpanecoordinator.h
    packagetoolservice.h
    navigationservice.h
    navigationwidget.h
    problemspanelcoordinator.h
    projectmodel.h
    referencespanelcoordinator.h
    referenceservice.h
    relationshipspanelcoordinator.h
    relationshipservice.h
    relationshipserviceordering.h
    relationshipanalysisqueue.h
    relationshipanalysiscontroller.h
    relationshipanalysisworker.h
    relationshipresultpublisher.h
    rtlinsightspanelcoordinator.h
    rtlbatcheditservice.h
    saferenameservice.h
    searchservice.h
    semanticdiffservice.h
    semanticanalysisrequest.h
    semanticchangeclassifier.h
    semanticdependencygraph.h
    incrementalanalysisplanservice.h
    incrementalsemanticanalysisworker.h
    semanticsourceremap.h
    semanticdecorationservice.h
    svmacrosemantics.h
    semanticpanelutils.h
    semanticpanelrefreshcoordinator.h
    semanticdockcoordinator.h
    semanticindex.h
    semanticsourcerange.h
    semanticindexcompletionfilters.h
    semanticindexlookuphelpers.h
    semanticindexmodulecontexthelpers.h
    semanticindexsnapshot.h
    semanticruntimecoordinator.h
    signaljourneyservice.h
    signalkernelgraphservice.h
    signalusagehotspotpanel.h
    signalusagehotspotservice.h
    signalkernelgraphpanelcoordinator.h
    slangmanager.h
    slangparseoptions.h
    slangrelationshiphelpers.h
    slangsymbolcollector.h
    slangsymbolcollectorhelpers.h
    slangsymbolpresentation.h
    smartrelationshipbuilder.h
    symbolanalyzer.h
    symbolanalyzerworkspace.h
    symbolhoverreports.h
    symbolhoverservice.h
    symbolpresentationservice.h
    symbolrelationshipengine.h
    symboloutlinemodel.h
    symbolsemanticmetadata.h
    symboltaxonomy.h
    tabdocumentqueries.h
    tabfileio.h
    tabmanager.h
    tabopencontroller.h
    tabsavecontroller.h
    tabtitlecontroller.h
    workspacemanager.h
    workspaceanalysisplanservice.h
    workspaceignoreservice.h
    usertemplateservice.h
    workspaceanalysisrequestqueue.h
    workspacesymbolanalysiscontroller.h

    # --- UI files ---
    mainwindow.ui

    # --- Resource files ---
    code.qrc
    images.qrc

    # --- Third-party C sources (tree-sitter) ---
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
)

# =========================================================
# 5. Core Library and Executable
# =========================================================
add_library(zeroslack_core STATIC ${ZEROSLACK_CORE_SOURCES})

# Main application target.
add_executable(demo main.cpp)

# Build a Windows GUI app without a console window.
if(WIN32)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

# =========================================================
# 6. Include Paths and Libraries
# =========================================================
# Project include paths.
target_include_directories(zeroslack_core PUBLIC
    ${ZEROSLACK_GENERATED_INCLUDE_DIR}
    thirdparty/tree_sitter/lib/include
    thirdparty/tree_sitter/lib/src
)

# Keep the repository root available for project-local "..." includes without
# letting root VERSION shadow the C++20 <version> standard header on Windows.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zeroslack_core PUBLIC
        "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
endif()

# Link libraries: Qt6 and Slang.
target_link_libraries(zeroslack_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    slang::slang
)

target_link_libraries(demo PRIVATE zeroslack_core)

# =========================================================
# 7. Compiler Flags and Definitions
# =========================================================

# Suppress selected warnings for tree-sitter C sources.
set_source_files_properties(
    thirdparty/tree_sitter/lib/src/lib.c
    thirdparty/tree_sitter_systemverilog/src/parser.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
)

# MinGW-specific definitions.
if(MINGW)
    target_compile_definitions(zeroslack_core PUBLIC __USE_MINGW_ANSI_STDIO=1)
endif()

# =========================================================
# 8. Regression Tests
# =========================================================
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(ZEROSLACK_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(ZEROSLACK_TEST_PATH
        "${ZEROSLACK_COMPILER_BIN_DIR};${QT_INSTALL_PREFIX}/bin;$ENV{PATH}"
    )
    string(REPLACE ";" "\\;" ZEROSLACK_TEST_PATH "${ZEROSLACK_TEST_PATH}")

    add_executable(ts_doc_test test_sv/ts_doc_test.cpp)
    target_link_libraries(ts_doc_test PRIVATE zeroslack_core)

    add_executable(completion_test test_sv/completion_test.cpp)
    target_link_libraries(completion_test PRIVATE zeroslack_core)

    add_executable(effective_value_test test_sv/effective_value_test.cpp)
    target_link_libraries(effective_value_test PRIVATE zeroslack_core)

    add_executable(jump_test test_sv/jump_test.cpp)
    target_link_libraries(jump_test PRIVATE zeroslack_core)

    add_executable(relationship_test test_sv/relationship_test.cpp)
    target_link_libraries(relationship_test PRIVATE zeroslack_core)

    add_executable(fsm_ui_snapshot_test test_sv/fsm_ui_snapshot_test.cpp)
    set_target_properties(fsm_ui_snapshot_test PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )
    target_link_libraries(fsm_ui_snapshot_test PRIVATE zeroslack_core)

    add_executable(gui_smoke_test test_sv/gui_smoke_test.cpp)
    target_link_libraries(gui_smoke_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(global_control_ow_test
        test_sv/global_control_ow_test.cpp)
    target_link_libraries(global_control_ow_test PRIVATE zeroslack_core)

    add_executable(insight_visual_style_test
        test_sv/insight_visual_style_test.cpp)
    target_link_libraries(insight_visual_style_test PRIVATE zeroslack_core)

    add_executable(signal_usage_hotspot_panel_test
        test_sv/signal_usage_hotspot_panel_test.cpp)
    target_link_libraries(signal_usage_hotspot_panel_test PRIVATE zeroslack_core)

    add_executable(large_file_perf_test test_sv/large_file_perf_test.cpp)
    target_link_libraries(large_file_perf_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_incremental_test
        test_sv/editor_incremental_test.cpp)
    target_link_libraries(editor_incremental_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(analysis_scheduler_test
        test_sv/analysis_scheduler_test.cpp)
    target_link_libraries(analysis_scheduler_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(relationship_perf_test test_sv/relationship_perf_test.cpp)
    target_link_libraries(relationship_perf_test PRIVATE zeroslack_core)

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
    add_test(NAME effective_value_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:effective_value_test>
    )
    add_test(NAME fsm_ui_snapshot_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:fsm_ui_snapshot_test>
            ${CMAKE_CURRENT_SOURCE_DIR}
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
    add_test(NAME global_control_ow_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:global_control_ow_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/huge_prj
    )
    set_tests_properties(global_control_ow_test PROPERTIES TIMEOUT 1200)
    add_test(NAME insight_visual_style_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:insight_visual_style_test>
    )
    add_test(NAME signal_usage_hotspot_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:signal_usage_hotspot_panel_test>
    )
    add_test(NAME large_file_perf_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:large_file_perf_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
    )
    add_test(NAME editor_incremental_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_incremental_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new/elec_phy_import/top/rtl_top.sv
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/huge_prj
    )
    add_test(NAME analysis_scheduler_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:analysis_scheduler_test>
    )
    add_test(NAME legacy_field_policy_guard
        COMMAND ${CMAKE_COMMAND}
            -DZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/legacy_field_policy_guard.ctest
    )
endif()
