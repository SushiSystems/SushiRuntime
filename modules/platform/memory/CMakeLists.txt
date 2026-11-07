# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME memory TIER platform INTERNAL
    SYCL
    SOURCES host_usm_source.cpp sycl_usm_source.cpp usm_allocator.cpp
    DEPENDS core log
    PRIVATE_DEPENDS profile)
