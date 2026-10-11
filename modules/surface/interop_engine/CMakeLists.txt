# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME interop_engine TIER surface
    SYCL
    SOURCES interop_engine.cpp
    DEPENDS core memory interop
    PRIVATE_DEPENDS logging device execution)

sushiruntime_add_module_tests(
    MODULE interop_engine
    SOURCES test_interop_import.cpp)
