# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE cluster_monitor
    SOURCES
        compile_cluster_monitor_log_category.cpp
        test_cluster_monitor.cpp
        test_cluster_monitor_step.cpp
        test_unsent_frame_log.cpp)
