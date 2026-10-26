# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME execution TIER engine INTERNAL
    SYCL
    SOURCES
        chain_plan.cpp
        chain_run.cpp
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
    PRIVATE_DEPENDS log sycl placement profile)
