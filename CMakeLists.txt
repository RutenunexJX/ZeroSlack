cmake_minimum_required(VERSION 3.27)

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

set(ZEROSLACK_WAVEWIDGETS_VALIDATION_LIBRARY "" CACHE FILEPATH
    "Optional wavewidgets library used for the real waveform-preview ABI CTest")
set(ZEROSLACK_LEGACY_FIXTURE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Read-only source tree containing the legacy .zs compatibility fixtures")

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

option(ZEROSLACK_SHARED_CORE
    "Build zeroslack_core as a shared library (disable only for compatibility comparisons)"
    ON)

set(ZEROSLACK_LINK_JOBS "1" CACHE STRING
    "Maximum concurrent Ninja link jobs (use 1 for MinGW GNU ld stability)")
if(NOT ZEROSLACK_LINK_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ZEROSLACK_LINK_JOBS must be a positive integer")
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS
        zeroslack_link_pool=${ZEROSLACK_LINK_JOBS})
    set(CMAKE_JOB_POOL_LINK zeroslack_link_pool)
endif()

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
    # Read the inherited PATH when the rule runs. Embedding the complete
    # configure-time PATH makes harmless IDE/session PATH changes alter
    # every Ninja command and forces a full rebuild after reconfigure.
    set(ZEROSLACK_BUILD_TOOL_LAUNCHER
        "${CMAKE_COMMAND} -E env --modify \"PATH=path_list_prepend:${ZEROSLACK_COMPILER_BIN_DIR}\" --")
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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg Network)
find_package(SuiteApp 1 CONFIG QUIET)

# =========================================================
# 4. Sources and Resources
# =========================================================
set(ZEROSLACK_CORE_SOURCES
    # --- C++ sources ---
    src/commands/actionregistry.cpp
    src/editor/annotationlayer.cpp
    src/ui/activitylogpanelcoordinator.cpp
    src/ui/activitylogservice.cpp
    src/analysis/analysiscoordinator.cpp
    src/analysis/analysiscoordinatordependencies.cpp
    src/analysis/analysiscoordinatorwiring.cpp
    src/analysis/analysisprogresscoordinator.cpp
    src/analysis/analysisscheduler.cpp
    src/analysis/analysisschedulerdocuments.cpp
    src/analysis/analysisschedulerrelationships.cpp
    src/analysis/analysisschedulerwiring.cpp
    src/ui/applicationthememanager.cpp
    src/semantic/clockresetdomainservice.cpp
    src/navigation/codepreviewservice.cpp
    src/editor/columnnumbertool.cpp
    src/commands/commandlayercommandregistry.cpp
    src/commands/commandlayercoordinator.cpp
    src/commands/commandlayerservice.cpp
    src/completion/codetemplatecontextanalyzer.cpp
    src/completion/codetemplateservice.cpp
    src/completion/customabbreviationservice.cpp
    src/app/mainwindow.cpp
    src/app/mainwindowcontextworkspace.cpp
    src/insights/rtlactioncoordinator.cpp
    src/insights/moduleblockdiagramservice.cpp
    src/semantic/modulebriefservice.cpp
    src/completion/completioncommandmode.cpp
    src/completion/completioncontexthelper.cpp
    src/completion/completionmatcher.cpp
    src/completion/completionmodel.cpp
    src/completion/completionmodelupdates.cpp
    src/completion/completionsemanticquery.cpp
    src/completion/completionservicecommand.cpp
    src/completion/completionservice.cpp
    src/completion/completionservicematcher.cpp
    src/navigation/definitionnavigationservice.cpp
    src/navigation/definitionpreviewservice.cpp
    src/navigation/definitionservice.cpp
    src/commands/declaresignalfactcollector.cpp
    src/commands/declaresignalservice.cpp
    src/navigation/diagnosticnavigationservice.cpp
    src/analysis/diagnosticservice.cpp
    src/analysis/diagnosticsrefreshcontroller.cpp
    src/documents/documentmodel.cpp
    src/documents/documentmodelconnections.cpp
    src/documents/documentregistry.cpp
    src/documents/documentregistrycursor.cpp
    src/documents/documentregistryidentity.cpp
    src/documents/documentregistryqueries.cpp
    src/documents/documentregistryreader.cpp
    src/documents/documentregistrystate.cpp
    src/documents/shareddocument.cpp
    src/documents/crashrecoveryservice.cpp
    src/documents/externaldocumentsynccontroller.cpp
    src/documents/documentsessionqueries.cpp
    src/documents/documentsessionstate.cpp
    src/documents/documentsessionversions.cpp
    src/settings/editorappearance.cpp
    src/settings/editorappearancepanel.cpp
    src/settings/editorappearancesettings.cpp
    src/editor/editorcompletionquery.cpp
    src/editor/editorcompletionui.cpp
    src/editor/editorcompletionworkflow.cpp
    src/semantic/effectivevalueservice.cpp
      src/editor/editorcompletionworkflowkeys.cpp
      src/editor/editorcoordinator.cpp
      src/editor/editorcolumnmodecontroller.cpp
      src/editor/editorcursornavigation.cpp
      src/editor/editorlexicalboundary.cpp
      src/editor/editorfileidentity.cpp
      src/editor/editorfolding.cpp
      src/editor/editorviewprojection.cpp
      src/editor/editorgeometry.cpp
    src/editor/editorgutter.cpp
    src/editor/editorhoverpopup.cpp
    src/editor/editorinsertpaletteservice.cpp
    src/editor/editorruntime.cpp
    src/editor/editorruntimeannotations.cpp
    src/editor/editorruntimecommands.cpp
    src/editor/editorruntimenavigation.cpp
    src/editor/editorruntimerename.cpp
    src/editor/editorselection.cpp
    src/editor/editordroppreviewoverlay.cpp
    src/editor/editorsplitcontroller.cpp
    src/editor/editorkeywordghostcontroller.cpp
    src/editor/editorlineoperationcontroller.cpp
    src/editor/editormulticursorcontroller.cpp
    src/editor/editorsignalselectioncontroller.cpp
    src/editor/editorstructuralinputcontroller.cpp
    src/editor/editortemplateslotcontroller.cpp
    src/editor/editoractioncontextservice.cpp
    src/editor/editorcontextmenumodel.cpp
    src/editor/editorsemanticcontextservice.cpp
    src/editor/editorsemanticruntime.cpp
    src/editor/editorsourcenavigationquery.cpp
    src/editor/editorsourcenavigation.cpp
    src/editor/editorsourcehover.cpp
    src/editor/editorsyntaxstate.cpp
    src/editor/editormodecontroller.cpp
    src/workspace/filecommandcoordinator.cpp
    src/editor/foldblockshelfmodel.cpp
    src/editor/foldblockshelfpanel.cpp
    src/editor/foldshelfpersistenceservice.cpp
    src/editor/foldshelfrestoreservice.cpp
    src/insights/fsmgraphlayout.cpp
    src/insights/fsmgraphservice.cpp
    src/editor/formattercursoranchor.cpp
    src/editor/formatterservice.cpp
    src/settings/settingscenterschema.cpp
    src/settings/settingscenterpanel.cpp
    src/settings/settingscenterservice.cpp
    src/editor/structuredwhitespaceformatter.cpp
    src/editor/ghostannotationservice.cpp
    src/insights/graphexportservice.cpp
    src/integrations/wave/wavetoolchainbundleservice.cpp
    src/integrations/wave/wavesimulationconfiguration.cpp
    src/integrations/wave/wavesimulationcoordinator.cpp
    src/integrations/wave/wavesimulationresultnavigationcoordinator.cpp
    src/integrations/wave/waveembeddedworkspaceloader.cpp
    src/integrations/wave/waveformpreviewloader.cpp
    src/integrations/wave/wavesimulationmodulemanifest.cpp
    src/integrations/wave/wavesimulationmanifestservice.cpp
    src/integrations/wave/wavesimulationpreparationservice.cpp
    src/integrations/wave/wavepreviewpanelcoordinator.cpp
    src/integrations/wave/wavepreviewpayloadadapter.cpp
    src/integrations/wave/wavepreviewservice.cpp
    src/settings/workspaceconfigurationdialog.cpp
    src/settings/workspaceconfigurationservice.cpp
    src/workspace/workspacesessioncoordinator.cpp
    src/workspace/workspacesessionstateservice.cpp
    src/commands/globalcontrolcoordinator.cpp
    src/ui/globalcontrolpanel.cpp
    src/commands/globalcontrolservice.cpp
    src/semantic/hierarchyservice.cpp
    src/semantic/hierarchyservicetraversal.cpp
    src/insights/insightgraphview.cpp
    src/insights/insightgraphcore.cpp
    src/insights/insightcanvas.cpp
    src/insights/insightfocuscontroller.cpp
    src/insights/insightfocuscontrolleractions.cpp
    src/ui/insightvisualstyle.cpp
    src/insights/liveinsightsession.cpp
    src/insights/liveinsightscontextview.cpp
    src/insights/liveinsightscontextprovider.cpp
    src/insights/liveinsighttoolpage.cpp
    src/insights/rtlinsightviewplugins.cpp
    src/insights/rtlinsightworkbench.cpp
    src/commands/inlinecommandmode.cpp
    src/navigation/sourcenavigationservice.cpp
    src/insights/statetransitiongraphservice.cpp
    src/insights/statetransitiontriggerservice.cpp
    src/ui/contextresource.cpp
    src/ui/contextrail.cpp
    src/ui/contextpeekhost.cpp
    src/ui/contextdockhost.cpp
    src/ui/contextworkspacecontroller.cpp
    src/integrations/pinloom/pinloomhostclient.cpp
    src/integrations/pinloom/pinloomcodelinkstore.cpp
    src/integrations/pinloom/pinloomcodelinkcoordinator.cpp
    src/integrations/pinloom/pinloomcontextview.cpp
    src/integrations/pinloom/pinloomcontextprovider.cpp
    src/integrations/suite/suitecontextcatalog.cpp
    src/semantic/semanticstateview.cpp
    src/workspace/workspacehubtypes.cpp
    src/workspace/workspacehubsession.cpp
    src/workspace/workspacehubmodel.cpp
    src/workspace/workspacehubview.cpp
    src/workspace/workspacehubcontextprovider.cpp
    src/workspace/workspacehubsuitebridge.cpp
    src/ui/temporaryeditorsession.cpp
    src/ui/temporaryeditorcontextview.cpp
    src/ui/temporaryeditorcontextprovider.cpp
    src/ui/temporaryeditorsearchpopup.cpp
    src/ui/temporaryeditorsearchprovider.cpp
    src/editor/triviapositionmap.cpp
    src/editor/tsdocument.cpp
    src/documents/unsaveddocumentmanager.cpp
    src/editor/mycodeeditor.cpp
    src/editor/myhighlighter.cpp
    src/navigation/navigationcommandcoordinator.cpp
    src/navigation/navigationmanager.cpp
    src/navigation/navigationmanagerconnections.cpp
    src/navigation/navigationmanagerdata.cpp
    src/navigation/navigationmanagerfileoperations.cpp
    src/navigation/navigationmanagerstate.cpp
    src/ui/navigationpanecoordinator.cpp
    src/ui/panellayoutcontroller.cpp
    src/commands/packagetoolservice.cpp
    src/navigation/navigationservice.cpp
    src/navigation/navigationservicehierarchy.cpp
    src/ui/navigationwidgetpresentation.cpp
    src/ui/navigationwidgettrees.cpp
    src/ui/navigationwidget.cpp
    src/ui/notificationcenter.cpp
    src/ui/problemspanelcoordinator.cpp
    src/workspace/projectmodel.cpp
    src/semantic/relationshipservice.cpp
    src/semantic/relationshipserviceordering.cpp
    src/semantic/relationshipanalysisqueue.cpp
    src/semantic/relationshipanalysisqueuelifecycle.cpp
    src/semantic/relationshipanalysiscontroller.cpp
    src/semantic/relationshipanalysiscontrollerasync.cpp
    src/semantic/relationshipanalysisworker.cpp
    src/semantic/relationshipresultpublisher.cpp
    src/insights/rtlinsightspanelactions.cpp
    src/insights/rtlinsightspanelcoordinator.cpp
    src/insights/rtlinsightspaneltestapi.cpp
    src/insights/rtlinsightspanelviewstate.cpp
    src/insights/rtlinsightsgraphcontroller.cpp
    src/insights/rtlinsightsgraphscenemapper.cpp
    src/insights/rtlinsightspresenter.cpp
    src/insights/rtlbatcheditservice.cpp
    src/insights/rtlconnectiontransformplanner.cpp
    src/insights/rtlconnectiontransformworkflow.cpp
    src/insights/rtlhighriskeditworkflow.cpp
    src/insights/rtlhighriskeditpanel.cpp
    src/commands/multisignalpropagationplanner.cpp
    src/commands/multisignalpropagationpanel.cpp
    src/commands/instancepairconnectionfacade.cpp
    src/commands/instancepairconnectionpanel.cpp
    src/commands/instancepairconnectionworkflow.cpp
    src/insights/rtlrenameplanner.cpp
    src/insights/rtlrenameworkflow.cpp
    src/commands/saferenameservice.cpp
    src/semantic/semanticrenamesupport.cpp
    src/commands/scopedsearchpanel.cpp
    src/commands/scopedreplaceworkflow.cpp
    src/commands/searchservice.cpp
    src/semantic/semanticdiffservice.cpp
    src/semantic/semanticanalysisrequest.cpp
    src/semantic/semanticchangeclassifier.cpp
    src/semantic/semanticdependencygraph.cpp
    src/analysis/incrementalanalysisplanservice.cpp
    src/analysis/incrementalsemanticanalysisworker.cpp
    src/semantic/semanticsourceremap.cpp
    src/semantic/semanticdecorationservice.cpp
    src/semantic/semanticpanelutils.cpp
    src/semantic/semanticpanelrefreshpanels.cpp
    src/semantic/semanticpanelrefreshcoordinator.cpp
    src/semantic/semanticdockcoordinator.cpp
    src/semantic/semanticindex.cpp
    src/semantic/semanticindexcompletion.cpp
    src/semantic/semanticindexcompletiontypequeries.cpp
    src/semantic/semanticindexcompletioncontext.cpp
    src/semantic/semanticindexlookup.cpp
    src/semantic/semanticindexlookuphelpers.cpp
    src/semantic/semanticindexmodulecontext.cpp
    src/semantic/semanticindexmodulecontexthelpers.cpp
    src/semantic/semanticindexrelationships.cpp
    src/semantic/semanticindexstore.cpp
    src/semantic/semanticindexsnapshot.cpp
    src/semantic/semanticindexsnapshotqueries.cpp
    src/semantic/semanticindexsnapshotpublication.cpp
    src/semantic/semanticruntimecoordinator.cpp
    src/semantic/semanticruntimewiring.cpp
    src/semantic/semanticstableidentity.cpp
    src/semantic/signaljourneyservice.cpp
    src/insights/signalkernelgraphservice.cpp
    src/insights/signalusagehotspotpanel.cpp
    src/insights/signalusagehotspotpanelactions.cpp
    src/insights/signalusagehotspotservice.cpp
    src/insights/signalkernelgraphpanelcoordinator.cpp
    src/semantic/slangmanagerdiagnostics.cpp
    src/semantic/slangrelationshiphelpers.cpp
    src/semantic/smartrelationshipbuilderanalysis.cpp
    src/semantic/smartrelationshipbuilder.cpp
    src/semantic/smartrelationshipbuildercontext.cpp
    src/semantic/smartrelationshipbuildertiming.cpp
    src/semantic/symbolanalyzer.cpp
    src/semantic/symbolanalyzerasync.cpp
    src/semantic/symbolanalyzerincremental.cpp
    src/semantic/symbolanalyzerpublication.cpp
    src/semantic/symbolanalyzerslang.cpp
    src/semantic/symbolanalyzerworkspace.cpp
    src/semantic/symbolhoverservice.cpp
    src/semantic/symbolpresentationservice.cpp
    src/semantic/symbolrelationshipqueries.cpp
    src/semantic/symbolrelationshipengine.cpp
    src/semantic/symboltaxonomy.cpp
    src/semantic/slangsymbolcollector.cpp
    src/semantic/slangsymbolcollectorhelpers.cpp
    src/semantic/slangsymbolpresentation.cpp
    src/semantic/slangmanagerrelationships.cpp
    src/semantic/slangmanagersymbols.cpp
    src/semantic/slangpreprocessorfacts.cpp
    src/documents/tabdocumentqueries.cpp
    src/documents/tabfileio.cpp
    src/documents/tabmanager.cpp
    src/workspace/workspacemanager.cpp
    src/workspace/workspacefileoperationservice.cpp
    src/analysis/workspaceanalysisplanservice.cpp
    src/workspace/workspaceignoreservice.cpp
    src/completion/usertemplateservice.cpp
    src/analysis/workspaceanalysisrequestqueue.cpp
    src/analysis/workspacesymbolanalysiscontroller.cpp
    src/analysis/workspacesymbolanalysiscontrollerlifecycle.cpp
    src/workspace/workspaceeditdocumentmanager.cpp
    src/workspace/workspaceedittransactionservice.cpp

    # --- Headers (not required by CMake, listed for IDEs) ---
    src/ui/activitylogpanelcoordinator.h
    src/ui/activitylogservice.h
    src/analysis/analysiscoordinator.h
    src/analysis/analysisprogresscoordinator.h
    src/analysis/analysisscheduler.h
    src/semantic/clockresetdomainservice.h
    src/navigation/codepreviewservice.h
    src/editor/columnnumbertool.h
    src/commands/commandlayercommandregistry.h
    src/commands/commandlayercoordinator.h
    src/commands/commandlayerservice.h
    src/completion/codetemplatecontextanalyzer.h
    src/completion/codetemplateservice.h
    src/completion/customabbreviationservice.h
    src/app/mainwindow.h
    src/ui/workspacechrome.h
    src/ui/roundedicons.h
    src/ui/uitypography.h
    src/insights/moduleblockdiagramservice.h
    src/semantic/modulebriefservice.h
    src/completion/completioncommandmode.h
    src/completion/completioncommandkindadapter.h
    version.h.in
    ${ZEROSLACK_GENERATED_INCLUDE_DIR}/version.h
    src/completion/completionmodel.h
    src/completion/completionmatcher.h
    src/completion/completioncontexthelper.h
    src/completion/completionservice.h
    src/completion/completionsemanticquery.h
    src/completion/completiontypes.h
    src/navigation/definitionnavigationservice.h
    src/navigation/definitionpreviewservice.h
    src/navigation/definitionservice.h
    src/navigation/diagnosticnavigationservice.h
    src/analysis/diagnosticservice.h
    src/analysis/diagnosticsrefreshcontroller.h
    src/documents/documentmodel.h
    src/documents/documentregistry.h
    src/documents/documentsnapshot.h
    src/documents/documentsessionstate.h
    src/documents/shareddocument.h
    src/settings/editorappearance.h
    src/settings/editorappearancepanel.h
    src/settings/editorappearancesettings.h
    src/editor/editorcompletionquery.h
    src/editor/editorcompletionui.h
    src/editor/editorcompletionworkflow.h
    src/semantic/effectivevalueservice.h
      src/editor/editorcoordinator.h
      src/editor/editorcursornavigation.h
      src/editor/editorlexicalboundary.h
      src/editor/editorfileidentity.h
      src/editor/editorfoldviewstate.h
      src/editor/editorfolding.h
      src/editor/editorviewprojection.h
      src/editor/editorgeometry.h
    src/editor/editorgutter.h
    src/editor/editorhoverpopup.h
    src/editor/editorinsertpaletteservice.h
    src/editor/editorruntime.h
    src/editor/editorselection.h
    src/editor/editorkeywordghostcontroller.h
    src/editor/editorlineoperationcontroller.h
    src/editor/editormulticursorcontroller.h
    src/editor/editorstructuralinputcontroller.h
    src/editor/editoractioncontextservice.h
    src/editor/editorcontextmenumodel.h
    src/editor/editorsemanticcontextservice.h
    src/editor/editorsemanticruntime.h
    src/editor/editorsourcenavigationquery.h
    src/editor/editorsourcenavigation.h
    src/editor/editorsourcehover.h
    src/editor/editorsyntaxstate.h
    src/editor/editormodecontroller.h
    src/workspace/filecommandcoordinator.h
    src/editor/foldblockshelfmodel.h
    src/editor/foldblockshelfpanel.h
    src/editor/foldshelfpersistenceservice.h
    src/editor/foldshelfrestoreservice.h
    src/insights/fsmgraphlayout.h
    src/insights/fsmgraphservice.h
    src/editor/formattercursoranchor.h
    src/editor/formatterservice.h
    src/editor/ghostannotationservice.h
    src/integrations/wave/wavepreviewpanelcoordinator.h
    src/integrations/wave/waveformpreviewloader.h
    src/integrations/wave/wavepreviewpayloadadapter.h
    src/integrations/wave/wavepreviewservice.h
    src/settings/workspaceconfigurationdialog.h
    src/settings/workspaceconfigurationservice.h
    src/workspace/workspacesessioncoordinator.h
    src/workspace/workspacesessionstateservice.h
    src/workspace/workspaceeditdocumentmanager.h
    src/workspace/workspaceedittransactionservice.h
    src/commands/globalcontrolcoordinator.h
    src/ui/globalcontrolpanel.h
    src/commands/globalcontrolservice.h
    src/semantic/hierarchyservice.h
    src/insights/insightgraphview.h
    src/insights/insightgraphcore.h
    src/insights/insightcanvas.h
    src/insights/insightfocuscontroller.h
    src/ui/insightvisualstyle.h
    src/insights/liveinsighttypes.h
    src/insights/liveinsightsession.h
    src/insights/liveinsightscontextview.h
    src/insights/liveinsightscontextprovider.h
    src/insights/liveinsighttoolpage.h
    src/insights/rtlinsightviewplugins.h
    src/insights/rtlinsightworkbench.h
    src/editor/editorsearchcandidate.h
    src/commands/inlinecommandmode.h
    src/navigation/sourcenavigationservice.h
    src/insights/statetransitiongraphservice.h
    src/insights/statetransitiontriggerservice.h
    src/ui/contextresource.h
    src/ui/contextworkspacestate.h
    src/ui/contextcontentprovider.h
    src/ui/contextrail.h
    src/ui/contextpeekhost.h
    src/ui/contextdockhost.h
    src/ui/contextworkspacecontroller.h
    src/ui/temporaryeditorsession.h
    src/ui/temporaryeditorcontextview.h
    src/ui/temporaryeditorcontextprovider.h
    src/ui/temporaryeditorsearchpopup.h
    src/ui/temporaryeditorsearchprovider.h
    src/editor/triviapositionmap.h
    src/editor/tsdocument.h
    src/editor/mycodeeditor.h
    src/editor/myhighlighter.h
    src/semantic/modulehierarchymodel.h
    src/navigation/navigationcommandcoordinator.h
    src/navigation/navigationmanager.h
    src/ui/navigationpanecoordinator.h
    src/commands/packagetoolservice.h
    src/navigation/navigationservice.h
    src/ui/navigationwidget.h
    src/ui/problemspanelcoordinator.h
    src/workspace/projectmodel.h
    src/semantic/relationshipservice.h
    src/semantic/relationshipserviceordering.h
    src/semantic/relationshipanalysisqueue.h
    src/semantic/relationshipanalysiscontroller.h
    src/semantic/relationshipanalysisworker.h
    src/semantic/relationshipresultpublisher.h
    src/insights/rtlinsightspanelcoordinator.h
    src/insights/rtlactioncoordinator.h
    src/insights/rtlbatcheditservice.h
    src/commands/saferenameservice.h
    src/commands/scopedsearchpanel.h
    src/commands/searchservice.h
    src/semantic/semanticdiffservice.h
    src/semantic/semanticanalysisrequest.h
    src/semantic/semanticchangeclassifier.h
    src/semantic/semanticdependencygraph.h
    src/analysis/incrementalanalysisplanservice.h
    src/analysis/incrementalsemanticanalysisworker.h
    src/semantic/semanticsourceremap.h
    src/semantic/semanticdecorationservice.h
    src/semantic/semanticpanelutils.h
    src/semantic/semanticpanelrefreshcoordinator.h
    src/semantic/semanticdockcoordinator.h
    src/semantic/semanticindex.h
    src/semantic/semanticsourcerange.h
    src/semantic/semanticindexcompletionfilters.h
    src/semantic/semanticindexlookuphelpers.h
    src/semantic/semanticindexmodulecontexthelpers.h
    src/semantic/semanticindexsnapshot.h
    src/semantic/semanticruntimecoordinator.h
    src/semantic/semanticstableidentity.h
    src/semantic/signaljourneyservice.h
    src/insights/signalkernelgraphservice.h
    src/insights/signalusagehotspotpanel.h
    src/insights/signalusagehotspotservice.h
    src/insights/signalkernelgraphpanelcoordinator.h
    src/semantic/slangmanager.h
    src/semantic/slangpreprocessorfacts.h
    src/semantic/slangparseoptions.h
    src/semantic/slangrelationshiphelpers.h
    src/semantic/slangsymbolcollector.h
    src/semantic/slangsymbolcollectorhelpers.h
    src/semantic/slangsymbolpresentation.h
    src/semantic/smartrelationshipbuilder.h
    src/semantic/symbolanalyzer.h
    src/semantic/symbolanalyzerworkspace.h
    src/semantic/symbolhoverreports.h
    src/semantic/symbolhoverservice.h
    src/semantic/symbolpresentationservice.h
    src/semantic/symbolrelationshipengine.h
    src/semantic/symboloutlinemodel.h
    src/semantic/symbolsemanticmetadata.h
    src/semantic/symboltaxonomy.h
    src/documents/tabdocumentqueries.h
    src/documents/tabfileio.h
    src/documents/tabmanager.h
    src/workspace/workspacemanager.h
    src/analysis/workspaceanalysisplanservice.h
    src/workspace/workspaceignoreservice.h
    src/completion/usertemplateservice.h
    src/analysis/workspaceanalysisrequestqueue.h
    src/analysis/workspacesymbolanalysiscontroller.h

    # --- UI files ---
    src/app/mainwindow.ui

    # --- Resource files ---
    code.qrc
    images.qrc
)

set(ZEROSLACK_TREE_SITTER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree_sitter/lib/src/lib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree_sitter_systemverilog/src/parser.c"
)
set(ZEROSLACK_TREE_SITTER_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(cmake/tree_sitter_runtime)

# =========================================================
# 5. Core Library and Executable
# =========================================================
set(ZEROSLACK_CORE_LIBRARY_TYPE STATIC)
if(ZEROSLACK_SHARED_CORE)
    set(ZEROSLACK_CORE_LIBRARY_TYPE SHARED)
endif()

include(cmake/source_layout.cmake)

add_library(zeroslack_core ${ZEROSLACK_CORE_LIBRARY_TYPE}
    ${ZEROSLACK_CORE_SOURCES}
    $<TARGET_OBJECTS:zeroslack_tree_sitter>)

if(ZEROSLACK_SHARED_CORE)
    target_compile_definitions(zeroslack_core PUBLIC
        ZEROSLACK_CORE_SHARED=1)
endif()

if(ZEROSLACK_SHARED_CORE AND WIN32)
    if(MINGW)
        if(NOT Python_EXECUTABLE)
            find_package(Python REQUIRED COMPONENTS Interpreter)
        endif()
        if(NOT CMAKE_DLLTOOL)
            message(FATAL_ERROR
                "ZEROSLACK_SHARED_CORE requires MinGW dlltool")
        endif()

        set(ZEROSLACK_CORE_EXPORT_DEF
            "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zeroslack_core.dir/exports.def")
        add_custom_command(TARGET zeroslack_core PRE_LINK
            COMMAND "${Python_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_mingw_exports.py"
                --dlltool "${CMAKE_DLLTOOL}"
                --output "${ZEROSLACK_CORE_EXPORT_DEF}"
                --link-response
                    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zeroslack_core.rsp"
                --link-working-directory "${CMAKE_CURRENT_BINARY_DIR}"
                --object-root
                    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zeroslack_core.dir"
                --object-root
                    "${CMAKE_CURRENT_BINARY_DIR}/cmake/tree_sitter_runtime/CMakeFiles/zeroslack_tree_sitter.dir"
            BYPRODUCTS
                "${ZEROSLACK_CORE_EXPORT_DEF}"
                "${ZEROSLACK_CORE_EXPORT_DEF}.raw"
                "${ZEROSLACK_CORE_EXPORT_DEF}.rsp"
            COMMENT "Generating bounded MinGW export table for zeroslack_core"
            VERBATIM)
        target_link_options(zeroslack_core PRIVATE
            "${ZEROSLACK_CORE_EXPORT_DEF}")
    else()
        # Explicit API annotations define the supported DLL boundary. Retain
        # CMake's supplemental export discovery for non-MinGW Windows tools.
        set_target_properties(zeroslack_core PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

message(STATUS
    "ZeroSlack core library type: ${ZEROSLACK_CORE_LIBRARY_TYPE}")

add_subdirectory(components/rtleditcore)

target_sources(zeroslack_core PRIVATE
    src/commands/exposesignaltotopservice.cpp
    src/commands/exposesignaltotoppreview.cpp
)

# Main application target.
add_executable(demo src/app/main.cpp)

# Read-only machine interface for AI and automation. The support layer links
# the existing semantic core, while the executable itself remains a console
# application and initializes only QCoreApplication.
add_library(zeroslack_cli_support STATIC
    src/cli/zeroslackcli.cpp
    src/cli/zeroslackcli.h)
target_link_libraries(zeroslack_cli_support PUBLIC
    zeroslack_core
    Qt6::Core)
if(SuiteApp_FOUND)
    target_link_libraries(zeroslack_cli_support PRIVATE SuiteApp::suiteapp)
    target_compile_definitions(zeroslack_cli_support PRIVATE
        ZEROSLACK_CLI_HAS_SUITEAPP=1)
endif()
add_executable(zeroslack_cli src/cli/zeroslackclimain.cpp)
set_target_properties(zeroslack_cli PROPERTIES
    OUTPUT_NAME "zeroslack-cli")
target_link_libraries(zeroslack_cli PRIVATE zeroslack_cli_support)

# Build a Windows GUI app without a console window.
if(WIN32)
    target_sources(demo PRIVATE resources/windows/zeroslack.rc)
    set_target_properties(demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

# =========================================================
# 6. Include Paths and Libraries
# =========================================================
# Project include paths.
target_include_directories(zeroslack_core PUBLIC
    ${ZEROSLACK_MODULE_INCLUDE_DIRS}
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

# Link libraries. In a shared-core developer build, Slang remains a private
# implementation dependency of the DLL. Consumers inherit only its compile
# usage requirements; tests that directly call Slang link it explicitly.
target_link_libraries(zeroslack_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
    rtledit::core
)
target_link_libraries(zeroslack_core PRIVATE Qt6::Network)

if(ZEROSLACK_SHARED_CORE)
    target_link_libraries(zeroslack_core PRIVATE slang::slang)
    target_link_libraries(zeroslack_core INTERFACE
        "$<BUILD_INTERFACE:$<COMPILE_ONLY:slang::slang>>")
else()
    target_link_libraries(zeroslack_core PUBLIC slang::slang)
endif()

if(SuiteApp_FOUND)
    target_link_libraries(zeroslack_core PRIVATE SuiteApp::suiteapp)
    target_compile_definitions(zeroslack_core PRIVATE
        ZEROSLACK_CORE_HAS_SUITEAPP=1)
endif()

target_link_libraries(demo PRIVATE zeroslack_core)
if(SuiteApp_FOUND)
    target_sources(demo PRIVATE
        src/integrations/suite/suiteappintegration.cpp
        src/integrations/suite/suiteappintegration.h)
    target_link_libraries(demo PRIVATE SuiteApp::suiteapp)
    target_compile_definitions(demo PRIVATE ZEROSLACK_HAS_SUITEAPP=1)
endif()

# =========================================================
# 7. Compiler Flags and Definitions
# =========================================================

# The synchronous Tree-sitter C translation units are isolated in their own
# object target. Its subdirectory applies extra optimization only in Debug and
# leaves every other source and configuration on the toolchain defaults.

# MinGW-specific definitions.
if(MINGW)
    target_compile_definitions(zeroslack_core PUBLIC __USE_MINGW_ANSI_STDIO=1)
endif()

# =========================================================
# 8. Regression Tests
# =========================================================
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    if(SuiteApp_FOUND)
        add_executable(zeroslack_suiteapp_integration_test
            test_sv/suiteapp_integration_test.cpp
            src/integrations/suite/suiteappintegration.cpp
            src/integrations/suite/suiteappintegration.h)
        target_link_libraries(zeroslack_suiteapp_integration_test
            PRIVATE zeroslack_core SuiteApp::suiteapp Qt6::Test)
    endif()

    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(ZEROSLACK_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(ZEROSLACK_TEST_PATH
        "${ZEROSLACK_COMPILER_BIN_DIR};${QT_INSTALL_PREFIX}/bin;$ENV{PATH}"
    )
    string(REPLACE ";" "\\;" ZEROSLACK_TEST_PATH "${ZEROSLACK_TEST_PATH}")

    add_executable(ts_doc_test test_sv/ts_doc_test.cpp)
    target_link_libraries(ts_doc_test
        PRIVATE zeroslack_core zeroslack_tree_sitter)

    add_executable(ts_large_file_semantics_test
        test_sv/ts_large_file_semantics_test.cpp)
    target_link_libraries(ts_large_file_semantics_test
        PRIVATE zeroslack_core zeroslack_tree_sitter Qt6::Core)

    add_executable(completion_test test_sv/completion_test.cpp)
    target_link_libraries(completion_test PRIVATE zeroslack_core)

    add_executable(formatter_cursor_anchor_test
        test_sv/formatter_cursor_anchor_test.cpp)
    target_link_libraries(formatter_cursor_anchor_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(formatter_tree_indentation_test
        test_sv/formatter_tree_indentation_test.cpp)
    target_link_libraries(formatter_tree_indentation_test
        PRIVATE zeroslack_core)

    add_executable(action_registry_test
        test_sv/action_registry_test.cpp
        src/commands/actionregistry.cpp
        src/completion/codetemplateservice.cpp
        src/commands/commandlayercommandregistry.cpp
        src/editor/editorfileidentity.cpp
        src/commands/globalcontrolservice.cpp
        src/commands/inlinecommandmode.cpp
        src/commands/packagetoolservice.cpp
        src/editor/tsdocument.cpp
        $<TARGET_OBJECTS:zeroslack_tree_sitter>)
    target_include_directories(action_registry_test PRIVATE
        thirdparty/tree_sitter/lib/include
        thirdparty/tree_sitter/lib/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(action_registry_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(action_registry_test PRIVATE
        Qt6::Core
        Qt6::Gui
        slang::slang)

    add_executable(editor_mode_controller_test
        test_sv/editor_mode_controller_test.cpp
        src/editor/editormodecontroller.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(editor_mode_controller_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(editor_mode_controller_test PRIVATE Qt6::Core)

    add_executable(annotation_layer_test
        test_sv/annotation_layer_test.cpp
        src/editor/annotationlayer.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(annotation_layer_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(annotation_layer_test PRIVATE Qt6::Core)

    add_executable(shared_document_test
        test_sv/shared_document_test.cpp)
    target_link_libraries(shared_document_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(external_document_sync_test
        test_sv/external_document_sync_test.cpp)
    target_link_libraries(external_document_sync_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(unsaved_document_manager_test
        test_sv/unsaved_document_manager_test.cpp)
    target_link_libraries(unsaved_document_manager_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(notification_center_test
        test_sv/notification_center_test.cpp
        src/ui/notificationcenter.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(notification_center_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(notification_center_test
        PRIVATE Qt6::Core)

    add_executable(crash_recovery_service_test
        test_sv/crash_recovery_service_test.cpp
        src/documents/crashrecoveryservice.cpp
        src/editor/editorfileidentity.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(crash_recovery_service_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(crash_recovery_service_test
        PRIVATE Qt6::Core)

    add_executable(declare_signal_proposal_test
        test_sv/declare_signal_proposal_test.cpp
        src/commands/declaresignalservice.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(declare_signal_proposal_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(declare_signal_proposal_test
        PRIVATE Qt6::Core)

    add_executable(declare_signal_fact_collector_test
        test_sv/declare_signal_fact_collector_test.cpp)
    target_link_libraries(declare_signal_fact_collector_test
        PRIVATE zeroslack_core)

    add_executable(settings_center_test
        test_sv/settings_center_test.cpp
        src/settings/settingscenterschema.cpp
        src/settings/settingscenterservice.cpp
        src/settings/workspaceconfigurationservice.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(settings_center_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(settings_center_test
        PRIVATE Qt6::Core Qt6::Gui)

    add_executable(settings_center_panel_test
        test_sv/settings_center_panel_test.cpp)
    target_link_libraries(settings_center_panel_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(wave_simulation_configuration_test
        test_sv/wave_simulation_configuration_test.cpp
        src/integrations/wave/wavetoolchainbundleservice.cpp
        src/integrations/wave/wavesimulationconfiguration.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wave_simulation_configuration_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(wave_simulation_configuration_test
        PRIVATE Qt6::Core)

    add_executable(wave_simulation_manifest_test
        test_sv/wave_simulation_manifest_test.cpp)
    target_link_libraries(wave_simulation_manifest_test
        PRIVATE zeroslack_core Qt6::Core)
    add_executable(wave_simulation_runtime_test
        test_sv/wave_simulation_runtime_test.cpp)
    target_link_libraries(wave_simulation_runtime_test
        PRIVATE zeroslack_core Qt6::Core)

    add_library(wavewidgets_fixture SHARED
        test_sv/wavewidgets_fixture.cpp)
    target_link_libraries(wavewidgets_fixture
        PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    set_target_properties(wavewidgets_fixture PROPERTIES
        PREFIX "")

    add_executable(wave_embedded_workspace_test
        test_sv/wave_embedded_workspace_test.cpp)
    target_link_libraries(wave_embedded_workspace_test
        PRIVATE zeroslack_core Qt6::Widgets)
    add_dependencies(wave_embedded_workspace_test
        wavewidgets_fixture)

    add_executable(live_insight_session_test
        test_sv/live_insight_session_test.cpp)
    target_link_libraries(live_insight_session_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(live_insights_context_provider_test
        test_sv/live_insights_context_provider_test.cpp)
    target_link_libraries(live_insights_context_provider_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(rtl_insight_workbench_test
        test_sv/rtl_insight_workbench_test.cpp)
    target_link_libraries(rtl_insight_workbench_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(live_insights_ui_snapshot_test
        test_sv/live_insights_ui_snapshot_test.cpp)
    target_link_libraries(live_insights_ui_snapshot_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(unified_peek_container_test
        test_sv/unified_peek_container_test.cpp)
    target_link_libraries(unified_peek_container_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(graph_export_service_test
        test_sv/graph_export_service_test.cpp
        src/insights/graphexportservice.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(graph_export_service_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(graph_export_service_test
        PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

    add_executable(graph_export_panel_integration_test
        test_sv/graph_export_panel_integration_test.cpp)
    target_link_libraries(graph_export_panel_integration_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(search_scope_service_test
        test_sv/search_scope_service_test.cpp)
    target_link_libraries(search_scope_service_test
        PRIVATE zeroslack_core)

    add_executable(scoped_search_panel_test
        test_sv/scoped_search_panel_test.cpp)
    target_link_libraries(scoped_search_panel_test
        PRIVATE zeroslack_core)

    add_executable(scoped_replace_workflow_test
        test_sv/scoped_replace_workflow_test.cpp)
    target_link_libraries(scoped_replace_workflow_test
        PRIVATE zeroslack_core)

    add_executable(source_structural_navigation_test
        test_sv/source_structural_navigation_test.cpp
        src/editor/tsdocument.cpp
        $<TARGET_OBJECTS:zeroslack_tree_sitter>)
    target_include_directories(source_structural_navigation_test PRIVATE
        thirdparty/tree_sitter/lib/include
        thirdparty/tree_sitter/lib/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(source_structural_navigation_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(source_structural_navigation_test
        PRIVATE Qt6::Core slang::slang)

    add_executable(editor_source_navigation_test
        test_sv/editor_source_navigation_test.cpp)
    target_link_libraries(editor_source_navigation_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_annotation_settings_test
        test_sv/editor_annotation_settings_test.cpp)
    target_link_libraries(editor_annotation_settings_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_visible_region_perf_test
        test_sv/editor_visible_region_perf_test.cpp)
    target_link_libraries(editor_visible_region_perf_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(workspace_file_operation_test
        test_sv/workspace_file_operation_test.cpp)
    target_link_libraries(workspace_file_operation_test
        PRIVATE zeroslack_core Qt6::Test Qt6::Widgets)

    add_executable(navigation_virtual_source_group_test
        test_sv/navigation_virtual_source_group_test.cpp
        src/ui/applicationthememanager.cpp
        src/ui/insightvisualstyle.cpp
        src/ui/navigationwidget.cpp
        src/ui/navigationwidgettrees.cpp
        src/ui/navigationwidgetpresentation.cpp
        src/editor/editorfileidentity.cpp
        src/semantic/symboltaxonomy.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(navigation_virtual_source_group_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(navigation_virtual_source_group_test
        PRIVATE Qt6::Widgets)

    add_executable(workspace_virtual_source_group_ui_test
        test_sv/workspace_virtual_source_group_ui_test.cpp
        src/settings/workspaceconfigurationdialog.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(workspace_virtual_source_group_ui_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(workspace_virtual_source_group_ui_test
        PRIVATE Qt6::Widgets)

    add_executable(rtl_insight_linkage_test
        test_sv/rtl_insight_linkage_test.cpp)
    target_link_libraries(rtl_insight_linkage_test
        PRIVATE zeroslack_core)

    add_executable(rtl_rename_planner_test
        test_sv/rtl_rename_planner_test.cpp)
    target_link_libraries(rtl_rename_planner_test
        PRIVATE zeroslack_core Qt6::Core)

    add_executable(rtl_connection_transform_planner_test
        test_sv/rtl_connection_transform_planner_test.cpp)
    target_link_libraries(rtl_connection_transform_planner_test
        PRIVATE zeroslack_core Qt6::Core
                rtledit::test_support)

    add_executable(rtl_transform_workflow_test
        test_sv/rtl_transform_workflow_test.cpp)
    target_link_libraries(rtl_transform_workflow_test
        PRIVATE zeroslack_core Qt6::Core
                rtledit::test_support)

    add_executable(rtl_high_risk_edit_panel_test
        test_sv/rtl_high_risk_edit_panel_test.cpp)
    target_link_libraries(rtl_high_risk_edit_panel_test
        PRIVATE zeroslack_core Qt6::Widgets)

    add_executable(rtl_action_coordinator_test
        test_sv/rtl_action_coordinator_test.cpp)
    target_link_libraries(rtl_action_coordinator_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(multi_signal_propagation_planner_test
        test_sv/multi_signal_propagation_planner_test.cpp)
    target_link_libraries(multi_signal_propagation_planner_test
        PRIVATE zeroslack_core Qt6::Core)

    add_executable(instance_pair_connection_facade_test
        test_sv/instance_pair_connection_facade_test.cpp)
    target_link_libraries(instance_pair_connection_facade_test
        PRIVATE zeroslack_core Qt6::Core)

    add_executable(instance_pair_connection_panel_test
        test_sv/instance_pair_connection_panel_test.cpp)
    target_link_libraries(instance_pair_connection_panel_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(instance_pair_connection_workflow_test
        test_sv/instance_pair_connection_workflow_test.cpp)
    target_link_libraries(instance_pair_connection_workflow_test
        PRIVATE zeroslack_core)

    add_executable(multi_signal_propagation_panel_test
        test_sv/multi_signal_propagation_panel_test.cpp)
    target_link_libraries(multi_signal_propagation_panel_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_file_identity_test
        test_sv/editor_file_identity_test.cpp
        src/editor/editorfileidentity.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(editor_file_identity_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(editor_file_identity_test
        PRIVATE Qt6::Core)

    add_executable(editor_context_menu_test
        test_sv/editor_context_menu_test.cpp
        src/commands/actionregistry.cpp
        src/editor/editorcontextmenumodel.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(editor_context_menu_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(editor_context_menu_test
        PRIVATE Qt6::Core Qt6::Gui)

    add_executable(insight_focus_controller_test
        test_sv/insight_focus_controller_test.cpp
        src/commands/actionregistry.cpp
        src/insights/insightfocuscontroller.cpp
        src/insights/insightfocuscontrolleractions.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(insight_focus_controller_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(insight_focus_controller_test
        PRIVATE Qt6::Widgets)

    add_executable(panel_layout_controller_test
        test_sv/panel_layout_controller_test.cpp
        src/commands/actionregistry.cpp
        src/ui/applicationthememanager.cpp
        src/ui/insightvisualstyle.cpp
        src/ui/panellayoutcontroller.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(panel_layout_controller_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(panel_layout_controller_test
        PRIVATE Qt6::Widgets Qt6::Test)

    add_executable(editor_split_controller_test
        test_sv/editor_split_controller_test.cpp
        src/commands/actionregistry.cpp
        src/ui/applicationthememanager.cpp
        src/editor/editordroppreviewoverlay.cpp
        src/editor/editorsplitcontroller.cpp
        src/ui/insightvisualstyle.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(editor_split_controller_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(editor_split_controller_test
        PRIVATE Qt6::Widgets Qt6::Test)

    add_executable(workspace_persistence_test
        test_sv/workspace_persistence_test.cpp
        src/ui/contextresource.cpp
        src/settings/workspaceconfigurationservice.cpp
        src/workspace/workspacesessionstateservice.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(workspace_persistence_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(workspace_persistence_test
        PRIVATE Qt6::Core)

    add_executable(workspace_session_coordinator_test
        test_sv/workspace_session_coordinator_test.cpp)
    target_link_libraries(workspace_session_coordinator_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(controller_boundary_test
        test_sv/controller_boundary_test.cpp)
    target_link_libraries(controller_boundary_test
        PRIVATE Qt6::Core)

    add_executable(effective_value_test test_sv/effective_value_test.cpp)
    target_link_libraries(effective_value_test
        PRIVATE zeroslack_core slang::slang)

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
    add_dependencies(gui_smoke_test wavewidgets_fixture)

    add_executable(temporary_editor_context_search_test
        test_sv/temporary_editor_context_search_test.cpp)
    target_link_libraries(temporary_editor_context_search_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(context_workspace_test
        test_sv/context_workspace_test.cpp)
    target_link_libraries(context_workspace_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(workspace_hub_test
        test_sv/workspace_hub_test.cpp)
    target_link_libraries(workspace_hub_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(temporary_editor_context_provider_test
        test_sv/temporary_editor_context_provider_test.cpp)
    target_link_libraries(temporary_editor_context_provider_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(pinloom_context_provider_test
        test_sv/pinloom_context_provider_test.cpp)
    target_link_libraries(pinloom_context_provider_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(pinloom_code_link_store_test
        test_sv/pinloom_code_link_store_test.cpp)
    target_link_libraries(pinloom_code_link_store_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(zeroslack_cli_test
        test_sv/zeroslack_cli_test.cpp)
    target_link_libraries(zeroslack_cli_test
        PRIVATE zeroslack_cli_support Qt6::Test)

    add_executable(editor_view_local_folding_test
        test_sv/editor_view_local_folding_test.cpp)
    target_link_libraries(editor_view_local_folding_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(expose_signal_to_top_test test_sv/expose_signal_to_top_test.cpp)
    target_link_libraries(expose_signal_to_top_test PRIVATE zeroslack_core)

    add_executable(expose_signal_to_top_gui_test
        test_sv/expose_signal_to_top_gui_test.cpp)
    target_link_libraries(expose_signal_to_top_gui_test PRIVATE zeroslack_core Qt6::Test)

    add_executable(expose_signal_to_top_fixture_test
        test_sv/expose_signal_to_top_fixture_test.cpp)
    target_link_libraries(expose_signal_to_top_fixture_test PRIVATE zeroslack_core)

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

    add_executable(editor_structural_input_test
        test_sv/editor_structural_input_test.cpp)
    target_link_libraries(editor_structural_input_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_line_operation_test
        test_sv/editor_line_operation_test.cpp
        src/editor/editorlineoperationcontroller.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(editor_line_operation_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(editor_line_operation_test
        PRIVATE Qt6::Widgets)

    add_executable(editor_multicursor_controller_test
        test_sv/editor_multicursor_controller_test.cpp)
    target_link_libraries(editor_multicursor_controller_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_paste_transaction_test
        test_sv/editor_paste_transaction_test.cpp)
    target_link_libraries(editor_paste_transaction_test
        PRIVATE zeroslack_core Qt6::Test)

    add_executable(editor_template_slot_controller_test
        test_sv/editor_template_slot_controller_test.cpp)
    target_link_libraries(editor_template_slot_controller_test
        PRIVATE zeroslack_core)

    add_executable(package_header_command_test
        test_sv/package_header_command_test.cpp
        src/commands/actionregistry.cpp
        src/editor/editorfileidentity.cpp
        src/commands/inlinecommandmode.cpp
        src/commands/packagetoolservice.cpp
        src/editor/tsdocument.cpp
        $<TARGET_OBJECTS:zeroslack_tree_sitter>)
    target_include_directories(package_header_command_test PRIVATE
        thirdparty/tree_sitter/lib/include
        thirdparty/tree_sitter/lib/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(package_header_command_test PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-iquote${CMAKE_CURRENT_SOURCE_DIR}>")
    endif()
    target_link_libraries(package_header_command_test
        PRIVATE Qt6::Core Qt6::Gui slang::slang)

    add_executable(analysis_scheduler_test
        test_sv/analysis_scheduler_test.cpp)
    target_link_libraries(analysis_scheduler_test PRIVATE zeroslack_core Qt6::Test)

    if(ZEROSLACK_SHARED_CORE)
        add_executable(shared_core_runtime_test
            test_sv/shared_core_runtime_test.cpp)
        target_link_libraries(shared_core_runtime_test
            PRIVATE zeroslack_core Qt6::Test)
    endif()

    add_executable(relationship_perf_test test_sv/relationship_perf_test.cpp)
    target_link_libraries(relationship_perf_test PRIVATE zeroslack_core)

    add_test(NAME ts_doc_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:ts_doc_test>
    )
    add_test(NAME ts_large_file_semantics_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:ts_large_file_semantics_test>
    )
    add_test(NAME completion_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            "ZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            $<TARGET_FILE:completion_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/test_symbols.sv
    )
    add_test(NAME formatter_cursor_anchor_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:formatter_cursor_anchor_test>
    )
    add_test(NAME formatter_tree_indentation_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:formatter_tree_indentation_test>
    )
    add_test(NAME action_registry_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:action_registry_test>
    )
    add_test(NAME editor_mode_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:editor_mode_controller_test>
    )
    add_test(NAME annotation_layer_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:annotation_layer_test>
    )
    add_test(NAME shared_document_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:shared_document_test>
    )
    add_test(NAME external_document_sync_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:external_document_sync_test>
    )
    add_test(NAME unsaved_document_manager_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:unsaved_document_manager_test>
    )
    add_test(NAME notification_center_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:notification_center_test>
    )
    add_test(NAME crash_recovery_service_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:crash_recovery_service_test>
    )
    add_test(NAME declare_signal_proposal_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:declare_signal_proposal_test>
    )
    add_test(NAME declare_signal_fact_collector_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:declare_signal_fact_collector_test>
    )
    add_test(NAME settings_center_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:settings_center_test>
    )
    add_test(NAME settings_center_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:settings_center_panel_test>
    )
    add_test(NAME wave_simulation_configuration_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:wave_simulation_configuration_test>
    )
    add_test(NAME wave_simulation_manifest_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "ZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            $<TARGET_FILE:wave_simulation_manifest_test>
    )
    add_test(NAME wave_simulation_runtime_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "ZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            $<TARGET_FILE:wave_simulation_runtime_test>
    )
    add_test(NAME wave_embedded_workspace_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:wave_embedded_workspace_test>
            $<TARGET_FILE:wavewidgets_fixture>
    )
    if(EXISTS "${ZEROSLACK_WAVEWIDGETS_VALIDATION_LIBRARY}")
        add_test(NAME waveform_preview_real_library_test
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${ZEROSLACK_TEST_PATH}"
                "QT_QPA_PLATFORM=offscreen"
                "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
                $<TARGET_FILE:wave_embedded_workspace_test>
                "${ZEROSLACK_WAVEWIDGETS_VALIDATION_LIBRARY}"
                --waveform-only
        )
        set_tests_properties(waveform_preview_real_library_test
            PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME live_insight_session_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:live_insight_session_test>
    )
    set_tests_properties(live_insight_session_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME live_insights_context_provider_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:live_insights_context_provider_test>
    )
    set_tests_properties(live_insights_context_provider_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME rtl_insight_workbench_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:rtl_insight_workbench_test>
    )
    set_tests_properties(rtl_insight_workbench_test
        PROPERTIES TIMEOUT 120)
    set(ZEROSLACK_LIVE_INSIGHTS_SNAPSHOT_WAVE_ARGS)
    if(EXISTS "${ZEROSLACK_WAVEWIDGETS_VALIDATION_LIBRARY}")
        list(APPEND ZEROSLACK_LIVE_INSIGHTS_SNAPSHOT_WAVE_ARGS
            --wave-library
            "${ZEROSLACK_WAVEWIDGETS_VALIDATION_LIBRARY}")
    endif()
    add_test(NAME live_insights_ui_snapshot_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:live_insights_ui_snapshot_test>
            --output-dir
            "${CMAKE_CURRENT_BINARY_DIR}/artifacts/ui/live-insights"
            ${ZEROSLACK_LIVE_INSIGHTS_SNAPSHOT_WAVE_ARGS}
    )
    set_tests_properties(live_insights_ui_snapshot_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME unified_peek_container_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:unified_peek_container_test>
    )
    add_test(NAME graph_export_service_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:graph_export_service_test>
    )
    add_test(NAME graph_export_panel_integration_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:graph_export_panel_integration_test>
    )
    add_test(NAME search_scope_service_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:search_scope_service_test>
    )
    add_test(NAME scoped_search_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:scoped_search_panel_test>
    )
    add_test(NAME scoped_replace_workflow_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:scoped_replace_workflow_test>
    )
    set_tests_properties(scoped_replace_workflow_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME source_structural_navigation_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:source_structural_navigation_test>
    )
    add_test(NAME editor_source_navigation_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_source_navigation_test>
    )
    add_test(NAME editor_annotation_settings_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_annotation_settings_test>
    )
    add_test(NAME editor_visible_region_perf_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_visible_region_perf_test>
    )
    add_test(NAME workspace_file_operation_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_file_operation_test>
    )
    add_test(NAME navigation_virtual_source_group_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:navigation_virtual_source_group_test>
    )
    add_test(NAME workspace_virtual_source_group_ui_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_virtual_source_group_ui_test>
    )
    add_test(NAME rtl_insight_linkage_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:rtl_insight_linkage_test>
    )
    add_test(NAME rtl_rename_planner_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:rtl_rename_planner_test>
    )
    add_test(NAME rtl_connection_transform_planner_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:rtl_connection_transform_planner_test>
    )
    add_test(NAME rtl_transform_workflow_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:rtl_transform_workflow_test>
    )
    add_test(NAME rtl_high_risk_edit_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:rtl_high_risk_edit_panel_test>
    )
    set_tests_properties(rtl_high_risk_edit_panel_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME rtl_action_coordinator_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:rtl_action_coordinator_test>
    )
    set_tests_properties(rtl_action_coordinator_test
        PROPERTIES TIMEOUT 180)
    add_test(NAME multi_signal_propagation_planner_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:multi_signal_propagation_planner_test>
    )
    add_test(NAME instance_pair_connection_facade_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:instance_pair_connection_facade_test>
    )
    add_test(NAME instance_pair_connection_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:instance_pair_connection_panel_test>
    )
    set_tests_properties(instance_pair_connection_panel_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME instance_pair_connection_workflow_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:instance_pair_connection_workflow_test>
    )
    set_tests_properties(instance_pair_connection_workflow_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME multi_signal_propagation_panel_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:multi_signal_propagation_panel_test>
    )
    set_tests_properties(multi_signal_propagation_panel_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME editor_file_identity_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:editor_file_identity_test>
    )
    add_test(NAME editor_context_menu_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:editor_context_menu_test>
    )
    add_test(NAME insight_focus_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:insight_focus_controller_test>
    )
    add_test(NAME panel_layout_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:panel_layout_controller_test>
    )
    add_test(NAME editor_split_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_split_controller_test>
    )
    add_test(NAME temporary_editor_context_search_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:temporary_editor_context_search_test>
    )
    add_test(NAME context_workspace_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:context_workspace_test>
    )
    add_test(NAME workspace_hub_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_hub_test>
    )
    add_test(NAME workspace_hub_150pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=1.5"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_hub_test>
    )
    add_test(NAME workspace_hub_125pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=1.25"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_hub_test>
    )
    add_test(NAME workspace_hub_200pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=2"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_hub_test>
    )
    set_tests_properties(
        workspace_hub_test
        workspace_hub_125pct_dpi_test
        workspace_hub_150pct_dpi_test
        workspace_hub_200pct_dpi_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME context_workspace_125pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=1.25"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:context_workspace_test>
    )
    add_test(NAME context_workspace_150pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=1.5"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:context_workspace_test>
    )
    add_test(NAME context_workspace_200pct_dpi_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_SCALE_FACTOR=2"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:context_workspace_test>
    )
    set_tests_properties(
        context_workspace_125pct_dpi_test
        context_workspace_150pct_dpi_test
        context_workspace_200pct_dpi_test
        PROPERTIES TIMEOUT 120)
    add_test(NAME temporary_editor_context_provider_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:temporary_editor_context_provider_test>
    )
    add_test(NAME pinloom_context_provider_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:pinloom_context_provider_test>
    )
    add_test(NAME pinloom_code_link_store_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:pinloom_code_link_store_test>
    )
    if(SuiteApp_FOUND)
        add_test(NAME zeroslack_suiteapp_integration_test
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${ZEROSLACK_TEST_PATH}"
                "QT_QPA_PLATFORM=offscreen"
                "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
                $<TARGET_FILE:zeroslack_suiteapp_integration_test>
        )
    endif()
    add_test(NAME editor_view_local_folding_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_view_local_folding_test>
    )
    add_test(NAME workspace_persistence_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:workspace_persistence_test>
            ${ZEROSLACK_LEGACY_FIXTURE_SOURCE_DIR}/test_sv/new
            ${ZEROSLACK_LEGACY_FIXTURE_SOURCE_DIR}/test_sv/huge_prj
    )
    add_test(NAME workspace_session_coordinator_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:workspace_session_coordinator_test>
    )
    add_test(NAME controller_boundary_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:controller_boundary_test>
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME jump_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            "ZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
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
            "ZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "ZEROSLACK_WAVEWIDGETS_LIBRARY=$<TARGET_FILE:wavewidgets_fixture>"
            $<TARGET_FILE:gui_smoke_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/test_symbols.sv
    )
    add_test(NAME expose_signal_to_top_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:expose_signal_to_top_test>
    )
    set_tests_properties(expose_signal_to_top_test PROPERTIES TIMEOUT 120)

    add_test(NAME expose_signal_to_top_gui_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:expose_signal_to_top_gui_test>
    )
    set_tests_properties(expose_signal_to_top_gui_test PROPERTIES
        TIMEOUT 120)

    add_test(NAME expose_signal_to_top_fixture_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:expose_signal_to_top_fixture_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/new
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/huge_prj
    )
    set_tests_properties(expose_signal_to_top_fixture_test PROPERTIES
        TIMEOUT 1200)

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
    add_test(NAME editor_structural_input_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_structural_input_test>
    )
    add_test(NAME editor_line_operation_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_line_operation_test>
    )
    add_test(NAME editor_multicursor_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_multicursor_controller_test>
    )
    add_test(NAME editor_paste_transaction_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_paste_transaction_test>
    )
    add_test(NAME editor_template_slot_controller_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:editor_template_slot_controller_test>
    )
    add_test(NAME package_header_command_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:package_header_command_test>
    )
    add_test(NAME analysis_scheduler_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
            $<TARGET_FILE:analysis_scheduler_test>
    )
    if(ZEROSLACK_SHARED_CORE)
        add_test(NAME shared_core_runtime_test
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${ZEROSLACK_TEST_PATH}"
                "QT_QPA_PLATFORM=offscreen"
                "QT_PLUGIN_PATH=${QT_INSTALL_PREFIX}/plugins"
                $<TARGET_FILE:shared_core_runtime_test>
        )
    endif()
    add_test(NAME relationship_perf_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:relationship_perf_test>
            ${CMAKE_CURRENT_SOURCE_DIR}/test_sv/huge_prj
    )
    set_tests_properties(relationship_perf_test PROPERTIES TIMEOUT 1200)
    add_test(NAME zeroslack_cli_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${ZEROSLACK_TEST_PATH}"
            $<TARGET_FILE:zeroslack_cli_test>
    )
    set_tests_properties(zeroslack_cli_test PROPERTIES TIMEOUT 300)
    add_test(NAME legacy_field_policy_guard
        COMMAND ${CMAKE_COMMAND}
            -DZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/legacy_field_policy_guard.ctest
    )
    add_test(NAME shared_core_policy_guard
        COMMAND ${CMAKE_COMMAND}
            -DZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_core_policy_guard.ctest
    )
    add_test(NAME version_documentation_guard
        COMMAND ${CMAKE_COMMAND}
            -DZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DZEROSLACK_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_documentation_guard.ctest
    )
    add_test(NAME tree_sitter_compile_options_guard
        COMMAND ${CMAKE_COMMAND}
            -DZEROSLACK_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DZEROSLACK_PROBE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tree_sitter_compile_options_probe
            -DZEROSLACK_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/tree_sitter_compile_options_guard
            -DZEROSLACK_C_COMPILER=${CMAKE_C_COMPILER}
            -DZEROSLACK_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DZEROSLACK_GENERATOR=${CMAKE_GENERATOR}
            -DZEROSLACK_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DZEROSLACK_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DZEROSLACK_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tree_sitter_compile_options_guard.ctest
    )
    set_tests_properties(tree_sitter_compile_options_guard PROPERTIES
        TIMEOUT 120
    )
endif()
