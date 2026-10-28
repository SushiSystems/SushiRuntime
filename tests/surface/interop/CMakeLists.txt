# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE interop
    SOURCES
        test_device_uuid.cpp
        test_external_handle_kind_set.cpp
        test_external_memory.cpp
        test_interop_capabilities.cpp)
