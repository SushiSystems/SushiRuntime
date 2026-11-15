# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE handle
    SOURCES
        compile_device_contexts.cpp
        compile_handle_home_reader.cpp
        test_bound_operands.cpp
        test_device_key.cpp
        test_element_layout.cpp
        test_global_handle.cpp
        test_handle_resolver.cpp
        test_handle_space.cpp
        test_handle_status.cpp
        test_local_handle_space.cpp
        test_memory_space_id.cpp
        test_transfer_path_id.cpp)
