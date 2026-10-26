# Keep Qt GUI results even when a Windows GUI executable has no stdout handle.
foreach(required QT_TEST_NAME QT_TEST_EXECUTABLE QT_TEST_LOG_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
if(DEFINED ENV{ZEROSLACK_QT_TEST_LOG_ROOT} AND NOT "$ENV{ZEROSLACK_QT_TEST_LOG_ROOT}" STREQUAL "")
    set(QT_TEST_LOG_ROOT "$ENV{ZEROSLACK_QT_TEST_LOG_ROOT}")
endif()
string(TIMESTAMP started "%Y%m%dT%H%M%S" UTC)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef invocation)
set(report "${QT_TEST_LOG_ROOT}/${QT_TEST_NAME}/${started}-${invocation}")
file(MAKE_DIRECTORY "${report}")
file(WRITE "${report}/invocation.txt"
    "test=${QT_TEST_NAME}\nexecutable=${QT_TEST_EXECUTABLE}\narguments=${QT_TEST_ARGUMENTS}\n"
    "platform=$ENV{QT_QPA_PLATFORM}\nscaleFactor=$ENV{QT_SCALE_FACTOR}\nstyle=$ENV{ZEROSLACK_TEST_UI_STYLE}\n")
message(STATUS "Qt GUI evidence: ${report}")
execute_process(
    COMMAND "${QT_TEST_EXECUTABLE}" ${QT_TEST_ARGUMENTS}
        -o "${report}/result.xml,junitxml" -o "${report}/test.log,txt"
    RESULT_VARIABLE result
    OUTPUT_FILE "${report}/stdout.log"
    ERROR_FILE "${report}/stderr.log")
file(WRITE "${report}/exit-code.txt" "${result}\n")
if(NOT "${result}" STREQUAL "0")
    foreach(log test.log stdout.log stderr.log)
        if(EXISTS "${report}/${log}")
            file(READ "${report}/${log}" contents LIMIT 65536)
            message("${log}:\n${contents}")
        endif()
    endforeach()
    message(FATAL_ERROR "${QT_TEST_NAME}: process exit code ${result}; evidence: ${report}")
endif()
message(STATUS "${QT_TEST_NAME}: process exit code ${result}")
