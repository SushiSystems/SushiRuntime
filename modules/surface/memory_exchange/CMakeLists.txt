# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME memory_exchange TIER surface INTERNAL
    SYCL
    SOURCES memory_exchange.cpp
    DEPENDS core log memory interop
    PRIVATE_DEPENDS sycl device execution)
