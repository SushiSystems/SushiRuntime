# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME api TIER surface
    SYCL
    SOURCES
        advanced_controls.cpp
        assertion_logging.cpp
        bound_pointers.cpp
        buffer.cpp
        dispatch_events.cpp
        emit_dynamic.cpp
        graph_id.cpp
        graph_partition.cpp
        host_write_watch.cpp
        interop.cpp
        late_window.cpp
        node_emitters.cpp
        partition_measure.cpp
        placement_scope.cpp
        planned_buffer.cpp
        plan_cache.cpp
        recorder.cpp
        region_coherence.cpp
        region_composer.cpp
        runtime.cpp
        step_machinery.cpp
        step_run.cpp
    DEPENDS
        core
        handle
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
        coherence
        topology
        profile)
