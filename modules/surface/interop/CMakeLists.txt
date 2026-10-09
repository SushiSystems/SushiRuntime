# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME interop TIER surface
    SYCL
    SOURCES interop.cpp
    DEPENDS core memory
    PRIVATE_DEPENDS device execution)

sushiruntime_add_module_tests(
    MODULE interop
    SOURCES test_interop_vocabulary.cpp)
