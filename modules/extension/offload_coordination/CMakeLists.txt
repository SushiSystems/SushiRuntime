# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME offload_coordination TIER extension INTERNAL
    SOURCES
        held_nodes.cpp
        offload_dispatcher.cpp
        rule_placement.cpp
        worker_directory.cpp
    DEPENDS core log machine placement api task transport wire link_security cluster_load buffer_exchange pending_tasks
    PRIVATE_DEPENDS region)
