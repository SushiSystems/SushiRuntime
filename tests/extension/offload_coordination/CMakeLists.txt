# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE offload_coordination
    SOURCES
        test_offload_coordination_log_category.cpp
        test_held_nodes.cpp
        test_offload_dispatcher.cpp
        test_offload_handles.cpp
        test_rule_placement.cpp
        test_task_description.cpp
        test_task_resubmission.cpp
        test_worker_directory.cpp)
