# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE memory
    SOURCES
        compile_memory_log_category.cpp
        compile_memory_probe.cpp
        compile_memory_space.cpp
        compile_residency_view.cpp
        compile_usm_request.cpp
        compile_usm_source.cpp
        test_borrowed_index.cpp
        test_coherence_intervals.cpp
        test_device_memory_space.cpp
        test_epoch_reclamation.cpp
        test_host_memory_space.cpp
        test_host_usm_source.cpp
        test_memory_pool_capacity.cpp
        test_residency_directory.cpp
        test_retire_observer.cpp
        test_sycl_queues.cpp
        test_sycl_usm_source.cpp
        test_system_memory_probe.cpp
        test_usm_allocation_owner.cpp
        test_usm_allocator.cpp)
