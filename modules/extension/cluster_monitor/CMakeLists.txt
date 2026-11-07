# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME cluster_monitor TIER extension INTERNAL
    SOURCES
        cluster_monitor.cpp
        cluster_monitor_step.cpp
    DEPENDS core log transport buffer_exchange pending_tasks offload_coordination
    PRIVATE_DEPENDS task wire frame_transport)
