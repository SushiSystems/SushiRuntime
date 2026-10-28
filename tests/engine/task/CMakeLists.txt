# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE task
    SOURCES
        test_dispatch_events.cpp
        test_late_bindings.cpp
        test_node.cpp
        test_node_placement.cpp
        test_node_profiling.cpp
        test_node_widths.cpp
        test_offload_coordinator.cpp
        test_offload_sink.cpp
        test_offload_state.cpp
        test_ready_sink.cpp
        test_region_link.cpp
        test_small_event_vector.cpp
        test_task.cpp
        test_task_submission.cpp
        test_task_types.cpp
        test_task_work.cpp
    DEVICE_SOURCES
        test_task_submission.cpp)
