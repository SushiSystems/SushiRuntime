# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE cluster_load
    SOURCES
        compile_cluster_statistics.cpp
        compile_placement_decision.cpp
        test_offload_policy.cpp)
