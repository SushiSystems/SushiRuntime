# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME runtime_assembly TIER engine INTERNAL
    HEADER_ONLY SYCL
    DEPENDS execution scheduler topology machine)
