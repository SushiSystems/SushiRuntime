# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME coherence TIER engine INTERNAL
    SYCL
    SOURCES
        coherence_planner.cpp
        directory_planned_node_factory.cpp
        logical_time_eviction.cpp
        plan_hash.cpp
        plan_splice.cpp
        planned_host_access.cpp
        usm_instance_allocator.cpp
    DEPENDS
        core
        handle
        log
        memory
        region
        task
        transfer
    PRIVATE_DEPENDS
        partitioning)
