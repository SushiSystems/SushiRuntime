# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME execution TIER engine
    SYCL
    SOURCES
        chain_plan.cpp
        chain_run.cpp
        scheduler_execution_engine.cpp
        runtime_context.cpp
    DEPENDS
        core
        topology
        memory
        device
        observability
        scheduler
        graph
        report
        machine
    PRIVATE_DEPENDS logging sycl task)

sushiruntime_add_module_tests(
    MODULE execution
    SOURCES
        test_async_execution.cpp
        test_device_pipeline.cpp
        test_heterogeneous_pipeline.cpp
        test_profiling_queue.cpp
        test_replay_host_write.cpp
        test_run_in_progress.cpp
        test_sycl_execution.cpp)
