# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME memory_exchange TIER surface INTERNAL
    SYCL
    SOURCES capability_cache.cpp memory_exchange.cpp
    DEPENDS core log interop
    PRIVATE_DEPENDS memory sycl device execution)
