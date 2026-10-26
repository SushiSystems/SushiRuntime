# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# The gate units are compiled at each level and linked into one program, so the level of a
# translation unit is tested against the others.
sushiruntime_add_module_tests(
    MODULE profile
    SOURCES
        gate_unit_level_one.cpp
        gate_unit_level_three.cpp
        gate_unit_level_two.cpp
        gate_unit_level_zero.cpp
        test_profile.cpp
        test_profile_gate.cpp
        test_profile_keys.cpp)
