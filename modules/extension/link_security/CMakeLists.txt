# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME link_security TIER extension INTERNAL
    SOURCES
        handshake.cpp
        hmac_sha256.cpp
        sha256.cpp
    DEPENDS core)
