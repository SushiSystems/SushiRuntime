# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE log
    SOURCES
        test_log_c_sink.cpp
        test_log_category.cpp
        test_log_format.cpp
        test_log_level.cpp
        test_log_project.cpp
        test_log_record.cpp
        test_log_sink.cpp
        test_sushiruntime_log_project.cpp)
