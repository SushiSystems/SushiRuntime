# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE distributed
    SOURCES
        compile_distributed_log_category.cpp
        test_cluster.cpp
        test_distributed.cpp
        test_registered_task/test_registered_task.cpp
        test_registered_task/test_nested_run_probe.cpp
        test_shared_secret.cpp
        test_tcp_transport.cpp
        test_worker_agent.cpp
        test_worker_main.cpp
        test_worker_options_parser.cpp
    BENCHMARK_SOURCES
        benchmark_cluster.cpp
    DEVICE_SOURCES
        benchmark_cluster.cpp
        test_distributed.cpp
        test_registered_task/test_nested_run_probe.cpp
        test_registered_task/test_registered_task.cpp)
