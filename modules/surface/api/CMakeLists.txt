# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME api TIER surface
    SYCL
    SOURCES
        advanced_controls.cpp
        assertion_logging.cpp
        buffer.cpp
        dispatch_events.cpp
        emit_dynamic.cpp
        graph_id.cpp
        interop.cpp
        late_window.cpp
        node_emitters.cpp
        placement_scope.cpp
        plan_cache.cpp
        recorder.cpp
        region_composer.cpp
        runtime.cpp
        step_machinery.cpp
        step_run.cpp
    DEPENDS
        core
        report
        log
        region
        machine
        placement
        interop
        context
    PRIVATE_DEPENDS
        sycl
        memory
        device
        observability
        task
        graph
        execution
        memory_exchange
        context_access
        engine_handles
        region_boundary
        topology
        profile)
