if(NOT DEFINED HORUSRF_RUN OR NOT DEFINED SOURCE_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "CLI acceptance paths were not provided")
endif()

file(REMOVE "${OUTPUT_FILE}")

get_filename_component(acceptance_directory "${OUTPUT_FILE}" DIRECTORY)
set(parse_invalid "${acceptance_directory}/cli-parse-invalid.hrf")
set(semantic_invalid "${acceptance_directory}/cli-semantic-invalid.hrf")
file(WRITE "${parse_invalid}" "characterize broken {\n  measure @\n}\n")
file(WRITE "${semantic_invalid}"
    "characterize broken {\n"
    "  reference power = 1 dB\n"
    "  sweep frequency 1 Hz .. 2 Hz step 1 Hz\n"
    "  measure power\n"
    "}\n")

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}"
    RESULT_VARIABLE no_csv_result OUTPUT_VARIABLE no_csv_output ERROR_VARIABLE no_csv_error
)
if(NOT no_csv_result EQUAL 0)
    message(FATAL_ERROR "no-CSV invocation failed: ${no_csv_result}: ${no_csv_error}")
endif()
foreach(expected IN ITEMS "characterization: tx_path" "samples: 101"
        "calibration: tx_power" "uncorrected_rms_error_db:"
        "corrected_rms_error_db:" "maximum_absolute_error_db:"
        "corrected_maximum_absolute_error_db:" "rms_improvement_ratio:")
    string(FIND "${no_csv_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "summary is missing '${expected}': ${no_csv_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --csv "${OUTPUT_FILE}"
    RESULT_VARIABLE csv_result OUTPUT_VARIABLE csv_output ERROR_VARIABLE csv_error
)
if(NOT csv_result EQUAL 0 OR NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "CSV invocation failed: ${csv_result}: ${csv_error}")
endif()
string(FIND "${csv_output}" "csv: ${OUTPUT_FILE}" csv_announcement)
if(csv_announcement EQUAL -1)
    message(FATAL_ERROR "CSV path was not announced: ${csv_output}")
endif()
file(STRINGS "${OUTPUT_FILE}" csv_lines)
list(LENGTH csv_lines csv_line_count)
if(NOT csv_line_count EQUAL 102)
    message(FATAL_ERROR "expected 102 CSV lines, got ${csv_line_count}")
endif()
list(GET csv_lines 0 csv_header)
if(NOT csv_header STREQUAL "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db")
    message(FATAL_ERROR "unexpected CSV header: ${csv_header}")
endif()
list(GET csv_lines 1 first_row)
list(GET csv_lines 101 last_row)
if(NOT first_row MATCHES "^2400000000,-10," OR NOT last_row MATCHES "^2500000000,-10,")
    message(FATAL_ERROR "unexpected boundary rows: ${first_row} / ${last_row}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}"
    RESULT_VARIABLE usage_result OUTPUT_VARIABLE usage_output ERROR_VARIABLE usage_error
)
if(NOT usage_result EQUAL 2 OR usage_error STREQUAL "")
    message(FATAL_ERROR "invalid usage contract failed: ${usage_result}")
endif()
execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}.missing"
    RESULT_VARIABLE missing_result OUTPUT_VARIABLE missing_output ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 3 OR missing_error STREQUAL "")
    message(FATAL_ERROR "missing-source contract failed: ${missing_result}")
endif()

foreach(case IN ITEMS parse semantic)
    if(case STREQUAL "parse")
        set(invalid_source "${parse_invalid}")
        set(expected_prefix "${parse_invalid}:2:11: parse.invalid_token:")
    else()
        set(invalid_source "${semantic_invalid}")
        set(expected_prefix "${semantic_invalid}:2:21: semantic.unexpected_quantity_type:")
    endif()
    set(failure_csv "${acceptance_directory}/cli-${case}-failure.csv")
    file(REMOVE "${failure_csv}")
    execute_process(
        COMMAND "${HORUSRF_RUN}" "${invalid_source}" --csv "${failure_csv}"
        RESULT_VARIABLE failure_result
        OUTPUT_VARIABLE failure_output
        ERROR_VARIABLE failure_error
    )
    if(NOT failure_result EQUAL 4)
        message(FATAL_ERROR "${case} failure returned ${failure_result}: ${failure_error}")
    endif()
    if(NOT failure_output STREQUAL "")
        message(FATAL_ERROR "${case} failure wrote stdout: ${failure_output}")
    endif()
    string(FIND "${failure_error}" "${expected_prefix}" prefix_position)
    if(NOT prefix_position EQUAL 0)
        message(FATAL_ERROR "${case} diagnostic has wrong prefix: ${failure_error}")
    endif()
    if(EXISTS "${failure_csv}")
        message(FATAL_ERROR "${case} failure created CSV: ${failure_csv}")
    endif()
endforeach()

file(REMOVE "${OUTPUT_FILE}" "${parse_invalid}" "${semantic_invalid}")
