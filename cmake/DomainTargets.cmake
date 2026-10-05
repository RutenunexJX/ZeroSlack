include(${CMAKE_CURRENT_LIST_DIR}/DomainSources.cmake)

# Build each state owner once. GUI and CLI consume the same DLL ABI; the
# semantic index and document parser are never duplicated inside the UI DLL.
function(zeroslack_domain_exports target domain)
    if(NOT ZEROSLACK_SHARED_CORE OR NOT WIN32)
        return()
    endif()
    if(NOT MINGW)
        set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        return()
    endif()
    if(NOT Python_EXECUTABLE)
        find_package(Python REQUIRED COMPONENTS Interpreter)
    endif()
    set(def "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir/exports.def")
    set(extra_roots)
    set(extra_objects "")
    if(domain STREQUAL "documents")
        list(APPEND extra_roots --object-root
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/tree_sitter_runtime/CMakeFiles/zeroslack_tree_sitter.dir")
        set(extra_objects "$<JOIN:$<TARGET_OBJECTS:zeroslack_tree_sitter>,\n>\n")
    endif()
    file(GENERATE OUTPUT "${def}.objects"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:${target}>,\n>\n${extra_objects}")
    add_custom_command(TARGET ${target} PRE_LINK
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_mingw_exports.py"
            --domain ${domain} --dlltool "${CMAKE_DLLTOOL}" --output "${def}"
            --link-response "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.rsp"
            --objects-manifest "${def}.objects"
            --link-working-directory "${CMAKE_CURRENT_BINARY_DIR}"
            --object-root "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir"
            ${extra_roots}
        BYPRODUCTS "${def}" "${def}.raw" "${def}.rsp"
        COMMENT "Generating bounded MinGW export table for ${target}"
        VERBATIM)
    target_link_options(${target} PRIVATE "${def}")
endfunction()

add_library(zeroslack_documents ${ZEROSLACK_CORE_LIBRARY_TYPE}
    ${ZEROSLACK_DOCUMENT_SOURCES} $<TARGET_OBJECTS:zeroslack_tree_sitter>)
add_library(ZeroSlack::Documents ALIAS zeroslack_documents)
target_include_directories(zeroslack_documents PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree_sitter/lib/include"
    PRIVATE src/documents src/editor)
target_link_libraries(zeroslack_documents PUBLIC Qt6::Core Qt6::Gui PRIVATE slang::slang)
if(ZEROSLACK_SHARED_CORE)
    target_compile_definitions(zeroslack_documents PUBLIC ZEROSLACK_DOCUMENTS_SHARED=1)
endif()
zeroslack_domain_exports(zeroslack_documents documents)

add_library(zeroslack_semantic ${ZEROSLACK_CORE_LIBRARY_TYPE} ${ZEROSLACK_SEMANTIC_SOURCES})
add_library(ZeroSlack::Semantic ALIAS zeroslack_semantic)
target_include_directories(zeroslack_semantic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE src/semantic src/analysis src/editor src/documents src/completion
            src/insights src/settings src/integrations/pinloom src/integrations/suite)
target_link_libraries(zeroslack_semantic PUBLIC Qt6::Core zeroslack_documents
    PRIVATE slang::slang)
if(ZEROSLACK_SHARED_CORE)
    target_compile_definitions(zeroslack_semantic PUBLIC ZEROSLACK_SEMANTIC_SHARED=1)
endif()
zeroslack_domain_exports(zeroslack_semantic semantic)

function(zeroslack_domain_test_runtime target)
    get_target_property(qt_core_runtime Qt6::Core LOCATION)
    get_filename_component(qt_runtime_dir "${qt_core_runtime}" DIRECTORY)
    file(TO_NATIVE_PATH "${qt_runtime_dir}" qt_runtime_path)
    file(TO_NATIVE_PATH "${ZEROSLACK_COMPILER_BIN_DIR}" compiler_runtime_path)
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" domain_runtime_path)
    set_tests_properties(${target} PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${qt_runtime_path};PATH=path_list_prepend:${compiler_runtime_path};PATH=path_list_prepend:${domain_runtime_path}")
endfunction()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(domain_contract_test test_sv/domain_contract_test.cpp)
    target_link_libraries(domain_contract_test PRIVATE ZeroSlack::Semantic ZeroSlack::Documents Qt6::Test)
    add_test(NAME domain_contract_test COMMAND domain_contract_test)
    zeroslack_domain_test_runtime(domain_contract_test)
endif()
