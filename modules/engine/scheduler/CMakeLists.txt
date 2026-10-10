# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME scheduler TIER engine
    SYCL
    SOURCES task_scheduler.cpp
    DEPENDS core logging observability task
    PRIVATE_DEPENDS sycl topology memory device)

sushiruntime_add_module_tests(
    MODULE scheduler
    SOURCES
        test_event_polling.cpp
        test_node_placement.cpp
        test_offload_seam.cpp
        test_range_task.cpp
        test_thermal_rebalance.cpp
        test_work_stealing_deque.cpp
        test_workload_coordinator.cpp)
