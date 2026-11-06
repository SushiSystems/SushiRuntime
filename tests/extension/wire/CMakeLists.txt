# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE wire
    SOURCES
        test_message_router.cpp
        test_wire_codec.cpp
        test_wire_frame.cpp
        test_wire_messages.cpp)
