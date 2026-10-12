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
        test_async_step.cpp
        test_boundary_access.cpp
        test_builder_surface.cpp
        test_callable_traits.cpp
        test_data_handles.cpp
        test_device_fault.cpp
        test_device_count.cpp
        test_device_resident.cpp
        test_device_sized_launch.cpp
        test_device_slot_liveness.cpp
        test_dynamic_graph.cpp
        test_dynamic_graph_node_events.cpp
        test_fluent_api.cpp
        test_fluent_hardening.cpp
        test_fluent_lifecycle.cpp
        test_late_bindings.cpp
        test_late_bound_vocabulary.cpp
        test_launch_reach.cpp
        test_late_count.cpp
        test_logging_names.cpp
        test_native_cpu_path.cpp
        test_node_event_records.cpp
        test_node_options_width.cpp
        test_plan_view.cpp
        test_recorder_scratch.cpp
        test_recorder_semantics.cpp
        test_region_commit.cpp
        test_region_emitter.cpp
        test_registration_index.cpp
        test_rule_registration.cpp
        test_run_driver.cpp
        test_run_report_reuse.cpp
        test_runtime_entry.cpp
        test_state_emitters.cpp
        test_state_move.cpp)
