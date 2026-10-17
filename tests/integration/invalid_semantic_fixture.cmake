if(NOT DEFINED HORUSRF_RUN OR NOT DEFINED SOURCE_FILE OR
   NOT DEFINED OUTPUT_FILE OR NOT DEFINED EXPECTED_CODE)
    message(FATAL_ERROR "invalid semantic fixture test arguments were not provided")
endif()

file(REMOVE "${OUTPUT_FILE}")

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --csv "${OUTPUT_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 4)
    message(FATAL_ERROR "fixture returned ${result}, expected 4: ${error}")
endif()
if(NOT output STREQUAL "")
    message(FATAL_ERROR "semantic failure wrote stdout: ${output}")
endif()
string(FIND "${error}" "${EXPECTED_CODE}" code_position)
if(code_position EQUAL -1)
    message(FATAL_ERROR "diagnostic did not contain '${EXPECTED_CODE}': ${error}")
endif()
if(EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "semantic failure created CSV: ${OUTPUT_FILE}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --check
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)

if(NOT check_result EQUAL 4)
    message(FATAL_ERROR "check returned ${check_result}, expected 4: ${check_error}")
endif()
if(NOT check_output STREQUAL "")
    message(FATAL_ERROR "semantic check failure wrote stdout: ${check_output}")
endif()
string(FIND "${check_error}" "${EXPECTED_CODE}" check_code_position)
if(check_code_position EQUAL -1)
    message(FATAL_ERROR "check diagnostic did not contain '${EXPECTED_CODE}': ${check_error}")
endif()
