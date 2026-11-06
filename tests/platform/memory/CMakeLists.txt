# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE memory
    SOURCES
        compile_memory_log_category.cpp
        compile_usm_request.cpp
        test_epoch_reclamation.cpp
        test_recording_usm_release_observer.cpp
        test_usm_allocation_owner.cpp
        test_usm_allocator.cpp
        test_usm_registry.cpp
        test_usm_release_observer.cpp)
