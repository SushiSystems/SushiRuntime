# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE transport
    SOURCES
        test_buffer_handle.cpp
        test_endpoint.cpp
        test_message_type.cpp
        test_recording_transport_events.cpp
        test_transport.cpp)
