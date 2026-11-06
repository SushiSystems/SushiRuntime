# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME task TIER engine INTERNAL
    SYCL
    SOURCES dispatch_events.cpp node_placement.cpp node_widths.cpp task_submission.cpp
    DEPENDS core report region
    PRIVATE_DEPENDS memory)
