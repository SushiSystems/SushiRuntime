# SushiRuntimeTests.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

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
# each with its own label and a timeout per case. The cases of a program, or of one module in a
# tier program, run in shards per size, <program>[.<module>].<size>_shard_<i>of<N>. A test named
# in SR_QUARANTINED_TESTS keeps its size and gains the quarantined label, which leaves it out of
# every gating run, and runs apart from the shards.
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

# The imported GTest targets ask for cxx_std_14, which a compiler newer than CMake's
# feature table cannot answer; the tree pins C++17 itself.
foreach(gtest_target GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    if(TARGET ${gtest_target})
        set_target_properties(${gtest_target} PROPERTIES INTERFACE_COMPILE_FEATURES "")
    endif()
endforeach()

# Every test program lands in one folder, so one copy of the library serves them all.
set(SR_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/tests")

# The cases one CTest test runs, per size. A process pays its start-up (the runtime, the logger)
# once, so a case run alone is mostly start-up. A shard runs its share of the cases in one
# process, and the share follows the cost of a case of its size: a small case is a few
# milliseconds, so 300 of them are a few seconds and the run still spreads over every core; a
# medium case uses threads, files or the device and takes up to 10 s, so 25 keep a shard to a
# few minutes at most; a large case takes up to 120 s and runs alone.
set(SR_SHARD_CASES_SMALL 300)
set(SR_SHARD_CASES_MEDIUM 25)
set(SR_SHARD_CASES_LARGE 1)

# Where each shard writes its per-case GoogleTest XML; the time reader reads the folder, so the
# per-case limits of the testing skill still apply to a case run inside a shard.
set(SR_GTEST_RESULTS_DIR "${PROJECT_BINARY_DIR}/test_results/gtest")

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
# program registers its Small_*, Medium_* and Large_* cases in shards, with 1, 10 and 120
# seconds per case. MODULE adds the module's name as a label on every test of the program. DEVICE_SOURCES names the
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
    # tests/ is an include root, so a test reaches the shared helpers as "support/<name>.hpp".
    target_include_directories(${ARG_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
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
    sushiruntime_scan_test_sources(patterns declared ${ARG_SOURCES})
    sushiruntime_register_sized_tests(${ARG_NAME} STEM "${ARG_NAME}" MODULE "${ARG_MODULE}"
        DECLARED ${declared} FILTER "*")
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

# sushiruntime_scan_test_sources(<patterns_var> <declared_var> <source>...)
#
# Reads the sources and sets <patterns_var> to the GoogleTest patterns that select their suites
# and <declared_var> to one entry per declaration, named by the suite or instantiation prefix
# that carries the size. A TEST_P or TYPED_TEST_P counts once however many instances it gets, so
# the declared list is a floor of the case count, and no pattern is dropped because of it.
#
# A plain or typed suite is `<suite>.*` and `<suite>/*`. An instantiated suite is named
# `<prefix>/<suite>.<case>/<index>`, and a typed one `<prefix>/<suite>/<index>.<case>`, so
# INSTANTIATE_* adds `*/<suite>.*` and `<prefix>/*`: the size sits in the suite of a TEST_P and
# in the prefix of a typed instantiation, whose suite lives in a support header.
function(sushiruntime_scan_test_sources patterns_var declared_var)
    set(patterns "")
    set(declared "")
    foreach(source IN LISTS ARGN)
        file(READ "${source}" text)
        string(REGEX MATCHALL "TYPED_TEST(_P)?[ \t]*[(][ \t\r\n]*[A-Za-z0-9_]+" typed "${text}")
        string(REGEX MATCHALL "(^|[^A-Za-z0-9_])TEST(_F|_P)?[ \t]*[(][ \t\r\n]*[A-Za-z0-9_]+" plain "${text}")
        foreach(use IN LISTS typed plain)
            string(REGEX REPLACE "^[^(]*[(][ \t\r\n]*" "" suite "${use}")
            list(APPEND declared "${suite}")
            list(APPEND patterns "${suite}.*" "${suite}/*" "*/${suite}.*")
        endforeach()
        string(REGEX MATCHALL
            "INSTANTIATE_(TYPED_)?TEST_(SUITE|CASE)_P[ \t]*[(][ \t\r\n]*[A-Za-z0-9_]+[ \t\r\n]*,[ \t\r\n]*[A-Za-z0-9_]+"
            instances "${text}")
        foreach(use IN LISTS instances)
            string(REGEX REPLACE "^[^(]*[(][ \t\r\n]*" "" arguments "${use}")
            string(REGEX REPLACE "[ \t\r\n]*,.*$" "" prefix "${arguments}")
            string(REGEX REPLACE "^.*,[ \t\r\n]*" "" suite "${arguments}")
            list(APPEND patterns "${prefix}/*" "*/${suite}.*")
            if(prefix MATCHES "^(Small|Medium|Large)_")
                list(APPEND declared "${prefix}")
            endif()
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    endforeach()
    list(REMOVE_DUPLICATES patterns)
    set(${patterns_var} "${patterns}" PARENT_SCOPE)
    set(${declared_var} "${declared}" PARENT_SCOPE)
endfunction()

# Registers one CTest test that runs <program> with <filter>, as shard <index> of <shards>.
# GoogleTest splits the filtered cases round-robin by GTEST_TOTAL_SHARDS and GTEST_SHARD_INDEX,
# and the test writes its per-case XML under SR_GTEST_RESULTS_DIR. GTEST_SHUFFLE and
# GTEST_RANDOM_SEED of the calling environment reach it.
function(sushiruntime_add_shard_test program name filter labels timeout index shards)
    add_test(NAME ${name}
        COMMAND ${program} --gtest_filter=${filter}
            --gtest_output=xml:${SR_GTEST_RESULTS_DIR}/${name}.xml
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/tests")
    set_tests_properties(${name} PROPERTIES
        LABELS "${labels}"
        TIMEOUT ${timeout}
        ENVIRONMENT "GTEST_TOTAL_SHARDS=${shards};GTEST_SHARD_INDEX=${index}")
endfunction()

# Registers the cases of <program> that the patterns select, of one size, in shards with the
# size's label and a timeout of the case timeout times the cases of a shard. The quarantined
# cases of the size run apart in one test that also carries `quarantined`, and a GoogleTest
# filter after '-' leaves them out of the shards. <size_group> is "<prefix>|<label>|<case
# timeout>". This is the one function that registers shards.
function(sushiruntime_register_size_shards program stem module size_group declared patterns)
    string(REPLACE "|" ";" parts "${size_group}")
    list(GET parts 0 prefix)
    list(GET parts 1 label)
    list(GET parts 2 case_timeout)
    string(TOUPPER "${label}" upper)

    set(selected "")
    foreach(pattern IN LISTS patterns)
        if(pattern STREQUAL "*")
            list(APPEND selected "${prefix}*" "*/${prefix}*")
        elseif(pattern MATCHES "^([*]/)?${prefix}")
            list(APPEND selected "${pattern}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES selected)
    if(NOT selected)
        return()
    endif()
    list(JOIN selected ":" filter)

    set(cases 0)
    foreach(name IN LISTS declared)
        if(name MATCHES "^${prefix}")
            math(EXPR cases "${cases} + 1")
        endif()
    endforeach()

    set(labels "${label}")
    if(NOT "${module}" STREQUAL "")
        list(APPEND labels "${module}")
    endif()

    # An instantiated suite is named <instance>/<suite>, so each name also carries the slash.
    set(quarantined "")
    set(quarantined_cases 0)
    foreach(name IN LISTS SR_QUARANTINED_TESTS)
        string(REGEX REPLACE "[.].*$" "" suite "${name}")
        if(name MATCHES "^${prefix}" AND suite IN_LIST declared)
            list(APPEND quarantined "${name}" "*/${name}")
            math(EXPR quarantined_cases "${quarantined_cases} + 1")
        endif()
    endforeach()
    if(quarantined)
        list(JOIN quarantined ":" quarantined_filter)
        string(APPEND filter "-${quarantined_filter}")
        math(EXPR quarantined_timeout "${case_timeout} * ${quarantined_cases}")
        sushiruntime_add_shard_test(${program} ${stem}.${label}_quarantined
            "${quarantined_filter}" "${labels};quarantined" ${quarantined_timeout} 0 1)
        math(EXPR cases "${cases} - ${quarantined_cases}")
    endif()
    if(cases LESS 1)
        set(cases 1)
    endif()

    math(EXPR shards "(${cases} + ${SR_SHARD_CASES_${upper}} - 1) / ${SR_SHARD_CASES_${upper}}")
    math(EXPR shard_cases "(${cases} + ${shards} - 1) / ${shards}")
    math(EXPR timeout "${case_timeout} * ${shard_cases}")
    math(EXPR last "${shards} - 1")
    foreach(index RANGE ${last})
        sushiruntime_add_shard_test(${program} ${stem}.${label}_shard_${index}of${shards}
            "${filter}" "${labels}" ${timeout} ${index} ${shards})
    endforeach()
endfunction()

# sushiruntime_register_sized_tests(<program> STEM <name> [MODULE <module>] DECLARED <name>...
#                                   FILTER <pattern>...)
#
# Registers the cases of <program> that the GoogleTest patterns select, in shards per size:
# Small_* with 1 second per case, Medium_* with 10 and Large_* with 120. STEM opens the name of
# each test, so two modules of one tier program register under distinct names; MODULE adds the
# module's name as a label on each of them. DECLARED lists the declarations the patterns come
# from (sushiruntime_scan_test_sources) and sizes the shards. A pattern names a suite as
# `<suite>.*`, or `*` for every suite.
function(sushiruntime_register_sized_tests program)
    cmake_parse_arguments(ARG "" "MODULE;STEM" "DECLARED;FILTER" ${ARGN})
    set(groups "Small_|small|1" "Medium_|medium|10" "Large_|large|120")
    foreach(group IN LISTS groups)
        sushiruntime_register_size_shards(${program} "${ARG_STEM}" "${ARG_MODULE}" "${group}"
            "${ARG_DECLARED}" "${ARG_FILTER}")
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
    # tests/ is an include root, so a test reaches the shared helpers as "support/<name>.hpp".
    target_include_directories(${ARG_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    sushiruntime_apply_sycl(${ARG_NAME} SOURCES ${ARG_SOURCES} DEVICE_SOURCES ${ARG_DEVICE_SOURCES})
    sushiruntime_check_test_sizes(${ARG_NAME} ${ARG_SOURCES})

    sushiruntime_scan_test_sources(patterns declared ${ARG_SOURCES})

    file(RELATIVE_PATH folder "${PROJECT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REGEX REPLACE "/.*$" "" tier "${folder}")
    set_property(GLOBAL APPEND PROPERTY SR_TEST_TIERS ${tier})
    set_property(GLOBAL APPEND PROPERTY SR_TEST_OBJECTS_${tier} ${ARG_NAME})
    set_property(TARGET ${ARG_NAME} PROPERTY SR_TEST_MODULE "${ARG_MODULE}")
    set_property(TARGET ${ARG_NAME} PROPERTY SR_TEST_PATTERNS "${patterns}")
    set_property(TARGET ${ARG_NAME} PROPERTY SR_TEST_DECLARED "${declared}")
endfunction()

# sushiruntime_add_tier_test_programs()
#
# Builds sr_<tier>_tests from the object libraries sushiruntime_add_test_objects() recorded for
# each tier and registers the tests of each library in shards under its module's label. Called
# once, after every test folder is read.
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
            get_target_property(declared ${object} SR_TEST_DECLARED)
            sushiruntime_register_sized_tests(${program} STEM "${program}.${module}"
                MODULE "${module}" DECLARED ${declared} FILTER ${patterns})
        endforeach()
    endforeach()
endfunction()
