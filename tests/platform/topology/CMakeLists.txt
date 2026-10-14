# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE topology
    SOURCES
        test_affinity.cpp
        test_hw_discovery.cpp
        test_numa_mapper.cpp
        test_topology_view.cpp)

# test_numa_mapper.cpp builds its own hwloc topologies to check the lookup against.
if(TARGET sr_topology_tests)
    target_link_libraries(sr_topology_tests PRIVATE PkgConfig::HWLOC)
endif()
