# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE observability
    SOURCES
        test_event_timestamps.cpp
        test_pairing_claim.cpp
        test_profiling_ledger.cpp
        test_transfer_counters.cpp
    DEVICE_SOURCES
        test_event_timestamps.cpp)
