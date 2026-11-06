# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME network_socket TIER extension INTERNAL
    SOURCES
        socket_posix.cpp
        socket_windows.cpp
    DEPENDS core log transport
    EXTERNAL "$<$<PLATFORM_ID:Windows>:ws2_32>")
