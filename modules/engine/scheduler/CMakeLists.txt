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
        dispatch_queue_selection.cpp
        dispatch_record.cpp
        event_polling.cpp
        failure_naming.cpp
        host_node_run.cpp
        offload_return.cpp
        task_scheduler.cpp
    DEPENDS core log sycl task machine report
    PRIVATE_DEPENDS observability topology memory device region work_stealing profile
    PUBLIC_EXTERNAL SushiLog::text)
