# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME device TIER platform INTERNAL
    SYCL
    SOURCES device_memory_probe.cpp device_queue_set.cpp integrated_device.cpp numa_route_table.cpp sycl_queue_factory.cpp
    DEPENDS core log machine memory profile
    PRIVATE_DEPENDS sycl)
