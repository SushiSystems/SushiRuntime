# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME core TIER foundation
    SOURCES version.cpp)

sushiruntime_add_module_tests(
    MODULE core
    SOURCES
        test_delegate.cpp
        test_error.cpp
        test_ref_counted.cpp
        test_version.cpp)
