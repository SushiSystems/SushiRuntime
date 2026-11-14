# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE placement
    SOURCES
        compile_run_placement.cpp
        test_placement_rule.cpp
        test_placement_rules.cpp
        test_placement_target_matching.cpp
        test_run_description.cpp
        test_run_placement_policy.cpp)
