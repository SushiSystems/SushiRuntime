# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME task TIER engine
    SYCL
    SOURCES node_placement.cpp node_widths.cpp
    DEPENDS core memory observability report region)

sushiruntime_add_module_tests(
    MODULE task
    SOURCES
        test_node_placement.cpp
        test_small_event_vector.cpp
        test_task_types.cpp)
