# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME master_link TIER extension INTERNAL
    SOURCES
        link_tick.cpp
        master_link.cpp
    DEPENDS core log machine task transport wire link_security
    PRIVATE_DEPENDS frame_transport)
