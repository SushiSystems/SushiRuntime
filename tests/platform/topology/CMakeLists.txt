# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE topology
    SOURCES
        test_affinity.cpp
        test_host_identity.cpp
        test_hw_discovery.cpp
        test_hwloc_topology_provider.cpp
        test_matrix_distance_model.cpp
        test_numa_mapper.cpp
        test_remapping_topology_provider.cpp
        test_topology_log_category.cpp
        test_topology_provider.cpp
        test_topology_view.cpp)

