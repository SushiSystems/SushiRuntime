# SushiRuntimeTests.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# One function builds a test program, wherever its sources live: a module's tests/ folder
# or the root tests/. Every program is linked, placed and registered the same way. A test
# links sushiruntime_internal_headers (cmake/SushiRuntimeLibrary.cmake), so it includes the
# headers of the INTERNAL modules, which the library's own target does not publish.
#
# Labels come from the GoogleTest suite-name prefix: Unit_*, Integration_*, Regression_*,
# Stress_* and Performance_*. Each group carries one label; `sr test --suite functional`
# is the regex 'unit|integration|regression'. A two-element LABELS list does not survive
# gtest_discover_tests(PROPERTIES ...), which re-expands the property without its list
# escaping.

if(NOT SR_BUILD_TESTS)
    function(sushiruntime_add_test_program)
    endfunction()
    function(sushiruntime_add_module_tests)
    endfunction()
    return()
endif()

enable_testing()
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# The imported GTest targets ask for cxx_std_14, which a compiler newer than CMake's
# feature table cannot answer; the tree pins C++17 itself.
foreach(gtest_target GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    if(TARGET ${gtest_target})
        set_target_properties(${gtest_target} PROPERTIES INTERFACE_COMPILE_FEATURES "")
    endif()
endforeach()

set(SR_TEST_COMMON_DIR "${PROJECT_SOURCE_DIR}/tests/common")

# Every test program lands in one folder, so one copy of the library serves them all.
set(SR_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/tests")

# sushiruntime.dll staged beside the test programs, once, by a target they all depend
# on. A POST_BUILD copy per program races in a parallel build: several links finish
# together and Windows reports a sharing violation on the file they all write.
if(WIN32)
    if(CMAKE_CONFIGURATION_TYPES)
        set(SR_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/tests/$<CONFIG>")
    endif()
    add_custom_target(sushiruntime_tests_runtime_dll
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SR_TEST_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sushiruntime>
            "${SR_TEST_OUTPUT_DIR}"
        COMMENT "Staging sushiruntime.dll next to the test binaries")
    add_dependencies(sushiruntime_tests_runtime_dll sushiruntime)
endif()

# sushiruntime_add_test_program(NAME <target> [BENCHMARK] [MODULE <module>] SOURCES <file>...)
#
# BENCHMARK registers the Stress_* and Performance_* suites with a 900 second limit; a
# program without it registers Unit_*, Integration_* and Regression_* with 120 seconds.
# MODULE adds the module's name as a second label on every test of the program.
function(sushiruntime_add_test_program)
    cmake_parse_arguments(ARG "BENCHMARK" "NAME;MODULE" "SOURCES" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_test_program(): NAME and SOURCES are required.")
    endif()

    add_executable(${ARG_NAME} "${SR_TEST_COMMON_DIR}/test_main.cpp" ${ARG_SOURCES})
    sushiruntime_apply_build_policy(${ARG_NAME})
    target_include_directories(${ARG_NAME} PRIVATE "${SR_TEST_COMMON_DIR}")
    target_link_libraries(${ARG_NAME} PRIVATE GTest::gtest sushiruntime_internal_headers)
    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
    add_sycl_to_target(TARGET ${ARG_NAME} SOURCES ${ARG_SOURCES})
    if(WIN32)
        add_dependencies(${ARG_NAME} sushiruntime_tests_runtime_dll)
    endif()

    if(ARG_BENCHMARK)
        set(groups "Stress_*|stress" "Performance_*|performance")
        set(timeout 900)
    else()
        set(groups "Unit_*|unit" "Integration_*|integration" "Regression_*|regression")
        set(timeout 120)
    endif()
    foreach(group IN LISTS groups)
        string(REPLACE "|" ";" parts "${group}")
        list(GET parts 0 filter)
        list(GET parts 1 label)
        if(NOT "${ARG_MODULE}" STREQUAL "")
            # The separator is escaped: the property list crosses a generated script.
            string(APPEND label "\;${ARG_MODULE}")
        endif()
        gtest_discover_tests(${ARG_NAME}
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/tests"
            TEST_FILTER "${filter}"
            PROPERTIES LABELS "${label}" TIMEOUT ${timeout})
    endforeach()
endfunction()

# sushiruntime_add_module_tests(MODULE <module> SOURCES <file under tests/>...)
#
# Builds sr_<module>_tests from the calling module's tests/ folder. The program may
# include the module's private headers, which sit in source/, by a quoted name.
function(sushiruntime_add_module_tests)
    cmake_parse_arguments(ARG "" "MODULE" "SOURCES" ${ARGN})
    if("${ARG_MODULE}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_module_tests(): MODULE and SOURCES are required.")
    endif()
    set(test_sources "")
    foreach(source IN LISTS ARG_SOURCES)
        list(APPEND test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source}")
    endforeach()
    sushiruntime_add_test_program(
        NAME "sr_${ARG_MODULE}_tests" MODULE "${ARG_MODULE}" SOURCES ${test_sources})
    target_include_directories("sr_${ARG_MODULE}_tests" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source")
endfunction()
