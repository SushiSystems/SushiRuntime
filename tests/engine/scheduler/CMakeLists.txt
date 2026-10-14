# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE scheduler
    SOURCES
        test_event_polling.cpp
        test_range_task.cpp
        test_task_scheduler.cpp
        test_thermal_proxy_strategy.cpp
        test_workload_coordinator.cpp)
