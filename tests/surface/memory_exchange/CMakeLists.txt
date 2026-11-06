# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE memory_exchange
    SOURCES
        test_capability_cache.cpp
        test_memory_exchange.cpp
        test_memory_exchange_log_category.cpp
        test_scripted_capability_probe.cpp)
