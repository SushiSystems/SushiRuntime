# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME coherence TIER engine INTERNAL
    SYCL
    SOURCES assignment_hash.cpp coherence_planner.cpp cost_model.cpp device_profile_file.cpp
            device_profile_probe.cpp directory_planned_node_factory.cpp dispatch_observation.cpp file_path.cpp
            fill_plan.cpp graph_partition_problem.cpp host_staged_path.cpp list_scheduling_partitioner.cpp
            logical_time_eviction.cpp partition_apply.cpp partition_ledger.cpp partition_model.cpp
            partition_report_builder.cpp partition_session.cpp peer_path.cpp plan_hash.cpp plan_splice.cpp problem_hash.cpp
            planned_host_access.cpp pool_occupancy.cpp profile_fold.cpp profile_measurement.cpp
            profile_transfer_cost.cpp profile_update.cpp same_context_path.cpp split_expansion.cpp
            split_sizing.cpp staging_ring.cpp
            transfer_observer_slot.cpp transfer_path_tally.cpp usm_instance_allocator.cpp
    DEPENDS core handle log machine region report memory task)
