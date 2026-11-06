# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE graph
    SOURCES
        test_chain_plan.cpp
        test_compiled_graph.cpp
        test_graph_log_category.cpp
        test_task_graph.cpp)
