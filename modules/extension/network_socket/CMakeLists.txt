# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME network_socket TIER extension INTERNAL
    SOURCES
        socket_posix.cpp
        socket_windows.cpp
    DEPENDS core log transport
    EXTERNAL "$<$<PLATFORM_ID:Windows>:ws2_32>")
