# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

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
        steal_order.cpp
        task_scheduler.cpp
        task_scheduler_executor.cpp
        task_scheduler_rounds.cpp
        task_scheduler_stealing.cpp
        task_scheduler_threads.cpp
        worker_pool_sizing.cpp
    DEPENDS core log sycl task machine report
    PRIVATE_DEPENDS observability topology memory device region work_stealing profile
    PUBLIC_EXTERNAL SushiLog::text)
