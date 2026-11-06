# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME pending_tasks TIER extension INTERNAL
    SOURCES
        pending_tasks.cpp
    DEPENDS core transport
    PRIVATE_DEPENDS task)
