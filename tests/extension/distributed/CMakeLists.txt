# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE distributed
    SOURCES
        test_buffer_view.cpp
        test_cluster.cpp
        test_distributed.cpp
        test_in_process_transport.cpp
        test_kernel_registry.cpp
        test_registered_task.cpp
        test_shared_secret.cpp
        test_tcp_transport.cpp
        test_worker_agent.cpp
        test_worker_main.cpp
        test_worker_options_parser.cpp
    BENCHMARK_SOURCES
        benchmark_cluster.cpp)
