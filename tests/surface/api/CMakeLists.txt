# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE api
    SOURCES
        compile_api_log_category.cpp
        data/test_buffer.cpp
        data/test_request_checks.cpp
        data/test_state.cpp
        detail/compile_late_controls.cpp
        detail/compile_node_description.cpp
        detail/compile_node_spec.cpp
        detail/compile_plan.cpp
        detail/compile_step_machinery.cpp
        detail/test_assertion_logging.cpp
        detail/test_before_step_hooks.cpp
        detail/test_callable_checks.cpp
        detail/test_callable_traits.cpp
        detail/test_device_fault.cpp
        detail/test_device_pass.cpp
        detail/test_dropped_outcome.cpp
        detail/test_emit_captured.cpp
        detail/test_emit_device_sized.cpp
        detail/test_emit_dynamic.cpp
        detail/test_emit_host.cpp
        detail/test_emit_late_count_resolver.cpp
        detail/test_emit_launch.cpp
        detail/test_emit_state.cpp
        detail/test_emit_task.cpp
        detail/test_emit_tracked.cpp
        detail/test_emit_untracked.cpp
        detail/test_graph_id.cpp
        detail/test_handle_access.cpp
        detail/test_late_count_resolution.cpp
        detail/test_late_window.cpp
        detail/test_node_stamp.cpp
        detail/test_step_run.cpp
        graph/plumbing/test_builder_surface.cpp
        graph/plumbing/test_plan_cache.cpp
        graph/plumbing/test_region_composer.cpp
        graph/plumbing/test_step_driver.cpp
        graph/test_access.cpp
        graph/test_device_sized.cpp
        graph/test_dispatch_events.cpp
        graph/test_dynamic_graph/test_dynamic_graph_regions.cpp
        graph/test_dynamic_graph/test_dynamic_graph_node_events.cpp
        graph/test_dynamic_graph/test_dynamic_graph_placement.cpp
        graph/test_dynamic_graph/test_dynamic_graph_world.cpp
        graph/test_graph.cpp
        graph/test_dynamic_graph/test_layer_equivalence.cpp
        graph/test_node_options.cpp
        graph/test_plan_view.cpp
        graph/test_recorder/test_recorder_parameter_limit.cpp
        graph/test_recorder/test_recorder_placement.cpp
        graph/test_recorder/test_recorder_scratch.cpp
        graph/test_recorder/test_recorder_semantics.cpp
        graph/test_region.cpp
        graph/test_recorder/test_registration_index.cpp
        graph/test_recorder/test_rule_registration.cpp
        graph/test_run_handle.cpp
        interop/test_host_export.cpp
        interop/test_interop.cpp
        launch/test_extent.cpp
        launch/test_nd_range.cpp
        runtime/test_advanced_controls.cpp
        runtime/test_errors.cpp
        runtime/test_host_runtime_config.cpp
        runtime/test_placement_scope.cpp
        runtime/test_runtime.cpp
        runtime/test_runtime_config.cpp
        test_fluent.cpp
        vocabulary/test_captures.cpp
        vocabulary/test_coupled.cpp
        vocabulary/test_dependencies.cpp
        vocabulary/test_dynamic.cpp
        vocabulary/test_late_count.cpp
        vocabulary/test_placement.cpp
    BENCHMARK_SOURCES
        graph/benchmark_dynamic_graph.cpp
        graph/benchmark_graph.cpp
    DEVICE_SOURCES
        data/test_buffer.cpp
        data/test_state.cpp
        detail/test_emit_captured.cpp
        detail/test_emit_device_sized.cpp
        detail/test_emit_dynamic.cpp
        detail/test_emit_late_count_resolver.cpp
        detail/test_emit_launch.cpp
        detail/test_emit_state.cpp
        detail/test_emit_task.cpp
        detail/test_emit_tracked.cpp
        detail/test_emit_untracked.cpp
        detail/test_node_stamp.cpp
        graph/benchmark_dynamic_graph.cpp
        graph/benchmark_graph.cpp
        graph/plumbing/test_builder_surface.cpp
        graph/plumbing/test_plan_cache.cpp
        graph/plumbing/test_step_driver.cpp
        graph/test_device_sized.cpp
        graph/test_dispatch_events.cpp
        graph/test_dynamic_graph/test_dynamic_graph_regions.cpp
        graph/test_dynamic_graph/test_dynamic_graph_node_events.cpp
        graph/test_dynamic_graph/test_dynamic_graph_world.cpp
        graph/test_graph.cpp
        graph/test_dynamic_graph/test_layer_equivalence.cpp
        graph/test_node_options.cpp
        graph/test_plan_view.cpp
        graph/test_recorder/test_recorder_parameter_limit.cpp
        graph/test_recorder/test_recorder_placement.cpp
        graph/test_recorder/test_recorder_scratch.cpp
        graph/test_recorder/test_recorder_semantics.cpp
        graph/test_region.cpp
        graph/test_recorder/test_registration_index.cpp
        graph/test_recorder/test_rule_registration.cpp
        graph/test_run_handle.cpp
        interop/test_host_export.cpp
        interop/test_interop.cpp
        runtime/test_advanced_controls.cpp
        runtime/test_errors.cpp
        runtime/test_host_runtime_config.cpp
        runtime/test_runtime.cpp
        runtime/test_runtime_config.cpp
        test_fluent.cpp
        vocabulary/test_dynamic.cpp
        vocabulary/test_late_count.cpp)

# Its own program: the test replaces the global operator new to count allocations, and a
# replacement is program-wide. Linked into another program it would count that program's
# allocations too and displace a sanitizer's own operator new.
sushiruntime_add_test_program(
    NAME sr_api_allocation_tests MODULE api
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_sushiruntime.cpp"
    DEVICE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_sushiruntime.cpp")
