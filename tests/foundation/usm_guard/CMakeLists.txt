# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE usm_guard
    SOURCES
        compile_shared_allocation_source.cpp
        test_in_memory_shared_allocation_source.cpp
        test_queue_shared_allocation_source.cpp
        test_usm_shared.cpp)
