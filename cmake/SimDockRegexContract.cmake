# Shared extraction preserves the original CMake list/semicolon normalization.
function(_zeroslack_regex_usage_sha256 path output)
    file(STRINGS "${path}" lines REGEX "QRegularExpression|QRegExp|std::regex|include[ \t]*<regex>")
    set(usage "")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        string(APPEND usage "${line}\n")
    endforeach()
    string(SHA256 actual "${usage}")
    set(${output} "${actual}" PARENT_SCOPE)
endfunction()

function(_zeroslack_verify_regex_inventory label source_dir source_revision extraction_version approved_paths required_fields)
    if(NOT extraction_version STREQUAL "cmake-file-strings-strip-v1")
        message(FATAL_ERROR "${label} extraction version mismatch: ${extraction_version}")
    endif()
    set(paths)
    foreach(entry IN LISTS ARGN)
        string(REPLACE "|" ";" fields "${entry}")
        list(LENGTH fields field_count)
        if(NOT field_count EQUAL required_fields)
            message(FATAL_ERROR "${label} entry metadata is malformed: ${entry}")
        endif()
        list(GET fields 0 relative)
        math(EXPR last_field "${required_fields} - 1")
        foreach(index RANGE 1 ${last_field})
            list(GET fields ${index} expected)
            string(LENGTH "${expected}" hash_length)
            if(NOT hash_length EQUAL 64 OR NOT expected MATCHES "^[0-9a-f]+$")
                message(FATAL_ERROR "${label} hash metadata is malformed: ${relative}")
            endif()
        endforeach()
        list(APPEND paths "${relative}")
    endforeach()
    list(SORT paths)
    if(NOT paths STREQUAL approved_paths)
        message(FATAL_ERROR "${label} source inventory mismatch: ${paths}")
    endif()
    foreach(entry IN LISTS ARGN)
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 relative)
        list(GET fields 1 expected)
        _zeroslack_regex_usage_sha256("${source_dir}/${relative}" actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR
                "${label} contract changed: ${relative}\n"
                "sourceRevision=${source_revision}; extraction=${extraction_version}\n"
                "expected=${expected}; actual=${actual}")
        endif()
        if(required_fields EQUAL 3)
            list(GET fields 2 expected_source)
            file(READ "${source_dir}/${relative}" content)
            string(REPLACE "\r\n" "\n" content "${content}")
            string(SHA256 actual_source "${content}")
            if(NOT actual_source STREQUAL expected_source)
                message(FATAL_ERROR
                    "${label} complete source changed: ${relative}\n"
                    "sourceRevision=${source_revision}; normalization=utf8-crlf-to-lf-v1\n"
                    "expected=${expected_source}; actual=${actual_source}")
            endif()
        endif()
    endforeach()
endfunction()

# The two reviews retain separate pinned source identities and inventories.
function(zeroslack_verify_simdock_regex_contract source_dir source_revision extraction_version)
    if(NOT source_revision STREQUAL "8a36ed8ecbb790c573664130b57f4c168ece340c")
        message(FATAL_ERROR "Frozen SimDock regex source revision mismatch: ${source_revision}")
    endif()
    set(approved_paths
        src/simulation/simdock/core/analyzer.cpp
        src/simulation/simdock/core/questasession.cpp
        src/simulation/simdock/core/testbench.cpp
        src/simulation/simdock/core/workspace.cpp)
    _zeroslack_verify_regex_inventory("Frozen SimDock regex" "${source_dir}"
        "${source_revision}" "${extraction_version}" "${approved_paths}" 2 ${ARGN})
endfunction()

function(zeroslack_verify_reviewed_regex_contract source_dir source_revision extraction_version source_normalization)
    if(NOT source_revision STREQUAL "ff75d747110ea9cef6339abf5442c0b8b1e2fe42")
        message(FATAL_ERROR "Reviewed Phase L regex source revision mismatch: ${source_revision}")
    endif()
    if(NOT source_normalization STREQUAL "utf8-crlf-to-lf-v1")
        message(FATAL_ERROR "Reviewed Phase L regex source normalization mismatch: ${source_normalization}")
    endif()
    set(approved_paths
        src/completion/simulationcommandprefix.cpp
        src/completion/templatecapabilities.cpp
        src/completion/usertemplateservice.cpp
        src/editor/tbinputassist.cpp
        src/integrations/xips/xipsassetsync.cpp
        src/integrations/xips/xipscontextprovider.cpp
        src/simulation/simdock/core/projectstore.cpp
        src/simulation/simdock/core/simulationevents.cpp
        src/ui/linkedcontextcontroller.cpp
        src/ui/templatemanagerdialog.cpp
        src/ui/welcomeanimation.cpp
    )
    _zeroslack_verify_regex_inventory("Reviewed Phase L regex" "${source_dir}"
        "${source_revision}" "${extraction_version}" "${approved_paths}" 3 ${ARGN})
endfunction()
