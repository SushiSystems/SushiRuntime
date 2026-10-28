# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE numa_lookup
    SOURCES
        test_numa_lookup.cpp)

# test_numa_lookup.cpp builds its own hwloc topologies to check the lookup against.
if(TARGET sr_numa_lookup_test_objects)
    target_link_libraries(sr_numa_lookup_test_objects PRIVATE PkgConfig::HWLOC)
endif()
