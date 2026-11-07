# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE master_link
    SOURCES
        compile_master_link_log_category.cpp
        test_link_tick.cpp
        test_master_link.cpp)
