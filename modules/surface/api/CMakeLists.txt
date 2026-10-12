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
        buffer.cpp
        emit_dynamic.cpp
        interop.cpp
        late_window.cpp
        node_emitters.cpp
        plan_cache.cpp
        recorder.cpp
        region_composer.cpp
        runtime.cpp
        step_machinery.cpp
        step_run.cpp
    DEPENDS
        core
        report
        region
        log
        machine
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
        context_access)

sushiruntime_add_module_tests(
    MODULE api
    SOURCES
        data/test_data_handles.cpp
        data/test_device_resident.cpp
        data/test_state_move.cpp
        detail/test_callable_traits.cpp
        detail/test_device_fault.cpp
        detail/test_launch_reach.cpp
        detail/test_state_emitters.cpp
        graph/recording/test_builder_surface.cpp
        graph/recording/test_device_sized_launch.cpp
        graph/recording/test_late_count.cpp
        graph/recording/test_native_cpu_path.cpp
        graph/recording/test_node_options_width.cpp
        graph/recording/test_plan_view.cpp
        graph/recording/test_recorder_scratch.cpp
        graph/recording/test_recorder_semantics.cpp
        graph/recording/test_registration_index.cpp
        graph/recording/test_rule_registration.cpp
        graph/regions/test_boundary_access.cpp
        graph/regions/test_dynamic_graph.cpp
        graph/regions/test_dynamic_graph_node_events.cpp
        graph/regions/test_region_commit.cpp
        graph/regions/test_region_emitter.cpp
        graph/stepping/test_async_step.cpp
        graph/stepping/test_node_event_records.cpp
        graph/stepping/test_run_driver.cpp
        graph/stepping/test_run_report_reuse.cpp
        runtime/test_logging_names.cpp
        runtime/test_runtime_entry.cpp
        test_fluent_api.cpp
        test_fluent_hardening.cpp
        test_fluent_lifecycle.cpp
        vocabulary/test_device_count.cpp
        vocabulary/test_device_slot_liveness.cpp
        vocabulary/test_late_bindings.cpp
        vocabulary/test_late_bound_vocabulary.cpp)
