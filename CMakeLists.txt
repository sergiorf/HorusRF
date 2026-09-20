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
endif()
