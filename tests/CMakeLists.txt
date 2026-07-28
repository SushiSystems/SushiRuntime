cmake_minimum_required(VERSION 3.22) # gtest_discover_tests(TEST_FILTER ...)

# ============================================================================
# SushiRuntime test pipeline
#
# Two suites, two binaries:
#   * sr_functional_tests  -> functional/{unit,regression,integration}
#                             Fast, deterministic, host-friendly. Runs on every
#                             commit in CI (ctest -L 'unit|integration|regression').
#   * sr_benchmark_tests   -> benchmark/{stress,performance}
#                             Heavy/slow (1M-task bomb, latency probes). Gated by
#                             SR_BUILD_BENCHMARKS, which defaults ON so local
#                             dev gets the binary out of the box. The CI fast lane
#                             passes -DSR_BUILD_BENCHMARKS=OFF to skip building it
#                             and runs only the functional label regex; the nightly
#                             lane leaves it ON and runs `ctest -L 'stress|performance'`.
#
# Shared GTest entry point and helpers live in common/.
# ============================================================================

option(SR_BUILD_BENCHMARKS "Build the heavy stress/performance test suite (CI fast lane sets OFF)" ON)

# Work around "No known features for CXX compiler Clang <ver>" at generate time.
# A bleeding-edge clang (e.g. 23.0.0 from the intel/llvm nightly) is newer than
# this CMake's compiler-feature database, so "Detecting CXX compile features"
# fails and CMake cannot resolve the INTERFACE_COMPILE_FEATURES (cxx_std_14) that
# the imported GTest targets carry — erroring for any target that links them.
# The whole tree already pins CMAKE_CXX_STANDARD=17 (see cmake/ProjectOptions),
# so that feature requirement is redundant; clear it on the GTest targets so the
# generate step succeeds. The library itself configures fine because it never
# links GTest.
foreach(_sr_gtest_tgt GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    if(TARGET ${_sr_gtest_tgt})
        set_target_properties(${_sr_gtest_tgt} PROPERTIES INTERFACE_COMPILE_FEATURES "")
    endif()
endforeach()

set(SR_TEST_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")

# Re-applies the cross-cutting target settings to a test binary: include dir for
# the shared helpers, GTest + library links, and the Windows runtime-DLL copy.
function(sushi_configure_test_target target)
    target_include_directories(${target} PRIVATE ${SR_TEST_COMMON_DIR})

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
    functional/unit/test_delegate.cpp
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
    functional/unit/test_event_polling.cpp
    functional/unit/test_work_stealing_deque.cpp
    functional/unit/test_fixed_order_reduce.cpp

    functional/regression/test_regression.cpp

    functional/integration/test_graph.cpp
    functional/integration/test_sycl_execution.cpp
    functional/integration/test_async_execution.cpp
    functional/integration/test_heterogeneous_pipeline.cpp
    functional/integration/test_scheduler_integration.cpp
    functional/integration/test_error_isolation.cpp
    functional/integration/test_cancellation.cpp
    functional/integration/test_workload_coordinator.cpp
    functional/integration/test_device_pipeline.cpp
    functional/integration/test_fluent_lifecycle.cpp
    functional/integration/test_fluent_api.cpp
    functional/integration/test_fluent_hardening.cpp
    functional/integration/test_late_bindings.cpp
    functional/integration/test_device_count.cpp
    functional/integration/test_async_step.cpp
    functional/integration/test_native_cpu_path.cpp
    functional/integration/test_device_resident.cpp
    functional/integration/test_perf_controls.cpp
    functional/integration/test_observability.cpp
    functional/integration/test_completion_latch.cpp
    functional/integration/test_cross_context_guard.cpp
    functional/integration/test_dynamic_graph.cpp
)
# Distributed layer tests only exist when the layer is built. They are
# hardware-free (protocol / in-process transport / registries), so they stay in
# the fast functional lane.
if(SR_ENABLE_DISTRIBUTED)
    target_sources(sr_functional_tests PRIVATE
        functional/unit/test_distributed.cpp
        functional/integration/test_distributed_e2e.cpp
        functional/integration/test_distributed_jepsen.cpp
    )
endif()

sushi_configure_test_target(sr_functional_tests)

# Sub-suite labels come from the GTest suite-name prefix convention
# (Unit_* / Integration_* / Regression_*), so a category filter is robust and
# new tests are picked up automatically as long as they follow the convention.
#
# Each group carries a SINGLE label. The "functional" umbrella is expressed by
# the consumer as a label regex (`ctest -L 'unit|integration|regression'`, which
# is what `sr test --suite functional` runs) rather than a second label
# baked in here: a two-element LABELS list (e.g. "unit;functional") is silently
# flattened to two separate one-element labels by this CMake version's
# GoogleTestAddTests.cmake (it loses the list escaping when re-expanding the
# property), so the umbrella label never actually registers. Keeping one label
# per group sidesteps that entirely and is robust across CMake versions.
gtest_discover_tests(sr_functional_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER "Unit_*"
    PROPERTIES LABELS "unit"
)
gtest_discover_tests(sr_functional_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER "Integration_*"
    PROPERTIES LABELS "integration"
)
gtest_discover_tests(sr_functional_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER "Regression_*"
    PROPERTIES LABELS "regression"
)

# ----------------------------------------------------------------------------
# Benchmark suite (nightly / manual)
# ----------------------------------------------------------------------------
if(SR_BUILD_BENCHMARKS)
    add_executable(sr_benchmark_tests
        common/test_main.cpp

        benchmark/stress/test_scheduler_stress.cpp
        benchmark/stress/test_memory_stress.cpp
        benchmark/stress/test_epoch_stress.cpp

        benchmark/performance/test_allocator_perf.cpp
        benchmark/performance/test_async_overlap_perf.cpp
        benchmark/performance/test_steal_locality_perf.cpp
        benchmark/performance/test_scheduler_dispatch_perf.cpp
        benchmark/performance/test_steal_numa_effect_perf.cpp
        benchmark/performance/test_graph_compile_perf.cpp
        benchmark/performance/test_dynamic_graph_perf.cpp
        benchmark/performance/test_recompose_churn_perf.cpp
        benchmark/performance/test_root_fanout_perf.cpp
        benchmark/performance/test_wide_fanout_perf.cpp
    )
    # Distributed offload benchmark only exists when the layer is built.
    if(SR_ENABLE_DISTRIBUTED)
        target_sources(sr_benchmark_tests PRIVATE
            benchmark/performance/test_distributed_offload_perf.cpp
        )
    endif()
    sushi_configure_test_target(sr_benchmark_tests)

    # Same prefix convention: Stress_* / Performance_*. As above, single labels;
    # the "benchmark" umbrella is the regex `ctest -L 'stress|performance'`.
    gtest_discover_tests(sr_benchmark_tests
        DISCOVERY_MODE PRE_TEST
        TEST_FILTER "Stress_*"
        PROPERTIES LABELS "stress"
    )
    gtest_discover_tests(sr_benchmark_tests
        DISCOVERY_MODE PRE_TEST
        TEST_FILTER "Performance_*"
        PROPERTIES LABELS "performance"
    )
endif()
