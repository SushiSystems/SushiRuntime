# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE memory
    SOURCES
        test_allocation.cpp
        test_epoch_reclamation.cpp
        test_memory_log_category.cpp
        test_usm_allocation_owner.cpp
        test_usm_allocator.cpp
        test_usm_registry.cpp
        test_usm_release_observer.cpp
        test_usm_request.cpp
    BENCHMARK_SOURCES
        benchmark_epoch_reclamation.cpp)
