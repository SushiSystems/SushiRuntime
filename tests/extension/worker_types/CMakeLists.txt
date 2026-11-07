# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE worker_types
    SOURCES
        distributed/test_buffer_view.cpp
        distributed/test_kernel_registry.cpp
        distributed/test_worker_statistics.cpp)
