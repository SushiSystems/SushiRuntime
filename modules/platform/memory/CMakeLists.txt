# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME memory TIER platform
    SYCL
    SOURCES usm_allocator.cpp
    DEPENDS core
    PRIVATE_DEPENDS logging)

sushiruntime_add_module_tests(
    MODULE memory
    SOURCES
        test_epoch_reclamation.cpp
        test_usm_allocator.cpp
        test_usm_registry.cpp)
