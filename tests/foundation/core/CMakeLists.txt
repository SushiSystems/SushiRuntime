# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE core
    SOURCES
        test_allocation.cpp
        test_assert.cpp
        test_bit.cpp
        test_clock.cpp
        test_common.cpp
        test_delegate.cpp
        test_determinism.cpp
        test_error.cpp
        test_error_translation.cpp
        test_executor.cpp
        test_export.cpp
        test_node_cost.cpp
        test_operation_id.cpp
        test_placement_class.cpp
        test_recording_allocation_owner.cpp
        test_ref_counted.cpp
        test_seeded_executor.cpp
        test_result.cpp
        test_span.cpp
        test_strong_id.cpp
        test_thread_pool_executor.cpp
        test_sushi_ptr.cpp
        test_version.cpp)
