cmake_minimum_required(VERSION 3.20)

# ============================================================================
# SushiRuntime test pipeline
#
# Two suites, two binaries:
#   * sr_functional_tests  -> functional/{unit,regression,integration}
#                             Fast, deterministic, host-friendly. Runs on every
#                             commit in CI (ctest -L functional).
#   * sr_benchmark_tests   -> benchmark/{stress,performance}
#                             Heavy/slow (1M-task bomb, latency probes). Gated by
#                             SUSHI_BUILD_BENCHMARKS, which defaults ON so local
#                             dev gets the binary out of the box. The CI fast lane
#                             passes -DSUSHI_BUILD_BENCHMARKS=OFF to skip building
#                             it and runs only `ctest -L functional`; the nightly
#                             lane leaves it ON and runs `ctest -L benchmark`.
#
# Shared GTest entry point and helpers live in common/.
# ============================================================================

option(SUSHI_BUILD_BENCHMARKS "Build the heavy stress/performance test suite (CI fast lane sets OFF)" ON)

set(SUSHI_TEST_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")

# Re-applies the cross-cutting target settings to a test binary: include dir for
# the shared helpers, GTest + library links, and the Windows runtime-DLL copy.
function(sushi_configure_test_target target)
    target_include_directories(${target} PRIVATE ${SUSHI_TEST_COMMON_DIR})

    target_link_libraries(${target} PRIVATE
        GTest::gtest
        sushiruntime
    )

    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:sushiruntime>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()
endfunction()

include(GoogleTest)

# ----------------------------------------------------------------------------
# Functional suite (CI)
# ----------------------------------------------------------------------------
add_executable(sr_functional_tests
    common/test_main.cpp

    functional/unit/test_logger.cpp
    functional/unit/test_topology.cpp
    functional/unit/test_topology_abstraction.cpp
    functional/unit/test_nvidia_topology.cpp
    functional/unit/test_memory_pool.cpp
    functional/unit/test_affinity.cpp
    functional/unit/test_numa_steal.cpp
    functional/unit/test_epoch_reclamation.cpp
    functional/unit/test_ref_counted.cpp
    functional/unit/test_range_task.cpp
    functional/unit/test_dependency_tracker.cpp
    functional/unit/test_thermal_rebalance.cpp

    functional/regression/test_regression.cpp

    functional/integration/test_graph.cpp
    functional/integration/test_sycl_execution.cpp
    functional/integration/test_async_execution.cpp
    functional/integration/test_heterogeneous_pipeline.cpp
    functional/integration/test_scheduler_integration.cpp
    functional/integration/test_workload_coordinator.cpp
    functional/integration/test_device_pipeline.cpp
)
# Distributed layer tests only exist when the layer is built. They are
# hardware-free (protocol / in-process transport / registries), so they stay in
# the fast functional lane.
if(SUSHI_ENABLE_DISTRIBUTED)
    target_sources(sr_functional_tests PRIVATE
        functional/unit/test_distributed.cpp
    )
endif()

sushi_configure_test_target(sr_functional_tests)

gtest_discover_tests(sr_functional_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "functional"
)

# ----------------------------------------------------------------------------
# Benchmark suite (nightly / manual)
# ----------------------------------------------------------------------------
if(SUSHI_BUILD_BENCHMARKS)
    add_executable(sr_benchmark_tests
        common/test_main.cpp

        benchmark/stress/test_scheduler_stress.cpp
        benchmark/stress/test_memory_stress.cpp
        benchmark/stress/test_epoch_stress.cpp

        benchmark/performance/test_allocator_perf.cpp
        benchmark/performance/test_async_overlap_perf.cpp
        benchmark/performance/test_steal_locality_perf.cpp
    )
    sushi_configure_test_target(sr_benchmark_tests)

    gtest_discover_tests(sr_benchmark_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "benchmark"
    )
endif()
