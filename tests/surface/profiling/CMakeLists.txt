# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE profiling
    SOURCES
        test_clock_conversion.cpp
        test_device_source.cpp
        test_device_track_name.cpp
        test_profiling_log_category.cpp
        test_register_profiler_parts/test_collect_profiling.cpp
        test_register_profiler_parts/test_device_timing_window.cpp
        test_register_profiler_parts/test_graph_identity.cpp
        test_register_profiler_parts/test_lock_wait.cpp
        test_register_profiler_parts/test_plan_zones.cpp
        test_register_profiler_parts/test_registration.cpp
        test_register_profiler_parts/test_run_counters.cpp
        test_register_profiler_parts/test_run_observer.cpp
        test_register_profiler_parts/test_run_zones.cpp
        test_register_profiler_parts/test_runtime_session.cpp
        test_register_profiler_parts/test_runtime_session_devices.cpp
        test_register_profiler_parts/test_runtime_session_stress.cpp
        test_register_profiler_parts/test_usm_allocator_zones.cpp
        test_run_feed.cpp
        test_run_queue.cpp
        test_runtime_environment.cpp
        test_source_names.cpp
        test_span_loss_tally.cpp
    BENCHMARK_SOURCES
        benchmark_run_overhead.cpp
    DEVICE_SOURCES
        benchmark_run_overhead.cpp
        test_register_profiler_parts/test_collect_profiling.cpp
        test_register_profiler_parts/test_device_timing_window.cpp
        test_register_profiler_parts/test_graph_identity.cpp
        test_register_profiler_parts/test_plan_zones.cpp
        test_register_profiler_parts/test_registration.cpp
        test_register_profiler_parts/test_run_counters.cpp
        test_register_profiler_parts/test_run_observer.cpp
        test_register_profiler_parts/test_run_zones.cpp
        test_register_profiler_parts/test_runtime_session.cpp
        test_register_profiler_parts/test_runtime_session_devices.cpp
        test_register_profiler_parts/test_runtime_session_stress.cpp
        test_register_profiler_parts/test_usm_allocator_zones.cpp)

# The tests include SushiProfiler headers and read its records.
target_link_libraries(sr_profiling_test_objects PRIVATE SushiProfiler::SushiProfiler)
if(TARGET sr_profiling_benchmarks)
    target_link_libraries(sr_profiling_benchmarks PRIVATE SushiProfiler::SushiProfiler)
endif()
