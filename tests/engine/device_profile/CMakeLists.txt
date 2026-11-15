# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE device_profile
    SOURCES
        compile_device_profile_log_category.cpp
        compile_probe_devices.cpp
        compile_profile_probe.cpp
        test_cost_model.cpp
        test_device_profile.cpp
        test_device_profile_file.cpp
        test_device_profile_probe.cpp
        test_device_profile_set.cpp
        test_dispatch_observation.cpp
        test_profile_fold.cpp
        test_profile_measurement.cpp
        test_profile_transfer_cost.cpp
        test_profile_update.cpp
    DEVICE_SOURCES
        test_device_profile_probe.cpp)
