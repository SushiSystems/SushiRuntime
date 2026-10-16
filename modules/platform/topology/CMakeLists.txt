# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME topology TIER platform INTERNAL
    SYCL
    SOURCES
        affinity.cpp
        hw_discovery.cpp
        hwloc_topology_provider.cpp
        numa_mapper.cpp
        remapping_topology_provider.cpp
        topology_view.cpp
    DEPENDS core log machine
    PRIVATE_DEPENDS logging numa_lookup
    EXTERNAL PkgConfig::HWLOC)
