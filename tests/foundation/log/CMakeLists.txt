# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE log
    SOURCES
        test_log_gate.cpp
        test_logger.cpp
        test_sushiruntime_log_project.cpp)
