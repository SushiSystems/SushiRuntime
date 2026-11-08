# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE scheduler
    SOURCES
        compile_event_readiness.cpp
        compile_heartbeat_clock.cpp
        compile_rebalancer_interfaces.cpp
        compile_scheduler_log_category.cpp
        compile_worker_pool_settings.cpp
        test_dispatch_placement.cpp
        test_dispatch_record.cpp
        test_event_polling.cpp
        test_failure_naming.cpp
        test_in_memory_event_readiness.cpp
        test_in_memory_telemetry.cpp
        test_manual_heartbeat_clock.cpp
        test_offload_gate.cpp
        test_quiescence_latch.cpp
        test_range_task.cpp
        test_recording_rebalance_strategy.cpp
        test_recording_task_migrator.cpp
        test_run_outcome.cpp
        test_steal_order.cpp
        test_sycl_event_readiness.cpp
        test_task_scheduler.cpp
        test_thermal_proxy_strategy.cpp
        test_worker_pool_sizing.cpp
        test_workload_coordinator.cpp)
