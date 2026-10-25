# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME memory_exchange TIER surface INTERNAL
    SYCL
    SOURCES capability_cache.cpp memory_exchange.cpp
    DEPENDS core log interop
    PRIVATE_DEPENDS memory sycl device execution)
