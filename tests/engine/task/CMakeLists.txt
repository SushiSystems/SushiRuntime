# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE task
    SOURCES
        compile_late_bindings.cpp
        compile_node_profiling.cpp
        compile_ready_sink.cpp
        compile_task_work.cpp
        compile_usm_placement_memory.cpp
        test_dispatch_events.cpp
        test_handle_location.cpp
        test_in_memory_offload_coordinator.cpp
        test_in_memory_offload_sink.cpp
        test_in_memory_placement_memory.cpp
        test_kernel_pointers.cpp
        test_node.cpp
        test_node_placement.cpp
        test_node_widths.cpp
        test_offload_coordinator.cpp
        test_offload_sink.cpp
        test_offload_state.cpp
        test_placement_memory.cpp
        test_recording_task.cpp
        test_refusing_offload_sink.cpp
        test_region_link.cpp
        test_small_event_vector.cpp
        test_placed_split.cpp
        test_split_dispatch.cpp
        test_task.cpp
        test_task_submission.cpp
        test_task_types.cpp
        test_usm_placement_memory.cpp
    BENCHMARK_SOURCES
        benchmark_residency_resolve.cpp
    DEVICE_SOURCES
        test_split_dispatch.cpp
        test_task_submission.cpp)
