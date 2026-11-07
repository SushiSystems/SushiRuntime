# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE device
    SOURCES
        compile_device_descriptor.cpp
        compile_device_log_category.cpp
        test_device_queue_set.cpp
        test_numa_route_table.cpp
        test_queue_flavour.cpp
    DEVICE_SOURCES
        test_device_queue_set.cpp)
