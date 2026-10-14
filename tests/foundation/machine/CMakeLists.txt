# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE machine
    SOURCES
        test_device_description.cpp
        test_device_filter.cpp
        test_device_kind.cpp
        test_pin_policy.cpp
        test_worker_pool.cpp)
