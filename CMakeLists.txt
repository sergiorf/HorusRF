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
    src/domain/units.cpp
)
target_compile_features(horusrf_domain PUBLIC cxx_std_20)
target_include_directories(horusrf_domain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
horusrf_enable_warnings(horusrf_domain)

add_library(horusrf_semantic STATIC
    src/semantic/analyzer.cpp
)
target_compile_features(horusrf_semantic PUBLIC cxx_std_20)
target_include_directories(horusrf_semantic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(horusrf_semantic PUBLIC horusrf_domain horusrf_parser)
horusrf_enable_warnings(horusrf_semantic)

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

    add_executable(horusrf_domain_tests tests/domain/quantity_tests.cpp)
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
endif()
