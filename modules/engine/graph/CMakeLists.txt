# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME graph TIER engine
    SYCL
    SOURCES compiled_graph.cpp dependency_tracker.cpp task_graph.cpp
    DEPENDS core memory observability task
    PRIVATE_DEPENDS logging)

sushiruntime_add_module_tests(
    MODULE graph
    SOURCES test_dependency_tracker.cpp)
