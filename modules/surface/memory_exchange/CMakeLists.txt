# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME memory_exchange TIER surface INTERNAL
    SYCL
    SOURCES memory_exchange.cpp
    DEPENDS core memory interop
    PRIVATE_DEPENDS logging device execution)

sushiruntime_add_module_tests(
    MODULE memory_exchange
    SOURCES test_interop_import.cpp)
