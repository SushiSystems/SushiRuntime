# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME transfer TIER engine INTERNAL
    SYCL
    SOURCES
        file_path.cpp
        host_staged_path.cpp
        peer_path.cpp
        same_context_path.cpp
        staging_ring.cpp
        transfer_observer_slot.cpp
    DEPENDS
        handle
        log
        memory)
