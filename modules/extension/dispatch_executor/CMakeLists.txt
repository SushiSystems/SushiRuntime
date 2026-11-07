# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME dispatch_executor TIER extension INTERNAL
    SYCL
    SOURCES
        dispatch_executor.cpp
        dispatch_job.cpp
        dispatch_plan.cpp
        dispatch_recorder.cpp
        dispatch_settler.cpp
        held_tasks.cpp
        worker_buffer_cache.cpp
    DEPENDS core log api device region transport wire worker_types
    PRIVATE_DEPENDS frame_transport
    EXTERNAL SushiLog::text)
