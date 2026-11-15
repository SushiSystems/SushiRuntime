# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE partitioning
    SOURCES
        compile_partition_measurement.cpp
        compile_partition_problem.cpp
        compile_partitioner.cpp
        test_assignment_hash.cpp
        test_fill_plan.cpp
        test_floor_partitioner.cpp
        test_fnv_hash.cpp
        test_list_scheduling_partitioner/test_partition_properties.cpp
        test_list_scheduling_partitioner/test_placement_and_refusal.cpp
        test_list_scheduling_partitioner/test_splitting.cpp
        test_partition_assignment.cpp
        test_partition_model.cpp
        test_partition_refusal.cpp
        test_partition_report_builder.cpp
        test_planned_homes.cpp
        test_pool_occupancy.cpp
        test_problem_hash.cpp
        test_split_expansion.cpp
        test_split_sizing.cpp
    DEVICE_SOURCES
        test_list_scheduling_partitioner/test_splitting.cpp)
