# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME memory TIER platform INTERNAL
    SYCL
    SOURCES borrowed_index.cpp coherence_intervals.cpp device_memory_space.cpp file_memory_space.cpp host_memory_space.cpp
            host_usm_source.cpp memory_pool_capacity.cpp residency_directory.cpp sycl_usm_source.cpp system_memory_probe.cpp
            usm_allocator.cpp
    DEPENDS core handle log profile)
