# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME device_profile TIER engine INTERNAL
    SYCL
    SOURCES
        cost_model.cpp
        device_profile_file.cpp
        device_profile_probe.cpp
        dispatch_observation.cpp
        profile_fold.cpp
        profile_measurement.cpp
        profile_transfer_cost.cpp
        profile_update.cpp
    DEPENDS
        core
        handle
        log
        memory
        report
        transfer)
