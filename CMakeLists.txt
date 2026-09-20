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
endif()
