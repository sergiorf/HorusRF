cmake_minimum_required(VERSION 3.20)

project(HorusRF VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(cmake/warnings.cmake)

add_library(horusrf_parser STATIC
    src/parser/lexer.cpp
    src/parser/parser.cpp
)
target_compile_features(horusrf_parser PUBLIC cxx_std_20)
target_include_directories(horusrf_parser PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
horusrf_enable_warnings(horusrf_parser)

add_library(horusrf_domain STATIC
    src/domain/quantity.cpp
    src/domain/results.cpp
    src/domain/units.cpp
)
target_compile_features(horusrf_domain PUBLIC cxx_std_20)
target_include_directories(horusrf_domain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
horusrf_enable_warnings(horusrf_domain)

add_library(horusrf_device INTERFACE)
target_compile_features(horusrf_device INTERFACE cxx_std_20)
target_include_directories(horusrf_device INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_device INTERFACE horusrf_domain)

add_library(horusrf_simulator STATIC src/device/simulated_rf_device.cpp)
target_compile_features(horusrf_simulator PUBLIC cxx_std_20)
target_include_directories(horusrf_simulator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_simulator PUBLIC horusrf_device horusrf_domain)
horusrf_enable_warnings(horusrf_simulator)

add_library(horusrf_procedural_example STATIC
    examples/cpp/tx_path_characterization.cpp
)
target_compile_features(horusrf_procedural_example PUBLIC cxx_std_20)
target_include_directories(horusrf_procedural_example PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples/cpp>
)
target_link_libraries(horusrf_procedural_example PUBLIC
    horusrf_device horusrf_domain
)
horusrf_enable_warnings(horusrf_procedural_example)

add_library(horusrf_semantic STATIC
    src/semantic/analyzer.cpp
)
target_compile_features(horusrf_semantic PUBLIC cxx_std_20)
target_include_directories(horusrf_semantic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_semantic PUBLIC horusrf_domain horusrf_parser)
horusrf_enable_warnings(horusrf_semantic)

add_library(horusrf_ir STATIC
    src/ir/lowering.cpp
)
target_compile_features(horusrf_ir PUBLIC cxx_std_20)
target_include_directories(horusrf_ir PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_ir PUBLIC horusrf_domain PRIVATE horusrf_semantic)
horusrf_enable_warnings(horusrf_ir)

add_library(horusrf_runtime STATIC
    src/runtime/characterization.cpp
    src/runtime/executor.cpp
)
target_compile_features(horusrf_runtime PUBLIC cxx_std_20)
target_include_directories(horusrf_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_runtime PUBLIC horusrf_ir horusrf_device horusrf_domain)
horusrf_enable_warnings(horusrf_runtime)

add_library(horusrf_output STATIC src/output/csv.cpp)
target_compile_features(horusrf_output PUBLIC cxx_std_20)
target_include_directories(horusrf_output PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_output PUBLIC horusrf_domain)
horusrf_enable_warnings(horusrf_output)

add_executable(horusrf-run src/cli/main.cpp)
target_compile_features(horusrf-run PRIVATE cxx_std_20)
target_link_libraries(horusrf-run PRIVATE
    horusrf_output horusrf_runtime horusrf_simulator horusrf_parser
    horusrf_semantic horusrf_ir
)
horusrf_enable_warnings(horusrf-run)

if(BUILD_TESTING)
    if(CMAKE_CONFIGURATION_TYPES)
        set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ctest-default-config.cmake"
        )
    endif()

    add_executable(horusrf_parser_tests
        tests/parser/lexer_tests.cpp
        tests/parser/parser_tests.cpp
    )
    target_compile_features(horusrf_parser_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_parser_tests PRIVATE tests)
    target_link_libraries(horusrf_parser_tests PRIVATE horusrf_parser)
    target_compile_definitions(horusrf_parser_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_parser_tests)
    add_test(NAME horusrf_parser_tests COMMAND horusrf_parser_tests)

    add_executable(horusrf_domain_tests
        tests/domain/quantity_tests.cpp
        tests/domain/results_tests.cpp
    )
    target_compile_features(horusrf_domain_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_domain_tests PRIVATE tests)
    target_link_libraries(horusrf_domain_tests PRIVATE horusrf_domain)
    horusrf_enable_warnings(horusrf_domain_tests)
    add_test(NAME horusrf_domain_tests COMMAND horusrf_domain_tests)

    add_executable(horusrf_semantic_tests tests/semantic/analyzer_tests.cpp)
    target_compile_features(horusrf_semantic_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_semantic_tests PRIVATE tests)
    target_link_libraries(horusrf_semantic_tests PRIVATE horusrf_semantic)
    target_compile_definitions(horusrf_semantic_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_semantic_tests)
    add_test(NAME horusrf_semantic_tests COMMAND horusrf_semantic_tests)

    add_executable(horusrf_ir_tests tests/ir/lowering_tests.cpp)
    target_compile_features(horusrf_ir_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_ir_tests PRIVATE tests)
    target_link_libraries(horusrf_ir_tests PRIVATE
        horusrf_ir horusrf_parser horusrf_semantic
    )
    target_compile_definitions(horusrf_ir_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_ir_tests)
    add_test(NAME horusrf_ir_tests COMMAND horusrf_ir_tests)

    add_executable(horusrf_simulator_tests
        tests/device/simulated_rf_device_tests.cpp
    )
    target_compile_features(horusrf_simulator_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_simulator_tests PRIVATE tests)
    target_link_libraries(horusrf_simulator_tests PRIVATE horusrf_simulator)
    horusrf_enable_warnings(horusrf_simulator_tests)
    add_test(NAME horusrf_simulator_tests COMMAND horusrf_simulator_tests)

    add_executable(horusrf_runtime_tests tests/runtime/executor_tests.cpp)
    target_compile_features(horusrf_runtime_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_runtime_tests PRIVATE tests)
    target_link_libraries(horusrf_runtime_tests PRIVATE
        horusrf_runtime horusrf_simulator horusrf_parser horusrf_semantic horusrf_ir
    )
    target_compile_definitions(horusrf_runtime_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_runtime_tests)
    add_test(NAME horusrf_runtime_tests COMMAND horusrf_runtime_tests)

    add_executable(horusrf_characterization_tests
        tests/runtime/characterization_tests.cpp
    )
    target_compile_features(horusrf_characterization_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_characterization_tests PRIVATE tests)
    target_link_libraries(horusrf_characterization_tests PRIVATE
        horusrf_runtime horusrf_simulator horusrf_parser horusrf_semantic horusrf_ir
    )
    target_compile_definitions(horusrf_characterization_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_characterization_tests)
    add_test(NAME horusrf_characterization_tests COMMAND horusrf_characterization_tests)

    add_executable(horusrf_procedural_example_tests
        tests/examples/tx_path_characterization_tests.cpp
    )
    target_compile_features(horusrf_procedural_example_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_procedural_example_tests PRIVATE tests)
    target_link_libraries(horusrf_procedural_example_tests PRIVATE
        horusrf_procedural_example horusrf_simulator
    )
    horusrf_enable_warnings(horusrf_procedural_example_tests)
    add_test(NAME horusrf_procedural_example_tests
        COMMAND horusrf_procedural_example_tests
    )

    add_executable(horusrf_csv_tests tests/output/csv_tests.cpp)
    target_compile_features(horusrf_csv_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_csv_tests PRIVATE tests)
    target_link_libraries(horusrf_csv_tests PRIVATE horusrf_output horusrf_domain)
    horusrf_enable_warnings(horusrf_csv_tests)
    add_test(NAME horusrf_csv_tests COMMAND horusrf_csv_tests)

    add_executable(horusrf_end_to_end_tests
        tests/integration/end_to_end_tests.cpp
    )
    target_compile_features(horusrf_end_to_end_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_end_to_end_tests PRIVATE tests)
    target_link_libraries(horusrf_end_to_end_tests PRIVATE
        horusrf_output horusrf_runtime horusrf_simulator horusrf_parser
        horusrf_semantic horusrf_ir
    )
    target_compile_definitions(horusrf_end_to_end_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_end_to_end_tests)
    add_test(NAME horusrf_end_to_end_tests COMMAND horusrf_end_to_end_tests)

    add_executable(horusrf_equivalence_tests
        tests/integration/equivalence_tests.cpp
    )
    target_compile_features(horusrf_equivalence_tests PRIVATE cxx_std_20)
    target_include_directories(horusrf_equivalence_tests PRIVATE tests examples/cpp)
    target_link_libraries(horusrf_equivalence_tests PRIVATE
        horusrf_procedural_example horusrf_runtime horusrf_simulator
        horusrf_parser horusrf_semantic horusrf_ir
    )
    target_compile_definitions(horusrf_equivalence_tests PRIVATE
        HORUSRF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    horusrf_enable_warnings(horusrf_equivalence_tests)
    add_test(NAME horusrf_equivalence_tests COMMAND horusrf_equivalence_tests)

    add_test(NAME horusrf_cli_acceptance
        COMMAND ${CMAKE_COMMAND}
            -DHORUSRF_RUN=$<TARGET_FILE:horusrf-run>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/tx_path_characterization.hrf
            -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/cli-acceptance-output.csv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cli_acceptance.cmake
    )

    set_tests_properties(
        horusrf_parser_tests
        horusrf_domain_tests
        horusrf_semantic_tests
        horusrf_ir_tests
        horusrf_simulator_tests
        horusrf_runtime_tests
        horusrf_characterization_tests
        horusrf_procedural_example_tests
        horusrf_csv_tests
        PROPERTIES LABELS unit
    )
    set_tests_properties(
        horusrf_end_to_end_tests
        horusrf_equivalence_tests
        PROPERTIES LABELS integration
    )
    set_tests_properties(horusrf_cli_acceptance PROPERTIES LABELS cli)
endif()
