# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE work_stealing
    SOURCES
        test_mpsc_ring.cpp
        test_spsc_ring.cpp
        test_work_stealing_deque.cpp)
