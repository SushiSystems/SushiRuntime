# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME topology TIER platform
    SYCL
    SOURCES
        affinity.cpp
        hw_discovery.cpp
        hwloc_topology_provider.cpp
        numa_lookup.cpp
        numa_mapper.cpp
        remapping_topology_provider.cpp
        topology_view.cpp
    DEPENDS core logging
    EXTERNAL PkgConfig::HWLOC)

sushiruntime_add_module_tests(
    MODULE topology
    SOURCES
        test_affinity.cpp
        test_hw_discovery.cpp
        test_numa_lookup.cpp
        test_topology_view.cpp)

# test_numa_lookup.cpp builds its own hwloc topologies to check the lookup against.
if(TARGET sr_topology_tests)
    target_link_libraries(sr_topology_tests PRIVATE PkgConfig::HWLOC)
endif()
