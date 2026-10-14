# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE execution
    SOURCES
        test_execution_engine.cpp
        test_runtime_context.cpp
        test_scheduler_execution_engine.cpp
    BENCHMARK_SOURCES
        benchmark_runtime_context.cpp
        benchmark_scheduler_execution_engine.cpp)
