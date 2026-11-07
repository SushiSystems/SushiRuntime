# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME topology TIER platform INTERNAL
    SYCL
    SOURCES
        affinity.cpp
        host_topology.cpp
        host_identity_posix.cpp
        host_identity_windows.cpp
        hw_discovery.cpp
        hwloc_numa_reader.cpp
        hwloc_topology_provider.cpp
        numa_mapper.cpp
        remapping_topology_provider.cpp
        sycl_device_enumerator.cpp
        topology_view.cpp
    DEPENDS core log machine
    PRIVATE_DEPENDS numa_lookup
    EXTERNAL PkgConfig::HWLOC)
