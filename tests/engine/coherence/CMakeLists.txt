# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE coherence
    SOURCES
        compile_coherence_log_category.cpp
        compile_eviction_policy.cpp
        compile_instance_allocator.cpp
        compile_planned_node_factory.cpp
        compile_residency_options.cpp
        test_coherence_planner/test_planned_steps.cpp
        test_coherence_planner/test_randomized_equivalence.cpp
        test_copy_plan.cpp
        test_directory_planned_node_factory.cpp
        test_logical_time_eviction.cpp
        test_pin_set.cpp
        test_plan_hash.cpp
        test_plan_splice.cpp
        test_planned_host_access.cpp
        test_residency_options.cpp
        test_usm_instance_allocator.cpp)
