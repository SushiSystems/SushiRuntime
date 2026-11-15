# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE coherence
    SOURCES
        compile_instance_allocator.cpp
        compile_peer_access.cpp
        compile_planned_node_factory.cpp
<<<<<<< ours
        compile_space_queues.cpp
        compile_transfer_observer.cpp
=======
        compile_profile_probe.cpp
>>>>>>> theirs
        compile_transfer_path.cpp
        test_coherence_planner.cpp
        test_copy_plan.cpp
        test_cost_model.cpp
        test_device_profile.cpp
        test_device_profile_file.cpp
        test_device_profile_set.cpp
        test_directory_planned_node_factory.cpp
        test_peer_path.cpp
        test_plan_splice.cpp
<<<<<<< ours
        test_same_context_path.cpp
=======
        test_profile_measurement.cpp
        test_profile_transfer_cost.cpp
>>>>>>> theirs
        test_transfer_cost.cpp
        test_usm_instance_allocator.cpp)
