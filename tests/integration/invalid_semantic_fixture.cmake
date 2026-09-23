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
