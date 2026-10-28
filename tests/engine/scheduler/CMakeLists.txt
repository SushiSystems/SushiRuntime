# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE scheduler
    SOURCES
        test_dispatch_record.cpp
        test_event_polling.cpp
        test_failure_naming.cpp
        test_offload_gate.cpp
        test_quiescence_latch.cpp
        test_range_task.cpp
        test_rebalancer_interfaces.cpp
        test_run_outcome.cpp
        test_scheduler_log_category.cpp
        test_task_scheduler.cpp
        test_thermal_proxy_strategy.cpp
        test_worker_pool_settings.cpp
        test_workload_coordinator.cpp
    DEVICE_SOURCES
        test_range_task.cpp)
