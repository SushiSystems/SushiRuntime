# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME scheduler TIER engine INTERNAL
    SYCL
    SOURCES
        dispatch_placement.cpp
        failure_naming.cpp
        offload_return.cpp
        task_scheduler.cpp
    DEPENDS core log sycl observability task machine
    PRIVATE_DEPENDS topology memory device region work_stealing profile report
    PUBLIC_EXTERNAL SushiLog::text)
