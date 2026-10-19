include_guard(GLOBAL)

set(HORUSRF_ANTLR_VERSION "4.13.2")
set(HORUSRF_ANTLR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/antlr4")
cmake_path(NORMAL_PATH HORUSRF_ANTLR_ROOT)
set(HORUSRF_ANTLR_JAR
    "${HORUSRF_ANTLR_ROOT}/antlr-${HORUSRF_ANTLR_VERSION}-complete.jar")
set(HORUSRF_ANTLR_JAR_SHA256
    "eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76")
set(HORUSRF_ANTLR_RUNTIME_SOURCE_DIR
    "${HORUSRF_ANTLR_ROOT}/runtime/src")

foreach(required_path IN ITEMS
        "${HORUSRF_ANTLR_JAR}"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/antlr4-runtime.h"
        "${HORUSRF_ANTLR_ROOT}/LICENSE.txt"
        "${HORUSRF_ANTLR_ROOT}/VERSION")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "The vendored ANTLR ${HORUSRF_ANTLR_VERSION} toolchain is incomplete: "
            "${required_path} is missing.")
    endif()
endforeach()

file(STRINGS "${HORUSRF_ANTLR_ROOT}/VERSION" vendored_antlr_version LIMIT_COUNT 1)
if(NOT vendored_antlr_version STREQUAL HORUSRF_ANTLR_VERSION)
    message(FATAL_ERROR
        "ANTLR tool/runtime version mismatch: the build pins "
        "${HORUSRF_ANTLR_VERSION}, but third_party/antlr4/VERSION contains "
        "${vendored_antlr_version}.")
endif()

file(SHA256 "${HORUSRF_ANTLR_JAR}" vendored_antlr_jar_sha256)
if(NOT vendored_antlr_jar_sha256 STREQUAL HORUSRF_ANTLR_JAR_SHA256)
    message(FATAL_ERROR
        "The vendored ANTLR generator checksum does not match the pinned "
        "${HORUSRF_ANTLR_VERSION} artifact. Expected ${HORUSRF_ANTLR_JAR_SHA256}, "
        "got ${vendored_antlr_jar_sha256}.")
endif()

find_package(Java 11 REQUIRED COMPONENTS Runtime)

function(horusrf_add_antlr4_runtime)
    if(TARGET horusrf_antlr4_runtime)
        return()
    endif()

    file(GLOB antlr_runtime_sources CONFIGURE_DEPENDS
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/atn/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/dfa/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/internal/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/misc/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/support/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/tree/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/tree/pattern/*.cpp"
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}/tree/xpath/*.cpp"
    )

    add_library(horusrf_antlr4_runtime STATIC ${antlr_runtime_sources})
    add_library(horusrf::antlr4_runtime ALIAS horusrf_antlr4_runtime)
    target_compile_features(horusrf_antlr4_runtime PRIVATE cxx_std_20)
    target_include_directories(horusrf_antlr4_runtime SYSTEM PUBLIC
        "${HORUSRF_ANTLR_RUNTIME_SOURCE_DIR}"
    )
    if(WIN32)
        target_compile_definitions(horusrf_antlr4_runtime PUBLIC ANTLR4CPP_STATIC)
    endif()
    if(MSVC)
        target_compile_options(horusrf_antlr4_runtime PRIVATE /w /MP)
    else()
        target_compile_options(horusrf_antlr4_runtime PRIVATE -w)
        find_package(Threads REQUIRED)
        target_link_libraries(horusrf_antlr4_runtime PRIVATE Threads::Threads)
    endif()
    if(APPLE)
        find_library(HORUSRF_COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
        target_link_libraries(horusrf_antlr4_runtime PRIVATE
            "${HORUSRF_COREFOUNDATION_LIBRARY}"
        )
    endif()
    set_target_properties(horusrf_antlr4_runtime PROPERTIES
        OUTPUT_NAME "horusrf-antlr4-runtime"
    )
endfunction()

function(horusrf_antlr4_generate target_name grammar_file output_directory)
    get_filename_component(grammar_file "${grammar_file}" ABSOLUTE)
    get_filename_component(grammar_name "${grammar_file}" NAME_WE)

    set(generated_sources
        "${output_directory}/${grammar_name}Lexer.cpp"
        "${output_directory}/${grammar_name}Parser.cpp"
        "${output_directory}/${grammar_name}BaseVisitor.cpp"
        "${output_directory}/${grammar_name}Visitor.cpp"
    )
    set(generated_headers
        "${output_directory}/${grammar_name}Lexer.h"
        "${output_directory}/${grammar_name}Parser.h"
        "${output_directory}/${grammar_name}BaseVisitor.h"
        "${output_directory}/${grammar_name}Visitor.h"
    )

    add_custom_command(
        OUTPUT ${generated_sources} ${generated_headers}
        BYPRODUCTS
            "${output_directory}/${grammar_name}.interp"
            "${output_directory}/${grammar_name}.tokens"
            "${output_directory}/${grammar_name}Lexer.interp"
            "${output_directory}/${grammar_name}Lexer.tokens"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${HORUSRF_ANTLR_JAR}"
            -Dlanguage=Cpp
            -visitor
            -no-listener
            -Xexact-output-dir
            -o "${output_directory}"
            "${grammar_file}"
        DEPENDS "${grammar_file}" "${HORUSRF_ANTLR_JAR}"
        COMMENT "Generating ${grammar_name} with ANTLR ${HORUSRF_ANTLR_VERSION}"
        VERBATIM
    )

    add_custom_target(${target_name}
        DEPENDS ${generated_sources} ${generated_headers}
    )
    set(${target_name}_SOURCES ${generated_sources} PARENT_SCOPE)
    set(${target_name}_HEADERS ${generated_headers} PARENT_SCOPE)
endfunction()
