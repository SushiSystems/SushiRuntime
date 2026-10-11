# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME observability TIER platform
    HEADER_ONLY SYCL
    DEPENDS core report)

sushiruntime_add_module_tests(
    MODULE observability
    SOURCES
        test_profiling_ledger.cpp
        test_transfer_counters.cpp)
