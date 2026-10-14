# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME graph TIER engine INTERNAL
    SYCL
    SOURCES compiled_graph.cpp task_graph.cpp
    DEPENDS core observability task region
    PRIVATE_DEPENDS logging dependency_tracking)
