# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME profiling TIER surface
    SYCL
    SOURCES
        device_source.cpp
        device_track_name.cpp
        register_profiler_parts.cpp
        run_queue.cpp
        runtime_environment.cpp
        span_loss_tally.cpp
    DEPENDS
        core
        api
        report
        log
    PRIVATE_DEPENDS
        profile
        machine
        execution
        context_access
        device
    PUBLIC_EXTERNAL SushiProfiler::SushiProfiler)
