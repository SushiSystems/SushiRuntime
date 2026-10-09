# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.22) # gtest_discover_tests(TEST_FILTER ...)

# The tests that cross module boundaries. A test of one module lives in that module's
# tests/ folder and builds sr_<module>_tests; cmake/SushiRuntimeTests.cmake holds the
# function every program is built with.
#
#   sr_functional_tests   unit/, integration/, regression/
#   sr_allocation_tests   allocation/
#   sr_benchmark_tests    benchmark/, under SR_BUILD_BENCHMARKS
#   Package.*             package/, through tools/packaging

option(SR_BUILD_BENCHMARKS
    "Build the heavy stress/performance test suite (CI fast lane sets OFF)" ON)

sushiruntime_add_test_program(
    NAME sr_functional_tests
    SOURCES
        unit/test_allocation.cpp
        unit/test_node_dispatch_width.cpp
        unit/test_numa_steal.cpp
        unit/test_nvidia_topology.cpp
        unit/test_registration_index.cpp
        unit/test_topology.cpp
        unit/test_topology_abstraction.cpp

        regression/test_regression.cpp

        integration/test_cancellation.cpp
        integration/test_chain_layer.cpp
        integration/test_completion_latch.cpp
        integration/test_co_tenancy.cpp
        integration/test_cross_context_guard.cpp
        integration/test_determinism.cpp
        integration/test_device_selection.cpp
        integration/test_error_isolation.cpp
        integration/test_graph.cpp
        integration/test_graph_profiling_switch.cpp
        integration/test_interop_host_export.cpp
        integration/test_layer_equivalence.cpp
        integration/test_node_event_records.cpp
        integration/test_observability.cpp
        integration/test_perf_controls.cpp
        integration/test_scheduler_integration.cpp)

# Its own program: the test replaces the global operator new to count allocations, and
# a replacement is program-wide. Linked into another program it would count that
# program's allocations too and displace a sanitizer's own operator new.
sushiruntime_add_test_program(
    NAME sr_allocation_tests
    SOURCES allocation/test_steady_state_allocation.cpp)

if(SR_BUILD_BENCHMARKS)
    set(SR_BENCHMARK_SOURCES
        benchmark/benchmark_scheduler_stress.cpp
        benchmark/benchmark_epoch_stress.cpp

        benchmark/benchmark_async_overlap.cpp
        benchmark/benchmark_steal_locality.cpp
        benchmark/benchmark_scheduler_dispatch.cpp
        benchmark/benchmark_profiling_queue.cpp
        benchmark/benchmark_steal_numa_effect.cpp
        benchmark/benchmark_graph_compile.cpp
        benchmark/benchmark_dynamic_graph.cpp
        benchmark/benchmark_recompose_churn.cpp
        benchmark/benchmark_root_fanout.cpp
        benchmark/benchmark_chain_dispatch.cpp
        benchmark/benchmark_wide_fanout.cpp)
    # The offload benchmark exists only when the distributed layer is built.
    if(SR_ENABLE_DISTRIBUTED)
        list(APPEND SR_BENCHMARK_SOURCES benchmark/benchmark_distributed_offload.cpp)
    endif()
    sushiruntime_add_test_program(
        NAME sr_benchmark_tests BENCHMARK
        SOURCES ${SR_BENCHMARK_SOURCES})
endif()

# Whether an installed SushiRuntime is usable from outside its own tree. Its own label,
# because it installs, configures a second CMake project, compiles it and runs it.
#
# Two tests joined by a fixture, so a failure says which half broke: a package that will
# not install is a different defect from one that installs and will not run.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    set(SR_PACKAGE_STAGE "${PROJECT_BINARY_DIR}/stage")

    add_test(NAME Package.InstallsToAPrefix
        COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
                                   --prefix "${SR_PACKAGE_STAGE}")
    set_tests_properties(Package.InstallsToAPrefix PROPERTIES
        FIXTURES_SETUP SR_PACKAGE
        LABELS "package")

    # The compiler is passed through: the runtime's kernels are header templates
    # instantiated in the consumer's own translation units, so a consumer built with a
    # different SYCL compiler is not testing this package.
    add_test(NAME Package.RunsAConsumerWithNothingButThePrefix
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/packaging/verify_installed_package.py"
                "${SR_PACKAGE_STAGE}"
                --compiler "${CMAKE_CXX_COMPILER}"
                --cmake "${CMAKE_COMMAND}"
                --ninja "${CMAKE_MAKE_PROGRAM}")
    set_tests_properties(Package.RunsAConsumerWithNothingButThePrefix PROPERTIES
        FIXTURES_REQUIRED SR_PACKAGE
        LABELS "package")
else()
    message(STATUS "Python3 not found: the packaging suite will not be registered.")
endif()
