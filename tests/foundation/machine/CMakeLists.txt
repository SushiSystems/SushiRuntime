# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE machine
    SOURCES
        compile_device_description.cpp
        compile_device_facts.cpp
        compile_device_kind.cpp
        compile_operating_system.cpp
        compile_pin_policy.cpp
        compile_worker_pool.cpp
        compile_device_filter.cpp
        test_node_facts.cpp
        test_node_label.cpp)
