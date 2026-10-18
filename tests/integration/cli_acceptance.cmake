if(NOT DEFINED HORUSRF_RUN OR NOT DEFINED SOURCE_FILE OR
   NOT DEFINED MULTIDIMENSIONAL_SOURCE_FILE OR
   NOT DEFINED NAMED_MEASUREMENTS_SOURCE_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "CLI acceptance paths were not provided")
endif()

if(NOT DEFINED QUALIFIED_SOURCE_FILE)
    message(FATAL_ERROR "qualified CLI acceptance path was not provided")
endif()

if(NOT DEFINED STUDIO_DEFAULT_SOURCE_FILE)
    message(FATAL_ERROR "Studio default CLI acceptance path was not provided")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${STUDIO_DEFAULT_SOURCE_FILE}" --check
    RESULT_VARIABLE studio_default_check_result
    OUTPUT_VARIABLE studio_default_check_output
    ERROR_VARIABLE studio_default_check_error
)
if(NOT studio_default_check_result EQUAL 0 OR
   NOT studio_default_check_output STREQUAL "" OR
   NOT studio_default_check_error STREQUAL "")
    message(FATAL_ERROR
        "Studio default check failed: ${studio_default_check_result}: ${studio_default_check_error}")
endif()

set(studio_default_json "${OUTPUT_FILE}.studio-default.json")
file(REMOVE "${studio_default_json}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${STUDIO_DEFAULT_SOURCE_FILE}" --json "${studio_default_json}"
    RESULT_VARIABLE studio_default_result
    OUTPUT_VARIABLE studio_default_output
    ERROR_VARIABLE studio_default_error
)
if(NOT studio_default_result EQUAL 0 AND NOT studio_default_result EQUAL 6)
    message(FATAL_ERROR
        "Studio default execution did not complete: ${studio_default_result}: ${studio_default_error}")
endif()
if(NOT EXISTS "${studio_default_json}")
    message(FATAL_ERROR "Studio default execution did not produce JSON")
endif()

file(READ "${studio_default_json}" studio_default_content)
string(JSON studio_default_schema ERROR_VARIABLE studio_default_parse_error
    GET "${studio_default_content}" schema name)
if(studio_default_parse_error)
    message(FATAL_ERROR "Studio default JSON is invalid: ${studio_default_parse_error}")
endif()
string(JSON studio_default_version GET "${studio_default_content}" schema version)
string(JSON studio_default_characterization GET "${studio_default_content}" characterization)
string(JSON studio_default_point_count LENGTH "${studio_default_content}" points)
string(JSON studio_default_summary_count
    LENGTH "${studio_default_content}" qualification assertions)
string(JSON studio_default_calibration_count LENGTH "${studio_default_content}" calibrations)
if(NOT studio_default_schema STREQUAL "horusrf.characterization-result" OR
   NOT studio_default_version EQUAL 1 OR
   NOT studio_default_characterization STREQUAL "tx_qualification" OR
   NOT studio_default_point_count EQUAL 126 OR
   NOT studio_default_summary_count EQUAL 3 OR
   NOT studio_default_calibration_count EQUAL 1)
    message(FATAL_ERROR "Studio default JSON has the wrong top-level contract")
endif()

set(studio_default_measurement_names output_power modulation_evm carrier_error)
set(studio_default_measurement_operations power evm frequency_error)
foreach(point_index RANGE 0 125)
    string(JSON point_content GET "${studio_default_content}" points ${point_index})
    string(JSON coordinate_count LENGTH "${point_content}" coordinates)
    string(JSON measurement_count LENGTH "${point_content}" measurements)
    string(JSON derived_count LENGTH "${point_content}" derived)
    string(JSON assertion_count LENGTH "${point_content}" assertions)
    if(NOT coordinate_count EQUAL 2 OR NOT measurement_count EQUAL 3 OR
       NOT derived_count EQUAL 1 OR NOT assertion_count EQUAL 3)
        message(FATAL_ERROR "Studio default point ${point_index} has the wrong shape")
    endif()

    string(JSON frequency_name GET "${point_content}" coordinates 0 name)
    string(JSON temperature_name GET "${point_content}" coordinates 1 name)
    math(EXPR expected_frequency_index "${point_index} / 6")
    math(EXPR expected_temperature_index "${point_index} % 6")
    math(EXPR expected_frequency "2400000000 + ${expected_frequency_index} * 5000000")
    math(EXPR expected_temperature "-20 + ${expected_temperature_index} * 20")
    string(JSON actual_frequency GET "${point_content}" coordinates 0 value)
    string(JSON actual_temperature GET "${point_content}" coordinates 1 display_value)
    if(NOT frequency_name STREQUAL "frequency" OR
       NOT temperature_name STREQUAL "temperature" OR
       NOT actual_frequency EQUAL expected_frequency OR
       NOT actual_temperature EQUAL expected_temperature)
        message(FATAL_ERROR
            "Studio default point ${point_index} is not in deterministic sweep order")
    endif()

    foreach(measurement_index RANGE 0 2)
        list(GET studio_default_measurement_names ${measurement_index} expected_name)
        list(GET studio_default_measurement_operations ${measurement_index} expected_operation)
        string(JSON actual_name GET
            "${point_content}" measurements ${measurement_index} name)
        string(JSON actual_operation GET
            "${point_content}" measurements ${measurement_index} operation)
        if(NOT actual_name STREQUAL expected_name OR
           NOT actual_operation STREQUAL expected_operation)
            message(FATAL_ERROR
                "Studio default point ${point_index} measurement ${measurement_index} is invalid")
        endif()
    endforeach()

    string(JSON derived_name GET "${point_content}" derived 0 name)
    if(NOT derived_name STREQUAL "power_error")
        message(FATAL_ERROR "Studio default point ${point_index} is missing power_error")
    endif()
endforeach()

foreach(assertion_index RANGE 0 2)
    string(JSON assertion_total GET
        "${studio_default_content}" qualification assertions ${assertion_index} total)
    if(NOT assertion_total EQUAL 126)
        message(FATAL_ERROR
            "Studio default assertion ${assertion_index} has ${assertion_total} evaluations")
    endif()
endforeach()

string(JSON calibration_name GET "${studio_default_content}" calibrations 0 name)
string(JSON calibration_dimension_count LENGTH
    "${studio_default_content}" calibrations 0 dimensions)
string(JSON calibration_frequency GET
    "${studio_default_content}" calibrations 0 dimensions 0 name)
string(JSON calibration_temperature GET
    "${studio_default_content}" calibrations 0 dimensions 1 name)
string(JSON calibration_entry_count LENGTH
    "${studio_default_content}" calibrations 0 entries)
if(NOT calibration_name STREQUAL "tx_power" OR
   NOT calibration_dimension_count EQUAL 2 OR
   NOT calibration_frequency STREQUAL "frequency" OR
   NOT calibration_temperature STREQUAL "temperature" OR
   NOT calibration_entry_count EQUAL 126)
    message(FATAL_ERROR "Studio default calibration surface has the wrong contract")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${QUALIFIED_SOURCE_FILE}" --check
    RESULT_VARIABLE qualified_check_result OUTPUT_VARIABLE qualified_check_output
    ERROR_VARIABLE qualified_check_error
)
if(NOT qualified_check_result EQUAL 0 OR NOT qualified_check_output STREQUAL "" OR
   NOT qualified_check_error STREQUAL "")
    message(FATAL_ERROR "qualified check failed: ${qualified_check_result}: ${qualified_check_error}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${QUALIFIED_SOURCE_FILE}"
    RESULT_VARIABLE qualified_result OUTPUT_VARIABLE qualified_output
    ERROR_VARIABLE qualified_error
)
if(NOT qualified_result EQUAL 6)
    message(FATAL_ERROR "qualification failure returned ${qualified_result}: ${qualified_error}")
endif()
foreach(expected IN ITEMS "qualification: fail" "qualification_points: 21"
        "assertion-1: total=21" "assertion-2: total=21")
    string(FIND "${qualified_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "qualified summary is missing '${expected}': ${qualified_output}")
    endif()
endforeach()
string(FIND "${qualified_error}" "qualification.assertion_failed: assertion-2 failed"
    qualified_diagnostic)
if(qualified_diagnostic EQUAL -1)
    message(FATAL_ERROR "qualified diagnostic missing: ${qualified_error}")
endif()

set(qualified_csv "${OUTPUT_FILE}.qualified")
file(REMOVE "${qualified_csv}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${QUALIFIED_SOURCE_FILE}" --csv "${qualified_csv}"
    RESULT_VARIABLE qualified_csv_result OUTPUT_VARIABLE qualified_csv_output
    ERROR_VARIABLE qualified_csv_error
)
if(NOT qualified_csv_result EQUAL 4 OR EXISTS "${qualified_csv}")
    message(FATAL_ERROR "qualified CSV gate failed: ${qualified_csv_result}: ${qualified_csv_error}")
endif()
string(FIND "${qualified_csv_error}" "output.csv_unrepresentable" qualified_csv_diagnostic)
if(qualified_csv_diagnostic EQUAL -1)
    message(FATAL_ERROR "qualified CSV diagnostic missing: ${qualified_csv_error}")
endif()

set(qualified_json "${OUTPUT_FILE}.qualified.json")
file(REMOVE "${qualified_json}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${QUALIFIED_SOURCE_FILE}" --json "${qualified_json}"
    RESULT_VARIABLE qualified_json_result OUTPUT_VARIABLE qualified_json_output
    ERROR_VARIABLE qualified_json_error
)
if(NOT qualified_json_result EQUAL 6 OR NOT EXISTS "${qualified_json}")
    message(FATAL_ERROR "qualified JSON contract failed: ${qualified_json_result}: ${qualified_json_error}")
endif()
string(FIND "${qualified_json_output}" "json: ${qualified_json}" qualified_json_announcement)
string(FIND "${qualified_json_error}" "qualification.assertion_failed" qualified_json_diagnostic)
if(qualified_json_announcement EQUAL -1 OR qualified_json_diagnostic EQUAL -1)
    message(FATAL_ERROR "qualified JSON reporting is incomplete")
endif()
file(READ "${qualified_json}" qualified_json_content)
string(JSON qualified_schema ERROR_VARIABLE qualified_json_parse_error
    GET "${qualified_json_content}" schema name)
string(JSON qualified_version GET "${qualified_json_content}" schema version)
string(JSON qualified_point_count LENGTH "${qualified_json_content}" points)
if(qualified_json_parse_error OR
   NOT qualified_schema STREQUAL "horusrf.characterization-result" OR
   NOT qualified_version EQUAL 1 OR NOT qualified_point_count EQUAL 21)
    message(FATAL_ERROR "qualified JSON is invalid or has the wrong contract: ${qualified_json_parse_error}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${NAMED_MEASUREMENTS_SOURCE_FILE}"
    RESULT_VARIABLE named_result OUTPUT_VARIABLE named_output ERROR_VARIABLE named_error
)
if(NOT named_result EQUAL 0 OR NOT named_error STREQUAL "")
    message(FATAL_ERROR "named-measurement execution failed: ${named_result}: ${named_error}")
endif()
foreach(expected IN ITEMS "characterization: named_measurements" "samples: 126"
        "calibration: tx_power")
    string(FIND "${named_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "named-measurement summary is missing '${expected}': ${named_output}")
    endif()
endforeach()

set(named_csv "${OUTPUT_FILE}.named")
file(REMOVE "${named_csv}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${NAMED_MEASUREMENTS_SOURCE_FILE}" --csv "${named_csv}"
    RESULT_VARIABLE named_csv_result OUTPUT_VARIABLE named_csv_output
    ERROR_VARIABLE named_csv_error
)
if(NOT named_csv_result EQUAL 4 OR NOT named_csv_output STREQUAL "" OR EXISTS "${named_csv}")
    message(FATAL_ERROR "named-measurement CSV gate failed: ${named_csv_result}: ${named_csv_error}")
endif()
string(FIND "${named_csv_error}" "output.csv_unrepresentable" named_csv_diagnostic)
if(named_csv_diagnostic EQUAL -1)
    message(FATAL_ERROR "named-measurement CSV diagnostic missing: ${named_csv_error}")
endif()

set(named_json "${OUTPUT_FILE}.named.json")
file(REMOVE "${named_json}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${NAMED_MEASUREMENTS_SOURCE_FILE}" --json "${named_json}"
    RESULT_VARIABLE named_json_result OUTPUT_VARIABLE named_json_output
    ERROR_VARIABLE named_json_error
)
if(NOT named_json_result EQUAL 0 OR NOT named_json_error STREQUAL "" OR
   NOT EXISTS "${named_json}")
    message(FATAL_ERROR "named JSON execution failed: ${named_json_result}: ${named_json_error}")
endif()
file(READ "${named_json}" named_json_content)
string(JSON named_point_count ERROR_VARIABLE named_json_parse_error
    LENGTH "${named_json_content}" points)
string(JSON named_entry_count LENGTH "${named_json_content}" calibrations 0 entries)
string(JSON first_temperature GET "${named_json_content}" points 0 coordinates 1 display_value)
if(named_json_parse_error OR NOT named_point_count EQUAL 126 OR
   NOT named_entry_count EQUAL 126 OR NOT first_temperature EQUAL -20)
    message(FATAL_ERROR "named JSON surface contract is invalid: ${named_json_parse_error}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${MULTIDIMENSIONAL_SOURCE_FILE}" --check
    RESULT_VARIABLE multidimensional_check_result
    OUTPUT_VARIABLE multidimensional_check_output
    ERROR_VARIABLE multidimensional_check_error
)
if(NOT multidimensional_check_result EQUAL 0 OR
   NOT multidimensional_check_output STREQUAL "" OR
   NOT multidimensional_check_error STREQUAL "")
    message(FATAL_ERROR
        "multidimensional check failed: ${multidimensional_check_result}: ${multidimensional_check_error}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${MULTIDIMENSIONAL_SOURCE_FILE}"
    RESULT_VARIABLE multidimensional_run_result
    OUTPUT_VARIABLE multidimensional_run_output
    ERROR_VARIABLE multidimensional_run_error
)
if(NOT multidimensional_run_result EQUAL 0 OR
   NOT multidimensional_run_error STREQUAL "")
    message(FATAL_ERROR
        "multidimensional execution failed: ${multidimensional_run_result}: ${multidimensional_run_error}")
endif()
foreach(expected IN ITEMS "characterization: temperature_characterization"
        "samples: 126" "calibration: tx_power")
    string(FIND "${multidimensional_run_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "multidimensional summary is missing '${expected}': ${multidimensional_run_output}")
    endif()
endforeach()

set(multidimensional_csv "${OUTPUT_FILE}.multidimensional")
file(REMOVE "${multidimensional_csv}")
execute_process(
    COMMAND "${HORUSRF_RUN}" "${MULTIDIMENSIONAL_SOURCE_FILE}" --csv "${multidimensional_csv}"
    RESULT_VARIABLE multidimensional_csv_result
    OUTPUT_VARIABLE multidimensional_csv_output
    ERROR_VARIABLE multidimensional_csv_error
)
if(NOT multidimensional_csv_result EQUAL 4 OR
   NOT multidimensional_csv_output STREQUAL "" OR
   EXISTS "${multidimensional_csv}")
    message(FATAL_ERROR
        "multidimensional CSV gate failed: ${multidimensional_csv_result}: ${multidimensional_csv_error}")
endif()
string(FIND "${multidimensional_csv_error}"
    "output.csv_unrepresentable" multidimensional_csv_diagnostic)
if(multidimensional_csv_diagnostic EQUAL -1)
    message(FATAL_ERROR
        "multidimensional CSV diagnostic missing: ${multidimensional_csv_error}")
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
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --check
    RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0 OR NOT check_output STREQUAL "" OR NOT check_error STREQUAL "")
    message(FATAL_ERROR "check invocation failed: ${check_result}: ${check_error}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --check --csv "${OUTPUT_FILE}"
    RESULT_VARIABLE mixed_result OUTPUT_VARIABLE mixed_output ERROR_VARIABLE mixed_error
)
if(NOT mixed_result EQUAL 2 OR mixed_error STREQUAL "")
    message(FATAL_ERROR "mutually exclusive options were accepted: ${mixed_result}")
endif()

execute_process(
    COMMAND "${HORUSRF_RUN}" "${SOURCE_FILE}" --csv "${OUTPUT_FILE}" --json "${OUTPUT_FILE}.json"
    RESULT_VARIABLE mixed_output_result OUTPUT_VARIABLE mixed_output_output
    ERROR_VARIABLE mixed_output_error
)
if(NOT mixed_output_result EQUAL 2 OR mixed_output_error STREQUAL "")
    message(FATAL_ERROR "CSV and JSON options were accepted together")
endif()

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

    set(failure_json "${acceptance_directory}/cli-${case}-failure.json")
    file(REMOVE "${failure_json}")
    execute_process(
        COMMAND "${HORUSRF_RUN}" "${invalid_source}" --json "${failure_json}"
        RESULT_VARIABLE json_failure_result OUTPUT_VARIABLE json_failure_output
        ERROR_VARIABLE json_failure_error
    )
    if(NOT json_failure_result EQUAL 4 OR NOT json_failure_output STREQUAL "" OR
       EXISTS "${failure_json}")
        message(FATAL_ERROR "${case} JSON failure contract failed: ${json_failure_result}")
    endif()

    execute_process(
        COMMAND "${HORUSRF_RUN}" "${invalid_source}" --check
        RESULT_VARIABLE check_failure_result
        OUTPUT_VARIABLE check_failure_output
        ERROR_VARIABLE check_failure_error
    )
    if(NOT check_failure_result EQUAL 4 OR NOT check_failure_output STREQUAL "")
        message(FATAL_ERROR "${case} check contract failed: ${check_failure_result}")
    endif()
    string(FIND "${check_failure_error}" "${expected_prefix}" check_prefix_position)
    if(NOT check_prefix_position EQUAL 0)
        message(FATAL_ERROR "${case} check diagnostic has wrong prefix: ${check_failure_error}")
    endif()
endforeach()

file(REMOVE "${OUTPUT_FILE}" "${multidimensional_csv}" "${named_csv}" "${qualified_csv}"
    "${named_json}" "${qualified_json}" "${studio_default_json}"
    "${parse_invalid}" "${semantic_invalid}")
