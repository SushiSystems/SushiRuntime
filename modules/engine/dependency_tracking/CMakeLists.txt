# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME dependency_tracking TIER engine INTERNAL
    SOURCES
        dependency_tracker.cpp
    DEPENDS core task region
    PRIVATE_DEPENDS memory)
