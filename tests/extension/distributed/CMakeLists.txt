# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE distributed
    SOURCES
        test_buffer_handle.cpp
        test_cluster.cpp
        test_distributed.cpp
        test_endpoint.cpp
        test_message_type.cpp
        test_offload_policy.cpp
        test_shared_secret.cpp
        test_transport.cpp
        test_worker_agent.cpp
        test_worker_main.cpp
    BENCHMARK_SOURCES
        benchmark_cluster.cpp)
