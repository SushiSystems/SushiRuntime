# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE interop
    SOURCES
        compile_device_uuid.cpp
        compile_external_memory.cpp
        test_external_handle_kind_set.cpp
        test_interop_capabilities.cpp)
