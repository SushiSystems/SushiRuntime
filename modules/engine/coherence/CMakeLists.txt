# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME coherence TIER engine INTERNAL
    SYCL
<<<<<<< ours
    SOURCES coherence_planner.cpp cost_model.cpp device_profile_file.cpp
            directory_planned_node_factory.cpp file_path.cpp host_staged_path.cpp logical_time_eviction.cpp peer_path.cpp
            plan_hash.cpp plan_splice.cpp planned_host_access.cpp profile_measurement.cpp profile_transfer_cost.cpp
=======
    SOURCES assignment_hash.cpp coherence_planner.cpp cost_model.cpp device_profile_file.cpp
            directory_planned_node_factory.cpp fill_plan.cpp host_staged_path.cpp list_scheduling_partitioner.cpp
            logical_time_eviction.cpp partition_model.cpp peer_path.cpp plan_hash.cpp plan_splice.cpp
            planned_host_access.cpp pool_occupancy.cpp profile_measurement.cpp profile_transfer_cost.cpp
>>>>>>> theirs
            same_context_path.cpp staging_ring.cpp usm_instance_allocator.cpp
    DEPENDS core handle log region memory task)
