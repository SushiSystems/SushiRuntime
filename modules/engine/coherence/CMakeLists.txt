# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME coherence TIER engine INTERNAL
    SYCL
<<<<<<< ours
    SOURCES coherence_planner.cpp directory_planned_node_factory.cpp peer_path.cpp plan_splice.cpp
            same_context_path.cpp usm_instance_allocator.cpp
=======
    SOURCES coherence_planner.cpp cost_model.cpp device_profile_file.cpp
            directory_planned_node_factory.cpp plan_splice.cpp profile_measurement.cpp
            profile_transfer_cost.cpp usm_instance_allocator.cpp
>>>>>>> theirs
    DEPENDS core handle region memory task)
