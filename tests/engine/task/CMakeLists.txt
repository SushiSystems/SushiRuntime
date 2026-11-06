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
        test_dispatch_events.cpp
        test_in_memory_offload_coordinator.cpp
        test_in_memory_offload_sink.cpp
        test_node.cpp
        test_node_placement.cpp
        test_node_widths.cpp
        test_offload_coordinator.cpp
        test_offload_sink.cpp
        test_offload_state.cpp
        test_recording_task.cpp
        test_region_link.cpp
        test_small_event_vector.cpp
        test_task.cpp
        test_task_submission.cpp
        test_task_types.cpp
    DEVICE_SOURCES
        test_task_submission.cpp)
