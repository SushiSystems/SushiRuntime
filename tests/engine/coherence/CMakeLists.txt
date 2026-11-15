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
        compile_peer_access.cpp
        compile_pinned_host_memory.cpp
        compile_planned_node_factory.cpp
        compile_profile_probe.cpp
        compile_residency_options.cpp
        compile_space_queues.cpp
        compile_transfer_observer.cpp
        compile_transfer_path.cpp
        test_coherence_planner/test_planned_steps.cpp
        test_coherence_planner/test_randomized_equivalence.cpp
        test_copy_plan.cpp
        test_cost_model.cpp
        test_device_profile.cpp
        test_device_profile_file.cpp
        test_device_profile_set.cpp
        test_directory_planned_node_factory.cpp
        test_host_staged_path.cpp
        test_logical_time_eviction.cpp
        test_peer_path.cpp
        test_pin_set.cpp
        test_plan_hash.cpp
        test_plan_splice.cpp
        test_planned_host_access.cpp
        test_profile_measurement.cpp
        test_profile_transfer_cost.cpp
        test_residency_options.cpp
        test_same_context_path.cpp
        test_staging_ring.cpp
        test_transfer_cost.cpp
        test_usm_instance_allocator.cpp)
