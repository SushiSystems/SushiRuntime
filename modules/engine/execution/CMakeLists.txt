# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME execution TIER engine INTERNAL
    SYCL
    SOURCES
        chain_plan.cpp
        chain_run.cpp
        host_execution_engine.cpp
        scheduler_execution_engine.cpp
        runtime_context.cpp
    DEPENDS
        core
        log
        topology
        memory
        device
        observability
        task
        scheduler
        graph
        report
        machine
        profile
    PRIVATE_DEPENDS log sycl placement)
