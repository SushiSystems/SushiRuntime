# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE process_environment
    SOURCES
        test_environment.cpp
        test_process_environment_log_category.cpp
        test_stop_signal.cpp)
