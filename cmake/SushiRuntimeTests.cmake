# SushiRuntimeTests.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# One function builds a test program. The tests of a module sit in tests/<tier>/<module>/,
# the mirror of its folder under modules/, and build an object library there, and each tier
# links the object libraries of its modules into one program. A program repeats the device link
# of the library it links, so one program per tier pays it once. Every program is linked,
# placed and registered the same way. A test links sushiruntime_internal_headers
# (cmake/SushiRuntimeLibrary.cmake), so it includes the headers of the INTERNAL modules,
# which the library's own target does not publish, and it links the objects of the
# TEST_ONLY modules, which hold its entry point.
#
# A test's size comes from its GoogleTest suite-name prefix: Small_*, Medium_* and Large_*,
# each with its own label and timeout. A test named in SR_QUARANTINED_TESTS keeps its size
# and gains the quarantined label, which leaves it out of every gating run.
# `sr test --label <size>` selects one label.

# Suite.Case names of the intermittent tests, each with its known issue
# in docs/backlog/.
set(SR_QUARANTINED_TESTS
    Medium_DataHandlesDeathTest.StateStopsOnAWindowPastTheEnd
)
# The tests AdaptiveCpp's CPU backend fails, KI-18 to KI-22 in docs/backlog/CORRECTNESS.md.
if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    list(APPEND SR_QUARANTINED_TESTS
        Medium_UsmAllocatorFailure.LogsTheRefusedRequest
        Medium_MemoryExchangeImport.AKindTheDeviceDoesNotImportIsRefusedByName
        Medium_PlacementScopeDevice.AKindOrMemoryNoDeviceHasIsAbsent
        Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone
        Medium_InteropBorrow.ARangeCopyReachesBorrowedMemoryTheRuntimeDidNotAllocate
    )
endif()

if(NOT SR_BUILD_TESTS)
    function(sushiruntime_add_test_program)
    endfunction()
    function(sushiruntime_add_module_tests)
    endfunction()
    function(sushiruntime_add_tier_test_programs)
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
# together and Windows reports a sharing violation on the file they all write. The
# GTest and hwloc DLLs are staged here too, from the configuration each one links:
# vcpkg's own deployment copies debug/bin into a Debug tree, which links GTest's
# Release build (cmake/SushiRuntimeLanguage.cmake), and gtest.dll then crashes on load.
if(WIN32)
    if(CMAKE_CONFIGURATION_TYPES)
        set(SR_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/tests/$<CONFIG>")
    endif()
    set(sr_profiler_dll "")
    if(SR_ENABLE_PROFILER)
        set(sr_profiler_dll "$<TARGET_FILE:SushiProfiler::SushiProfiler>")
    endif()
    # hwloc links through pkg-config, from debug/lib in a Debug tree and lib otherwise.
    set(sr_vcpkg_bin
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin")
    add_custom_target(sushiruntime_tests_runtime_dll
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SR_TEST_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sushiruntime>
            $<TARGET_FILE:SushiLog::SushiLog>
            ${sr_profiler_dll}
            $<TARGET_FILE:GTest::gtest>
            "${sr_vcpkg_bin}/hwloc-15.dll"
            "${SR_TEST_OUTPUT_DIR}"
        COMMENT "Staging the runtime and its package DLLs next to the test binaries")
    add_dependencies(sushiruntime_tests_runtime_dll sushiruntime)
endif()

# sushiruntime_add_test_program(NAME <target> [BENCHMARK] [MODULE <module>] SOURCES <file>...
#                               [DEVICE_SOURCES <file>...])
#
# A BENCHMARK program is built and not registered with CTest; `sr bench` runs it. Any other
# program registers Small_* with 60 seconds, Medium_* with 300 and Large_* with 900. MODULE
# adds the module's name as a label on every test of the program. DEVICE_SOURCES names the
# SOURCES that instantiate a kernel, the only units compiled for the device.
function(sushiruntime_add_test_program)
    cmake_parse_arguments(ARG "BENCHMARK" "NAME;MODULE" "SOURCES;DEVICE_SOURCES" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_test_program(): NAME and SOURCES are required.")
    endif()

    # The staging target above deploys the DLLs; vcpkg's per-program copy would undo it.
    set(VCPKG_APPLOCAL_DEPS OFF)
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    sushiruntime_apply_build_policy(${ARG_NAME})
    get_property(test_objects GLOBAL PROPERTY SUSHIRUNTIME_TEST_OBJECT_TARGETS)
    target_link_libraries(${ARG_NAME} PRIVATE
        ${test_objects} GTest::gtest sushiruntime_internal_headers)
    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
    sushiruntime_apply_sycl(${ARG_NAME} SOURCES ${ARG_SOURCES} DEVICE_SOURCES ${ARG_DEVICE_SOURCES})
    if(WIN32)
        add_dependencies(${ARG_NAME} sushiruntime_tests_runtime_dll)
    endif()

    if(ARG_BENCHMARK)
        return()
    endif()
    sushiruntime_check_test_sizes(${ARG_NAME} ${ARG_SOURCES})
    sushiruntime_register_sized_tests(${ARG_NAME} MODULE "${ARG_MODULE}" FILTER "*")
endfunction()

# sushiruntime_check_test_sizes(<target> <source>...)
#
# A suite with no size prefix would register no test, so the build of <target> refuses it; the
# check reruns whenever one of the sources changes.
function(sushiruntime_check_test_sizes target)
    set(ARG_NAME ${target})
    set(ARG_SOURCES ${ARGN})
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
endfunction()

# sushiruntime_register_sized_tests(<program> [MODULE <module>] FILTER <pattern>...)
#
# Registers the tests of <program> that the GoogleTest patterns select: Small_* with 60
# seconds, Medium_* with 300 and Large_* with 900. MODULE adds the module's name as a label on
# each of them. A pattern names a suite as `<suite>.*`, or `*` for every suite.
function(sushiruntime_register_sized_tests program)
    cmake_parse_arguments(ARG "" "MODULE" "FILTER" ${ARGN})
    set(ARG_NAME ${program})
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

        set(patterns "")
        foreach(pattern IN LISTS ARG_FILTER)
            if(pattern STREQUAL "*")
                list(APPEND patterns "${prefix}*")
            elseif(pattern MATCHES "^${prefix}")
                list(APPEND patterns "${pattern}")
            endif()
        endforeach()
        if(NOT patterns)
            continue()
        endif()
        list(JOIN patterns ":" filter)

        # A GoogleTest filter after '-' excludes, so the quarantined tests the patterns select
        # register apart.
        set(quarantined "")
        foreach(name IN LISTS SR_QUARANTINED_TESTS)
            string(REGEX REPLACE "[.].*$" "" suite "${name}")
            if(name MATCHES "^${prefix}"
               AND ("*" IN_LIST ARG_FILTER OR "${suite}.*" IN_LIST ARG_FILTER))
                list(APPEND quarantined "${name}")
            endif()
        endforeach()
        list(JOIN quarantined ":" quarantined_filter)
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
#                               [BENCHMARK_SOURCES <file>...] [DEVICE_SOURCES <file>...])
#
# Called from tests/<tier>/<module>/CMakeLists.txt, with paths relative to that folder.
# SOURCES build the object library sr_<module>_test_objects, which
# sushiruntime_add_tier_test_programs() links into sr_<tier>_tests; BENCHMARK_SOURCES build
# sr_<module>_benchmarks under
# SR_BUILD_BENCHMARKS. DEVICE_SOURCES names the units of either list that instantiate a
# kernel, the only units compiled for the device. Both programs include the helpers of the
# test folder by a quoted name, and never a header under modules/<tier>/<module>/source/: a
# brick a test needs is a module.
function(sushiruntime_add_module_tests)
    cmake_parse_arguments(ARG "" "MODULE" "SOURCES;BENCHMARK_SOURCES;DEVICE_SOURCES" ${ARGN})
    if("${ARG_MODULE}" STREQUAL "" OR (NOT ARG_SOURCES AND NOT ARG_BENCHMARK_SOURCES))
        message(FATAL_ERROR
            "sushiruntime_add_module_tests(): MODULE and SOURCES or BENCHMARK_SOURCES are "
            "required.")
    endif()
    foreach(source IN LISTS ARG_DEVICE_SOURCES)
        if(NOT source IN_LIST ARG_SOURCES AND NOT source IN_LIST ARG_BENCHMARK_SOURCES)
            message(FATAL_ERROR
                "sushiruntime_add_module_tests(${ARG_MODULE}): DEVICE_SOURCES names '${source}', "
                "which neither SOURCES nor BENCHMARK_SOURCES lists.")
        endif()
    endforeach()
    set(programs "")
    if(ARG_SOURCES)
        list(APPEND programs "sr_${ARG_MODULE}_test_objects|SOURCES")
    endif()
    if(ARG_BENCHMARK_SOURCES AND SR_BUILD_BENCHMARKS)
        list(APPEND programs "sr_${ARG_MODULE}_benchmarks|BENCHMARK_SOURCES")
    endif()
    foreach(program IN LISTS programs)
        string(REPLACE "|" ";" parts "${program}")
        list(GET parts 0 name)
        list(GET parts 1 list_name)
        set(sources "")
        set(device_sources "")
        foreach(source IN LISTS ARG_${list_name})
            list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            if(source IN_LIST ARG_DEVICE_SOURCES)
                list(APPEND device_sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            endif()
        endforeach()
        if(list_name STREQUAL "BENCHMARK_SOURCES")
            sushiruntime_add_test_program(
                NAME "${name}" BENCHMARK MODULE "${ARG_MODULE}" SOURCES ${sources}
                DEVICE_SOURCES ${device_sources})
        else()
            sushiruntime_add_test_objects(NAME "${name}" MODULE "${ARG_MODULE}" SOURCES ${sources}
                DEVICE_SOURCES ${device_sources})
        endif()
        # A test reaches its module through the include roots only; source/ stays private.
        target_include_directories("${name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
endfunction()

# sushiruntime_add_test_objects(NAME <target> MODULE <module> SOURCES <file>...
#                               [DEVICE_SOURCES <file>...])
#
# Builds the tests of one module as an object library and records it, with the suites its
# sources declare, for the program of its tier: the first folder under tests/.
function(sushiruntime_add_test_objects)
    cmake_parse_arguments(ARG "" "NAME;MODULE" "SOURCES;DEVICE_SOURCES" ${ARGN})
    add_library(${ARG_NAME} OBJECT ${ARG_SOURCES})
    sushiruntime_apply_build_policy(${ARG_NAME})
    get_property(test_objects GLOBAL PROPERTY SUSHIRUNTIME_TEST_OBJECT_TARGETS)
    target_link_libraries(${ARG_NAME} PRIVATE
        ${test_objects} GTest::gtest sushiruntime_internal_headers)
    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    sushiruntime_apply_sycl(${ARG_NAME} SOURCES ${ARG_SOURCES} DEVICE_SOURCES ${ARG_DEVICE_SOURCES})
    sushiruntime_check_test_sizes(${ARG_NAME} ${ARG_SOURCES})

    set(suites "")
    foreach(source IN LISTS ARG_SOURCES)
        file(READ "${source}" text)
        string(REGEX MATCHALL "TYPED_TEST(_P)?[ \t]*[(][ \t\r\n]*[A-Za-z0-9_]+" typed "${text}")
        string(REGEX MATCHALL "TEST(_F|_P)?[ \t]*[(][ \t\r\n]*[A-Za-z0-9_]+" plain "${text}")
        foreach(use IN LISTS typed plain)
            string(REGEX REPLACE "^[^(]*[(][ \t\r\n]*" "" suite "${use}")
            list(APPEND suites "${suite}")
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    endforeach()
    list(REMOVE_DUPLICATES suites)
    set(patterns "")
    foreach(suite IN LISTS suites)
        # A typed suite's tests are named <suite>/<index>.<case>.
        list(APPEND patterns "${suite}.*" "${suite}/*")
    endforeach()

    file(RELATIVE_PATH folder "${PROJECT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REGEX REPLACE "/.*$" "" tier "${folder}")
    set_property(GLOBAL APPEND PROPERTY SR_TEST_TIERS ${tier})
    set_property(GLOBAL APPEND PROPERTY SR_TEST_OBJECTS_${tier} ${ARG_NAME})
    set_property(TARGET ${ARG_NAME} PROPERTY SR_TEST_MODULE "${ARG_MODULE}")
    set_property(TARGET ${ARG_NAME} PROPERTY SR_TEST_PATTERNS "${patterns}")
endfunction()

# sushiruntime_add_tier_test_programs()
#
# Builds sr_<tier>_tests from the object libraries sushiruntime_add_test_objects() recorded for
# each tier and registers the tests of each library under its module's label. Called once,
# after every test folder is read.
function(sushiruntime_add_tier_test_programs)
    get_property(tiers GLOBAL PROPERTY SR_TEST_TIERS)
    list(REMOVE_DUPLICATES tiers)
    get_property(test_objects GLOBAL PROPERTY SUSHIRUNTIME_TEST_OBJECT_TARGETS)
    # The staging target deploys the DLLs; vcpkg's per-program copy would undo it.
    set(VCPKG_APPLOCAL_DEPS OFF)
    foreach(tier IN LISTS tiers)
        get_property(objects GLOBAL PROPERTY SR_TEST_OBJECTS_${tier})
        set(program sr_${tier}_tests)
        add_executable(${program})
        sushiruntime_apply_build_policy(${program})
        target_link_libraries(${program} PRIVATE
            ${objects} ${test_objects} GTest::gtest sushiruntime_internal_headers)
        set_target_properties(${program} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
        sushiruntime_apply_sycl(${program})
        if(WIN32)
            add_dependencies(${program} sushiruntime_tests_runtime_dll)
        endif()
        foreach(object IN LISTS objects)
            get_target_property(module ${object} SR_TEST_MODULE)
            get_target_property(patterns ${object} SR_TEST_PATTERNS)
            sushiruntime_register_sized_tests(${program} MODULE "${module}" FILTER ${patterns})
        endforeach()
    endforeach()
endfunction()
