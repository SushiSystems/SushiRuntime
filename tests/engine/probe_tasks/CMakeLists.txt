# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE probe_tasks
    SOURCES
        test_atomic_counter_task.cpp
        test_order_recorder.cpp
        test_priority_tracking_task.cpp)
