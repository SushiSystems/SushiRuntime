# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME context_access TIER surface INTERNAL
    SYCL
    SOURCES context_access.cpp
    DEPENDS core execution context
    PRIVATE_DEPENDS memory_exchange)
