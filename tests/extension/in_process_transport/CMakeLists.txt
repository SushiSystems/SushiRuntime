# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE in_process_transport
    SOURCES
        compile_in_process_transport_log_category.cpp
        compile_transport_drive.cpp
        test_executor_drive.cpp
        test_in_process_transport.cpp
        test_thread_drive.cpp)
