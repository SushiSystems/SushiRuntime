# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME memory TIER platform INTERNAL
    SYCL
    SOURCES borrowed_index.cpp coherence_horizon.cpp coherence_intervals.cpp device_memory_space.cpp file_memory_space.cpp
            guarded_device_pages.cpp guarded_usm_source.cpp host_memory_space.cpp host_usm_source.cpp host_write_table.cpp
            memory_pool_capacity.cpp page_protection_posix.cpp page_protection_windows.cpp residency_directory.cpp
            residency_reader.cpp sycl_usm_source.cpp system_memory_probe.cpp usm_allocator.cpp
    DEPENDS core handle log profile)
