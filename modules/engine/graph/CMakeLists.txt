# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME graph TIER engine INTERNAL
    SYCL
    SOURCES compiled_graph.cpp task_graph.cpp
    DEPENDS core log observability task region report
    PRIVATE_DEPENDS dependency_tracking profile
    EXTERNAL SushiLog::text)
