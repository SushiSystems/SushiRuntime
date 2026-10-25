# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE device
    SOURCES
        test_device_descriptor.cpp
        test_device_log_category.cpp
        test_device_queue_set.cpp
        test_queue_flavour.cpp
    DEVICE_SOURCES
        test_device_queue_set.cpp)
