# SushiRuntimeTests.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# One function builds a test program. The tests of a module sit in tests/<tier>/<module>/,
# the mirror of its folder under modules/, and build one program there. Every program is
# linked, placed and registered the same way. A test links sushiruntime_internal_headers
# (cmake/SushiRuntimeLibrary.cmake), so it includes the headers of the INTERNAL modules,
# which the library's own target does not publish, and it links the objects of the
# TEST_ONLY modules, which hold its entry point.
#
# A test's size comes from its GoogleTest suite-name prefix: Small_*, Medium_* and Large_*,
# each with its own label and timeout. A test named in SR_QUARANTINED_TESTS keeps its size
# and gains the quarantined label, which leaves it out of every gating run.
# `sr test --label <size>` selects one label.

# Suite.Case names of the intermittent tests, each with its entry in
# docs/reference/KNOWN_ISSUES.md.
set(SR_QUARANTINED_TESTS "")

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
            $<TARGET_FILE:SushiLog::SushiLog>
            "${SR_TEST_OUTPUT_DIR}"
        COMMENT "Staging sushiruntime.dll and sushilog.dll next to the test binaries")
    add_dependencies(sushiruntime_tests_runtime_dll sushiruntime)
endif()

# sushiruntime_add_test_program(NAME <target> [BENCHMARK] [MODULE <module>] SOURCES <file>...)
#
# A BENCHMARK program is built and not registered with CTest; `sr bench` runs it. Any other
# program registers Small_* with 60 seconds, Medium_* with 300 and Large_* with 900. MODULE
# adds the module's name as a label on every test of the program.
function(sushiruntime_add_test_program)
    cmake_parse_arguments(ARG "BENCHMARK" "NAME;MODULE" "SOURCES" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_test_program(): NAME and SOURCES are required.")
    endif()

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    sushiruntime_apply_build_policy(${ARG_NAME})
    get_property(test_objects GLOBAL PROPERTY SUSHIRUNTIME_TEST_OBJECT_TARGETS)
    target_link_libraries(${ARG_NAME} PRIVATE
        ${test_objects} GTest::gtest sushiruntime_internal_headers)
    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
    add_sycl_to_target(TARGET ${ARG_NAME} SOURCES ${ARG_SOURCES})
    if(WIN32)
        add_dependencies(${ARG_NAME} sushiruntime_tests_runtime_dll)
    endif()

    if(ARG_BENCHMARK)
        return()
    endif()

    # A suite with no size prefix would register no test, so the build refuses it; the check
    # reruns whenever one of the program's sources changes.
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}_sizes.stamp")
    # A list in one command argument would split, so the sources cross as one '|'-joined word.
    list(JOIN ARG_SOURCES "|" joined_sources)
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND ${CMAKE_COMMAND} "-DSOURCES=${joined_sources}" "-DSTAMP=${stamp}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckTestSizes.cmake"
        DEPENDS ${ARG_SOURCES} "${PROJECT_SOURCE_DIR}/cmake/CheckTestSizes.cmake"
        COMMENT "Checking the test sizes of ${ARG_NAME}"
        VERBATIM)
    add_custom_target(${ARG_NAME}_sizes DEPENDS "${stamp}")
    add_dependencies(${ARG_NAME} ${ARG_NAME}_sizes)
    set(groups "Small_|small|60" "Medium_|medium|300" "Large_|large|900")
    foreach(group IN LISTS groups)
        string(REPLACE "|" ";" parts "${group}")
        list(GET parts 0 prefix)
        list(GET parts 1 label)
        list(GET parts 2 timeout)
        if(NOT "${ARG_MODULE}" STREQUAL "")
            # The separator is escaped: the property list crosses a generated script.
            string(APPEND label "\;${ARG_MODULE}")
        endif()

        # A GoogleTest filter after '-' excludes, so the quarantined tests register apart.
        set(quarantined "")
        foreach(name IN LISTS SR_QUARANTINED_TESTS)
            if(name MATCHES "^${prefix}")
                list(APPEND quarantined "${name}")
            endif()
        endforeach()
        list(JOIN quarantined ":" quarantined_filter)

        set(filter "${prefix}*")
        if(quarantined_filter)
            string(APPEND filter "-${quarantined_filter}")
            gtest_discover_tests(${ARG_NAME}
                DISCOVERY_MODE PRE_TEST
                WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/tests"
                TEST_FILTER "${quarantined_filter}"
                PROPERTIES LABELS "${label}\;quarantined" TIMEOUT ${timeout})
        endif()
        gtest_discover_tests(${ARG_NAME}
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/tests"
            TEST_FILTER "${filter}"
            PROPERTIES LABELS "${label}" TIMEOUT ${timeout})
    endforeach()
endfunction()

# sushiruntime_add_module_tests(MODULE <module> [SOURCES <file>...]
#                               [BENCHMARK_SOURCES <file>...])
#
# Called from tests/<tier>/<module>/CMakeLists.txt, with paths relative to that folder.
# SOURCES build sr_<module>_tests; BENCHMARK_SOURCES build sr_<module>_benchmarks under
# SR_BUILD_BENCHMARKS. Both programs include the helpers of the test folder by a quoted name,
# and never a header under modules/<tier>/<module>/source/: a brick a test needs is a module.
function(sushiruntime_add_module_tests)
    cmake_parse_arguments(ARG "" "MODULE" "SOURCES;BENCHMARK_SOURCES" ${ARGN})
    if("${ARG_MODULE}" STREQUAL "" OR (NOT ARG_SOURCES AND NOT ARG_BENCHMARK_SOURCES))
        message(FATAL_ERROR
            "sushiruntime_add_module_tests(): MODULE and SOURCES or BENCHMARK_SOURCES are "
            "required.")
    endif()
    set(programs "")
    if(ARG_SOURCES)
        list(APPEND programs "sr_${ARG_MODULE}_tests|SOURCES")
    endif()
    if(ARG_BENCHMARK_SOURCES AND SR_BUILD_BENCHMARKS)
        list(APPEND programs "sr_${ARG_MODULE}_benchmarks|BENCHMARK_SOURCES")
    endif()
    foreach(program IN LISTS programs)
        string(REPLACE "|" ";" parts "${program}")
        list(GET parts 0 name)
        list(GET parts 1 list_name)
        set(sources "")
        foreach(source IN LISTS ARG_${list_name})
            list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        endforeach()
        if(list_name STREQUAL "BENCHMARK_SOURCES")
            sushiruntime_add_test_program(
                NAME "${name}" BENCHMARK MODULE "${ARG_MODULE}" SOURCES ${sources})
        else()
            sushiruntime_add_test_program(NAME "${name}" MODULE "${ARG_MODULE}" SOURCES ${sources})
        endif()
        # A test reaches its module through the include roots only; source/ stays private.
        target_include_directories("${name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
endfunction()
