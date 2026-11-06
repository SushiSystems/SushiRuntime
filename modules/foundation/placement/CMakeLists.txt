# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME placement TIER foundation
    SOURCES
        placement_rules.cpp
        placement_target_matching.cpp
    DEPENDS
        core
        machine)
