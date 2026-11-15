# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME partitioning TIER engine INTERNAL
    SOURCES
        assignment_hash.cpp
        fill_plan.cpp
        floor_partitioner.cpp
        list_schedule.cpp
        list_scheduling_partitioner.cpp
        partition_model.cpp
        partition_report_builder.cpp
        planned_homes.cpp
        pool_occupancy.cpp
        problem_hash.cpp
        range_max_tree.cpp
        split_expansion.cpp
        split_sizing.cpp
        step_simulation.cpp
    DEPENDS
        core
        handle
        machine
        report
        device_profile
    PRIVATE_DEPENDS
        region)
