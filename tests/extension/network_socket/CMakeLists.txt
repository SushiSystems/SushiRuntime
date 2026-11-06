# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE network_socket
    SOURCES
        compile_network_socket_log_category.cpp
        compile_socket_option_report.cpp
        test_socket.cpp)
